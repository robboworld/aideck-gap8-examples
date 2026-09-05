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
static volatile const char *phase = "boot";

static void publish_class(const char *cls) {
  cpxPrintToConsole(LOG_TO_CRTP, "%s\n", cls);
}

static void publish_result(
  const arrow2_result_packet_t *result,
  const arrow2_input_stats_t *stats
) {
  const char *cls;

  if (stats == NULL) {
    return;
  }
  if (result->margin < ARROW2_MARGIN_MIN ||
      result->logits[result->class_id] < (int32_t)ARROW2_LOGIT_MIN) {
    cls = "uncertain";
  } else {
    cls = arrow2_class_name(result->class_id);
  }
  publish_class(cls);
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

#ifdef ARROW2_CAMERA_INPUT
static struct pi_device camera;
static pi_task_t camera_task;
static uint8_t *camera_buffer;
static EventGroupHandle_t capture_events;
#define CAPTURE_DONE_BIT (1 << 0)

static void camera_handler(void *argument) {
  (void)argument;
  xEventGroupSetBits(capture_events, CAPTURE_DONE_BIT);
}

static int process_camera_frame(void) {
  arrow2_input_stats_t stats;
  if (arrow2_preprocess(camera_buffer, model_input)) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2 preprocessing failed\n");
    return -1;
  }
  (void)arrow2_input_analyze(model_input, &stats);
  if (run_inference(&stats)) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2 inference failed\n");
    return -1;
  }
  return 0;
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  phase = "camera pi_open";
  if (pi_camera_open(&camera)) {
    return -1;
  }
  /* AEG only — full START/orient/STOP before first capture starved frames. */
  phase = "camera aeg";
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
  return 0;
}
#endif

static void application(void) {
  phase = "cpx init";
  cpxInit();
  phase = "cpx ready";
  vTaskDelay(300 / portTICK_PERIOD_MS);

  /* Network + golden first so CRTP works even if Himax hangs (e.g. JTAG). */
  phase = "inference init";
  if (arrow2_inference_init()) {
    phase = "inference init failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "network ready";

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
  if (run_inference(NULL)) {
    phase = "golden inference failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "golden ok";

#ifdef ARROW2_CAMERA_INPUT
  phase = "camera open";
  if (open_camera()) {
    phase = "camera open failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "camera open ok";

  phase = "camera alloc";
  camera_buffer = (uint8_t *)pi_l2_malloc(ARROW2_CAMERA_BYTES);
  if (camera_buffer == NULL) {
    phase = "camera alloc failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "camera loop";
  capture_events = xEventGroupCreate();
  if (capture_events == NULL) {
    phase = "camera event failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "camera loop";
  while (1) {
    pi_camera_capture_async(
      &camera,
      camera_buffer,
      ARROW2_CAMERA_BYTES,
      pi_task_callback(&camera_task, camera_handler, NULL)
    );
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
    xEventGroupWaitBits(
      capture_events,
      CAPTURE_DONE_BIT,
      pdTRUE,
      pdFALSE,
      portMAX_DELAY
    );
    pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
    (void)process_camera_frame();
    vTaskDelay(CAMERA_INFER_PERIOD_US / 1000 / portTICK_PERIOD_MS);
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
