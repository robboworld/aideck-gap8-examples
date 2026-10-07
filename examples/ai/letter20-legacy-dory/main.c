#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "pmsis.h"
#include "bsp/bsp.h"
#include "bsp/camera/himax.h"
#include "cpx.h"
#include "wifi.h"
#include "network.h"
#include "letter20_commands.h"

extern int cycle_network_execution;

#define CAMERA_WIDTH 324
#define CAMERA_HEIGHT 244
#define CAMERA_BYTES (CAMERA_WIDTH * CAMERA_HEIGHT)
#define INPUT_BYTES (160 * 160)
#define GRID 20
#define HEAD_CHANNELS 25
#define HEAD_OUT_BYTES (GRID * GRID * HEAD_CHANNELS * sizeof(int32_t))
#define FEAT_BYTES (GRID * GRID * 48)
#define L2_ARENA_BYTES 200000
#define LETTER20_EMPTY_ID 19
/* NEMO int32 head ≈ 2466 per logit (val median). Val: empty frames best-cell
 * obj <= -11016, letter frames p10 = -2220. Gate at about -2 logits. */
#ifndef OBJ_GATE_INT32
#define OBJ_GATE_INT32 (-5000)
#endif

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
/* 0 under JTAG_HYPER — CPX console wedges; enable after cold boot without probe. */
#ifndef LETTER20_CRTP_REPORT
#define LETTER20_CRTP_REPORT 0
#endif
#ifndef LETTER20_SKIP_HEAD
#define LETTER20_SKIP_HEAD 0
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

static inline void l2_phase(uint32_t code) {
  /* FC TCDM — NOT L2 heap (pi_l2_malloc arena would overwrite 0x1c05xxxx). */
  *(volatile uint32_t *)0x1b000800 = code;
}
static inline void l2_beat(void) {
  *(volatile uint32_t *)0x1b000810 += 1u;
}
#if LETTER20_USE_CAMERA
static struct pi_device camera;
static pi_task_t camera_task;
static EventGroupHandle_t camera_ev;
#define CAPTURE_DONE_BIT (1 << 0)
/* Static L2 — open Himax before any large pi_l2_malloc. */
static PI_L2 uint8_t camera_frame[CAMERA_BYTES];
static PI_L2 uint8_t feat_l2[FEAT_BYTES] __attribute__((aligned(16)));
static PI_L2 int32_t head_l2[GRID * GRID * HEAD_CHANNELS] __attribute__((aligned(16)));

static volatile uint32_t capture_done_flag;

static void capture_done_cb(void *arg) {
  (void)arg;
  capture_done_flag = 1;
  if (camera_ev)
    xEventGroupSetBits(camera_ev, CAPTURE_DONE_BIT);
}

/* Async capture + START/STOP. Poll a flag — EventGroup alone can wedge when
 * the FreeRTOS tick is dead under JTAG_HYPER. */
static void capture_frame(uint8_t *dst) {
  l2_phase(0xB00700D0u);
  capture_done_flag = 0;
  memset(&camera_task, 0, sizeof(camera_task));
  l2_phase(0xB00700D1u);
  pi_camera_capture_async(&camera, dst, CAMERA_BYTES,
                          pi_task_callback(&camera_task, capture_done_cb, NULL));
  l2_phase(0xB00700D2u);
  pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
  l2_phase(0xB00700D3u);
  {
    const uint32_t t0 = pi_time_get_us();
    while (!capture_done_flag) {
      if ((pi_time_get_us() - t0) > 2000000u) {
        l2_phase(0xDEAD00CAu);
        break;
      }
    }
  }
  l2_phase(0xB00700D4u);
  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  pi_time_wait_us(50000);
  l2_phase(0xB00700D5u);
}
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
  pi_himax_conf_init(&config);
  config.format = PI_CAMERA_QVGA;
  pi_open_from_conf(camera, &config);
  l2_phase(0xB0070020u);
  if (pi_camera_open(camera)) {
    l2_phase(0xDEAD0020u);
    return -1;
  }
  l2_phase(0xB0070021u);
  /* Skip START/reg_set/1s wait — reg_set hung under JTAG_HYPER. AEG on first
   * capture path instead. Software 180° (H+V) after capture matches streamer
   * mount (IMG_ORIENTATION=3) without touching Himax regs. */
  pi_camera_control(camera, PI_CAMERA_CMD_AEG_INIT, 0);
  l2_phase(0xB0070024u);
  return 0;
}

/* Himax raw is upside-down on this deck mount; H+V = reverse byte order. */
static void flip_hv_inplace(uint8_t *img) {
  for (int i = 0, j = CAMERA_BYTES - 1; i < j; ++i, --j) {
    const uint8_t t = img[i];
    img[i] = img[j];
    img[j] = t;
  }
}

