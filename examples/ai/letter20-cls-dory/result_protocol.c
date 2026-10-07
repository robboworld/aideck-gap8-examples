#include "result_protocol.h"

static const char *const class_names[LETTER20_CLASS_COUNT] = {
  LETTER20_CLASS_0,
  LETTER20_CLASS_1,
  LETTER20_CLASS_2,
  LETTER20_CLASS_3,
  LETTER20_CLASS_4,
  LETTER20_CLASS_5,
  LETTER20_CLASS_6,
  LETTER20_CLASS_7,
  LETTER20_CLASS_8,
  LETTER20_CLASS_9,
  LETTER20_CLASS_10,
  LETTER20_CLASS_11,
  LETTER20_CLASS_12,
  LETTER20_CLASS_13,
  LETTER20_CLASS_14,
  LETTER20_CLASS_15,
  LETTER20_CLASS_16,
  LETTER20_CLASS_17,
  LETTER20_CLASS_18,
  LETTER20_CLASS_19,
};

const char *letter20_class_name(uint8_t class_id) {
  if (class_id >= LETTER20_CLASS_COUNT) {
    return "?";
  }
  return class_names[class_id];
}

letter20_top3_t letter20_top3(const int32_t logits[LETTER20_CLASS_COUNT]) {
  letter20_top3_t out = {{0, 0, 0}, {INT32_MIN, INT32_MIN, INT32_MIN}};
  for (int i = 0; i < LETTER20_CLASS_COUNT; ++i) {
    const int32_t v = logits[i];
    if (v > out.logit[0]) {
      out.top[2] = out.top[1];
      out.logit[2] = out.logit[1];
      out.top[1] = out.top[0];
      out.logit[1] = out.logit[0];
      out.top[0] = (uint8_t)i;
      out.logit[0] = v;
    } else if (v > out.logit[1]) {
      out.top[2] = out.top[1];
      out.logit[2] = out.logit[1];
      out.top[1] = (uint8_t)i;
      out.logit[1] = v;
    } else if (v > out.logit[2]) {
      out.top[2] = (uint8_t)i;
      out.logit[2] = v;
    }
  }
  return out;
}
