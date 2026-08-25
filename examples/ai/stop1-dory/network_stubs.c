#include "network.h"
#include <stdint.h>
#include <string.h>

int cycle_network_execution;

void network_initialize(void) {}
void network_terminate(void) {}
void network_run_cluster(void *args) { (void)args; }
void execute_layer_fork(void *arg) { (void)arg; }

struct network_run_token network_run_async(void *l2_buffer, size_t l2_buffer_size,
                                           void *l2_final_output, int exec,
                                           int initial_dir) {
  (void)l2_buffer;
  (void)l2_buffer_size;
  (void)l2_final_output;
  (void)exec;
  (void)initial_dir;
  struct network_run_token t;
  memset(&t, 0, sizeof(t));
  return t;
}

void network_run_wait(struct network_run_token token) { (void)token; }

void network_run(void *l2_buffer, size_t l2_buffer_size, void *l2_final_output,
                 int exec, int initial_dir) {
  (void)l2_buffer;
  (void)l2_buffer_size;
  /* Produce a quiet "no detection" head so report path is safe. */
  if (l2_final_output) {
    memset(l2_final_output, 0, 20 * 20 * 6 * sizeof(int32_t));
  }
  (void)exec;
  (void)initial_dir;
  cycle_network_execution = 0;
}
