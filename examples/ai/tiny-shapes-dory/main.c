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

#ifdef TINY_SHAPES_CAMERA_INPUT
#include "bsp/camera/himax.h"
#endif

#define IMG_ORIENTATION 0x0101
#define CAMERA_INFER_PERIOD_US (500000)

static uint8_t *model_input;
static tiny_shapes_result_packet_t last_result;
static volatile int has_result;
static volatile const char *phase = "boot";

static void publish_result(const tiny_shapes_result_packet_t *result) {
  last_result = *result;
  has_result = 1;
  cpxPrintToConsole(
    LOG_TO_CRTP,
    "tiny-shapes class=%s logits=[%ld,%ld,%ld] margin=%lu cycles=%lu\n",
    tiny_shapes_class_name(result->class_id),
    (long)result->logits[0],
    (long)result->logits[1],
    (long)result->logits[2],
    (unsigned long)result->margin,
    (unsigned long)result->cycles
  );
}

static int run_inference(void) {
  int32_t logits[TINY_SHAPES_CLASS_COUNT];
  uint32_t cycles = 0;
  if (tiny_shapes_inference_run(model_input, logits, &cycles)) {
    return -1;
  }
  const tiny_shapes_result_packet_t result =
    tiny_shapes_decode(logits, cycles);
  publish_result(&result);
  return 0;
}

static void heartbeat_task(void *parameters) {
  (void)parameters;
  const TickType_t delay = 1000 / portTICK_PERIOD_MS;
  while (1) {
    if (has_result) {
      cpxPrintToConsole(
        LOG_TO_CRTP,
        "tiny-shapes class=%s logits=[%ld,%ld,%ld] cycles=%lu phase=%s\n",
        tiny_shapes_class_name(last_result.class_id),
        (long)last_result.logits[0],
        (long)last_result.logits[1],
        (long)last_result.logits[2],
        (unsigned long)last_result.cycles,
        (const char *)phase
      );
    } else {
      cpxPrintToConsole(
        LOG_TO_CRTP,
        "tiny-shapes status: %s\n",
        (const char *)phase
      );
    }
    vTaskDelay(delay);
  }
}

#ifdef TINY_SHAPES_CAMERA_INPUT
static struct pi_device camera;
static pi_task_t camera_task;
static uint8_t *camera_buffer;

static void capture_next_frame(void);

static void camera_handler(void *argument) {
  (void)argument;
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  if (tiny_shapes_preprocess(camera_buffer, model_input)) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes preprocessing failed\n");
  } else if (run_inference()) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes inference failed\n");
  }
  pi_time_wait_us(CAMERA_INFER_PERIOD_US);
  capture_next_frame();
}

static void capture_next_frame(void) {
  pi_camera_capture_async(
    &camera,
    camera_buffer,
    TINY_SHAPES_CAMERA_BYTES,
    pi_task_callback(&camera_task, camera_handler, NULL)
  );
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  if (pi_camera_open(&camera)) {
    return -1;
  }
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  uint8_t orientation = 3;
  uint8_t reg_value = 0;
  pi_camera_reg_set(&camera, IMG_ORIENTATION, &orientation);
  pi_time_wait_us(1000000);
  pi_camera_reg_get(&camera, IMG_ORIENTATION, &reg_value);
  if (orientation != reg_value) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: camera rotate failed\n");
    return -1;
  }
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
  return 0;
}
#endif

static void application(void) {
  phase = "cpx init";
  cpxInit();
  phase = "cpx ready";
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: cpx ready\n");

  xTaskCreate(
    heartbeat_task,
    "hb",
    configMINIMAL_STACK_SIZE * 2,
    NULL,
    tskIDLE_PRIORITY + 1,
    NULL
  );
  pi_time_wait_us(300000);

#ifdef TINY_SHAPES_CAMERA_INPUT
  /* Match wifi-img-streamer: clocks/voltage, then Himax, before heavy L2 use. */
  phase = "clocks";
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: clocks set\n");

  phase = "camera open";
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: opening camera...\n");
  if (open_camera()) {
    phase = "camera open failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "camera open ok";
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: camera open ok\n");
#endif

  phase = "inference init";
  if (tiny_shapes_inference_init()) {
    phase = "inference init failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "network ready";

  model_input = (uint8_t *)pi_l2_malloc(TINY_SHAPES_INPUT_BYTES);
  if (model_input == NULL) {
    phase = "input alloc failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "golden load";
  if (tiny_shapes_load_golden_input(model_input) != 0) {
    phase = "golden load failed";
    while (1) {
      pi_yield();
    }
  }
  {
    unsigned int sum = 0;
    for (unsigned i = 0; i < TINY_SHAPES_INPUT_BYTES; ++i) {
      sum += model_input[i];
    }
    cpxPrintToConsole(
      LOG_TO_CRTP,
      "tiny-shapes: golden_sum=%u expect=68734\n",
      sum
    );
  }

  phase = "golden run";
  if (run_inference()) {
    phase = "golden inference failed";
    while (1) {
      pi_yield();
    }
  }
  phase = "golden ok";

#ifdef TINY_SHAPES_CAMERA_INPUT
  phase = "camera alloc";
  camera_buffer = (uint8_t *)pi_l2_malloc(TINY_SHAPES_CAMERA_BYTES);
  if (camera_buffer == NULL) {
    phase = "camera alloc failed";
    while (1) {
      pi_yield();
    }
  }

  phase = "camera loop";
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes: camera loop\n");
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
  return pmsis_kickoff((void *)application);
}
