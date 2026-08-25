#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "pmsis.h"
#include "bsp/bsp.h"
#include "bsp/camera/himax.h"
#include "bsp/buffer.h"
#include "gaplib/jpeg_encoder.h"
#include "stdio.h"
#include "cpx.h"
#include "wifi.h"
#include "network.h"
#ifndef STOP1_STREAM_ONLY
#include "embedded_weights.h"
#endif
#include "stop1_commands.h"

extern int cycle_network_execution;

/* Camera / stream path mirrors examples/other/wifi-img-streamer. */
#define IMG_ORIENTATION 0x0101
#define CAM_WIDTH 324
#define CAM_HEIGHT 244
#define CAMERA_BYTES (CAM_WIDTH * CAM_HEIGHT)
#define INPUT_BYTES (160 * 160)
#define GRID 20
#define HEAD_CHANNELS 6
#define L2_ARENA_BYTES 200000
/*
 * Present gate on DORY int32 head: score = objectness + class logit (CHW).
 * Calibrated on v3 w24 vs float board-aligned F1 optimum (~8.26):
 *   scale≈23866 (corr≈0.999 on calib input) → RAW_MIN≈197000.
 * Old default (-2e6) treated almost every frame as present (empty-scene FP).
 */
#ifndef STOP1_PRESENT_RAW_MIN
#define STOP1_PRESENT_RAW_MIN (197000L)
#endif
/* Require this many consecutive above-threshold frames before present=1. */
#ifndef STOP1_TEMPORAL_REQUIRED
#define STOP1_TEMPORAL_REQUIRED 2
#endif

#ifndef STREAM_RAW_IMAGES
#define STREAM_RAW_IMAGES 0
#endif
#ifndef REPORT_EVERY
#define REPORT_EVERY 1
#endif
#ifndef DETECTOR_PERIOD_US
#define DETECTOR_PERIOD_US 0
#endif
#ifndef DETECTOR_EVERY_N
#define DETECTOR_EVERY_N 4
#endif
#ifndef DETECTOR_START_FRAME
#define DETECTOR_START_FRAME 60
#endif
#ifndef DETECTOR_SPAWN_FRAME
#define DETECTOR_SPAWN_FRAME 50
#endif
#if defined(STOP1_INPUT_STATIC)
#define STOP1_USE_CAMERA 0
#elif defined(STOP1_INPUT_CAMERA)
#define STOP1_USE_CAMERA 1
#else
#define STOP1_USE_CAMERA 1
#endif

typedef struct {
  uint8_t magic;
  uint16_t width;
  uint16_t height;
  uint8_t depth;
  uint8_t type;
  uint32_t size;
} __attribute__((packed)) img_header_t;

typedef enum {
  RAW_ENCODING = 0,
  JPEG_ENCODING = 1
} __attribute__((packed)) StreamerMode_t;

static int wifiClientConnected = 0;
static CPXPacket_t rxp;
static CPXPacket_t txp;

#if STOP1_USE_CAMERA
static pi_task_t task1;
static unsigned char *imgBuff;
static struct pi_device camera;
static pi_buffer_t buffer;
static EventGroupHandle_t evGroup;
#define CAPTURE_DONE_BIT (1 << 0)

static jpeg_encoder_t jpeg_encoder;
static pi_buffer_t header;
static uint32_t headerSize;
static pi_buffer_t footer;
static uint32_t footerSize;
static pi_buffer_t jpeg_data;
static uint32_t jpegSize;
#if STREAM_RAW_IMAGES
static StreamerMode_t streamerMode = RAW_ENCODING;
#else
static StreamerMode_t streamerMode = JPEG_ENCODING;
#endif
#endif

static void rx_task(void *parameters) {
  (void)parameters;
  while (1) {
    cpxReceivePacketBlocking(CPX_F_WIFI_CTRL, &rxp);
    WiFiCTRLPacket_t *wifiCtrl = (WiFiCTRLPacket_t *)rxp.data;
    switch (wifiCtrl->cmd) {
      case WIFI_CTRL_STATUS_WIFI_CONNECTED:
        cpxPrintToConsole(LOG_TO_CRTP, "Wifi connected (%u.%u.%u.%u)\n",
                          wifiCtrl->data[0], wifiCtrl->data[1],
                          wifiCtrl->data[2], wifiCtrl->data[3]);
        break;
      case WIFI_CTRL_STATUS_CLIENT_CONNECTED:
        cpxPrintToConsole(LOG_TO_CRTP, "Wifi client connection status: %u\n",
                          wifiCtrl->data[0]);
        wifiClientConnected = wifiCtrl->data[0];
        break;
      default:
        break;
    }
  }
}

