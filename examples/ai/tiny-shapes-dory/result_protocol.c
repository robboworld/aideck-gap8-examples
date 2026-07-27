#include "result_protocol.h"

#include <limits.h>
#include <stddef.h>

#include "model_metadata.h"

static const char *const class_names[TINY_SHAPES_CLASS_COUNT] = {
  TINY_SHAPES_CLASS_0,
  TINY_SHAPES_CLASS_1,
  TINY_SHAPES_CLASS_2,
};

const char *tiny_shapes_class_name(uint8_t class_id) {
  if (class_id >= TINY_SHAPES_CLASS_COUNT) {
    return "unknown";
  }
  return class_names[class_id];
}

tiny_shapes_result_packet_t tiny_shapes_decode(
  const int32_t logits[3],
  uint32_t cycles
) {
  int best_index = 0;
  int second_index = 1;
  if (logits[second_index] > logits[best_index]) {
    best_index = 1;
    second_index = 0;
  }
  for (int index = 2; index < TINY_SHAPES_CLASS_COUNT; ++index) {
    if (logits[index] > logits[best_index]) {
      second_index = best_index;
      best_index = index;
    } else if (logits[index] > logits[second_index]) {
      second_index = index;
    }
  }

  int64_t difference =
    (int64_t)logits[best_index] - (int64_t)logits[second_index];
  if (difference < 0) {
    difference = 0;
  }
  if (difference > UINT_MAX) {
    difference = UINT_MAX;
  }

  tiny_shapes_result_packet_t result = {
    .version = TINY_SHAPES_RESULT_VERSION,
    .class_id = (uint8_t)best_index,
    .reserved = 0,
    .logits = {logits[0], logits[1], logits[2]},
    .margin = (uint32_t)difference,
    .cycles = cycles,
  };
  return result;
}
