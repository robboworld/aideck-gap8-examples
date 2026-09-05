#ifndef ARROW2_PREPROCESS_H
#define ARROW2_PREPROCESS_H

#include <stdint.h>

#define ARROW2_CAMERA_WIDTH 324
#define ARROW2_CAMERA_HEIGHT 244
#define ARROW2_CAMERA_BYTES \
  (ARROW2_CAMERA_WIDTH * ARROW2_CAMERA_HEIGHT)

typedef struct {
  uint16_t mean;
  uint16_t range;
  uint16_t dark;
  uint16_t bright;
  uint16_t bright_center;
  /* 0 = pass (arrow-like), else reject reason for console. */
  const char *reject;
} arrow2_input_stats_t;

int arrow2_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
);

/* Fill stats for the 32x32 model input. Returns 1 if blank/rejected. */
int arrow2_input_analyze(
  const uint8_t *model_input,
  arrow2_input_stats_t *stats
);

#endif