#ifdef SETUP_WIFI_AP
static void setupWiFi(void) {
  static char ssid[] = "AI Deck stop1";
  cpxPrintToConsole(LOG_TO_CRTP, "Setting up WiFi AP\n");
  txp.route.destination = CPX_T_ESP32;
  rxp.route.source = CPX_T_GAP8;
  txp.route.function = CPX_F_WIFI_CTRL;
  txp.route.version = CPX_VERSION;
  WiFiCTRLPacket_t *wifiCtrl = (WiFiCTRLPacket_t *)txp.data;

  wifiCtrl->cmd = WIFI_CTRL_SET_SSID;
  memcpy(wifiCtrl->data, ssid, sizeof(ssid));
  txp.dataLength = sizeof(ssid);
  cpxSendPacketBlocking(&txp);

  wifiCtrl->cmd = WIFI_CTRL_WIFI_CONNECT;
  wifiCtrl->data[0] = 0x01;
  txp.dataLength = 2;
  cpxSendPacketBlocking(&txp);
}
#endif

#if STOP1_USE_CAMERA
static void capture_done_cb(void *arg) {
  (void)arg;
  xEventGroupSetBits(evGroup, CAPTURE_DONE_BIT);
}

static void createImageHeaderPacket(CPXPacket_t *packet, uint32_t imgSize,
                                    StreamerMode_t imgType) {
  img_header_t *imgHeader = (img_header_t *)packet->data;
  imgHeader->magic = 0xBC;
  imgHeader->width = CAM_WIDTH;
  imgHeader->height = CAM_HEIGHT;
  imgHeader->depth = 1;
  imgHeader->type = imgType;
  imgHeader->size = imgSize;
  packet->dataLength = sizeof(img_header_t);
}

static void sendBufferViaCPX(CPXPacket_t *packet, uint8_t *buf,
                             uint32_t bufferSize) {
  uint32_t offset = 0;
  uint32_t size = 0;
  do {
    size = sizeof(packet->data);
    if (offset + size > bufferSize) {
      size = bufferSize - offset;
    }
    memcpy(packet->data, &buf[offset], size);
    packet->dataLength = size;
    cpxSendPacketBlocking(packet);
    offset += size;
  } while (size == sizeof(packet->data));
}

static int open_pi_camera_himax(struct pi_device *device) {
  struct pi_himax_conf cam_conf;

  pi_himax_conf_init(&cam_conf);
  cam_conf.format = PI_CAMERA_QVGA;
  pi_open_from_conf(device, &cam_conf);
  if (pi_camera_open(device)) {
    return -1;
  }

  /* rotate image — identical to wifi-img-streamer */
  pi_camera_control(device, PI_CAMERA_CMD_START, 0);
  uint8_t set_value = 3;
  uint8_t reg_value;
  pi_camera_reg_set(device, IMG_ORIENTATION, &set_value);
  pi_time_wait_us(1000000);
  pi_camera_reg_get(device, IMG_ORIENTATION, &reg_value);
  if (set_value != reg_value) {
    cpxPrintToConsole(LOG_TO_CRTP, "Failed to rotate camera image\n");
    return -1;
  }
  pi_camera_control(device, PI_CAMERA_CMD_STOP, 0);
  pi_camera_control(device, PI_CAMERA_CMD_AEG_INIT, 0);
  return 0;
}

static void preprocess(const uint8_t *camera_frame, uint8_t *input) {
  memset(input, 0, INPUT_BYTES);
  for (int y = 0; y < 120; ++y) {
    const int source_y = y * CAM_HEIGHT / 120;
    uint8_t *out = input + (y + 20) * 160;
    const uint8_t *in = camera_frame + source_y * CAM_WIDTH;
    for (int x = 0; x < 160; ++x) {
      out[x] = in[x * CAM_WIDTH / 160];
    }
  }
}
#else
static void fill_static_input(uint8_t *input) {
  memset(input, 24, INPUT_BYTES);
  for (int y = 40; y < 120; ++y) {
    memset(input + y * 160 + 40, 200, 80);
  }
}
#endif

static int stop1_temporal_hits;

