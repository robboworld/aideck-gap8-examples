#ifndef ARROW2_ACTUATE_H
#define ARROW2_ACTUATE_H

#include <stdint.h>

#define ARROW2_ACT_MAGIC 0xA2u
#define ARROW2_ACT_VERSION 1u

#define ARROW2_ACT_CMD_STOP 0u
#define ARROW2_ACT_CMD_TWITCH_LEFT 1u
#define ARROW2_ACT_CMD_TWITCH_RIGHT 2u

#define ARROW2_ACT_PACKET_BYTES 4u

/* Enable CPX APP TX and reset debounce/cooldown state. */
void arrow2_actuate_init(void);

/*
 * Call after a gated class string is published.
 * Only "left" / "right" can fire a twitch (2-frame debounce, ~1.5 s cooldown).
 * "empty" / "uncertain" reset the debounce streak.
 */
void arrow2_actuate_on_class(const char *cls);

#endif
