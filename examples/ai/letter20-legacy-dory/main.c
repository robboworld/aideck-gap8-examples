#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "pmsis.h"
#include "bsp/bsp.h"
#include "bsp/camera/himax.h"
#include "cpx.h"
#include "wifi.h"
#include "network.h"
#include "embedded_weights.h"
#include "letter20_commands.h"

extern int cycle_network_execution;

#define CAMERA_WIDTH 324
#define CAMERA_HEIGHT 244
#define CAMERA_BYTES (CAMERA_WIDTH * CAMERA_HEIGHT)
#define INPUT_BYTES (160 * 160)
#define GRID 20
#define HEAD_CHANNELS 25
#define L2_ARENA_BYTES 200000

#ifndef STREAM_RAW_IMAGES
#define STREAM_RAW_IMAGES 0
#endif
#ifndef REPORT_EVERY
#define REPORT_EVERY 1
#endif
#ifndef DETECTOR_PERIOD_US
#define DETECTOR_PERIOD_US 0
#endif
#if defined(LETTER20_INPUT_STATIC)
#define LETTER20_USE_CAMERA 0
#elif defined(LETTER20_INPUT_CAMERA)
#define LETTER20_USE_CAMERA 1
#else
#define LETTER20_USE_CAMERA 1
#endif

typedef struct {
  uint8_t magic;
  uint16_t width;
  uint16_t height;
  uint8_t depth;
  uint8_t type;
  uint32_t size;
} __attribute__((packed)) image_header_t;

static volatile int wifi_client_connected;
static CPXPacket_t wifi_rx, wifi_tx;
#if LETTER20_USE_CAMERA
static struct pi_device camera;
#endif

static void wifi_control_task(void *arg) {
  (void)arg;
  while (1) {
    cpxReceivePacketBlocking(CPX_F_WIFI_CTRL, &wifi_rx);
    WiFiCTRLPacket_t *control = (WiFiCTRLPacket_t *)wifi_rx.data;
    if (control->cmd == WIFI_CTRL_STATUS_CLIENT_CONNECTED) {
      wifi_client_connected = control->data[0];
      cpxPrintToConsole(LOG_TO_CRTP, "letter20 wifi client=%d\n", wifi_client_connected);
    }
  }
}

static void setup_wifi_ap(void) {
#ifdef SETUP_WIFI_AP
  static char ssid[] = "AI Deck letter20";
  wifi_tx.route.destination = CPX_T_ESP32;
  wifi_rx.route.source = CPX_T_GAP8;
  wifi_tx.route.function = CPX_F_WIFI_CTRL;
  wifi_tx.route.version = CPX_VERSION;
  WiFiCTRLPacket_t *control = (WiFiCTRLPacket_t *)wifi_tx.data;
  control->cmd = WIFI_CTRL_SET_SSID;
  memcpy(control->data, ssid, sizeof(ssid));
  wifi_tx.dataLength = sizeof(ssid);
  cpxSendPacketBlocking(&wifi_tx);
  control->cmd = WIFI_CTRL_WIFI_CONNECT;
  control->data[0] = 1;
  wifi_tx.dataLength = 2;
  cpxSendPacketBlocking(&wifi_tx);
#endif
}

static void send_image(uint8_t *image) {
  if (!wifi_client_connected) return;
  image_header_t *header = (image_header_t *)wifi_tx.data;
  header->magic = 0xBC;
  header->width = CAMERA_WIDTH;
  header->height = CAMERA_HEIGHT;
  header->depth = 1;
  header->type = 0;
  header->size = CAMERA_BYTES;
  wifi_tx.dataLength = sizeof(*header);
  cpxSendPacketBlocking(&wifi_tx);
  for (uint32_t offset = 0; offset < CAMERA_BYTES;) {
    uint32_t size = sizeof(wifi_tx.data);
    if (offset + size > CAMERA_BYTES) size = CAMERA_BYTES - offset;
    memcpy(wifi_tx.data, image + offset, size);
    wifi_tx.dataLength = size;
    cpxSendPacketBlocking(&wifi_tx);
    offset += size;
  }
}

