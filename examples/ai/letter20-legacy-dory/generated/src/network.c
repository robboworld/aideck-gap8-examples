/*
 * network.c
 * Alessio Burrello <alessio.burrello@unibo.it>
 * Thorir Mar Ingolfsson <thoriri@iis.ee.ethz.ch>
 *
 * Copyright (C) 2019-2020 University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define DEFINE_CONSTANTS
#include "net_utils.h"
#include "pmsis.h"
#include "network.h"
#include "directional_allocator.h"
#include "mem.h"
#include "embedded_weights.h"
#include <string.h>
#include "BNReluConvolution3.h"
#include "Convolution4.h"
#include "BNReluConvolution2.h"
#include "BNReluConvolution0.h"
#include "BNReluConvolution1.h"


#define VERBOSE 0

#define L3_WEIGHTS_SIZE 4000000
#define L3_INPUT_SIZE 1500000
#define L3_OUTPUT_SIZE 1500000
static void *L3_weights = NULL;
static void *L3_input = NULL;
static void *L3_output = NULL;
int cycle_network_execution;
/* One cluster device for backbone + head — close/open each frame corrupts Conv. */
static struct pi_device cluster_dev;
static int cluster_opened;
/* Cluster→FC completion in L2 — FC TCDM polls can miss cluster writes under JTAG. */
static PI_L2 volatile uint32_t letter20_infer_done;

int network_cluster_ensure_open(void)
{
  struct pi_cluster_conf conf;

  if (cluster_opened)
    return 0;
  *(volatile uint32_t *)0x1b00081c = 0x0FE40001u;
  pi_cluster_conf_init(&conf);
  conf.id = 0;
  memset(&cluster_dev, 0, sizeof(cluster_dev));
  pi_open_from_conf(&cluster_dev, &conf);
  *(volatile uint32_t *)0x1b00081c = 0x0FE40002u;
  if (pi_cluster_open(&cluster_dev)) {
    *(volatile uint32_t *)0x1b00081c = 0x0FE40BADu;
    return -1;
  }
  cluster_opened = 1;
  *(volatile uint32_t *)0x1b00081c = 0x0FE40003u;
  /* After pure JTAG load, raising CL to 175 MHz without PMU can crash.
   * Leave cluster at default clock for bring-up. */
  return 0;
}

struct pi_device *network_cluster_device(void)
{
  return &cluster_dev;
}

void network_cluster_close(void)
{
  if (!cluster_opened)
    return;
  pi_cluster_close(&cluster_dev);
  cluster_opened = 0;
}
/* Flash-resident weights (same pattern as coco20/stop1 — no HyperRAM). */
void network_initialize() {
  L3_weights = (void *)letter20_dory_weights;
  L3_input = NULL;
  L3_output = NULL;
  for (int i = 0; i < 5; i++) {
    L3_weights_size[i] = weights_size[i];
  }
}

int32_t *letter20_pending_head_out;
const uint8_t *letter20_pending_feat;

void network_terminate() {
  L3_weights = NULL;
  L3_input = NULL;
  L3_output = NULL;
}

void execute_layer_fork(void *args) {
  layer_args_t *layer_args = (layer_args_t *)args;
  *(volatile uint32_t *)0x1b000820 = 0x1A4E0000u | (layer_args->layer_id & 0xffu);
  if (pi_core_id() == 0) layer_args->L1_buffer = pmsis_l1_malloc(36700);

  switch (layer_args->layer_id)
  {
    case 0:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution0, args);
      break;
    case 1:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution1, args);
      break;
    case 2:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution2, args);
      break;
    case 3:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution3, args);
      break;
    case 4:
      pi_cl_team_fork(NUM_CORES, (void *)Convolution4, args);
      break;
  }

  if (pi_core_id() == 0) pmsis_l1_malloc_free(layer_args->L1_buffer, 36700);
  *(volatile uint32_t *)0x1b000820 = 0x1A4ED000u | (layer_args->layer_id & 0xffu);
}

struct network_run_token network_run_async(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir)
{
  /* Keep cluster powered across frames — close/open every infer hangs / corrupts
   * Convolution4 output on this AI-deck (same fix as coco20/himax21). */
  static unsigned int args[5];
  static struct pi_cluster_task cluster_task;
  static pi_task_t fc_task;
  int i;

