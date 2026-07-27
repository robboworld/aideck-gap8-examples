#ifndef TINY_SHAPES_PREPROCESS_H
#define TINY_SHAPES_PREPROCESS_H

#include <stdint.h>

#define TINY_SHAPES_CAMERA_WIDTH 324
#define TINY_SHAPES_CAMERA_HEIGHT 244
#define TINY_SHAPES_CAMERA_BYTES \
  (TINY_SHAPES_CAMERA_WIDTH * TINY_SHAPES_CAMERA_HEIGHT)

int tiny_shapes_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
);

#endif
