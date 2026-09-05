#ifndef ARROW2_INFERENCE_H
#define ARROW2_INFERENCE_H

#include <stdint.h>

int arrow2_inference_init(void);
int arrow2_inference_run(
  const uint8_t input[1024],
  int32_t logits[2],
  uint32_t *cycles
);
int arrow2_load_golden_input(uint8_t input[1024]);
void arrow2_inference_deinit(void);

#endif
