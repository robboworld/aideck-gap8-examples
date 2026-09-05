#include <stdint.h>
#include <string.h>

#include "bsp/bsp.h"
#include "inference.h"
#include "model_metadata.h"
#include "network.h"
#include "pmsis.h"
#include "preprocess.h"
#include "result_protocol.h"
#include "cpx.h"

#ifdef ARROW2_CAMERA_INPUT
#include "bsp/camera/himax.h"
#endif

#define IMG_ORIENTATION 0x0101
#define CAMERA_INFER_PERIOD_US (500000)
/* Margin below this → class=uncertain (not a third model class). */
#ifndef ARROW2_MARGIN_MIN
#define ARROW2_MARGIN_MIN 8000u
#endif
/* Winning logit must be clearly positive (empty scenes often pick right with
 * a small positive / less-negative score). */
#ifndef ARROW2_LOGIT_MIN
#define ARROW2_LOGIT_MIN 5500
#endif

static uint8_t *model_input;
static arrow2_result_packet_t last_result;
static arrow2_input_stats_t last_stats;
static volatile int has_result;
static volatile const char *phase = "boot";
static volatile const char *last_gate = "boot";

static const char *publish_class_name(const arrow2_result_packet_t *result) {
  if (last_gate != NULL && last_gate[0] != '\0' &&
      !(last_gate[0] == 'o' && last_gate[1] == 'k')) {
    return "uncertain";
  }
  if (result->margin < ARROW2_MARGIN_MIN) {
    return "uncertain";
  }
  if (result->logits[result->class_id] < (int32_t)ARROW2_LOGIT_MIN) {
    return "uncertain";
  }
  return arrow2_class_name(result->class_id);
}

static void publish_line(
  const char *cls,
  const char *gate,
  const int32_t logits[2],
  uint32_t margin,
  uint32_t cycles,
  const arrow2_input_stats_t *stats
) {
  /* Keep under ~70 chars — long CRTP console lines corrupt/truncate. */
  cpxPrintToConsole(
    LOG_TO_CRTP,
    "a2 %s g=%s [%ld,%ld] m=%lu mn=%u rg=%u dk=%u br=%u bc=%u\n",
    cls,
    gate,
    (long)logits[0],
    (long)logits[1],
    (unsigned long)margin,
    (unsigned)(stats != NULL ? stats->mean : 0),
    (unsigned)(stats != NULL ? stats->range : 0),
    (unsigned)(stats != NULL ? stats->dark : 0),
    (unsigned)(stats != NULL ? stats->bright : 0),
    (unsigned)(stats != NULL ? stats->bright_center : 0)
  );
  (void)cycles;
}

static void publish_result(
  const arrow2_result_packet_t *result,
  const arrow2_input_stats_t *stats
) {
  const char *gate = "ok";
  const char *cls;

  last_result = *result;
  if (stats != NULL) {
    last_stats = *stats;
  }
  has_result = 1;

  if (result->margin < ARROW2_MARGIN_MIN) {
    gate = "weak_m";
  } else if (result->logits[result->class_id] < (int32_t)ARROW2_LOGIT_MIN) {
    gate = "weak_l";
  }
  last_gate = gate;
  cls = (gate[0] == 'o') ? arrow2_class_name(result->class_id) : "uncertain";
  publish_line(
    cls,
    gate,
    result->logits,
    result->margin,
    result->cycles,
    stats
  );
}

static void publish_blank(const arrow2_input_stats_t *stats) {
  static const int32_t z[2] = {0, 0};
  arrow2_result_packet_t blank = {
    .version = ARROW2_RESULT_VERSION,
    .class_id = 0,
    .reserved = 0,
    .logits = {0, 0},
    .margin = 0,
    .cycles = 0,
  };
  last_result = blank;
  if (stats != NULL) {
    last_stats = *stats;
    last_gate = stats->reject != NULL ? stats->reject : "blank";
  } else {
    last_gate = "blank";
  }
  has_result = 1;
  publish_line(
    "uncertain",
    last_gate,
    z,
    0,
    0,
    stats
  );
}

