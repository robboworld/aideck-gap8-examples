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
  "BNReluConvolution0_weights.hex", "BNReluConvolution2_weights.hex", "BNReluConvolution4_weights.hex", "FullyConnected6_weights.hex"
};
static int L3_weights_size[4];
static int layers_pointers[7];
static char * Layers_name[7] = {"BNReluConvolution0", "Pooling1", "BNReluConvolution2", "Pooling3", "BNReluConvolution4", "Pooling5", "FullyConnected6"};
static int L3_input_layers[7] = {1,
0, 0, 0, 0, 0, 0};
static int L3_output_layers[7] = {0, 0, 0, 0, 0, 0, 0};
static int allocate_layer[7] = {1, 0, 1, 0, 1, 0, 1};
static int branch_input[7] = {0, 0, 0, 0, 0, 0, 0};
static int branch_output[7] = {0, 0, 0, 0, 0, 0, 0};
static int branch_change[7] = {0, 0, 0, 0, 0, 0, 0};
static int weights_checksum[7] = {18521, 0, 166412, 0, 462402, 0, 96917};
static int weights_size[7] = {200, 0, 1408, 0, 3840, 0, 768};
static int activations_checksum[7][1] = {{
  47077  },
{
  202628  },
{
  64432  },
{
  52540  },
{
  25205  },
{
  22566  },
{
  14055  }
};
static int activations_size[7] = {1024, 8192, 2048, 4096, 1024, 1536, 384};
static int out_mult_vector[7] = {1, 1, 1, 1, 1, 1, 1};
static int out_shift_vector[7] = {24, 0, 24, 0, 24, 0, 0};
static int activations_out_checksum[7][1] = {{
  202628 },
{
  64432 },
{
  52540 },
{
  25205 },
{
  22566 },
{
  14055 },
{
  924 }
};
static int activations_out_size[7] = {8192, 2048, 4096, 1024, 1536, 384, 8};
static int layer_with_weights[7] = {1, 0, 1, 0, 1, 0, 1};
#endif

#endif  // __NETWORK_H__
