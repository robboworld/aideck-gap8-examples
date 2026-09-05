#include "inference.h"

#include <stddef.h>
#include <string.h>

#include "mem.h"
#include "model_metadata.h"
#include "network.h"
#include "pmsis.h"

/* DORY network.c accumulates layer cycles here. */
extern int cycle_network_execution;

static uint8_t *l2_arena;
static int32_t *l2_logits;
static int initialized;

int arrow2_inference_init(void) {
  if (initialized) {
    return 0;
  }
  mem_init();
  network_initialize();
  l2_arena = (uint8_t *)pi_l2_malloc(ARROW2_L2_ARENA_BYTES);
  if (l2_arena == NULL) {
    network_terminate();
    return -1;
  }
  l2_logits = (int32_t *)pi_l2_malloc(
    ARROW2_CLASS_COUNT * sizeof(int32_t)
  );
  if (l2_logits == NULL) {
    pi_l2_free(l2_arena, ARROW2_L2_ARENA_BYTES);
    l2_arena = NULL;
    network_terminate();
    return -1;
  }
  initialized = 1;
  return 0;
}

int arrow2_inference_run(
  const uint8_t input[ARROW2_INPUT_BYTES],
  int32_t logits[ARROW2_CLASS_COUNT],
  uint32_t *cycles
) {
  if (!initialized || input == NULL || logits == NULL || l2_logits == NULL) {
    return -1;
  }
  memcpy(l2_arena, input, ARROW2_INPUT_BYTES);
  network_run(
    l2_arena,
    ARROW2_L2_ARENA_BYTES,
    l2_logits,
    0,
    1
  );
  memcpy(logits, l2_logits, ARROW2_CLASS_COUNT * sizeof(int32_t));
  if (cycles != NULL) {
    *cycles = (uint32_t)cycle_network_execution;
  }
  return 0;
}

int arrow2_load_golden_input(
  uint8_t input[ARROW2_INPUT_BYTES]
) {
  if (!initialized || input == NULL) {
    return -1;
  }
  void *l3_input = ram_malloc(ARROW2_INPUT_BYTES);
  if (l3_input == NULL) {
    return -2;
  }
  const size_t size = load_file_to_ram(l3_input, "inputs.hex");
  if (size != ARROW2_INPUT_BYTES) {
    ram_free(l3_input, ARROW2_INPUT_BYTES);
    return -3;
  }
  ram_read(input, l3_input, ARROW2_INPUT_BYTES);
  ram_free(l3_input, ARROW2_INPUT_BYTES);
  return 0;
}

void arrow2_inference_deinit(void) {
  if (!initialized) {
    return;
  }
  network_terminate();
  if (l2_logits != NULL) {
    pi_l2_free(l2_logits, ARROW2_CLASS_COUNT * sizeof(int32_t));
    l2_logits = NULL;
  }
  pi_l2_free(l2_arena, ARROW2_L2_ARENA_BYTES);
  l2_arena = NULL;
  initialized = 0;
}
