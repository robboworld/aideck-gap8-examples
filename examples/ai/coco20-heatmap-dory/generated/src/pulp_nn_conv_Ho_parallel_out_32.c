/* Int32 output kernel for the final unactivated 3x3 detector head.
 *
 * DORY emits a 32-bit output tensor for this layer but its stock GAP8
 * template calls the uint8 activation kernel.  The latter truncates logits
 * and interprets the int32 biases as int8.  This kernel preserves the signed
 * accumulation needed by the heatmap decoder.  Earlier layers remain the
 * generated DORY/PULP-NN kernels.
 */
#include "pmsis.h"
#include <stdint.h>

void pulp_nn_conv_Ho_parallel_out_32(
    const uint8_t *input, int32_t *output, const int8_t *weights,
    const int32_t *bias, uint16_t input_width, uint16_t input_height,
    uint16_t input_channels, uint16_t output_width, uint16_t output_height,
    uint16_t output_channels, uint16_t pad_top, uint16_t pad_bottom,
    uint16_t pad_left, uint16_t pad_right) {
  (void)pad_bottom;
  (void)pad_right;
  /* The generic Convolution4 template supplies an outer team barrier.  Run
   * this small 24-channel head on the cluster controller only: its scalar
   * int32 implementation avoids a GAP8 FreeRTOS worker synchronisation
   * fault, while the preceding backbone still uses all eight cores. */
  if (pi_core_id() != 0) return;
  const uint16_t row_begin = 0;
  const uint16_t row_end = output_height;
  for (uint16_t oy = row_begin; oy < row_end; ++oy) {
    for (uint16_t ox = 0; ox < output_width; ++ox) {
      for (uint16_t oc = 0; oc < output_channels; ++oc) {
        int32_t sum = bias == NULL ? 0 : bias[oc];
        const int8_t *kernel = weights + (size_t)oc * 9U * input_channels;
        for (uint16_t ky = 0; ky < 3; ++ky) {
          const int iy = (int)oy + (int)ky - (int)pad_top;
          if (iy < 0 || iy >= input_height) continue;
          for (uint16_t kx = 0; kx < 3; ++kx) {
            const int ix = (int)ox + (int)kx - (int)pad_left;
            if (ix < 0 || ix >= input_width) continue;
            const uint8_t *pixel = input + ((size_t)iy * input_width + (uint16_t)ix) * input_channels;
            const int8_t *coefficients = kernel + ((size_t)ky * 3U + kx) * input_channels;
            for (uint16_t ic = 0; ic < input_channels; ++ic) sum += (int32_t)pixel[ic] * coefficients[ic];
          }
        }
        output[((size_t)oy * output_width + ox) * output_channels + oc] = sum;
      }
    }
  }
  /* pi_cl_team_fork performs the join. An extra explicit barrier here can
   * deadlock the final head on GAP8/FreeRTOS after the worker threads finish. */
}
