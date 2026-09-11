#include "arrow2_actuate.h"

#include <string.h>

#include "cpx.h"
#include "pmsis.h"
#include "FreeRTOS.h"
#include "task.h"

#ifndef ARROW2_ACT_DEBOUNCE
#define ARROW2_ACT_DEBOUNCE 2
#endif
#ifndef ARROW2_ACT_COOLDOWN_MS
#define ARROW2_ACT_COOLDOWN_MS 1500u
#endif
#ifndef ARROW2_ACT_SEND_TIMEOUT_MS
#define ARROW2_ACT_SEND_TIMEOUT_MS 50u
#endif

static CPXPacket_t tx_packet;
static uint8_t seq;
static int streak_cmd;
static int streak_count;
static TickType_t last_twitch_tick;
static uint8_t have_twitch;

static int class_to_cmd(const char *cls) {
  if (cls == NULL) {
    return -1;
  }
  if (strcmp(cls, "left") == 0) {
    return (int)ARROW2_ACT_CMD_TWITCH_LEFT;
  }
  if (strcmp(cls, "right") == 0) {
    return (int)ARROW2_ACT_CMD_TWITCH_RIGHT;
  }
  return -1;
}

static int send_cmd(uint8_t cmd) {
  cpxInitRoute(CPX_T_GAP8, CPX_T_STM32, CPX_F_APP, &tx_packet.route);
  tx_packet.data[0] = ARROW2_ACT_MAGIC;
  tx_packet.data[1] = ARROW2_ACT_VERSION;
  tx_packet.data[2] = cmd;
  tx_packet.data[3] = seq++;
  tx_packet.dataLength = ARROW2_ACT_PACKET_BYTES;
  return cpxSendPacket(&tx_packet, ARROW2_ACT_SEND_TIMEOUT_MS) ? 0 : -1;
}

void arrow2_actuate_init(void) {
  cpxEnableFunction(CPX_F_APP);
  seq = 0;
  streak_cmd = -1;
  streak_count = 0;
  last_twitch_tick = 0;
  have_twitch = 0;
}

void arrow2_actuate_on_class(const char *cls) {
  const int cmd = class_to_cmd(cls);
  TickType_t now;

  if (cmd < 0) {
    streak_cmd = -1;
    streak_count = 0;
    return;
  }

  if (cmd == streak_cmd) {
    streak_count++;
  } else {
    streak_cmd = cmd;
    streak_count = 1;
  }

  if (streak_count < ARROW2_ACT_DEBOUNCE) {
    return;
  }

  now = xTaskGetTickCount();
  if (have_twitch &&
      (now - last_twitch_tick) < pdMS_TO_TICKS(ARROW2_ACT_COOLDOWN_MS)) {
    return;
  }

  if (send_cmd((uint8_t)cmd) != 0) {
    return;
  }

  last_twitch_tick = now;
  have_twitch = 1;
  streak_count = 0;

  if (cmd == (int)ARROW2_ACT_CMD_TWITCH_LEFT) {
    cpxPrintToConsole(LOG_TO_CRTP, "twitch L\n");
  } else {
    cpxPrintToConsole(LOG_TO_CRTP, "twitch R\n");
  }
}
