#include "pmsis.h"
#include "bsp/bsp.h"
#include "cpx.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef LINK_DORY
#include "mem.h"
#include "network.h"
#include "model_metadata.h"
#include "result_protocol.h"
#ifdef TINY_SHAPES_EMBED_WEIGHTS
#include "embedded_weights.h"
/* Keep embed blobs linked even when STAGE stops early (avoid DCE size skew). */
volatile const void *tiny_shapes_keep_embed = (const void *)inputs;
#endif
#endif

#ifndef TINY_SHAPES_STAGE
#define TINY_SHAPES_STAGE 4
#endif

static char status_msg[160] = "boot";
static int32_t last_logits[3];
static int has_logits;

static void pulse(const char *msg) {
  snprintf(status_msg, sizeof(status_msg), "%s", msg);
  for (int i = 0; i < 5; ++i) {
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes status: %s\n", status_msg);
    pi_time_wait_us(200000);
  }
}

static void run_pipeline(void) {
#ifdef LINK_DORY
  pulse("mem_init...");
  int rc = mem_init();
  if (rc) {
    snprintf(status_msg, sizeof(status_msg), "mem_init failed rc=%d", rc);
    return;
  }
  {
    pulse("l2ext smoke...");
    void *p = ram_malloc(64);
    if (p == NULL) {
      snprintf(status_msg, sizeof(status_msg), "l2ext alloc failed");
      return;
    }
    uint8_t wb[64];
    uint8_t rb[64];
    for (int i = 0; i < 64; ++i) {
      wb[i] = (uint8_t)i;
      rb[i] = 0x5A;
    }
    ram_write(p, wb, 64);
    ram_read(rb, p, 64);
    int errors = 0;
    for (int i = 0; i < 64; ++i) {
      if (wb[i] != rb[i]) {
        errors++;
      }
    }
    snprintf(status_msg, sizeof(status_msg), "l2ext e=%d", errors);
    pulse(status_msg);
    if (errors) {
      return;
    }
  }
  if (TINY_SHAPES_STAGE < 2) {
    snprintf(status_msg, sizeof(status_msg), "stage1 mem ok");
    return;
  }

  pulse("network_initialize...");
  network_initialize();
  pulse("network_initialize done");
  if (TINY_SHAPES_STAGE < 3) {
    snprintf(status_msg, sizeof(status_msg), "stage2 network ok");
    return;
  }

  pulse("loading golden...");
  uint8_t *arena = (uint8_t *)pi_l2_malloc(TINY_SHAPES_L2_ARENA_BYTES);
  if (arena == NULL) {
    snprintf(status_msg, sizeof(status_msg), "arena alloc failed");
    return;
  }

  void *l3_in = ram_malloc(TINY_SHAPES_INPUT_BYTES);
  if (l3_in == NULL) {
    snprintf(status_msg, sizeof(status_msg), "l3 input alloc failed");
    return;
  }

  size_t size = load_file_to_ram(l3_in, "inputs.hex");
  if (size != TINY_SHAPES_INPUT_BYTES) {
    snprintf(status_msg, sizeof(status_msg), "inputs size=%u", (unsigned)size);
    return;
  }
  ram_read(arena, l3_in, TINY_SHAPES_INPUT_BYTES);
  {
    unsigned int sum = 0;
    for (unsigned i = 0; i < TINY_SHAPES_INPUT_BYTES; ++i) {
      sum += arena[i];
    }
    snprintf(status_msg, sizeof(status_msg), "golden_sum=%u expect=68734", sum);
    pulse(status_msg);
  }
  if (TINY_SHAPES_STAGE < 4) {
    snprintf(status_msg, sizeof(status_msg), "stage3 golden ok");
    ram_free(l3_in, TINY_SHAPES_INPUT_BYTES);
    return;
  }

#ifdef TINY_SHAPES_SKIP_RUN
  snprintf(status_msg, sizeof(status_msg), "skip_run");
  ram_free(l3_in, TINY_SHAPES_INPUT_BYTES);
  return;
#endif

  pulse("network_run...");
  network_run(arena, TINY_SHAPES_L2_ARENA_BYTES, last_logits, 0, 1);
  has_logits = 1;
  {
    const tiny_shapes_result_packet_t result =
      tiny_shapes_decode(last_logits, (uint32_t)network_status.cycles);
    snprintf(
      status_msg,
      sizeof(status_msg),
      "class=%s logits=[%ld,%ld,%ld] cyc=%d done=%08x in=%u w=%u out=%u",
      tiny_shapes_class_name(result.class_id),
      (long)result.logits[0],
      (long)result.logits[1],
      (long)result.logits[2],
      (int)network_status.cycles,
      (unsigned)network_status.done,
      (unsigned)network_status.chk_in0,
      (unsigned)network_status.chk_w0,
      (unsigned)network_status.chk_out
    );
  }
  ram_free(l3_in, TINY_SHAPES_INPUT_BYTES);
#else
  snprintf(status_msg, sizeof(status_msg), "no dory");
#endif
}

static void application(void) {
  pi_bsp_init();
  cpxInit();
  pulse("cpx ready");
  run_pipeline();

  while (1) {
    cpxPrintToConsole(
      LOG_TO_CRTP,
      has_logits ? "tiny-shapes %s\n" : "tiny-shapes status: %s\n",
      status_msg
    );
    pi_time_wait_us(1000 * 1000);
  }
}

int main(void) {
  return pmsis_kickoff((void *)application);
}
