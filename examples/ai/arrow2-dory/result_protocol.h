#ifndef ARROW2_RESULT_PROTOCOL_H
#define ARROW2_RESULT_PROTOCOL_H

#include <stdint.h>

#define ARROW2_RESULT_VERSION 2

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t class_id;
  uint16_t reserved;
  int32_t logits[3];
  uint32_t margin;
  uint32_t cycles;
} arrow2_result_packet_t;

const char *arrow2_class_name(uint8_t class_id);
arrow2_result_packet_t arrow2_decode(
  const int32_t logits[3],
  uint32_t cycles
);

#endif
