#include <stdint.h>
#include <string.h>
#include "pmsis.h"
#include "bsp/bsp.h"
#include "bsp/camera/himax.h"
#include "cpx.h"
#include "wifi.h"
#include "network.h"
#include "embedded_weights.h"

extern int cycle_network_execution;

#define CAMERA_WIDTH 324
#define CAMERA_HEIGHT 244
#define CAMERA_BYTES (CAMERA_WIDTH * CAMERA_HEIGHT)
#define INPUT_BYTES (160 * 160)
/* The graph's peak live activation footprint is ~145 KiB.  200 KiB leaves
 * room for the 120 KiB flash-loaded weight block, camera frame and RTOS. */
#define L2_ARENA_BYTES 200000
#define HEAD_WEIGHT_OFFSET (400 + 7680 + 35840 + 58880)
#ifndef STREAM_RAW_IMAGES
#define STREAM_RAW_IMAGES 0
#endif
#ifndef REPORT_EVERY
#define REPORT_EVERY 1
#endif
#ifndef DETECTOR_PERIOD_US
#define DETECTOR_PERIOD_US 0
#endif
#if defined(COCO20_INPUT_STATIC)
#define COCO20_USE_CAMERA 0
#elif defined(COCO20_INPUT_CAMERA)
#define COCO20_USE_CAMERA 1
#else
#define COCO20_USE_CAMERA 1
#endif

/* HIMAX21_MTSD */
static const char *names[21] = {"stop", "yield", "no-entry", "no-parking", "no-stopping", "keep-right", "keep-left", "priority-road", "roundabout", "go-straight", "turn-left", "turn-right", "no-left-turn", "no-right-turn", "no-u-turn", "max-speed", "info-ped", "warn-ped", "roadworks", "children", "other"};

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
#if COCO20_USE_CAMERA
static struct pi_device camera;
#endif

static void wifi_control_task(void *arg) {
  (void)arg;
  while (1) {
    cpxReceivePacketBlocking(CPX_F_WIFI_CTRL, &wifi_rx);
    WiFiCTRLPacket_t *control = (WiFiCTRLPacket_t *)wifi_rx.data;
    if (control->cmd == WIFI_CTRL_STATUS_CLIENT_CONNECTED) {
      wifi_client_connected = control->data[0];
      cpxPrintToConsole(LOG_TO_CRTP, "coco20 wifi client=%d\n", wifi_client_connected);
    }
  }
}

