#include "preprocess.h"

#include <stddef.h>

#include "model_metadata.h"

/* Must match tinyml_demo.letter20.contrast_stretch_u8 */
#define CONTRAST_LO_PCT 2
#define CONTRAST_HI_PCT 98

static void contrast_stretch_u8(uint8_t *patch, int n) {
  uint32_t hist[256];
  uint32_t total;
  uint32_t lo_count;
  uint32_t hi_count;
  uint32_t seen;
  int lo;
  int hi;
  int i;
  int value;

  for (i = 0; i < 256; ++i) {
    hist[i] = 0;
  }
  for (i = 0; i < n; ++i) {
    hist[patch[i]]++;
  }
  total = (uint32_t)n;
  lo_count = (total * (uint32_t)CONTRAST_LO_PCT) / 100u;
  hi_count = (total * (uint32_t)CONTRAST_HI_PCT) / 100u;
  if (hi_count <= lo_count) {
    hi_count = lo_count + 1u;
  }
  lo = 0;
  hi = 255;
  seen = 0;
  for (value = 0; value < 256; ++value) {
    seen += hist[value];
    if (seen > lo_count) {
      lo = value;
      break;
    }
  }
  seen = 0;
  for (value = 0; value < 256; ++value) {
    seen += hist[value];
    if (seen >= hi_count) {
      hi = value;
      break;
    }
  }
  if (hi <= lo) {
    return;
  }
  for (i = 0; i < n; ++i) {
    int32_t stretched = ((int32_t)patch[i] - lo) * 255 / (hi - lo);
    if (stretched < 0) {
      stretched = 0;
    } else if (stretched > 255) {
      stretched = 255;
    }
    patch[i] = (uint8_t)stretched;
  }
}

int letter20_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
) {
  if (camera_frame == NULL || model_input == NULL) {
    return -1;
  }

  /* Whole-frame nearest neighbour; must match
   * tinyml_demo.letter20_cls.preprocess_full_nn. */
  for (int output_y = 0; output_y < LETTER20_INPUT_HEIGHT; ++output_y) {
    int source_y =
      (output_y * LETTER20_CAMERA_HEIGHT + LETTER20_INPUT_HEIGHT / 2) /
      LETTER20_INPUT_HEIGHT;
    if (source_y > LETTER20_CAMERA_HEIGHT - 1) {
      source_y = LETTER20_CAMERA_HEIGHT - 1;
    }
    for (int output_x = 0; output_x < LETTER20_INPUT_WIDTH; ++output_x) {
      int source_x =
        (output_x * LETTER20_CAMERA_WIDTH + LETTER20_INPUT_WIDTH / 2) /
        LETTER20_INPUT_WIDTH;
      if (source_x > LETTER20_CAMERA_WIDTH - 1) {
        source_x = LETTER20_CAMERA_WIDTH - 1;
      }
      model_input[output_y * LETTER20_INPUT_WIDTH + output_x] =
        camera_frame[source_y * LETTER20_CAMERA_WIDTH + source_x];
    }
  }
  contrast_stretch_u8(model_input, LETTER20_INPUT_BYTES);
  return 0;
}

