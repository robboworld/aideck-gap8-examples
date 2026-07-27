#ifndef TINY_SHAPES_RESULT_PROTOCOL_H
#define TINY_SHAPES_RESULT_PROTOCOL_H

#include <stdint.h>

#define TINY_SHAPES_RESULT_VERSION 1

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t class_id;
  uint16_t reserved;
  int32_t logits[3];
  uint32_t margin;
  uint32_t cycles;
} tiny_shapes_result_packet_t;

const char *tiny_shapes_class_name(uint8_t class_id);
tiny_shapes_result_packet_t tiny_shapes_decode(
  const int32_t logits[3],
  uint32_t cycles
);

#endif
