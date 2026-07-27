#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/bsp.h"
#include "bsp/camera/himax.h"
#include "inference.h"
#include "model_metadata.h"
#include "pmsis.h"
#include "preprocess.h"
#include "result_protocol.h"

#ifdef TINY_SHAPES_ENABLE_CPX
#include "cpx.h"
#endif

#define IMG_ORIENTATION 0x0101

static uint8_t *model_input;

static void publish_result(const tiny_shapes_result_packet_t *result) {
  printf(
    "class=%s id=%u logits=[%ld,%ld,%ld] margin=%lu cycles=%lu\n",
    tiny_shapes_class_name(result->class_id),
    result->class_id,
    (long)result->logits[0],
    (long)result->logits[1],
    (long)result->logits[2],
    (unsigned long)result->margin,
    (unsigned long)result->cycles
  );
#ifdef TINY_SHAPES_ENABLE_CPX
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

  CPXPacket_t packet;
  cpxInitRoute(CPX_T_GAP8, CPX_T_STM32, CPX_F_APP, &packet.route);
  memcpy(packet.data, result, sizeof(*result));
  packet.dataLength = sizeof(*result);
  cpxSendPacketBlocking(&packet);
#endif
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

#ifdef TINY_SHAPES_CAMERA_INPUT
static struct pi_device camera;
static pi_task_t camera_task;
static uint8_t *camera_buffer;

static void capture_next_frame(void);

static void camera_handler(void *argument) {
  (void)argument;
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, NULL);
  if (tiny_shapes_preprocess(camera_buffer, model_input)) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes preprocessing failed\n");
  } else if (run_inference()) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes inference failed\n");
  }
  capture_next_frame();
}

static void capture_next_frame(void) {
  pi_camera_capture_async(
    &camera,
    camera_buffer,
    TINY_SHAPES_CAMERA_BYTES,
    pi_task_callback(&camera_task, camera_handler, NULL)
  );
  pi_camera_control(&camera, PI_CAMERA_CMD_START, NULL);
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  if (pi_camera_open(&camera)) {
    return -1;
  }
  pi_camera_control(&camera, PI_CAMERA_CMD_START, NULL);
  uint8_t orientation = 3;
  pi_camera_reg_set(&camera, IMG_ORIENTATION, &orientation);
  pi_time_wait_us(1000000);
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, NULL);
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, NULL);
  return 0;
}
#endif

static void application(void *argument) {
  (void)argument;
  pi_bsp_init();
  pi_freq_set(PI_FREQ_DOMAIN_FC, 200000000);
  pi_freq_set(PI_FREQ_DOMAIN_CL, 170000000);

#ifdef TINY_SHAPES_ENABLE_CPX
  cpxInit();
  cpxEnableFunction(CPX_F_APP);
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes DORY starting\n");
#endif

  if (tiny_shapes_inference_init()) {
    printf("tiny-shapes inference initialization failed\n");
    pmsis_exit(-1);
  }
  model_input = (uint8_t *)pi_l2_malloc(TINY_SHAPES_INPUT_BYTES);
  if (model_input == NULL) {
    printf("tiny-shapes input allocation failed\n");
    pmsis_exit(-2);
  }

#ifdef TINY_SHAPES_CAMERA_INPUT
  camera_buffer = (uint8_t *)pi_l2_malloc(TINY_SHAPES_CAMERA_BYTES);
  if (camera_buffer == NULL || open_camera()) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes camera setup failed\n");
    pmsis_exit(-3);
  }
  capture_next_frame();
  while (1) {
    pi_yield();
  }
#else
  if (tiny_shapes_load_golden_input(model_input) || run_inference()) {
    printf("tiny-shapes static inference failed\n");
    pmsis_exit(-4);
  }
  tiny_shapes_inference_deinit();
  pi_l2_free(model_input, TINY_SHAPES_INPUT_BYTES);
  pmsis_exit(0);
#endif
}

int main(void) {
  return pmsis_kickoff(application);
}
