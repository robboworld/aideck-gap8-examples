#include "preprocess.h"

#include <stddef.h>

#include "model_metadata.h"

#define CENTER_CROP_SIZE ARROW2_CAMERA_HEIGHT
#define CENTER_CROP_X \
  ((ARROW2_CAMERA_WIDTH - CENTER_CROP_SIZE) / 2)

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
  return 0;
}

int arrow2_input_analyze(
  const uint8_t *model_input,
  arrow2_input_stats_t *stats
) {
  /*
   * Training domain: bright filled arrow on dark background, roughly centered.
   * Thresholds match main.c blank gate used to kill empty-room "right" spam.
   */
  enum {
    DARK_MAX = 80,
    BRIGHT_MIN = 180,
    MIN_DARK = 520,
    MIN_BRIGHT = 70,
    MAX_BRIGHT = 420,
    MIN_RANGE = 120,
    MAX_MEAN = 95,
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
  const char *reject = NULL;

  if (stats != NULL) {
    stats->mean = 0;
    stats->range = 0;
    stats->dark = 0;
    stats->bright = 0;
    stats->bright_center = 0;
    stats->reject = "null";
  }
  if (model_input == NULL) {
    if (stats != NULL) {
      stats->reject = "null";
    }
    return 1;
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
  }

  if (sum / ARROW2_INPUT_BYTES > MAX_MEAN) {
    reject = "mean"; /* scene too bright — need dark bg */
  } else if ((uint32_t)(mx - mn) < MIN_RANGE) {
    reject = "range"; /* low contrast */
  } else if (dark < MIN_DARK) {
    reject = "dark"; /* not enough dark background pixels */
  } else if (bright < MIN_BRIGHT) {
    reject = "bri_lo"; /* arrow too small / dim */
  } else if (bright > MAX_BRIGHT) {
    reject = "bri_hi"; /* too much bright (window/paper flood) */
  } else if (bright_center * 5 < bright * 4) {
    reject = "center"; /* bright mass not in inner region */
  }

  if (stats != NULL) {
    stats->reject = reject != NULL ? reject : "ok";
  }
  return reject != NULL;
}
