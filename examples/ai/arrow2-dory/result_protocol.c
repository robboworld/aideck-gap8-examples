#include "result_protocol.h"

#include <limits.h>
#include <stddef.h>

#include "model_metadata.h"

static const char *const class_names[ARROW2_CLASS_COUNT] = {
  ARROW2_CLASS_0,
  ARROW2_CLASS_1,
  ARROW2_CLASS_2,
};

const char *arrow2_class_name(uint8_t class_id) {
  if (class_id >= ARROW2_CLASS_COUNT) {
    return "unknown";
  }
  return class_names[class_id];
}

arrow2_result_packet_t arrow2_decode(
  const int32_t logits[3],
  uint32_t cycles
) {
  int best_index = 0;
  int second_index = 1;
  int i;

  for (i = 1; i < ARROW2_CLASS_COUNT; ++i) {
    if (logits[i] > logits[best_index]) {
      second_index = best_index;
      best_index = i;
    } else if (i == 1 || logits[i] > logits[second_index]) {
      if (i != best_index) {
        second_index = i;
      }
    }
  }
  if (second_index == best_index) {
    second_index = best_index == 0 ? 1 : 0;
  }

  int64_t difference =
    (int64_t)logits[best_index] - (int64_t)logits[second_index];
  if (difference < 0) {
    difference = 0;
  }
  if (difference > UINT_MAX) {
    difference = UINT_MAX;
  }

  arrow2_result_packet_t result = {
    .version = ARROW2_RESULT_VERSION,
    .class_id = (uint8_t)best_index,
    .reserved = 0,
    .logits = {logits[0], logits[1], logits[2]},
    .margin = (uint32_t)difference,
    .cycles = cycles,
  };
  return result;
}
