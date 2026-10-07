#ifndef LETTER20_PREPROCESS_H
#define LETTER20_PREPROCESS_H

#include <stdint.h>

#define LETTER20_CAMERA_WIDTH 324
#define LETTER20_CAMERA_HEIGHT 244
#define LETTER20_CAMERA_BYTES \
  (LETTER20_CAMERA_WIDTH * LETTER20_CAMERA_HEIGHT)

int letter20_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
);

#endif
