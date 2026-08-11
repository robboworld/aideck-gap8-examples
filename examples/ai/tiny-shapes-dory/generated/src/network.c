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
#include <string.h>
#ifdef TINY_SHAPES_ENABLE_CPX
#include "cpx.h"
#endif
#include "BNReluConvolution2.h"
#include "Pooling3.h"
#include "Pooling1.h"
#include "FullyConnected6.h"
#include "BNReluConvolution4.h"
#include "BNReluConvolution0.h"
#include "Pooling5.h"


#ifndef DORY_DISABLE_CHECKSUMS
#define VERBOSE 1
#endif

#define L3_WEIGHTS_SIZE 8192
#define L3_INPUT_SIZE 2048
#define L3_OUTPUT_SIZE 2048
static void *L3_weights = NULL;
static void *L3_input = NULL;
static void *L3_output = NULL;

network_status_t network_status;

static unsigned int byte_sum(const uint8_t *d, size_t size) {
  unsigned int sum = 0;
  for (size_t i = 0; i < size; ++i) {
    sum += d[i];
  }
  return sum;
}

/* Moves the weights and the biases into the L2-backed L3 pool. */
void network_initialize() {
#ifdef TINY_SHAPES_BRINGUP
  cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes ni: alloc\n");
#endif
  L3_weights = ram_malloc(L3_WEIGHTS_SIZE);
  L3_input = ram_malloc(L3_INPUT_SIZE);
  L3_output = ram_malloc(L3_OUTPUT_SIZE);
#ifdef TINY_SHAPES_BRINGUP
  cpxPrintToConsole(
    LOG_TO_CRTP,
    "tiny-shapes ni: ptrs %p %p %p\n",
    L3_weights, L3_input, L3_output
  );
#endif
  if (L3_weights == NULL || L3_input == NULL || L3_output == NULL) {
    return;
  }

  uint8_t *w_ptr = (uint8_t *)L3_weights;
  for (int i = 0; i < 4; i++) {
#ifdef TINY_SHAPES_BRINGUP
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes ni: load %d\n", i);
#endif
    size_t size = load_file_to_ram(w_ptr, L3_weights_files[i]);
    L3_weights_size[i] = (int)size;
    w_ptr += size;
#ifdef TINY_SHAPES_BRINGUP
    cpxPrintToConsole(LOG_TO_CRTP, "tiny-shapes ni: loaded %d n=%u\n", i, (unsigned)size);
#endif
  }
}

/* Remove RAM memory */
void network_terminate() {
  ram_free(L3_weights, L3_WEIGHTS_SIZE);
  ram_free(L3_input, L3_INPUT_SIZE);
  ram_free(L3_output, L3_OUTPUT_SIZE);
}

void execute_layer_fork(void *args) {
  layer_args_t *layer_args = (layer_args_t *)args;
  if (pi_core_id() == 0) layer_args->L1_buffer = pmsis_l1_malloc(36700);

  switch (layer_args->layer_id)
  {
    case 0:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution0, args);
      break;
    case 1:
      pi_cl_team_fork(NUM_CORES, (void *)Pooling1, args);
      break;
    case 2:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution2, args);
      break;
    case 3:
      pi_cl_team_fork(NUM_CORES, (void *)Pooling3, args);
      break;
    case 4:
      pi_cl_team_fork(NUM_CORES, (void *)BNReluConvolution4, args);
      break;
    case 5:
      pi_cl_team_fork(NUM_CORES, (void *)Pooling5, args);
      break;
    case 6:
      pi_cl_team_fork(NUM_CORES, (void *)FullyConnected6, args);
      break;
  }

  if (pi_core_id() == 0) pmsis_l1_malloc_free(layer_args->L1_buffer, 36700);
}

