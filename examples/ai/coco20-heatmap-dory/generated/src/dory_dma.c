#include "dory_dma.h"

#include <stdint.h>
#include <string.h>

#include "pmsis.h"

/*
 * PMSIS/AI-deck path: prefer core-driven copies. The FreeRTOS cluster DMA
 * path has been unreliable for this DORY graph on physical GAP8; L2 and L1
 * are both core-accessible from the cluster, so memcpy is correct and simple.
 */
static void copy_blocking(
  void *external,
  void *local,
  unsigned int size,
  int dir_to_local
) {
  if (dir_to_local) {
    memcpy(local, external, size);
  } else {
    memcpy(external, local, size);
  }
}

void dory_dma_memcpy_hwc_to_chw(DMA_copy *copy) {
  if (pi_core_id() != 0) {
    return;
  }
  uint8_t *external = (uint8_t *)copy->ext;
  uint8_t *local = (uint8_t *)copy->loc;
  const unsigned int pixels =
    copy->number_of_1d_copies * copy->number_of_2d_copies;
  for (unsigned int channel = 0; channel < copy->length_1d_copy; ++channel) {
    for (unsigned int pixel = 0; pixel < pixels; ++pixel) {
      if (copy->dir) {
        local[channel * pixels + pixel] =
          external[pixel * copy->stride_1d + channel];
      } else {
        external[pixel * copy->stride_1d + channel] =
          local[channel * pixels + pixel];
      }
    }
  }
}

void dory_dma_memcpy_1d_async(DMA_copy *copy) {
  if (pi_core_id() != 0) {
    return;
  }
  const unsigned int size =
    copy->length_1d_copy *
    copy->number_of_1d_copies *
    copy->number_of_2d_copies;
  copy_blocking(
    (void *)copy->ext,
    (void *)copy->loc,
    size,
    copy->dir
  );
}

void dory_dma_memcpy_2d_async(DMA_copy *copy) {
  dory_dma_memcpy_3d_async(copy);
}

void dory_dma_memcpy_3d_async(DMA_copy *copy) {
  if (pi_core_id() != 0) {
    return;
  }
  uint8_t *external = (uint8_t *)copy->ext;
  uint8_t *local = (uint8_t *)copy->loc;
  for (
    unsigned int plane = 0;
    plane < copy->number_of_2d_copies;
    ++plane
  ) {
    for (
      unsigned int row = 0;
      row < copy->number_of_1d_copies;
      ++row
    ) {
      uint8_t *external_row =
        external + plane * copy->stride_2d + row * copy->stride_1d;
      uint8_t *local_row =
        local +
        (plane * copy->number_of_1d_copies + row) *
        copy->length_1d_copy;
      copy_blocking(
        external_row,
        local_row,
        copy->length_1d_copy,
        copy->dir
      );
    }
  }
}

void dory_dma_memcpy_async(DMA_copy *copy) {
  if (copy->hwc_to_chw) {
    dory_dma_memcpy_hwc_to_chw(copy);
  } else if (
    copy->number_of_2d_copies == 1 &&
    copy->number_of_1d_copies == 1
  ) {
    dory_dma_memcpy_1d_async(copy);
  } else {
    dory_dma_memcpy_3d_async(copy);
  }
}

void dory_dma_free(DMA_copy *copy) {
  (void)copy;
}

void dory_dma_barrier(DMA_copy *copy) {
  (void)copy;
  pi_cl_team_barrier(0);
}

int dory_dma_allocate(void) {
  return 0;
}
