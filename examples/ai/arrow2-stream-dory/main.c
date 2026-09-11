/**
 * Experimental: SoftAP JPEG stream + on-drone arrow2 classify.
 * Stable radio-only app remains examples/ai/arrow2-dory/.
 */
#include <stdint.h>
#include <string.h>

#include "bsp/bsp.h"
#include "bsp/buffer.h"
#include "bsp/camera/himax.h"
#include "cpx.h"
#include "gaplib/jpeg_encoder.h"
#include "inference.h"
#include "model_metadata.h"
#include "pmsis.h"
#include "preprocess.h"
#include "result_protocol.h"
#include "wifi.h"

#define IMG_ORIENTATION 0x0101
#define CAM_WIDTH 324
#define CAM_HEIGHT 244
#define CAM_BYTES (CAM_WIDTH * CAM_HEIGHT)

#ifndef ARROW2_MARGIN_MIN
#define ARROW2_MARGIN_MIN 8000u
#endif
#ifndef ARROW2_LOGIT_MIN
#define ARROW2_LOGIT_MIN 5500
#endif
#ifndef INFER_EVERY_N
#define INFER_EVERY_N 2
#endif

typedef enum {
  RAW_ENCODING = 0,
  JPEG_ENCODING = 1
} __attribute__((packed)) StreamerMode_t;

typedef struct {
  uint8_t magic;
  uint16_t width;
  uint16_t height;
  uint8_t depth;
  uint8_t type;
  uint32_t size;
} __attribute__((packed)) img_header_t;

static EventGroupHandle_t evGroup;
#define CAPTURE_DONE_BIT (1 << 0)

static pi_task_t cam_task;
static unsigned char *imgBuff;
static struct pi_device camera;
static pi_buffer_t buffer;
static jpeg_encoder_t jpeg_encoder;
static pi_buffer_t header;
static uint32_t headerSize;
static pi_buffer_t footer;
static uint32_t footerSize;
static pi_buffer_t jpeg_data;
static uint32_t jpegSize;
static StreamerMode_t streamerMode = JPEG_ENCODING;

static int wifiClientConnected;
static int wifi_client_needs_route;
static CPXPacket_t rxp;
static CPXPacket_t txp;

static uint8_t *model_input;
static arrow2_result_packet_t last_result;
static arrow2_input_stats_t last_stats;
static volatile const char *last_gate = "boot";
static volatile int has_result;

static void capture_done_cb(void *arg) {
  (void)arg;
  xEventGroupSetBits(evGroup, CAPTURE_DONE_BIT);
}

static void rx_task(void *parameters) {
  (void)parameters;
  while (1) {
    cpxReceivePacketBlocking(CPX_F_WIFI_CTRL, &rxp);
    WiFiCTRLPacket_t *wifiCtrl = (WiFiCTRLPacket_t *)rxp.data;
    switch (wifiCtrl->cmd) {
      case WIFI_CTRL_STATUS_WIFI_CONNECTED:
        cpxPrintToConsole(
          LOG_TO_CRTP,
          "Wifi connected (%u.%u.%u.%u)\n",
          wifiCtrl->data[0],
          wifiCtrl->data[1],
          wifiCtrl->data[2],
          wifiCtrl->data[3]
        );
        break;
      case WIFI_CTRL_STATUS_CLIENT_CONNECTED:
        wifiClientConnected = wifiCtrl->data[0];
        if (wifiClientConnected == 1) {
          wifi_client_needs_route = 1;
        }
        cpxPrintToConsole(
          LOG_TO_CRTP,
          "Wifi client connection status: %u\n",
          wifiCtrl->data[0]
        );
        break;
      default:
        break;
    }
  }
}

