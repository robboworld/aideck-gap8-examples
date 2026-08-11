#include "pmsis.h"
#include "cpx.h"

static void application(void) {
  cpxInit();
  cpxPrintToConsole(LOG_TO_CRTP, "coco20 boot probe: cpx ready\n");
  while (1) {
    cpxPrintToConsole(LOG_TO_CRTP, "coco20 boot probe: alive\n");
    pi_time_wait_us(1000000);
  }
}
int main(void) { return pmsis_kickoff((void *)application); }
