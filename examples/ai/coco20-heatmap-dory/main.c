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
#define HEAD_OUT_ELEMS (20 * 20 * 25)
#define HEAD_OUT_BYTES (HEAD_OUT_ELEMS * sizeof(int32_t))
#define FEAT_BYTES 32000
#ifndef STREAM_RAW_IMAGES
#define STREAM_RAW_IMAGES 0
#endif
#ifndef REPORT_EVERY
#define REPORT_EVERY 1
#endif
#ifndef REPORT_PERIOD_US
#define REPORT_PERIOD_US 2000000
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
static pi_task_t camera_task;
static EventGroupHandle_t camera_ev;
#define CAPTURE_DONE_BIT (1 << 0)
/* Static L2 frame — open Himax before any large pi_l2_malloc (stop1 lesson). */
static PI_L2 uint8_t camera_frame[CAMERA_BYTES];

static void capture_done_cb(void *arg) {
  (void)arg;
  xEventGroupSetBits(camera_ev, CAPTURE_DONE_BIT);
}

/* Same order as wifi-img-streamer / stop1: async capture, then START, wait, STOP.
 * Blocking pi_camera_capture() after START hangs on this AI-deck. */
static void capture_frame(uint8_t *dst) {
  pi_camera_capture_async(&camera, dst, CAMERA_BYTES,
                          pi_task_callback(&camera_task, capture_done_cb, NULL));
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  xEventGroupWaitBits(camera_ev, CAPTURE_DONE_BIT, pdTRUE, pdFALSE,
                      (TickType_t)portMAX_DELAY);
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
}
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

#ifndef HIMAX21_MIN_LOGIT
/* DORY head is int32 NCHW [25,20,20]; GVSOC static peak class logit ≈ -2717. */
#define HIMAX21_MIN_LOGIT 0
#endif
#ifndef HIMAX21_TEMPORAL_REQUIRED
#define HIMAX21_TEMPORAL_REQUIRED 2
#endif

static int himax21_temporal_hits;
static uint32_t last_print_us;

/* Q8 exp(-k) for k=0..8: 256, 94, 35, 13, 5, 2, 1, 1, 0 */
static const uint16_t kExpQ8[9] = {256, 94, 35, 13, 5, 2, 1, 1, 0};

/* Softmax % over 21 class peaks. Temperature tracks the peak range so
 * both GVSOC-scale (~1e3) and the current ~4e8 garbage logits stay finite. */
static int softmax_pct(const int32_t peak[21], int best_class) {
  int32_t mx = peak[best_class];
  int32_t mn = mx;
  int i;
  for (i = 0; i < 21; ++i)
    if (peak[i] < mn) mn = peak[i];
  int32_t T = (mx - mn) / 8;
  if (T < 1) T = 1;
  uint32_t sum = 0;
  uint16_t e_best = 0;
  for (i = 0; i < 21; ++i) {
    int32_t d = mx - peak[i];
    int b = (int)((d + T / 2) / T);
    if (b < 0) b = 0;
    if (b > 8) b = 8;
    uint16_t e = kExpQ8[b];
    sum += e;
    if (i == best_class) e_best = e;
  }
  if (sum == 0) return 0;
  int pct = (int)((100u * e_best + sum / 2) / sum);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

/* DORY Convolution4 writes int32 NCHW: index = cls*H*W + row*W + col. */
static inline int32_t out_at(const int32_t *output, int cls, int row, int col) {
  return output[cls * 20 * 20 + row * 20 + col];
}

static int is_local_max(const int32_t *output, int cls, int row, int col) {
  int32_t center = out_at(output, cls, row, col);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int r = row + dy, c = col + dx;
      if (r < 0 || r >= 20 || c < 0 || c >= 20) continue;
      if (out_at(output, cls, r, c) > center) return 0;
    }
  }
  return 1;
}

static void report(const int32_t *output) {
  int32_t best = INT32_MIN;
  int32_t peak[21];
  int best_class = 0, best_row = 0, best_col = 0;
  int cls;
  for (cls = 0; cls < 21; ++cls) peak[cls] = INT32_MIN;
  /* Class heatmaps are channels 0..20; channels 21..24 are box (skip). */
  for (cls = 0; cls < 21; ++cls) {
    for (int row = 0; row < 20; ++row) {
      for (int col = 0; col < 20; ++col) {
        int32_t value = out_at(output, cls, row, col);
        if (value > peak[cls]) peak[cls] = value;
        if (!is_local_max(output, cls, row, col)) continue;
        if (value > best) {
          best = value;
          best_class = cls;
          best_row = row;
          best_col = col;
        }
      }
    }
  }
  const int instant = (best >= (int32_t)HIMAX21_MIN_LOGIT);
  if (instant) {
    if (himax21_temporal_hits < HIMAX21_TEMPORAL_REQUIRED) himax21_temporal_hits += 1;
  } else {
    himax21_temporal_hits = 0;
  }
  const int present = (himax21_temporal_hits >= HIMAX21_TEMPORAL_REQUIRED);
  const int pct = softmax_pct(peak, best_class);
  const uint32_t now = pi_time_get_us();
  if (last_print_us != 0 && (now - last_print_us) < (uint32_t)REPORT_PERIOD_US)
    return;
  last_print_us = now;
  if (!present) {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 none L=%ld p=%d\n", (long)best, pct);
  } else {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 see=%s L=%ld p=%d c=%d,%d\n",
                      names[best_class], (long)best, pct, best_col, best_row);
  }
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
    output[oc * 400 + oy * 20 + ox] = sum;
  }
}

