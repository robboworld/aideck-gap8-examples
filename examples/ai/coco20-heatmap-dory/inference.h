#ifndef TINY_SHAPES_INFERENCE_H
#define TINY_SHAPES_INFERENCE_H

#include <stdint.h>

int tiny_shapes_inference_init(void);
int tiny_shapes_inference_run(
  const uint8_t input[1024],
  int32_t logits[3],
  uint32_t *cycles
);
int tiny_shapes_load_golden_input(uint8_t input[1024]);
void tiny_shapes_inference_deinit(void);

#endif