struct network_run_token network_run_async(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir)
{
  struct pi_device cluster_dev = {0};
  struct pi_cluster_conf conf;
  struct pi_cluster_task cluster_task = {0};
  /* Must outlive the cluster task — cannot live on this stack frame.
   * args[5] is a completion canary written by the cluster. */
  static unsigned int args[6];
  pi_cluster_conf_init(&conf);
  conf.id=0;
  args[0] = (unsigned int) l2_buffer;
  args[1] = (unsigned int) l2_buffer_size;
  args[2] = (unsigned int) l2_final_output;
  args[3] = (unsigned int) exec;
  args[4] = (unsigned int) initial_dir;
  args[5] = 0;
  pi_cluster_task(&cluster_task, network_run_cluster, args);
  pi_open_from_conf(&cluster_dev, &conf);
  if (pi_cluster_open(&cluster_dev)) {
    return (struct network_run_token){0};
  }
  cluster_task.stack_size = 6096;
  cluster_task.slave_stack_size = 1024;
  pi_cluster_send_task_to_cl(&cluster_dev, &cluster_task);
  return (struct network_run_token) {
    .cluster_dev = cluster_dev
  };
}

void network_run_wait(struct network_run_token token)
{
  pi_cluster_close(&token.cluster_dev);
}

void network_run(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir)
{
  network_status.done = 0;
  /* First layer is marked L3_input: DMA reads activations from HyperRAM.
   * Callers place the frame at the start of l2_buffer — promote it to L3. */
  if (L3_input != NULL && l2_buffer != NULL) {
    ram_write(L3_input, l2_buffer, (size_t)activations_size[0]);
  }
  struct network_run_token token =
    network_run_async(l2_buffer, l2_buffer_size, l2_final_output, exec, initial_dir);
  network_run_wait(token);
}

void network_run_cluster(void *args) {
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
  network_status.cycles = 0;
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
  for (int i = 0; i < 7; i++) {
/* MEMORY ALLOCATION
  - allocate memory if layer is executed from L3;
  - allocate weights
  - read weights
*/
    L2_output = dmalloc(activations_out_size[i], !dir);
    if (L3_input_layers[i] == 1)
      L2_input = dmalloc(activations_size[i], dir);

    if (layer_with_weights[i] == 1)
      L2_weights = dmalloc(weights_size[i], dir);

    if (allocate_layer[i] == 1)
      cl_ram_read(L2_weights, L3_weights_curr, weights_size[i]);

    if (i == 0) {
      network_status.chk_in0 = byte_sum((const uint8_t *)L2_input, (size_t)activations_size[0]);
      network_status.chk_w0 = byte_sum((const uint8_t *)L2_weights, (size_t)weights_size[0]);
    }

#ifdef VERBOSE
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
    // performance measurements: end
    pi_perf_stop();
    perf_cyc =  pi_perf_read(PI_PERF_CYCLES);
    network_status.cycles += perf_cyc;


    // TODO: What error?
    // prevents error from compiler
    asm volatile("": : :"memory");
    unsigned int temp = L3_input;
    L3_input = L3_output;
    asm volatile("": : :"memory");
    L3_output = temp;
    asm volatile("": : :"memory");

#ifdef VERBOSE
    printf("Layer %s %d ended: \n", Layers_name[i], i);
    if (L3_output_layers[i]==1) {
      printf("Output in L3. Expected checksum: %d\n", activations_out_checksum[i][exec]);
    } else {
      checksum(i + 1 < 7 ? "L2 output" : "final output",
               L2_output, activations_out_size[i], activations_out_checksum[i][exec]);
    }
    printf("\n");
#endif

    // Free memory
    if (layer_with_weights[i] == 1)
      dfree(weights_size[i], dir);
    dfree(activations_size[i], dir);
    if (branch_input[i] == 1)
      dfree(bypass_dimension, dir);
    L2_input = L2_output;
    // Residual connections
    if (i < 6) {
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

  //memcpy(L2_output, l2_final_output, activations_out_size[6]); // BUGGY!
  for (int i=0; i<activations_out_size[6]; i++)
    *((uint8_t*)(l2_final_output+i)) = *((uint8_t*)(L2_output+i));

  network_status.chk_out = byte_sum((const uint8_t *)L2_output, (size_t)activations_out_size[6]);

  /* Ensure FC core observes cluster-side writes to shared L2. */
  asm volatile("": : :"memory");
  network_status.done = 0xA5A5A5A5u;
  real_args[5] = 0xA5A5A5A5u;
  asm volatile("": : :"memory");


/* ---------------------------------- */
/* --------- SECTION 2 END ---------- */
/* ---------------------------------- */

/* ---------------------------------- */
/* -------- SECTION 3 BEGIN --------- */
/* ---------------------------------- */


/* ---------------------------------- */
/* --------- SECTION 3 END ---------- */
/* ---------------------------------- */
}
