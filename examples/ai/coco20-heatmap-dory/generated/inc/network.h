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
  "BNReluConvolution0_weights.hex", "BNReluConvolution1_weights.hex", "BNReluConvolution2_weights.hex", "BNReluConvolution3_weights.hex", "BNReluConvolution4_weights.hex", "Convolution5_weights.hex"
};
static int L3_weights_size[6];
static int layers_pointers[6];
static char * Layers_name[6] = {"BNReluConvolution0", "BNReluConvolution1", "BNReluConvolution2", "BNReluConvolution3", "BNReluConvolution4", "Convolution5"};
static int L3_input_layers[6] = {1,
0, 0, 0, 0, 0};
static int L3_output_layers[6] = {0, 0, 0, 0, 0, 0};
static int allocate_layer[6] = {1, 1, 1, 1, 1, 1};
static int branch_input[6] = {0, 0, 0, 0, 0, 0};
static int branch_output[6] = {0, 0, 0, 0, 0, 0};
static int branch_change[6] = {0, 0, 0, 0, 0, 0};
static int weights_checksum[6] = {37960, 945925, 4534316, 7575974, 7270920, 2614440};
static int weights_size[6] = {400, 7680, 35840, 58880, 58880, 17376};
static int activations_checksum[6][1] = {{
  2522130  },
{
  265986  },
{
  306083  },
{
  264640  },
{
  399395  },
{
  538548  }
};
static int activations_size[6] = {25600, 102400, 76800, 32000, 32000, 32000};
static int out_mult_vector[6] = {1, 1, 1, 1, 1, 1};
static int out_shift_vector[6] = {24, 24, 24, 24, 24, 0};
static int activations_out_checksum[6][1] = {{
  265986 },
{
  306083 },
{
  264640 },
{
  399395 },
{
  538548 },
{
  6383197 }
};
static int activations_out_size[6] = {102400, 76800, 32000, 32000, 32000, 38400};
static int layer_with_weights[6] = {1, 1, 1, 1, 1, 1};
#endif

#endif  // __NETWORK_H__
