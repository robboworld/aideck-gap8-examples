#include <stdint.h>
#include "pmsis.h"
#include "cpx.h"
#include "embedded_weights.h"

/* Stage 2a: prove that the generated 120 KiB constant weight block itself
 * can coexist with a GAP8 application image. */
static void application(void) {
  cpxInit();
  cpxPrintToConsole(LOG_TO_CRTP,
                    "coco20 weights probe: ready size=%d first=%u last=%u\\n",
                    (int)coco20_dory_weights_len,
                    coco20_dory_weights[0],
                    coco20_dory_weights[coco20_dory_weights_len - 1]);
  /* Let CPX transmit the baseline marker before touching the FLL. */
  pi_time_wait_us(1000000);
  pi_freq_set(PI_FREQ_DOMAIN_FC, 250000000);
  __pi_pmu_voltage_set(PI_PMU_DOMAIN_FC, 1200);
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 weights probe: frequency ready\\n");
  while (1) {
    pi_time_wait_us(1000000);
  }
}

int main(void) { return pmsis_kickoff((void *)application); }
