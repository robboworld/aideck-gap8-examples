#include "pmsis.h"

#define log2(x) __builtin_pulp_fl1(x)
#define min(a, b) ((a) < (b) ? (a) : (b))

/* GAP8 head kernel: pointwise int8 weights -> int32 logits. */
void pulp_nn_pointwise_HoWo_parallel_32(
    const uint8_t *input, uint8_t *unused_im2col, const int32_t *bias,
    int32_t *output, const int8_t *weights, int64_t *unused_k,
    int64_t *unused_lambda, uint16_t unused_mult, uint16_t unused_shift,
    uint16_t dim_in_x, uint16_t dim_in_y, uint16_t channels_in,
    uint16_t dim_out_x, uint16_t dim_out_y, uint16_t channels_out,
    uint16_t unused_kernel_x, uint16_t unused_kernel_y,
    uint16_t unused_padding_top, uint16_t unused_padding_bottom,
    uint16_t unused_padding_left, uint16_t unused_padding_right,
    uint16_t unused_stride_x, uint16_t unused_stride_y,
    int unused_relu, int unused_batch_norm) {
  (void) unused_im2col; (void) unused_k; (void) unused_lambda;
  (void) unused_mult; (void) unused_shift; (void) dim_in_x;
  (void) unused_kernel_x; (void) unused_kernel_y; (void) unused_padding_top;
  (void) unused_padding_bottom; (void) unused_padding_left;
  (void) unused_padding_right; (void) unused_stride_x; (void) unused_stride_y;
  (void) unused_relu; (void) unused_batch_norm;
  int core = pi_core_id();
  int chunk = (dim_out_y >> log2(NUM_CORES)) + ((dim_out_y & (NUM_CORES - 1)) != 0);
  int y0 = min(chunk * core, dim_out_y);
  int y1 = min(y0 + chunk, dim_out_y);
  for (int y = y0; y < y1; y++) {
    for (int x = 0; x < dim_out_x; x++) {
      const uint8_t *pixel = input + (y * dim_in_x + x) * channels_in;
      int32_t *out = output + (y * dim_out_x + x) * channels_out;
      for (int co = 0; co < channels_out; co++) {
        int32_t sum = bias ? bias[co] : 0;
        for (int ci = 0; ci < channels_in; ci++)
          sum += (int32_t) pixel[ci] * (int32_t) weights[co * channels_in + ci];
        out[co] = sum;
      }
    }
  }
  pi_cl_team_barrier(0);
}