static void preprocess(const uint8_t *camera, uint8_t *input) {
  /* Top-center 160x160 after H+V flip — A sits in the upper band. */
  const int y0 = 0;
  const int x0 = (CAMERA_WIDTH - 160) / 2;
  for (int y = 0; y < 160; ++y) {
    const uint8_t *in = camera + (y0 + y) * CAMERA_WIDTH + x0;
    memcpy(input + y * 160, in, 160);
  }
}
#endif

/* Synthetic 160x160 (24 background, 200 square at [50,110)). PC reference via
 * the NEMO int8 ONNX: layer-3 byte-sum 171708, head byte-sum 7030061,
 * cell0 = -19279 612 -9565 -5947. */
#define SELFTEST_FEAT_SUM 171708UL
#define SELFTEST_HEAD_SUM 7030061UL
static void fill_static_input(uint8_t *input) {
  memset(input, 24, INPUT_BYTES);
  for (int y = 50; y < 110; ++y) {
    memset(input + y * 160 + 50, 200, 60);
  }
}

static unsigned long byte_sum(const void *data, size_t bytes) {
  const uint8_t *raw = (const uint8_t *)data;
  unsigned long sum = 0;
  for (size_t i = 0; i < bytes; ++i) sum += raw[i];
  return sum;
}

/* 3x3 int8→int32 head on 8 cluster cores (same math as the old FC loop).
 * DORY Convolution4 still skipped — DMA/tiling wrote garbage int32. */