static void report_stop1(const int32_t *output) {
  int32_t best_score = INT32_MIN;
  int best_row = 0, best_col = 0;
  int32_t best_obj = 0, best_cls = 0;
  for (int row = 0; row < GRID; ++row) {
    for (int col = 0; col < GRID; ++col) {
      const int base = (row * GRID + col) * HEAD_CHANNELS;
      const int32_t objectness = output[base];
      const int32_t class_logit = output[base + 1];
      const int32_t score = objectness + class_logit;
      if (score > best_score) {
        best_score = score;
        best_row = row;
        best_col = col;
        best_obj = objectness;
        best_cls = class_logit;
      }
    }
  }
  const int32_t *box = output + ((best_row * GRID + best_col) * HEAD_CHANNELS) + 2;
  const int instant = (best_score > STOP1_PRESENT_RAW_MIN);
  if (instant) {
    if (stop1_temporal_hits < STOP1_TEMPORAL_REQUIRED) {
      stop1_temporal_hits += 1;
    }
  } else {
    stop1_temporal_hits = 0;
  }
  const int present = (stop1_temporal_hits >= STOP1_TEMPORAL_REQUIRED);
  /* One short line — CRTP console may split long prints mid-field. */
  cpxPrintToConsole(
      LOG_TO_CRTP,
      "stop1 present=%d raw=%d hit=%d/%d obj=%ld cls=%ld score=%ld thr=%ld cell=%d,%d box=%ld,%ld,%ld,%ld cycles=%d\n",
      present, instant, stop1_temporal_hits, STOP1_TEMPORAL_REQUIRED,
      (long)best_obj, (long)best_cls, (long)best_score, (long)STOP1_PRESENT_RAW_MIN,
      best_col, best_row, (long)box[0], (long)box[1], (long)box[2], (long)box[3],
      cycle_network_execution);
}

#if STOP1_USE_CAMERA && !defined(STOP1_STREAM_ONLY)
static TaskHandle_t inferTaskHandle;
static volatile uint8_t infer_dory_ready;
/* Snapshot for inference — allocated only after camera/JPEG buffers succeed. */
static uint8_t *infer_frame_copy;

static void infer_task(void *parameters) {
  (void)parameters;
  uint8_t *arena = (uint8_t *)pi_l2_malloc(L2_ARENA_BYTES);
  if (arena == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP, "stop1 infer: L2 arena alloc failed\n");
    vTaskDelete(NULL);
    return;
  }
  network_initialize();
  infer_dory_ready = 1;
  cpxPrintToConsole(LOG_TO_CRTP, "stop1 DORY ready\n");
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (imgBuff == NULL || infer_frame_copy == NULL) {
      continue;
    }
    memcpy(infer_frame_copy, imgBuff, CAMERA_BYTES);
    int32_t *head_output =
        (int32_t *)(arena + L2_ARENA_BYTES -
                    GRID * GRID * HEAD_CHANNELS * sizeof(int32_t));
    const uint32_t t0 = pi_time_get_us();
    preprocess(infer_frame_copy, arena);
    const uint32_t t_preprocess = pi_time_get_us();
    network_run(arena, L2_ARENA_BYTES, head_output, 0, 1);
    const uint32_t t_inference = pi_time_get_us();
    cpxPrintToConsole(
        LOG_TO_CRTP,
        "stop1 timing us: capture=0 stream=0 preprocess=%lu inference=%lu "
        "camera_to_result=%lu\n",
        (unsigned long)(t_preprocess - t0),
        (unsigned long)(t_inference - t_preprocess),
        (unsigned long)(t_inference - t0));
    report_stop1(head_output);
    pi_time_wait_us(DETECTOR_PERIOD_US);
  }
}
#endif

static void camera_task(void *parameters) {
  (void)parameters;
  unsigned int frame = 0;

  vTaskDelay(2000);

#ifdef SETUP_WIFI_AP
  setupWiFi();
  /* Restore APP route after WiFi-CTRL commands (same as coco20 / wifi-img-streamer). */
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &txp.route);
#endif

#if STOP1_USE_CAMERA
  cpxPrintToConsole(LOG_TO_CRTP, "Starting camera task...\n");
  uint32_t resolution = CAM_WIDTH * CAM_HEIGHT;
  uint32_t captureSize = resolution * sizeof(unsigned char);

  cpxPrintToConsole(LOG_TO_CRTP, "stop1 stream alloc...\n");
  imgBuff = (unsigned char *)pmsis_l2_malloc(captureSize);
  if (imgBuff == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP, "Failed to allocate Memory for Image \n");
    return;
  }
  cpxPrintToConsole(LOG_TO_CRTP, "stop1 stream alloc ok\n");

