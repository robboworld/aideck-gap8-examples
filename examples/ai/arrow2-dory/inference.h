#ifndef ARROW2_INFERENCE_H
#define ARROW2_INFERENCE_H

#include <stdint.h>

#include "model_metadata.h"

int arrow2_inference_init(void);
int arrow2_inference_run(
  const uint8_t input[ARROW2_INPUT_BYTES],
  int32_t logits[ARROW2_CLASS_COUNT],
  uint32_t *cycles
);
int arrow2_load_golden_input(uint8_t input[ARROW2_INPUT_BYTES]);
void arrow2_inference_deinit(void);

#endif