static void setupWiFi(void) {
  static char ssid[] = "AI Deck arrow2";
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

static void createImageHeaderPacket(
  CPXPacket_t *packet,
  uint32_t imgSize,
  StreamerMode_t imgType
) {
  img_header_t *imgHeader = (img_header_t *)packet->data;
  imgHeader->magic = 0xBC;
  imgHeader->width = CAM_WIDTH;
  imgHeader->height = CAM_HEIGHT;
  imgHeader->depth = 1;
  imgHeader->type = imgType;
  imgHeader->size = imgSize;
  packet->dataLength = sizeof(img_header_t);
}

static int send_cpx_or_abort(CPXPacket_t *packet) {
  if (wifiClientConnected != 1) {
    return -1;
  }
  return cpxSendPacket(packet, 200) ? 0 : -1;
}

static int sendBufferViaCPX(
  CPXPacket_t *packet,
  uint8_t *buf,
  uint32_t bufferSize
) {
  uint32_t offset = 0;
  do {
    uint32_t size;
    if (wifiClientConnected != 1) {
      return -1;
    }
    size = sizeof(packet->data);
    if (offset + size > bufferSize) {
      size = bufferSize - offset;
    }
    memcpy(packet->data, &buf[offset], size);
    packet->dataLength = size;
    if (!cpxSendPacket(packet, 200)) {
      return -1;
    }
    offset += size;
  } while (offset < bufferSize);
  return 0;
}

static int open_camera(void) {
  struct pi_himax_conf configuration;
  pi_himax_conf_init(&configuration);
  configuration.format = PI_CAMERA_QVGA;
  pi_open_from_conf(&camera, &configuration);
  if (pi_camera_open(&camera)) {
    return -1;
  }
  /* Skip blocking orient I2C verify (JTAG-sensitive). */
  pi_camera_control(&camera, PI_CAMERA_CMD_AEG_INIT, 0);
  return 0;
}

static void publish_line(
  const char *cls,
  const char *gate,
  const int32_t logits[2],
  uint32_t margin,
  const arrow2_input_stats_t *stats
) {
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
}

static void paint_status_bar(const char *cls) {
  /* Top 10 rows: gray=uncertain, bright=left, dark=right — visible in SoftAP. */
  uint8_t tone = 128;
  int y;
  int x;
  if (cls[0] == 'l') {
    tone = 220;
  } else if (cls[0] == 'r') {
    tone = 40;
  }
  for (y = 0; y < 10; ++y) {
    for (x = 0; x < CAM_WIDTH; ++x) {
      imgBuff[y * CAM_WIDTH + x] = tone;
    }
  }
}

static const char *classify_frame(void) {
  arrow2_input_stats_t stats;
  int32_t logits[ARROW2_CLASS_COUNT];
  uint32_t cycles = 0;
  const char *gate = "ok";
  const char *cls = "uncertain";

  if (arrow2_preprocess(imgBuff, model_input)) {
    return "uncertain";
  }
  if (arrow2_input_analyze(model_input, &stats)) {
    last_stats = stats;
    last_gate = stats.reject;
    has_result = 1;
    last_result.logits[0] = 0;
    last_result.logits[1] = 0;
    last_result.margin = 0;
    publish_line("uncertain", last_gate, last_result.logits, 0, &stats);
    return "uncertain";
  }
  if (arrow2_inference_run(model_input, logits, &cycles)) {
    return "uncertain";
  }
  last_result = arrow2_decode(logits, cycles);
  last_stats = stats;
  if (last_result.margin < ARROW2_MARGIN_MIN) {
    gate = "weak_m";
  } else if (last_result.logits[last_result.class_id] < (int32_t)ARROW2_LOGIT_MIN) {
    gate = "weak_l";
  } else {
    cls = arrow2_class_name(last_result.class_id);
  }
  if (gate[0] != 'o') {
    cls = "uncertain";
  }
  last_gate = gate;
  has_result = 1;
  publish_line(cls, gate, last_result.logits, last_result.margin, &stats);
  return cls;
}

static void camera_task(void *parameters) {
  (void)parameters;
  unsigned int frame = 0;
  const char *cls = "uncertain";

  vTaskDelay(2000 / portTICK_PERIOD_MS);
  setupWiFi();

  cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: infer init...\n");
  if (arrow2_inference_init()) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: infer init failed\n");
    while (1) {
      pi_yield();
    }
  }
  model_input = (uint8_t *)pi_l2_malloc(ARROW2_INPUT_BYTES);
  if (model_input == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: model_input alloc failed\n");
    while (1) {
      pi_yield();
    }
  }

  imgBuff = (unsigned char *)pi_l2_malloc(CAM_BYTES);
  if (imgBuff == NULL) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: imgBuff alloc failed\n");
    while (1) {
      pi_yield();
    }
  }

  if (open_camera()) {
    cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: camera open failed\n");
    while (1) {
      pi_yield();
    }
  }
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: camera ok\n");

  {
    struct jpeg_encoder_conf enc_conf;
    jpeg_encoder_conf_init(&enc_conf);
    enc_conf.width = CAM_WIDTH;
    enc_conf.height = CAM_HEIGHT;
    enc_conf.flags = 0;
    if (jpeg_encoder_open(&jpeg_encoder, &enc_conf)) {
      cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: jpeg open failed\n");
      while (1) {
        pi_yield();
      }
    }
    pi_buffer_init(&buffer, PI_BUFFER_TYPE_L2, imgBuff);
    pi_buffer_set_format(
      &buffer, CAM_WIDTH, CAM_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY
    );
    header.size = 1024;
    header.data = pmsis_l2_malloc(1024);
    footer.size = 10;
    footer.data = pmsis_l2_malloc(10);
    jpeg_data.size = 1024 * 15;
    jpeg_data.data = pmsis_l2_malloc(1024 * 15);
    if (header.data == 0 || footer.data == 0 || jpeg_data.data == 0) {
      cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: jpeg buf alloc failed\n");
      while (1) {
        pi_yield();
      }
    }
    jpeg_encoder_header(&jpeg_encoder, &header, &headerSize);
    jpeg_encoder_footer(&jpeg_encoder, &footer, &footerSize);
  }

  pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
  cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &txp.route);
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: loop (jpeg+onboard)\n");

  while (1) {
    /* Always capture + classify on drone; stream only with SoftAP client. */
    pi_camera_capture_async(
      &camera,
      imgBuff,
      CAM_WIDTH * CAM_HEIGHT,
      pi_task_callback(&cam_task, capture_done_cb, NULL)
    );
    pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
    xEventGroupWaitBits(
      evGroup, CAPTURE_DONE_BIT, pdTRUE, pdFALSE, (TickType_t)portMAX_DELAY
    );
    pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);

    if ((frame % INFER_EVERY_N) == 0) {
      cls = classify_frame();
    }
    paint_status_bar(cls);

    if (wifiClientConnected == 1) {
      if (wifi_client_needs_route) {
        wifi_client_needs_route = 0;
        cpxInitRoute(CPX_T_GAP8, CPX_T_WIFI_HOST, CPX_F_APP, &txp.route);
      }
      jpeg_encoder_process(&jpeg_encoder, &buffer, &jpeg_data, &jpegSize);
      {
        uint32_t imgSize = headerSize + jpegSize + footerSize;
        createImageHeaderPacket(&txp, imgSize, streamerMode);
        if (send_cpx_or_abort(&txp) != 0) {
          ++frame;
          continue;
        }
        memcpy(txp.data, header.data, headerSize);
        txp.dataLength = headerSize;
        if (send_cpx_or_abort(&txp) != 0) {
          ++frame;
          continue;
        }
        if (sendBufferViaCPX(&txp, (uint8_t *)jpeg_data.data, jpegSize) != 0) {
          ++frame;
          continue;
        }
        memcpy(txp.data, footer.data, footerSize);
        txp.dataLength = footerSize;
        (void)send_cpx_or_abort(&txp);
      }
    } else {
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    ++frame;
  }
}

static void application(void) {
  cpxInit();
  cpxEnableFunction(CPX_F_WIFI_CTRL);
  cpxPrintToConsole(LOG_TO_CRTP, "arrow2-stream: cpx ready\n");

  evGroup = xEventGroupCreate();
  xTaskCreate(
    rx_task,
    "rx",
    configMINIMAL_STACK_SIZE * 2,
    NULL,
    tskIDLE_PRIORITY + 3,
    NULL
  );
  xTaskCreate(
    camera_task,
    "cam",
    configMINIMAL_STACK_SIZE * 24,
    NULL,
    tskIDLE_PRIORITY + 1,
    NULL
  );
  while (1) {
    pi_yield();
  }
}

int main(void) {
  pi_bsp_init();
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  return pmsis_kickoff((void *)application);
}