#if !defined(STOP1_STREAM_ONLY)
  infer_frame_copy = (uint8_t *)pmsis_l2_malloc(CAMERA_BYTES);
  if (infer_frame_copy == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP,
                      "stop1 infer frame alloc failed — stream-only mode\n");
  } else {
    cpxPrintToConsole(LOG_TO_CRTP, "stop1 infer frame ok\n");
  }
#endif

  cpxPrintToConsole(LOG_TO_CRTP, "stop1 opening himax\n");
  if (open_pi_camera_himax(&camera)) {
    cpxPrintToConsole(LOG_TO_CRTP, "Failed to open camera\n");
    return;
  }
  cpxPrintToConsole(LOG_TO_CRTP, "stop1 camera opened\n");

  if (streamerMode == JPEG_ENCODING) {
    struct jpeg_encoder_conf enc_conf;
    jpeg_encoder_conf_init(&enc_conf);
    enc_conf.width = CAM_WIDTH;
    enc_conf.height = CAM_HEIGHT;
    enc_conf.flags = 0;

    if (jpeg_encoder_open(&jpeg_encoder, &enc_conf)) {
      cpxPrintToConsole(LOG_TO_CRTP, "Failed initialize JPEG encoder\n");
      return;
    }

    pi_buffer_init(&buffer, PI_BUFFER_TYPE_L2, imgBuff);
    pi_buffer_set_format(&buffer, CAM_WIDTH, CAM_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY);

    header.size = 1024;
    header.data = pmsis_l2_malloc(1024);
    footer.size = 10;
    footer.data = pmsis_l2_malloc(10);
    jpeg_data.size = 1024 * 15;
    jpeg_data.data = pmsis_l2_malloc(1024 * 15);
    if (header.data == 0 || footer.data == 0 || jpeg_data.data == 0) {
      cpxPrintToConsole(LOG_TO_CRTP, "Could not allocate memory for JPEG image\n");
      return;
    }
    jpeg_encoder_header(&jpeg_encoder, &header, &headerSize);
    jpeg_encoder_footer(&jpeg_encoder, &footer, &footerSize);
  }

  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  cpxPrintToConsole(LOG_TO_CRTP, "stop1 stream loop (jpeg)\n");

  while (1) {
    /* Do not gate on wifiClientConnected: ESP32 join/leave CPX events are
     * often missed; TCP clients on SoftAP never receive frames otherwise. */
    {
      const uint32_t t0 = pi_time_get_us();
      pi_camera_capture_async(&camera, imgBuff, resolution,
                              pi_task_callback(&task1, capture_done_cb, NULL));
      pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
      xEventGroupWaitBits(evGroup, CAPTURE_DONE_BIT, pdTRUE, pdFALSE,
                          (TickType_t)portMAX_DELAY);
      pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
      const uint32_t t_capture = pi_time_get_us();

      if (streamerMode == JPEG_ENCODING) {
        jpeg_encoder_process(&jpeg_encoder, &buffer, &jpeg_data, &jpegSize);
        uint32_t imgSize = headerSize + jpegSize + footerSize;
        createImageHeaderPacket(&txp, imgSize, JPEG_ENCODING);
        cpxSendPacketBlocking(&txp);
        memcpy(txp.data, header.data, headerSize);
        txp.dataLength = headerSize;
        cpxSendPacketBlocking(&txp);
        sendBufferViaCPX(&txp, (uint8_t *)jpeg_data.data, jpegSize);
        memcpy(txp.data, footer.data, footerSize);
        txp.dataLength = footerSize;
        cpxSendPacketBlocking(&txp);
      } else {
        createImageHeaderPacket(&txp, captureSize, RAW_ENCODING);
        cpxSendPacketBlocking(&txp);
        sendBufferViaCPX(&txp, imgBuff, captureSize);
      }
      const uint32_t t_stream = pi_time_get_us();

#if !defined(STOP1_STREAM_ONLY)
      if (infer_frame_copy != NULL && inferTaskHandle == NULL &&
          frame >= DETECTOR_SPAWN_FRAME) {
        if (xTaskCreate(infer_task, "infer_task", configMINIMAL_STACK_SIZE * 8,
                        NULL, tskIDLE_PRIORITY, &inferTaskHandle) == pdPASS) {
          cpxPrintToConsole(LOG_TO_CRTP, "stop1 infer task spawned\n");
        } else {
          cpxPrintToConsole(LOG_TO_CRTP, "stop1 infer task spawn failed\n");
        }
      }
      if (infer_dory_ready && inferTaskHandle != NULL &&
          infer_frame_copy != NULL && frame >= DETECTOR_START_FRAME &&
          (frame % DETECTOR_EVERY_N) == 0) {
        xTaskNotifyGive(inferTaskHandle);
      }
#else
      (void)t_capture;
      (void)t_stream;
#endif
      if (frame % 30 == 0) {
        cpxPrintToConsole(
            LOG_TO_CRTP,
            "stop1 stream capture=%lu stream=%lu bytes=%lu\n",
            (unsigned long)(t_capture - t0),
            (unsigned long)(t_stream - t_capture),
            (unsigned long)(streamerMode == JPEG_ENCODING
                                ? (headerSize + jpegSize + footerSize)
                                : captureSize));
      }
      ++frame;
    }
#else
    uint8_t *arena = NULL;
    int network_ready = 0;
    if (!network_ready) {
      arena = pmsis_l2_malloc(L2_ARENA_BYTES);
      if (!arena) {
        cpxPrintToConsole(LOG_TO_CRTP, "stop1: L2 allocation failed arena\n");
        return;
      }
      network_initialize();
      network_ready = 1;
    }
    const uint32_t t0 = pi_time_get_us();
    int32_t *head_output =
        (int32_t *)(arena + L2_ARENA_BYTES -
                    GRID * GRID * HEAD_CHANNELS * sizeof(int32_t));
    fill_static_input(arena);
    const uint32_t t_preprocess = pi_time_get_us();
    network_run(arena, L2_ARENA_BYTES, head_output, 0, 1);
    const uint32_t t_inference = pi_time_get_us();
    if (++frame % REPORT_EVERY == 0) {
      cpxPrintToConsole(
          LOG_TO_CRTP,
          "stop1 timing us: capture=%lu stream=%lu preprocess=%lu "
          "inference=%lu camera_to_result=%lu\n",
          0UL, 0UL, (unsigned long)(t_preprocess - t0),
          (unsigned long)(t_inference - t_preprocess),
          (unsigned long)(t_inference - t0));
      report_stop1(head_output);
    }
    pi_time_wait_us(DETECTOR_PERIOD_US);
#endif
  }
}