#if LETTER20_USE_CAMERA
static int open_himax(struct pi_device *camera) {
  struct pi_himax_conf config;
  uint8_t orientation = 3;
  pi_himax_conf_init(&config);
  config.format = PI_CAMERA_QVGA;
  pi_open_from_conf(camera, &config);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: opening Himax\n");
  if (pi_camera_open(camera)) {
    cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: open failed\n");
    return -1;
  }
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: opened\n");
  pi_camera_control(camera, PI_CAMERA_CMD_START, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: started\n");
  pi_camera_reg_set(camera, 0x0101, &orientation);
  pi_time_wait_us(1000000);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: orientation set\n");
  pi_camera_control(camera, PI_CAMERA_CMD_STOP, 0);
  pi_camera_control(camera, PI_CAMERA_CMD_AEG_INIT, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 camera: ready\n");
  return 0;
}

static void preprocess(const uint8_t *camera, uint8_t *input) {
  memset(input, 0, INPUT_BYTES);
  for (int y = 0; y < 120; ++y) {
    const int source_y = y * CAMERA_HEIGHT / 120;
    uint8_t *out = input + (y + 20) * 160;
    const uint8_t *in = camera + source_y * CAMERA_WIDTH;
    for (int x = 0; x < 160; ++x) out[x] = in[x * CAMERA_WIDTH / 160];
  }
}
#else
static void fill_static_input(uint8_t *input) {
  memset(input, 24, INPUT_BYTES);
  for (int y = 50; y < 110; ++y) {
    memset(input + y * 160 + 50, 200, 60);
  }
}
#endif

static void report_legacy(const int32_t *output) {
  int32_t best_score = INT32_MIN;
  int best_class = 0, best_row = 0, best_col = 0;
  int32_t best_obj = 0;

  for (int row = 0; row < GRID; ++row) {
    for (int col = 0; col < GRID; ++col) {
      const int base = (row * GRID + col) * HEAD_CHANNELS;
      const int32_t objectness = output[base];
      int32_t class_logit = INT32_MIN;
      int class_id = 0;
      for (int cls = 0; cls < 20; ++cls) {
        const int32_t value = output[base + 1 + cls];
        if (value > class_logit) {
          class_logit = value;
          class_id = cls;
        }
      }
      const int32_t score = objectness + class_logit;
      if (score > best_score) {
        best_score = score;
        best_class = class_id;
        best_row = row;
        best_col = col;
        best_obj = objectness;
      }
    }
  }

  const int32_t *box = output + ((best_row * GRID + best_col) * HEAD_CHANNELS) + 21;
  cpxPrintToConsole(
      LOG_TO_CRTP,
      "letter20 top=%s cmd=%s obj=%ld cls=%ld score=%ld cell=%d,%d "
      "box_raw=[%ld,%ld,%ld,%ld] cycles=%d\n",
      letter20_names[best_class], letter20_commands[best_class],
      (long)best_obj, (long)(best_score - best_obj), (long)best_score,
      best_col, best_row,
      (long)box[0], (long)box[1], (long)box[2], (long)box[3],
      cycle_network_execution);
}

static void detector_task(void *arg) {
  (void)arg;
  uint8_t *arena;
#if LETTER20_USE_CAMERA
  uint8_t *camera_buffer;
#endif

  vTaskDelay(2000);
  setup_wifi_ap();
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &wifi_tx.route);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 raising FC to 150 MHz\n");
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  pi_freq_set(PI_FREQ_DOMAIN_FC, 150000000);
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 freq fc=%u cl=%u\n",
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_FC),
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_CL));
#if LETTER20_USE_CAMERA
  camera_buffer = pi_l2_malloc(CAMERA_BYTES);
  if (!camera_buffer) {
    cpxPrintToConsole(LOG_TO_CRTP, "letter20: L2 allocation failed camera=%p\n", camera_buffer);
    return;
  }
  if (open_himax(&camera)) {
    cpxPrintToConsole(LOG_TO_CRTP, "letter20: camera open failed\n");
    return;
  }
#endif
  arena = pi_l2_malloc(L2_ARENA_BYTES);
  if (!arena) {
    cpxPrintToConsole(LOG_TO_CRTP, "letter20: L2 allocation failed arena=%p\n", arena);
    return;
  }
  network_initialize();
#if LETTER20_USE_CAMERA
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 timing mode: camera raw-stream=%d model=160x160 legacy25\n",
                    STREAM_RAW_IMAGES);
#else
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 timing mode: static (no Himax) model=160x160 legacy25\n");
#endif
  unsigned int frame = 0;
  while (1) {
    const uint32_t t0 = pi_time_get_us();
#if LETTER20_USE_CAMERA
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
    pi_camera_capture(&camera, camera_buffer, CAMERA_BYTES);
    pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
    const uint32_t t_capture = pi_time_get_us();
#if STREAM_RAW_IMAGES
    send_image(camera_buffer);
#endif
    const uint32_t t_stream = pi_time_get_us();
    int32_t *head_output = (int32_t *)(arena + L2_ARENA_BYTES - GRID * GRID * HEAD_CHANNELS * sizeof(int32_t));
    preprocess(camera_buffer, arena);
    const uint32_t t_preprocess = pi_time_get_us();
#else
    const uint32_t t_capture = t0;
    const uint32_t t_stream = t0;
    int32_t *head_output = (int32_t *)(arena + L2_ARENA_BYTES - GRID * GRID * HEAD_CHANNELS * sizeof(int32_t));
    fill_static_input(arena);
    const uint32_t t_preprocess = pi_time_get_us();
#endif
    network_run(arena, L2_ARENA_BYTES, head_output, 0, 1);
    const uint32_t t_inference = pi_time_get_us();
    if (++frame % REPORT_EVERY == 0) {
      cpxPrintToConsole(
          LOG_TO_CRTP,
          "letter20 timing us: capture=%lu stream=%lu preprocess=%lu inference=%lu camera_to_result=%lu\n",
          (unsigned long)(t_capture - t0), (unsigned long)(t_stream - t_capture),
          (unsigned long)(t_preprocess - t_stream), (unsigned long)(t_inference - t_preprocess),
          (unsigned long)(t_inference - t0));
      report_legacy(head_output);
    }
    pi_time_wait_us(DETECTOR_PERIOD_US);
  }
}

static void application(void) {
  cpxInit();
  cpxEnableFunction(CPX_F_WIFI_CTRL);
#if LETTER20_USE_CAMERA
  cpxPrintToConsole(LOG_TO_CRTP, "-- letter20 legacy detector --\n");
#else
  cpxPrintToConsole(LOG_TO_CRTP, "-- letter20 legacy detector static --\n");
#endif
  if (xTaskCreate(wifi_control_task, "wifi-ctrl", configMINIMAL_STACK_SIZE * 2,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS ||
      xTaskCreate(detector_task, "detector", configMINIMAL_STACK_SIZE * 6,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "letter20: task creation failed\n");
    return;
  }
  while (1) pi_yield();
}

int main(void) {
  pi_bsp_init();
  return pmsis_kickoff((void *)application);
}
