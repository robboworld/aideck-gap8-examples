/*
 * letter20 image classifier on AI-deck GAP8.
 *
 * Himax QVGA frame -> whole-frame 32x32 NN resize + contrast stretch ->
 * DORY TinyShapeCNN (20 classes: 19 letters + empty) -> one CRTP console
 * line per frame:
 *
 *   L20 <frame> <top1> <logit1> <top2> <logit2> <top3> <logit3> <kcycles> <ms>
 *
 * and every LETTER20_DUMP_EVERY frames the model input as 32 lines
 *   L20i <frame> <row> <32 hex nibbles>
 *
 * The host viewer (tinyml_demo/scripts/letter20_radio_viewer.py) turns logits
 * into confidences and applies smoothing; no thresholds live on the drone.
 */
#include <stdint.h>
#include <string.h>

#include "bsp/bsp.h"
#include "cpx.h"
#include "inference.h"
#include "model_metadata.h"
#include "network.h"
#include "pmsis.h"
#include "preprocess.h"
#include "result_protocol.h"
#include "bsp/camera/himax.h"

/* Same register/value as wifi-img-streamer, which captured the dataset. */
#define IMG_ORIENTATION 0x0101
#define IMG_ORIENTATION_VALUE 3
#ifndef LETTER20_PERIOD_MS
#define LETTER20_PERIOD_MS 100
#endif
/* Every N frames send the 32x32 model input (4 bit/pixel) for the viewer. */
/* A capture that does not complete within this time restarts the camera. */
#ifndef LETTER20_CAPTURE_TIMEOUT_MS
#define LETTER20_CAPTURE_TIMEOUT_MS 1000
#endif
#ifndef LETTER20_DUMP_EVERY
#define LETTER20_DUMP_EVERY 20
#endif

static uint8_t *model_input;
static struct pi_device camera;
static pi_task_t camera_task;
static uint8_t *camera_buffer;
static EventGroupHandle_t capture_events;
#define CAPTURE_DONE_BIT (1 << 0)