#define LED_PIN 2
static pi_device_t led_gpio_dev;

static void hb_task(void *parameters) {
  (void)parameters;
  pi_gpio_pin_configure(&led_gpio_dev, LED_PIN, PI_GPIO_OUTPUT);
  const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
  while (1) {
    pi_gpio_pin_write(&led_gpio_dev, LED_PIN, 1);
    vTaskDelay(xDelay);
    pi_gpio_pin_write(&led_gpio_dev, LED_PIN, 0);
    vTaskDelay(xDelay);
  }
}

static void start_example(void) {
  struct pi_uart_conf conf;
  struct pi_device device;
  pi_uart_conf_init(&conf);
  conf.baudrate_bps = 115200;
  pi_open_from_conf(&device, &conf);
  if (pi_uart_open(&device)) {
    printf("[UART] open failed !\n");
    pmsis_exit(-1);
  }

  cpxInit();
  cpxEnableFunction(CPX_F_WIFI_CTRL);
  cpxPrintToConsole(LOG_TO_CRTP, "-- stop1 detector streamer --\n");

#if STOP1_USE_CAMERA
  evGroup = xEventGroupCreate();
#endif

  BaseType_t xTask;
  xTask = xTaskCreate(hb_task, "hb_task", configMINIMAL_STACK_SIZE * 2, NULL,
                      tskIDLE_PRIORITY + 1, NULL);
  if (xTask != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "HB task did not start !\n");
    pmsis_exit(-1);
  }

  xTask = xTaskCreate(camera_task, "camera_task", configMINIMAL_STACK_SIZE * 4,
                      NULL, tskIDLE_PRIORITY + 1, NULL);
  if (xTask != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "Camera task did not start !\n");
    pmsis_exit(-1);
  }

  xTask = xTaskCreate(rx_task, "rx_task", configMINIMAL_STACK_SIZE * 2, NULL,
                      tskIDLE_PRIORITY + 1, NULL);
  if (xTask != pdPASS) {
    cpxPrintToConsole(LOG_TO_CRTP, "RX task did not start !\n");
    pmsis_exit(-1);
  }

  while (1) {
    pi_yield();
  }
}

int main(void) {
  pi_bsp_init();
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  return pmsis_kickoff((void *)start_example);
}