static void setup_wifi_ap(void) {
#ifdef SETUP_WIFI_AP
  static char ssid[] = "AI Deck detector";
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
  header->type = 0; /* raw Bayer */
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

#if COCO20_USE_CAMERA
static int open_himax(struct pi_device *camera) {
  struct pi_himax_conf config;
  uint8_t orientation = 3;
  pi_himax_conf_init(&config);
  config.format = PI_CAMERA_QVGA;
  pi_open_from_conf(camera, &config);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: opening Himax\n");
  if (pi_camera_open(camera)) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: open failed\n");
    return -1;
  }
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: opened\n");
  /* Проверенная последовательность инициализации из wifi-img-streamer. */
  pi_camera_control(camera, PI_CAMERA_CMD_START, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: started\n");
  pi_camera_reg_set(camera, 0x0101, &orientation);
  pi_time_wait_us(1000000);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: orientation set\n");
  pi_camera_control(camera, PI_CAMERA_CMD_STOP, 0);
  pi_camera_control(camera, PI_CAMERA_CMD_AEG_INIT, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 camera: ready\n");
  return 0;
}

static void preprocess(const uint8_t *camera, uint8_t *input) {
  memset(input, 0, INPUT_BYTES);
  /* Native Himax QVGA is 324x244. Downsample to 160x120 and letterbox it
   * exactly as during training, preserving the camera aspect ratio. */
  for (int y = 0; y < 120; ++y) {
    const int source_y = y * CAMERA_HEIGHT / 120;
    uint8_t *out = input + (y + 20) * 160;
    const uint8_t *in = camera + source_y * CAMERA_WIDTH;
    for (int x = 0; x < 160; ++x) out[x] = in[x * CAMERA_WIDTH / 160];
  }
}
#else
/* Synthetic 160x160 grayscale: letterbox-like dark bars + bright box.
 * Used when Himax is unavailable; only for bring-up / inference timing. */
static void fill_static_input(uint8_t *input) {
  memset(input, 24, INPUT_BYTES);
  for (int y = 20; y < 140; ++y) {
    memset(input + y * 160, 48, 160);
  }
  for (int y = 50; y < 110; ++y) {
    memset(input + y * 160 + 50, 200, 60);
  }
}
#endif

static void report(const int32_t *output) {
  int32_t best = INT32_MIN; int best_class = 0, best_row = 0, best_col = 0;
  for (int row = 0; row < 20; ++row) for (int col = 0; col < 20; ++col)
    for (int cls = 0; cls < 21; ++cls) {
      int32_t value = output[((row * 20 + col) * 25) + cls];
      if (value > best) { best = value; best_class = cls; best_row = row; best_col = col; }
    }
  const int32_t *box = output + ((best_row * 20 + best_col) * 25) + 21;
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 top=%s logit=%ld cell=%d,%d box_raw=[%ld,%ld,%ld,%ld] cycles=%d\n", names[best_class], (long)best, best_col, best_row, (long)box[0], (long)box[1], (long)box[2], (long)box[3], cycle_network_execution);
}

/* The detector head uses the exact DORY-packed weights: 3x3x80x25 int8,
 * followed by 25 int32 biases. It runs on FC to avoid the GAP8 cluster's
 * int32 worker issue; backbone layers still run on all eight cluster cores. */
static void detector_head_fc(const uint8_t *input, int32_t *output) {
  const int8_t *weights = (const int8_t *)(coco20_dory_weights + HEAD_WEIGHT_OFFSET);
  const int32_t *bias = (const int32_t *)(coco20_dory_weights + HEAD_WEIGHT_OFFSET + 18000);
  for (int oy = 0; oy < 20; ++oy) for (int ox = 0; ox < 20; ++ox) for (int oc = 0; oc < 25; ++oc) {
    int32_t sum = bias[oc];
    const int8_t *kernel = weights + oc * 9 * 80;
    for (int ky = 0; ky < 3; ++ky) {
      int iy = oy + ky - 1;
      if (iy < 0 || iy >= 20) continue;
      for (int kx = 0; kx < 3; ++kx) {
        int ix = ox + kx - 1;
        if (ix < 0 || ix >= 20) continue;
        const uint8_t *pixel = input + (iy * 20 + ix) * 80;
        const int8_t *coeff = kernel + (ky * 3 + kx) * 80;
        for (int ic = 0; ic < 80; ++ic) sum += (int32_t)pixel[ic] * coeff[ic];
      }
    }
    output[(oy * 20 + ox) * 25 + oc] = sum;
  }
}

static void detector_task(void *arg) {
  (void)arg;
  uint8_t *arena;
#if COCO20_USE_CAMERA
  uint8_t *camera_buffer;
#endif
  /* Даём CPX-маршрутизатору ESP32 время на запуск, как в штатном стримере. */
  vTaskDelay(2000);
  setup_wifi_ap();
  /* setup_wifi_ap uses the same packet for control commands. Restore the
   * application route before sending image bytes. */
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &wifi_tx.route);
  /* Raise clocks only after CPX is alive. Doing this in main() before
   * pmsis_kickoff hangs this board (see runtime_probe.c); wifi-img-streamer
   * can do it earlier, but coco20 cannot. */
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 raising FC to 150 MHz\n");
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  pi_freq_set(PI_FREQ_DOMAIN_FC, 150000000);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 freq fc=%u cl=%u\n",
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_FC),
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_CL));
#if COCO20_USE_CAMERA
  camera_buffer = pi_l2_malloc(CAMERA_BYTES);
  if (!camera_buffer) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20: L2 allocation failed camera=%p\n", camera_buffer);
    return;
  }
  if (open_himax(&camera)) { cpxPrintToConsole(LOG_TO_CRTP, "coco20: camera open failed\n"); return; }
  /* Himax использует временную L2-память при открытии; выделяем арену
   * детектора только после завершения инициализации камеры. */
