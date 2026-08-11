/*
 * network.h
 * Alessio Burrello <alessio.burrello@unibo.it>
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

#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <stddef.h>
#include "pmsis.h"


struct network_run_token {
  struct pi_device cluster_dev;
};


void network_terminate();
void network_initialize();
void network_run_cluster(void * args);
struct network_run_token network_run_async(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir);
void network_run_wait(struct network_run_token token);
void network_run(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output, int exec, int initial_dir);
void execute_layer_fork(void *arg);


#ifdef DEFINE_CONSTANTS
// allocation of buffers with parameters needed by the network execution
static const char * L3_weights_files[] = {
  "BNReluConvolution0_weights.hex", "BNReluConvolution1_weights.hex", "BNReluConvolution2_weights.hex", "BNReluConvolution3_weights.hex", "Convolution4_weights.hex"
};
static int L3_weights_size[5];
static int layers_pointers[5];
static char * Layers_name[5] = {"BNReluConvolution0", "BNReluConvolution1", "BNReluConvolution2", "BNReluConvolution3", "Convolution4"};
static int L3_input_layers[5] = {1,
0, 0, 0, 0};
static int L3_output_layers[5] = {0, 0, 0, 0, 0};
static int allocate_layer[5] = {1, 1, 1, 1, 1};
static int branch_input[5] = {0, 0, 0, 0, 0};
static int branch_output[5] = {0, 0, 0, 0, 0};
static int branch_change[5] = {0, 0, 0, 0, 0};
static int weights_checksum[5] = {39802, 904573, 4593090, 7494434, 2502064};
static int weights_size[5] = {400, 7680, 35840, 58880, 17376};
static int activations_checksum[5][1] = {{
  2522130  },
{
  267588  },
{
  255141  },
{
  181193  },
{
  296140  }
};
static int activations_size[5] = {25600, 102400, 76800, 32000, 32000};
static int out_mult_vector[5] = {1, 1, 1, 1, 1};
static int out_shift_vector[5] = {24, 24, 24, 24, 0};
static int activations_out_checksum[5][1] = {{
  267588 },
{
  255141 },
{
  181193 },
{
  296140 },
{
  6481726 }
};
static int activations_out_size[5] = {102400, 76800, 32000, 32000, 38400};
static int layer_with_weights[5] = {1, 1, 1, 1, 1};
#endif

#endif  // __NETWORK_H__