static void halt(const char *why) {
  while (1) {
    cpxPrintToConsole(LOG_TO_CRTP, "L20 error %s\n", why);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

static void camera_handler(void *argument) {
  (void)argument;
  xEventGroupSetBits(capture_events, CAPTURE_DONE_BIT);
}

static void classify_and_publish(uint32_t frame, uint32_t started_us) {
  int32_t logits[LETTER20_CLASS_COUNT];
  uint32_t cycles = 0;
  if (letter20_inference_run(model_input, logits, &cycles)) {
    cpxPrintToConsole(LOG_TO_CRTP, "L20 error inference\n");
    return;
  }
  const letter20_top3_t t = letter20_top3(logits);
  const uint32_t ms = (pi_time_get_us() - started_us) / 1000u;
  cpxPrintToConsole(
    LOG_TO_CRTP, "L20 %u %s %d %s %d %s %d %u %u\n",
    (unsigned)frame,
    letter20_class_name(t.top[0]), (int)t.logit[0],
    letter20_class_name(t.top[1]), (int)t.logit[1],
    letter20_class_name(t.top[2]), (int)t.logit[2],
    (unsigned)(cycles / 1000u), (unsigned)ms
  );
}

static void dump_model_input(uint32_t frame) {
  static const char hex[] = "0123456789abcdef";
  char row[LETTER20_INPUT_WIDTH + 1];
  uint32_t sum = 0;
  uint8_t lo = 255, hi = 0;
  for (int i = 0; i < LETTER20_CAMERA_BYTES; i += 7) {
    const uint8_t v = camera_buffer[i];
    sum += v;
    lo = v < lo ? v : lo;
    hi = v > hi ? v : hi;
  }
  cpxPrintToConsole(
    LOG_TO_CRTP, "L20s %u %u %u %u\n", (unsigned)frame,
    (unsigned)(sum / ((LETTER20_CAMERA_BYTES + 6) / 7)), (unsigned)lo, (unsigned)hi
  );
  for (int y = 0; y < LETTER20_INPUT_HEIGHT; ++y) {
    for (int x = 0; x < LETTER20_INPUT_WIDTH; ++x) {
      row[x] = hex[model_input[y * LETTER20_INPUT_WIDTH + x] >> 4];
    }
    row[LETTER20_INPUT_WIDTH] = '\0';
    cpxPrintToConsole(LOG_TO_CRTP, "L20i %u %d %s\n", (unsigned)frame, y, row);
  }
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  uint8_t value = IMG_ORIENTATION_VALUE;
  uint8_t readback = 0;

  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  if (pi_camera_open(&camera)) {
    return -1;
  }
  /* Exactly the wifi-img-streamer sequence used to capture the dataset. */
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  pi_camera_reg_set(&camera, IMG_ORIENTATION, &value);
  pi_time_wait_us(1000000);
  pi_camera_reg_get(&camera, IMG_ORIENTATION, &readback);
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "L20 camera orientation=%u\n", readback);
  /* The streamer idles for seconds before its first capture; capturing right
   * after AEG_INIT can leave the first transfer hanging. */
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  return 0;
}

/* Returns 0 when a frame landed in camera_buffer, -1 on timeout. */
static int capture_frame(void) {
  xEventGroupClearBits(capture_events, CAPTURE_DONE_BIT);
  pi_camera_capture_async(
    &camera, camera_buffer, LETTER20_CAMERA_BYTES,
    pi_task_callback(&camera_task, camera_handler, NULL)
  );
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  const EventBits_t bits = xEventGroupWaitBits(
    capture_events, CAPTURE_DONE_BIT, pdTRUE, pdFALSE,
    LETTER20_CAPTURE_TIMEOUT_MS / portTICK_PERIOD_MS
  );
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  return (bits & CAPTURE_DONE_BIT) ? 0 : -1;
}

static void application(void) {
  cpxInit();
  vTaskDelay(300 / portTICK_PERIOD_MS);
  cpxPrintToConsole(LOG_TO_CRTP, "L20 boot classes=%d\n", LETTER20_CLASS_COUNT);

  if (letter20_inference_init()) {
    halt("inference_init");
  }
  model_input = (uint8_t *)pi_l2_malloc(LETTER20_INPUT_BYTES);
  if (model_input == NULL) {
    halt("input_alloc");
  }
  /* Golden self-test: DORY-generated input must reproduce the host class. */
  if (letter20_load_golden_input(model_input) != 0) {
    halt("golden_load");
  }
  {
    int32_t logits[LETTER20_CLASS_COUNT];
    uint32_t cycles = 0;
    if (letter20_inference_run(model_input, logits, &cycles)) {
      halt("golden_run");
    }
    const letter20_top3_t t = letter20_top3(logits);
    cpxPrintToConsole(
      LOG_TO_CRTP, "L20 golden %s %d kcyc=%u\n",
      letter20_class_name(t.top[0]), (int)t.logit[0], (unsigned)(cycles / 1000u)
    );
  }

  if (open_camera()) {
    halt("camera_open");
  }
  camera_buffer = (uint8_t *)pi_l2_malloc(LETTER20_CAMERA_BYTES);
  capture_events = xEventGroupCreate();
  if (camera_buffer == NULL || capture_events == NULL) {
    halt("camera_alloc");
  }
  cpxPrintToConsole(LOG_TO_CRTP, "L20 ready\n");

  uint32_t timeouts = 0;
  for (uint32_t frame = 0;; ++frame) {
    const uint32_t started_us = pi_time_get_us();
    if (capture_frame() != 0) {
      ++timeouts;
      cpxPrintToConsole(LOG_TO_CRTP, "L20 error capture_timeout n=%u, restarting camera\n", (unsigned)timeouts);
      pi_camera_close(&camera);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      if (open_camera()) {
        halt("camera_reopen");
      }
      continue;
    }
    if (letter20_preprocess(camera_buffer, model_input) == 0) {
      classify_and_publish(frame, started_us);
      if (LETTER20_DUMP_EVERY > 0 && frame % LETTER20_DUMP_EVERY == 0) {
        dump_model_input(frame);
      }
    }
    vTaskDelay(LETTER20_PERIOD_MS / portTICK_PERIOD_MS);
  }
}

int main(void) {
  pi_bsp_init();
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  return pmsis_kickoff((void *)application);
}