#endif
  arena = pi_l2_malloc(L2_ARENA_BYTES);
  if (!arena) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20: L2 allocation failed arena=%p\n", arena);
    return;
  }
  network_initialize();
#if COCO20_USE_CAMERA
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 timing mode: camera raw-stream=%d model=160x160\n",
                    STREAM_RAW_IMAGES);
#else
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 timing mode: static (no Himax) model=160x160\n");
#endif
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 freq fc=%u cl=%u\n",
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_FC),
                    (unsigned)pi_freq_get(PI_FREQ_DOMAIN_CL));
  unsigned int frame = 0;
  while (1) {
    const uint32_t t0 = pi_time_get_us();
#if COCO20_USE_CAMERA
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
    pi_camera_capture(&camera, camera_buffer, CAMERA_BYTES);
    pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
    const uint32_t t_capture = pi_time_get_us();
#if STREAM_RAW_IMAGES
    send_image(camera_buffer);
#endif
    const uint32_t t_stream = pi_time_get_us();
    int32_t *head_output = (int32_t *)(arena + L2_ARENA_BYTES - 20 * 20 * 25 * sizeof(int32_t));
    preprocess(camera_buffer, arena);
    const uint32_t t_preprocess = pi_time_get_us();
#else
    const uint32_t t_capture = t0;
    const uint32_t t_stream = t0;
    int32_t *head_output = (int32_t *)(arena + L2_ARENA_BYTES - 20 * 20 * 25 * sizeof(int32_t));
    fill_static_input(arena);
    const uint32_t t_preprocess = pi_time_get_us();
#endif
    network_run(arena, L2_ARENA_BYTES, head_output, 0, 1);
    const uint32_t t_inference = pi_time_get_us();
    if (frame == 0) {
      cpxPrintToConsole(LOG_TO_CRTP, "coco20 freq_after_cluster fc=%u cl=%u\n",
                        (unsigned)pi_freq_get(PI_FREQ_DOMAIN_FC),
                        (unsigned)pi_freq_get(PI_FREQ_DOMAIN_CL));
    }
    if (++frame % REPORT_EVERY == 0) {
      cpxPrintToConsole(LOG_TO_CRTP,
        "coco20 timing us: capture=%lu stream=%lu preprocess=%lu inference=%lu camera_to_result=%lu\n",
        (unsigned long)(t_capture - t0), (unsigned long)(t_stream - t_capture),
        (unsigned long)(t_preprocess - t_stream), (unsigned long)(t_inference - t_preprocess),
        (unsigned long)(t_inference - t0));
      report(head_output);
    }
    pi_time_wait_us(DETECTOR_PERIOD_US);
  }
}

static void application(void) {
  cpxInit();
  cpxEnableFunction(CPX_F_WIFI_CTRL);
#if COCO20_USE_CAMERA
  cpxPrintToConsole(LOG_TO_CRTP, "-- coco20 detector streamer --\n");
#else
  cpxPrintToConsole(LOG_TO_CRTP, "-- coco20 detector static (no Himax) --\n");
#endif
  if (xTaskCreate(wifi_control_task, "wifi-ctrl", configMINIMAL_STACK_SIZE * 2,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS ||
      xTaskCreate(detector_task, "detector", configMINIMAL_STACK_SIZE * 6,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20: task creation failed\n");
    return;
  }
  while (1) pi_yield();
}
int main(void) {
  pi_bsp_init();
  return pmsis_kickoff((void *)application);
}