  args[0] = (unsigned int) l2_buffer;
  args[1] = (unsigned int) l2_buffer_size;
  args[2] = (unsigned int) l2_final_output;
  args[3] = (unsigned int) exec;
  args[4] = (unsigned int) initial_dir;

  if (network_cluster_ensure_open())
    return (struct network_run_token){ .cluster_dev = cluster_dev };
  memset(&cluster_task, 0, sizeof(cluster_task));
  pi_cluster_task(&cluster_task, network_run_cluster, args);
  cluster_task.stack_size = 3500;
  cluster_task.slave_stack_size = 3400;
  /* FC TCDM breadcrumb: hang locus for 2nd kick. */
  *(volatile uint32_t *)0x1b000818 = 0xC1570000u;
  *(volatile uint32_t *)0x1b000814 = 0x5E400001u;
  letter20_infer_done = 0;
  /* Async + L2 poll: blocking send_task often never returns under JTAG_HYPER
   * even after network_run_cluster has finished. */
  pi_task_block(&fc_task);
  pi_cluster_send_task_to_cl_async(&cluster_dev, &cluster_task, &fc_task);
  *(volatile uint32_t *)0x1b000814 = 0x5E400010u;
  for (i = 0; i < 20000; ++i) {
    if (letter20_infer_done == 0xC1570002u)
      break;
    *(volatile uint32_t *)0x1b000814 = 0x5E400100u | (i & 0xffu);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  *(volatile uint32_t *)0x1b000814 = 0x5E400002u;
  return (struct network_run_token) {
    .cluster_dev = cluster_dev
  };
}

void network_run_wait(struct network_run_token token)
{
  (void)token;
  (void)cycle_network_execution;
}

void network_run(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir)
{
  network_run_wait(network_run_async(l2_buffer, l2_buffer_size, l2_final_output, exec, initial_dir));
}

void network_run_cluster(void *args) {
  *(volatile uint32_t *)0x1b000818 = 0xC1570001u;
  unsigned int * real_args = (unsigned int *) args;
  void * l2_buffer = (void *) real_args[0];
  size_t l2_buffer_size = (size_t) real_args[1];
  void * l2_final_output = (void *) real_args[2];
  int exec = (int) real_args[3];
  int dir = (int) real_args[4];
/*
  - initial buffer allocation L2 and L1
  - variable declaration
*/
/* ---------------------------------- */
/* -------- SECTION 0 BEGIN --------- */
/* ---------------------------------- */
  void *L2_output = NULL;
  void *L2_input = NULL;
  void *L2_weights = NULL;
  void *L3_weights_curr = L3_weights;
  void *bypass_activations = NULL;

  int residual_number = 0;
  int bypass_dimension = 0;
  int perf_cyc = 0;
/* ---------------------------------- */
/* --------- SECTION 0 END ---------- */
/* ---------------------------------- */

/*
  - initial copies from L3 of input
  - copies of weights of first 2 layers
*/
/* ---------------------------------- */
/* -------- SECTION 1 BEGIN --------- */
/* ---------------------------------- */
  directional_allocator_init(l2_buffer, l2_buffer_size);

/* ---------------------------------- */
/* --------- SECTION 1 END ---------- */
/* ---------------------------------- */
  // perf measurement begin
  cycle_network_execution = 0;
/* MAIN SECTION
  - for loop over all the layers of the network
  - double buffering using L3
  - check on layers to be executed from L3
  - residual check at the end of each layer
*/
/* ---------------------------------- */
/* -------- SECTION 2 BEGIN --------- */
/* ---------------------------------- */
  int weight_l_cnt = 0; // count how many layers with weights we have processed to increment the weights_L3 pointer
  /* Layers 0-3 on cluster. Convolution4 (int32 head) produces garbage on this
   * GAP8; FC runs that 3x3 head instead (same workaround as himax21). */
  for (int i = 0; i < 4; i++) {
/* MEMORY ALLOCATION
  - allocate memory if layer is executed from L3;
  - allocate weights
  - read weights
*/
    *(volatile uint32_t *)0x1b000824 = 0x10000000u | (unsigned)i;
    L2_output = dmalloc(activations_out_size[i], !dir);
    *(volatile uint32_t *)0x1b000824 = 0x11000000u | (unsigned)i;
    if (L3_input_layers[i] == 1)
      L2_input = dmalloc(activations_size[i], dir);

    if (layer_with_weights[i] == 1)
      L2_weights = dmalloc(weights_size[i], dir);
    *(volatile uint32_t *)0x1b000824 = 0x12000000u | (unsigned)i;

    if (allocate_layer[i] == 1)
      cl_ram_read(L2_weights, L3_weights_curr, weights_size[i]);
    *(volatile uint32_t *)0x1b000824 = 0x13000000u | (unsigned)i;

#if VERBOSE
    if (L3_input_layers[i] == 1)
      printf("Input in L3\n");
    else
    if (i == 0 || branch_change[i-1] == 0) {
      checksum("L2 input", L2_input, activations_size[i], activations_checksum[i][exec]);
      if (allocate_layer[i] == 1)
        checksum("L2 weights", L2_weights, weights_size[i], weights_checksum[i]);
      else
        printf("Weights in L3\n");
    }
    else
      printf("Switching branch, already checked activation\n");
#endif

    layer_args_t largs = {
      .L3_input = (unsigned int) L3_input,
      .L3_output = (unsigned int) L3_output,
      .L3_after_weights = (unsigned int) L3_weights_curr,
      .L2_input = (unsigned int) L2_input,
      .bypass = (unsigned int) bypass_activations,
      .L2_output = (unsigned int) L2_output,
      .L2_weights = (unsigned int) L2_weights,
      .L1_buffer = 0,
      .ram = (unsigned int) get_ram_ptr(),
      .out_mult = (unsigned int) out_mult_vector[i],
      .out_shift = (unsigned int) out_shift_vector[i],
      .layer_id = i
    };

/*
- Execution of the layers_pointers
*/
    // perf measurement begin
    pi_perf_conf(1<<PI_PERF_CYCLES);
    pi_perf_reset();
    pi_perf_stop();
    pi_perf_start();
    execute_layer_fork((void *) &largs);
    *(volatile uint32_t *)0x1b000824 = 0x14000000u | (unsigned)i;
    // performance measurements: end
    pi_perf_stop();
    *(volatile uint32_t *)0x1b000824 = 0x15000000u | (unsigned)i;
    perf_cyc =  pi_perf_read(PI_PERF_CYCLES);
    cycle_network_execution += perf_cyc;


    // TODO: What error?
    // prevents error from compiler
    asm volatile("": : :"memory");
    unsigned int temp = L3_input;
    L3_input = L3_output;
    asm volatile("": : :"memory");
    L3_output = temp;
    asm volatile("": : :"memory");
    *(volatile uint32_t *)0x1b000824 = 0x16000000u | (unsigned)i;

#if VERBOSE
    printf("Layer %s %d ended: \n", Layers_name[i], i);
    if (L3_output_layers[i]==1) {
      printf("Output in L3. Expected checksum: %d\n", activations_out_checksum[i][exec]);
    } else {
      checksum(i + 1 < 5 ? "L2 output" : "final output",
               L2_output, activations_out_size[i], activations_out_checksum[i][exec]);
    }
    printf("\n");
#endif

    // Free memory
    if (layer_with_weights[i] == 1)
      dfree(weights_size[i], dir);
    *(volatile uint32_t *)0x1b000824 = 0x17000000u | (unsigned)i;
    dfree(activations_size[i], dir);
    *(volatile uint32_t *)0x1b000824 = 0x18000000u | (unsigned)i;
    if (branch_input[i] == 1)
      dfree(bypass_dimension, dir);
    L2_input = L2_output;
    *(volatile uint32_t *)0x1b000824 = 0x19000000u | (unsigned)i;
    // Residual connections
    if (i < 4) {
      if (branch_input[i+1] == 1) {
        bypass_activations = dmalloc(bypass_dimension, !dir);
        residual_number--;
        cl_ram_read(bypass_activations, layers_pointers[residual_number], bypass_dimension);
        cl_ram_free(layers_pointers[residual_number], bypass_dimension);
      }

      // TODO I feel like this should look ahead instead of back
      if (i > 0 && branch_output[i-1]==1 && L3_input_layers[i]==1) { // TODO don't understand this condition
        L3_input = cl_ram_malloc(1500000);
      }
      if (branch_output[i]==1 && L3_output_layers[i]==1) {
        cl_ram_free(L3_input + activations_out_size[i], 1500000 - activations_out_size[i]);
        layers_pointers[residual_number] = L3_input;
        residual_number++;
        bypass_dimension = activations_out_size[i];
      } else
    if (branch_output[i]==1 || branch_change[i] == 1) {
        layers_pointers[residual_number] = cl_ram_malloc(activations_out_size[i]);
        cl_ram_write(layers_pointers[residual_number], L2_output, activations_out_size[i]);
        residual_number++;
        bypass_dimension = activations_out_size[i];
    }

      if (branch_change[i]==1) {
        dfree(activations_out_size[i], !dir);
        L2_input = dmalloc(activations_size[i + 1], !dir);
        cl_ram_read(L2_input, layers_pointers[residual_number - 2], activations_size[i + 1]);
        cl_ram_free(layers_pointers[residual_number - 2], activations_size[i + 1]);
      }
      if (L3_output_layers[i] == 1)
        dfree(activations_out_size[i], !dir);
    }
    if (layer_with_weights[i])
       L3_weights_curr += L3_weights_size[weight_l_cnt++];
    dir = !dir;
  }

  for (int i = 0; i < activations_out_size[3]; i++)
    *((uint8_t *)(l2_final_output + i)) = *((uint8_t *)(L2_output + i));

  /* Inline head in the same cluster task — second send_task wedges under JTAG. */
  if (letter20_pending_head_out != NULL) {
    const uint8_t *feat = letter20_pending_feat != NULL
                              ? letter20_pending_feat
                              : (const uint8_t *)l2_final_output;
    network_cluster_run_head_inline(feat, letter20_pending_head_out);
    /* Compact TCDM report only — full report_legacy blew the cluster stack. */
    {
      const int32_t *output = letter20_pending_head_out;
      int32_t best_score = -2147483647 - 1;
      int best_class = 19;
      int32_t best_obj = -2147483647 - 1;
      int row, col, cls;
      for (row = 0; row < 20; ++row) {
        for (col = 0; col < 20; ++col) {
          const int base = (row * 20 + col) * 25;
          const int32_t objectness = output[base];
          int32_t class_logit = -2147483647 - 1;
          int class_id = 0;
          for (cls = 0; cls < 19; ++cls) {
            const int32_t value = output[base + 1 + cls];
            if (value > class_logit) {
              class_logit = value;
              class_id = cls;
            }
          }
          const int32_t score = objectness + class_logit;
          if (score > best_score) {
            best_score = score;
            best_class = class_id;
            best_obj = objectness;
          }
        }
      }
      {
        volatile uint32_t *st = (volatile uint32_t *)0x1b000800;
        static uint32_t frame_ctr;
        const int present = best_obj > -8000;
        const int shown = present ? best_class : 19;
        /* Also publish class-A logit at the winning cell for triage. */
        int32_t a_logit = -2147483647 - 1;
        int row, col;
        for (row = 0; row < 20; ++row) {
          for (col = 0; col < 20; ++col) {
            const int base = (row * 20 + col) * 25;
            const int32_t objectness = output[base];
            const int32_t al = output[base + 1]; /* class 0 = A */
            const int32_t sc = objectness + al;
            if (sc > a_logit)
              a_logit = sc;
          }
        }
        st[0] = 0xA11E0000u | (frame_ctr++ & 0xffu);
        st[1] = (uint32_t)shown;
        st[2] = (uint32_t)best_obj;
        st[3] = (uint32_t)best_score;
        st[4] = (uint32_t)a_logit;
      }
    }
  }

/* ---------------------------------- */
/* --------- SECTION 2 END ---------- */
/* ---------------------------------- */

/* ---------------------------------- */
/* -------- SECTION 3 BEGIN --------- */
/* ---------------------------------- */


/* ---------------------------------- */
/* --------- SECTION 3 END ---------- */
/* ---------------------------------- */
  *(volatile uint32_t *)0x1b000818 = 0xC1570002u;
  letter20_infer_done = 0xC1570002u;
}
