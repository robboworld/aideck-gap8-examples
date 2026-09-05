#include "preprocess.h"

#include <stddef.h>

#include "model_metadata.h"

#define CENTER_CROP_SIZE ARROW2_CAMERA_HEIGHT
#define CENTER_CROP_X \
  ((ARROW2_CAMERA_WIDTH - CENTER_CROP_SIZE) / 2)

/* Must match tinyml_demo.arrow2.contrast_stretch_u8 */
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

int arrow2_preprocess(
  const uint8_t *camera_frame,
  uint8_t *model_input
) {
  if (camera_frame == NULL || model_input == NULL) {
    return -1;
  }

  for (int output_y = 0; output_y < ARROW2_INPUT_HEIGHT; ++output_y) {
    const int source_y =
      (output_y * CENTER_CROP_SIZE + ARROW2_INPUT_HEIGHT / 2) /
      ARROW2_INPUT_HEIGHT;
    for (
      int output_x = 0;
      output_x < ARROW2_INPUT_WIDTH;
      ++output_x
    ) {
      const int source_x =
        CENTER_CROP_X +
        (output_x * CENTER_CROP_SIZE + ARROW2_INPUT_WIDTH / 2) /
        ARROW2_INPUT_WIDTH;
      model_input[output_y * ARROW2_INPUT_WIDTH + output_x] =
        camera_frame[source_y * ARROW2_CAMERA_WIDTH + source_x];
    }
  }
  contrast_stretch_u8(model_input, ARROW2_INPUT_BYTES);
  return 0;
}

int arrow2_input_analyze(
  const uint8_t *model_input,
  arrow2_input_stats_t *stats
) {
  /*
   * Diagnostic stats only. Classification of empty scenes is handled by the
   * 3-class CNN (left/right/empty); do not reject frames here.
   */
  enum {
    DARK_MAX = 80,
    BRIGHT_MIN = 180,
    BORDER = 5
  };
  uint32_t dark = 0;
  uint32_t bright = 0;
  uint32_t bright_center = 0;
  uint32_t sum = 0;
  uint8_t mn = 255;
  uint8_t mx = 0;
  int y;
  int x;

  if (stats != NULL) {
    stats->mean = 0;
    stats->range = 0;
    stats->dark = 0;
    stats->bright = 0;
    stats->bright_center = 0;
    stats->reject = "ok";
  }
  if (model_input == NULL) {
    if (stats != NULL) {
      stats->reject = "null";
    }
    return 0;
  }

  for (y = 0; y < ARROW2_INPUT_HEIGHT; ++y) {
    for (x = 0; x < ARROW2_INPUT_WIDTH; ++x) {
      const uint8_t p =
        model_input[y * ARROW2_INPUT_WIDTH + x];
      sum += p;
      if (p < mn) {
        mn = p;
      }
      if (p > mx) {
        mx = p;
      }
      if (p <= DARK_MAX) {
        dark++;
      }
      if (p >= BRIGHT_MIN) {
        bright++;
        if (
          y >= BORDER && y < ARROW2_INPUT_HEIGHT - BORDER &&
          x >= BORDER && x < ARROW2_INPUT_WIDTH - BORDER
        ) {
          bright_center++;
        }
      }
    }
  }

  if (stats != NULL) {
    stats->mean = (uint16_t)(sum / ARROW2_INPUT_BYTES);
    stats->range = (uint16_t)(mx - mn);
    stats->dark = (uint16_t)dark;
    stats->bright = (uint16_t)bright;
    stats->bright_center = (uint16_t)bright_center;
    stats->reject = "ok";
  }
  /* Always accept — empty class is predicted by the network. */
  return 0;
}
