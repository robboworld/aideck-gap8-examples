#include "inference.h"

#include <stddef.h>
#include <string.h>

#include "mem.h"
#include "model_metadata.h"
#include "network.h"
#include "pmsis.h"

extern int cycle_network_execution;

static uint8_t *l2_arena;
static int initialized;

int tiny_shapes_inference_init(void) {
  if (initialized) {
    return 0;
  }
  mem_init();
  network_initialize();
  l2_arena = (uint8_t *)pi_l2_malloc(TINY_SHAPES_L2_ARENA_BYTES);
  if (l2_arena == NULL) {
    network_terminate();
    return -1;
  }
  initialized = 1;
  return 0;
}

int tiny_shapes_inference_run(
  const uint8_t input[TINY_SHAPES_INPUT_BYTES],
  int32_t logits[TINY_SHAPES_CLASS_COUNT],
  uint32_t *cycles
) {
  if (!initialized || input == NULL || logits == NULL) {
    return -1;
  }
  memcpy(l2_arena, input, TINY_SHAPES_INPUT_BYTES);
  network_run(
    l2_arena,
    TINY_SHAPES_L2_ARENA_BYTES,
    logits,
    0,
    1
  );
  if (cycles != NULL) {
    *cycles = (uint32_t)cycle_network_execution;
  }
  return 0;
}

int tiny_shapes_load_golden_input(
  uint8_t input[TINY_SHAPES_INPUT_BYTES]
) {
  if (!initialized || input == NULL) {
    return -1;
  }
  void *l3_input = ram_malloc(TINY_SHAPES_INPUT_BYTES);
  if (l3_input == NULL) {
    return -2;
  }
  const size_t size = load_file_to_ram(l3_input, "inputs.hex");
  if (size != TINY_SHAPES_INPUT_BYTES) {
    ram_free(l3_input, TINY_SHAPES_INPUT_BYTES);
    return -3;
  }
  ram_read(input, l3_input, TINY_SHAPES_INPUT_BYTES);
  ram_free(l3_input, TINY_SHAPES_INPUT_BYTES);
  return 0;
}

void tiny_shapes_inference_deinit(void) {
  if (!initialized) {
    return;
  }
  network_terminate();
  pi_l2_free(l2_arena, TINY_SHAPES_L2_ARENA_BYTES);
  l2_arena = NULL;
  initialized = 0;
}