static int run_inference(const arrow2_input_stats_t *stats) {
  int32_t logits[ARROW2_CLASS_COUNT];
  uint32_t cycles = 0;
  if (arrow2_inference_run(model_input, logits, &cycles)) {
    return -1;
  }
  const arrow2_result_packet_t result = arrow2_decode(logits, cycles);
  publish_result(&result, stats);
  return 0;
}

static void heartbeat_task(void *parameters) {
  (void)parameters;
  const TickType_t delay = 1000 / portTICK_PERIOD_MS;
  while (1) {
    if (has_result) {
      publish_line(
        publish_class_name(&last_result),
        (const char *)last_gate,
        last_result.logits,
        last_result.margin,
        last_result.cycles,
        &last_stats
      );
    } else {
      cpxPrintToConsole(
        LOG_TO_CRTP,
        "arrow2 status: %s\n",
        (const char *)phase
      );
    }
    vTaskDelay(delay);
  }
}

#ifdef ARROW2_CAMERA_INPUT
static struct pi_device camera;
static pi_task_t camera_task;
static uint8_t *camera_buffer;

static void capture_next_frame(void);

static void camera_handler(void *argument) {
  (void)argument;
  arrow2_input_stats_t stats;
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  if (arrow2_preprocess(camera_buffer, model_input)) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2 preprocessing failed\n");
  } else if (arrow2_input_analyze(model_input, &stats)) {
    publish_blank(&stats);
  } else if (run_inference(&stats)) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2 inference failed\n");
  }
  pi_time_wait_us(CAMERA_INFER_PERIOD_US);
  capture_next_frame();
}

static void capture_next_frame(void) {
  pi_camera_capture_async(
    &camera,
    camera_buffer,
    ARROW2_CAMERA_BYTES,
    pi_task_callback(&camera_task, camera_handler, NULL)
  );
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  phase = "camera pi_open";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: camera pi_open...\n");
  if (pi_camera_open(&camera)) {
    return -1;
  }
  /* Soft bring-up: skip Himax I2C orient (often hangs with J-Link attached). */
  phase = "camera aeg";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: camera aeg...\n");
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
  return 0;
}
#endif

static void application(void) {
  phase = "cpx init";
  cpxInit();
  phase = "cpx ready";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: cpx ready\n");

  xTaskCreate(
    heartbeat_task,
    "hb",
    configMINIMAL_STACK_SIZE * 4,
    NULL,
    tskIDLE_PRIORITY + 1,
    NULL
  );
  vTaskDelay(300 / portTICK_PERIOD_MS);

  /* Network + golden first so CRTP works even if Himax hangs (e.g. JTAG). */
  phase = "inference init";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: inference init...\n");
  if (arrow2_inference_init()) {
    phase = "inference init failed";
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2: inference init failed\n");
    while (1) {
      pi_yield();
    }
  }
  phase = "network ready";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: network ready\n");

  model_input = (uint8_t *)pi_l2_malloc(ARROW2_INPUT_BYTES);
  if (model_input == NULL) {
    phase = "input alloc failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "golden load";
  if (arrow2_load_golden_input(model_input) != 0) {
    phase = "golden load failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "golden run";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: golden run...\n");
  if (run_inference(NULL)) {
    phase = "golden inference failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "golden ok";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: golden ok\n");

#ifdef ARROW2_CAMERA_INPUT
  phase = "camera open";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: opening camera...\n");
  if (open_camera()) {
    phase = "camera open failed";
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2: camera open failed (golden still OK)\n");
    while (1) {
      pi_yield();
    }
  }
  phase = "camera open ok";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: camera open ok\n");

  phase = "camera alloc";
  camera_buffer = (uint8_t *)pi_l2_malloc(ARROW2_CAMERA_BYTES);
  if (camera_buffer == NULL) {
    phase = "camera alloc failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "camera loop";
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2: camera loop\n");
  capture_next_frame();
  while (1) {
    pi_yield();
  }
#else
  phase = "static ok";
  while (1) {
    pi_yield();
  }
#endif
}

int main(void) {
  pi_bsp_init();
  /* Match stop1 / wifi-img-streamer: set FC clocks before FreeRTOS kickoff. */
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  return pmsis_kickoff((void *)application);
}
