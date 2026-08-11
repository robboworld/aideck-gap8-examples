#include "preprocess.h"

#include <stddef.h>

#include "model_metadata.h"

#define CENTER_CROP_SIZE TINY_SHAPES_CAMERA_HEIGHT
#define CENTER_CROP_X \
  ((TINY_SHAPES_CAMERA_WIDTH - CENTER_CROP_SIZE) / 2)

int tiny_shapes_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
) {
  if (camera_frame == NULL || model_input == NULL) {
    return -1;
  }

  for (int output_y = 0; output_y < TINY_SHAPES_INPUT_HEIGHT; ++output_y) {
    const int source_y =
      (output_y * CENTER_CROP_SIZE + TINY_SHAPES_INPUT_HEIGHT / 2) /
      TINY_SHAPES_INPUT_HEIGHT;
    for (
      int output_x = 0;
      output_x < TINY_SHAPES_INPUT_WIDTH;
      ++output_x
    ) {
      const int source_x =
        CENTER_CROP_X +
        (output_x * CENTER_CROP_SIZE + TINY_SHAPES_INPUT_WIDTH / 2) /
        TINY_SHAPES_INPUT_WIDTH;
      model_input[output_y * TINY_SHAPES_INPUT_WIDTH + output_x] =
        camera_frame[source_y * TINY_SHAPES_CAMERA_WIDTH + source_x];
    }
  }
  return 0;
}
