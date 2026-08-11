#include "net_utils.h"
#include "pmsis.h"

void print_perf(const char *name, const int cycles, const int macs) {
  printf("\n%s performance:\n", name);
  printf("  - num cycles: %d\n", cycles);
  printf("  - MACs: %d\n", macs );
  if (cycles > 0) {
    float perf = (float) macs / (float) cycles;
    printf("  - MAC/cycle: %g\n", perf);
  } else {
    printf("  - MAC/cycle: n/a (0 cycles)\n");
  }
  printf("  - n. of Cores: %d\n\n", NUM_CORES);
}

void checksum(const char *name, const uint8_t *d, size_t size, uint32_t sum_true) {
  uint32_t sum = 0;
  for (int i = 0; i < size; i++) sum += d[i];

  printf("Checking %s: Checksum ", name);
  if (sum_true == sum)
    printf("OK\n");
  else
    printf("Failed: true [%u] vs. calculated [%u]\n", sum_true, sum);
}