static void __attribute__((unused)) dump_head_layout(const int32_t *output) {
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  for (int i = 0; i < HEAD_OUT_ELEMS; ++i) {
    if (output[i] < mn) mn = output[i];
    if (output[i] > mx) mx = output[i];
  }
  int32_t best_n = INT32_MIN;
  int nc = 0, nr = 0, ncol = 0;
  for (int cls = 0; cls < 21; ++cls)
    for (int row = 0; row < 20; ++row)
      for (int col = 0; col < 20; ++col) {
        int32_t v = output[cls * 400 + row * 20 + col];
        if (v > best_n) {
          best_n = v;
          nc = cls;
          nr = row;
          ncol = col;
        }
      }
  int32_t best_h = INT32_MIN;
  int hc = 0, hr = 0, hcol = 0;
  for (int row = 0; row < 20; ++row)
    for (int col = 0; col < 20; ++col)
      for (int cls = 0; cls < 21; ++cls) {
        int32_t v = output[(row * 20 + col) * 25 + cls];
        if (v > best_h) {
          best_h = v;
          hc = cls;
          hr = row;
          hcol = col;
        }
      }
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 rng %ld..%ld\n", (long)mn, (long)mx);
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 nchw L=%ld %s c=%d,%d\n",
                    (long)best_n, names[nc], ncol, nr);
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 hwc L=%ld %s c=%d,%d\n",
                    (long)best_h, names[hc], hcol, hr);
}

static void detector_task(void *arg) {
  (void)arg;
  uint8_t *arena;
  int32_t *head_output;
#if COCO20_USE_CAMERA
  uint8_t *camera_buffer = camera_frame;
#endif
  /* Даём CPX-маршрутизатору ESP32 время на запуск, как в штатном стримере. */
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 wait cpx\n");
  vTaskDelay(2000);
  /* SoftAP / PMU / FLL have wedged this deck after JTAG on recent boots
   * (hang after "wifi ap" or inside __pi_pmu_voltage_set). Detection only
   * needs CRTP — bring those back once see/none is stable. */
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 skip wifi/pmu\n");
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &wifi_tx.route);
#if COCO20_USE_CAMERA
  /* Open Himax BEFORE large L2 allocs — pi_l2_malloc(79KiB) first wedges open. */
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 open cam\n");
  if (open_himax(&camera)) {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 fail cam\n");
    return;
  }
#endif
  arena = pi_l2_malloc(L2_ARENA_BYTES);
  if (!arena) {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 fail arena\n");
    return;
  }
  /* No extra L2 malloc: 200 KiB DORY arena + 79 KiB camera is the budget.
   * After preprocess, cluster writes 32 KiB features into camera_frame[0..],
   * FC head writes int32 logits into camera_frame[FEAT_BYTES..] (40 KiB).
   * Next capture overwrites the whole frame. */
#if COCO20_USE_CAMERA
  head_output = (int32_t *)(camera_buffer + FEAT_BYTES);
#else
  head_output = (int32_t *)(arena + L2_ARENA_BYTES - HEAD_OUT_BYTES);
#endif
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 net init\n");
  network_initialize();
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 loop\n");
  /* Let CRTP drain boot spam before first capture. */
  pi_time_wait_us(200000);
  unsigned int frame = 0;
  while (1) {
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 capt %u\n", frame);
#if COCO20_USE_CAMERA
    capture_frame(camera_buffer);
#if STREAM_RAW_IMAGES
    send_image(camera_buffer);
#endif
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 prep %u\n", frame);
    preprocess(camera_buffer, arena);
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 infer %u\n", frame);
    network_run(arena, L2_ARENA_BYTES, camera_buffer, 0, 1);
    detector_head_fc(camera_buffer, head_output);
#else
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 prep %u\n", frame);
    fill_static_input(arena);
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 infer %u\n", frame);
    network_run(arena, L2_ARENA_BYTES, head_output, 0, 1);
#endif
    if (frame < 3) cpxPrintToConsole(LOG_TO_CRTP, "himax21 infer ok %u\n", frame);
    if (++frame % REPORT_EVERY == 0) {
      report(head_output);
    }
    pi_time_wait_us(DETECTOR_PERIOD_US);
  }
}

static void application(void) {
  cpxInit();
  cpxEnableFunction(CPX_F_WIFI_CTRL);
  /* Short boot line first — proves CPX→CRTP before Wi‑Fi/PMU. */
  cpxPrintToConsole(LOG_TO_CRTP, "himax21 boot\n");
#if COCO20_USE_CAMERA
  camera_ev = xEventGroupCreate();
  if (camera_ev == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 fail ev\n");
    return;
  }
#endif
  if (xTaskCreate(wifi_control_task, "wifi-ctrl", configMINIMAL_STACK_SIZE * 2,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS ||
      xTaskCreate(detector_task, "detector", configMINIMAL_STACK_SIZE * 24,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "himax21 fail tasks\n");
    return;
  }
  while (1) pi_yield();
}
int main(void) {
  pi_bsp_init();
  return pmsis_kickoff((void *)application);
}
