#include "pmsis.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

/* Int32 3x3 convolution for the heatmap detector head (24 out, no requant). */
void pulp_nn_conv_Ho_parallel_int32(
    const uint8_t *input, uint8_t *unused_im2col, const int32_t *bias,
    int32_t *output, const int8_t *weights, int64_t *unused_k,
    int64_t *unused_lambda, uint16_t unused_mult, uint16_t unused_shift,
    uint16_t dim_in_x, uint16_t dim_in_y, uint16_t channels_in,
    uint16_t dim_out_x, uint16_t dim_out_y, uint16_t channels_out,
    uint16_t kernel_x, uint16_t kernel_y,
    uint16_t padding_top, uint16_t padding_bottom,
    uint16_t padding_left, uint16_t padding_right,
    uint16_t stride_x, uint16_t stride_y,
    int unused_relu, int unused_batch_norm) {
  (void) unused_im2col; (void) unused_k; (void) unused_lambda;
  (void) unused_mult; (void) unused_shift; (void) unused_relu;
  (void) unused_batch_norm; (void) padding_bottom; (void) padding_right;
  int core = pi_core_id();
  int chunk = (dim_out_y / NUM_CORES) + ((dim_out_y % NUM_CORES) != 0);
  int y0 = min(chunk * core, dim_out_y);
  int y1 = min(y0 + chunk, dim_out_y);
  for (int oy = y0; oy < y1; oy++) {
    for (int ox = 0; ox < dim_out_x; ox++) {
      int32_t *out = output + (oy * dim_out_x + ox) * channels_out;
      for (int co = 0; co < channels_out; co++) {
        int32_t sum = bias ? bias[co] : 0;
        const int8_t *w = weights + co * kernel_y * kernel_x * channels_in;
        for (int ky = 0; ky < kernel_y; ky++) {
          for (int kx = 0; kx < kernel_x; kx++) {
            int iy = oy * stride_y + ky - padding_top;
            int ix = ox * stride_x + kx - padding_left;
            if (iy < 0 || ix < 0 || iy >= dim_in_y || ix >= dim_in_x)
              continue;
            const uint8_t *pixel = input + (iy * dim_in_x + ix) * channels_in;
            const int8_t *wk = w + (ky * kernel_x + kx) * channels_in;
            for (int ci = 0; ci < channels_in; ci++)
              sum += (int32_t) pixel[ci] * (int32_t) wk[ci];
          }
        }
        out[co] = sum;
      }
    }
  }
  pi_cl_team_barrier(0);
}
