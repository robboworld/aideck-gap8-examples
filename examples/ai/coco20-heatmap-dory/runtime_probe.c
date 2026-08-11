#include "pmsis.h"
#include "cpx.h"
#include "network.h"

/*
 * Hardware bring-up stage 2.  This deliberately does not open the camera or
 * run a cluster job: it proves that the DORY executable and the flash-resident
 * weights can start on GAP8 before we add the remaining moving parts.
 */
static void application(void) {
  cpxInit();
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 probe: CPX ready\n");
  /* The AI Deck boot image already configures its FLL.  Explicitly changing
   * it to 250 MHz hangs this board before the application can run. */
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 probe: using boot frequency\n");
  network_initialize();
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 probe: network initialized\n");
  while (1) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20 probe: alive\n");
    pi_time_wait_us(1000000);
  }
}

int main(void) {
  return pmsis_kickoff((void *)application);
}
