#ifndef LETTER20_RESULT_PROTOCOL_H
#define LETTER20_RESULT_PROTOCOL_H

#include <stdint.h>
#include <limits.h>

#include "model_metadata.h"

typedef struct {
  uint8_t top[3];
  int32_t logit[3];
} letter20_top3_t;

const char *letter20_class_name(uint8_t class_id);
letter20_top3_t letter20_top3(const int32_t logits[LETTER20_CLASS_COUNT]);

#endif
