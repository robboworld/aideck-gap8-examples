#ifndef LETTER20_INFERENCE_H
#define LETTER20_INFERENCE_H

#include <stdint.h>

#include "model_metadata.h"

int letter20_inference_init(void);
int letter20_inference_run(
  const uint8_t input[LETTER20_INPUT_BYTES],
  int32_t logits[LETTER20_CLASS_COUNT],
  uint32_t *cycles
);
int letter20_load_golden_input(uint8_t input[LETTER20_INPUT_BYTES]);
void letter20_inference_deinit(void);

#endif