void report_legacy(const int32_t *output) {
  /* DORY head is HWC: cell-major, channels last (1 obj + 20 cls + 4 box). */
  int32_t best_score = INT32_MIN;
  int best_class = 0, best_row = 0, best_col = 0;
  int32_t best_obj = 0;
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  const int elems = GRID * GRID * HEAD_CHANNELS;

  for (int i = 0; i < elems; ++i) {
    if (output[i] < mn) mn = output[i];
    if (output[i] > mx) mx = output[i];
  }

  /* Rank letter cells only (class 19 = empty is background). PC decode uses
   * sigmoid(obj)*softmax(cls) >= 0.25; additive int32 ranking + an objectness
   * gate is the closest cheap equivalent. */
  for (int row = 0; row < GRID; ++row) {
    for (int col = 0; col < GRID; ++col) {
      const int base = (row * GRID + col) * HEAD_CHANNELS;
      const int32_t objectness = output[base];
      int32_t class_logit = INT32_MIN;
      int class_id = 0;
      for (int cls = 0; cls < LETTER20_EMPTY_ID; ++cls) {
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
  const int present = best_obj > OBJ_GATE_INT32;
  const int shown = present ? best_class : LETTER20_EMPTY_ID;

  /* JTAG-visible status in FC TCDM (not L2 heap):
   * 0x1b000800 = 0xA11E0000 | (frame&0xff) when reporting
   * 0x1b000804 = shown class id (0=A .. 19=empty)
   * 0x1b000808 = best_obj
   * 0x1b00080c = best_score
   */
  {
    volatile uint32_t *st = (volatile uint32_t *)0x1b000800;
    static uint32_t frame_ctr;
    st[0] = 0xA11E0000u | (frame_ctr++ & 0xffu);
    st[1] = (uint32_t)shown;
    st[2] = (uint32_t)best_obj;
    st[3] = (uint32_t)best_score;
  }
#if LETTER20_CRTP_REPORT
  {
    static unsigned skip;
    if ((++skip % 10u) == 1u) {
      cpxPrintToConsole(
          LOG_TO_CRTP,
          "letter20 top=%s cmd=%s obj=%ld score=%ld cell=%d,%d best=%s\n",
          letter20_names[shown], letter20_commands[shown],
          (long)best_obj, (long)best_score, best_col, best_row,
          letter20_names[best_class]);
    }
  }
#else
  (void)mn;
  (void)mx;
  (void)best_col;
  (void)best_row;
#endif
}

static void detector_task(void *arg) {
  (void)arg;
  uint8_t *arena;
  uint8_t *feat_buf = NULL;
  int32_t *head_output;
  uint32_t last_report_us = 0;
#if LETTER20_USE_CAMERA
  uint8_t *camera_buffer = camera_frame;
  feat_buf = feat_l2;
  head_output = head_l2;
#endif

  l2_phase(0xB0070010u);
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &wifi_tx.route);
  (void)setup_wifi_ap;
  l2_phase(0xB0070011u);
#if LETTER20_USE_CAMERA
  if (open_himax(&camera)) {
    l2_phase(0xDEAD0011u);
    return;
  }
  l2_phase(0xB0070012u);
#endif
  arena = pi_l2_malloc(L2_ARENA_BYTES);
  if (!arena) {
    l2_phase(0xDEAD0012u);
    return;
  }
  l2_phase(0xB0070013u);
#if LETTER20_USE_CAMERA
  /* feat_l2 / head_l2 are static PI_L2 — avoid heap after Himax. */
#else
  head_output = (int32_t *)pi_l2_malloc(HEAD_OUT_BYTES);
  if (!head_output) {
    l2_phase(0xDEAD0013u);
    return;
  }
#endif
  l2_phase(0xB0070014u);
  network_initialize();
  l2_phase(0xB0070015u);
  unsigned int frame = 0;
  while (1) {
    l2_phase(0xB0070060u | (frame & 0xffu));
    const uint32_t t0 = pi_time_get_us();
#if LETTER20_USE_CAMERA
    if (frame == 0) {
      capture_frame(camera_buffer);
      flip_hv_inplace(camera_buffer);
      preprocess(camera_buffer, arena);
    }
    const uint32_t t_capture = pi_time_get_us();
    const uint32_t t_stream = t_capture;
    const uint32_t t_preprocess = t_capture;
#else
    const uint32_t t_capture = t0;
    const uint32_t t_stream = t0;
    fill_static_input(arena);
    const uint32_t t_preprocess = pi_time_get_us();
#endif
    l2_phase(0xB0070070u | (frame & 0xffu));
#if LETTER20_USE_CAMERA
    network_prepare_inline_head();
    letter20_pending_feat = feat_buf;
    letter20_pending_head_out = head_output;
    network_run(arena, L2_ARENA_BYTES, feat_buf, 0, 1);
    letter20_pending_head_out = NULL;
    letter20_pending_feat = NULL;
    l2_phase(0xB0070088u | (frame & 0xffu));
#else
    network_run(arena, L2_ARENA_BYTES, arena + INPUT_BYTES, 0, 1);
    l2_phase(0xB0070080u | (frame & 0xffu));
#if !LETTER20_SKIP_HEAD
    network_run_head(arena + INPUT_BYTES, head_output);
#endif
#endif
#if LETTER20_SKIP_HEAD
    memset(head_output, 0, HEAD_OUT_BYTES);
#endif
    l2_phase(0xB0070090u | (frame & 0xffu));
    const uint32_t t_inference = pi_time_get_us();
    ++frame;
    last_report_us = t_inference;
    report_legacy(head_output);
    l2_phase(0xB00700A0u | ((frame - 1) & 0xffu));
    l2_beat();
#if LETTER20_USE_CAMERA
    {
      unsigned sticky = 0;
      while (1) {
        l2_beat();
        report_legacy(head_output);
        vTaskDelay(pdMS_TO_TICKS(200));
        if (++sticky >= 10u) {
          sticky = 0;
          capture_frame(camera_buffer);
          flip_hv_inplace(camera_buffer);
          preprocess(camera_buffer, arena);
          l2_phase(0xB0070070u | (frame & 0xffu));
          network_prepare_inline_head();
          letter20_pending_feat = feat_buf;
          letter20_pending_head_out = head_output;
          network_run(arena, L2_ARENA_BYTES, feat_buf, 0, 1);
          letter20_pending_head_out = NULL;
          letter20_pending_feat = NULL;
          l2_phase(0xB0070090u | (frame & 0xffu));
          ++frame;
          report_legacy(head_output);
          l2_phase(0xB00700A0u | ((frame - 1) & 0xffu));
        }
      }
    }
#else
    pi_time_wait_us(2000);
    l2_phase(0xB00700C0u | ((frame - 1) & 0xffu));
#endif
  }
}

static void application(void) {
  /* Start detector before any CPX console I/O — SPI to ESP often wedges
   * after JTAG load and cpxPrintToConsole would stall bring-up. */
  l2_phase(0xB0070001u);
  cpxInit();
  l2_phase(0xB0070002u);
  cpxEnableFunction(CPX_F_WIFI_CTRL);
  l2_phase(0xB0070003u);
  /* Leave FLL/PMU at ROM defaults after JTAG — raising clocks wedges/crashes. */
  l2_phase(0xB0070004u);
#if LETTER20_USE_CAMERA
  camera_ev = xEventGroupCreate();
  if (camera_ev == NULL) {
    l2_phase(0xDEAD0001u);
    return;
  }
#endif
  if (xTaskCreate(wifi_control_task, "wifi-ctrl", configMINIMAL_STACK_SIZE * 2,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS ||
      xTaskCreate(detector_task, "detector", configMINIMAL_STACK_SIZE * 24,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
    l2_phase(0xDEAD0002u);
    return;
  }
  l2_phase(0xB0070005u);
  /* Best-effort boot line — must not block task bring-up. */
  cpxPrintToConsole(LOG_TO_CRTP, "letter20 boot\n");
  while (1) pi_yield();
}

int main(void) {
  pi_bsp_init();
  /* Do not set FLL/PMU here — after JTAG this deck can wedge before CPX.
   * Clocks go up after the first CRTP line. */
  return pmsis_kickoff((void *)application);
}
