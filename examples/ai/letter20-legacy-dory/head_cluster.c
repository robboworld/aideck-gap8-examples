#include <stdint.h>
#include <string.h>
#include "pmsis.h"
#include "network.h"
#include "embedded_weights.h"

#define HEAD_GRID 20
#define HEAD_CH 25
#define HEAD_IC 48
#define HEAD_K 9
#define HEAD_WEIGHT_OFFSET (400 + 5120 + 14592 + 21504)
#define HEAD_WEIGHT_BYTES 10900
#define HEAD_BIAS_OFF 10800
#define HEAD_KERN (HEAD_K * HEAD_IC)

#define SumDotp(a, b, c) __builtin_pulp_sdotusp4((a), (b), (c))

typedef struct {
  const uint8_t *feat;
  int32_t *out;
  const int8_t *w;
  const int32_t *bias;
} head_job_t;

static PI_L2 uint8_t conv4_w_l2[HEAD_WEIGHT_BYTES];
static int conv4_w_copied;

static inline int32_t dot48(const uint8_t *pixel, const int8_t *coeff, int32_t sum)
{
  unsigned int ic;
  for (ic = 0; ic < HEAD_IC; ic += 4) {
    v4u in = *((v4u *)(pixel + ic));
    v4s wt = *((v4s *)(coeff + ic));
    sum = SumDotp(in, wt, sum);
  }
  return sum;
}

/* Same 3x3 int8→int32 as the FC head; 4-wide sdot + 4-channel unroll. */
static void detector_head_core(void *arg)
{
  head_job_t *j = (head_job_t *)arg;
  const int core = pi_core_id();
  const int ncores = NUM_CORES;
  const int y0 = (HEAD_GRID * core) / ncores;
  const int y1 = (HEAD_GRID * (core + 1)) / ncores;
  const int8_t *weights = j->w;
  const int32_t *bias = j->bias;
  const uint8_t *input = j->feat;
  int32_t *output = j->out;
  int oy, ox, oc, ky, kx;

  for (oy = y0; oy < y1; ++oy) {
    for (ox = 0; ox < HEAD_GRID; ++ox) {
      int32_t *cell = output + (oy * HEAD_GRID + ox) * HEAD_CH;
      for (oc = 0; oc + 3 < HEAD_CH; oc += 4) {
        int32_t s0 = bias[oc];
        int32_t s1 = bias[oc + 1];
        int32_t s2 = bias[oc + 2];
        int32_t s3 = bias[oc + 3];
        const int8_t *k0 = weights + oc * HEAD_KERN;
        const int8_t *k1 = k0 + HEAD_KERN;
        const int8_t *k2 = k1 + HEAD_KERN;
        const int8_t *k3 = k2 + HEAD_KERN;
        for (ky = 0; ky < 3; ++ky) {
          int iy = oy + ky - 1;
          if (iy < 0 || iy >= HEAD_GRID)
            continue;
          for (kx = 0; kx < 3; ++kx) {
            int ix = ox + kx - 1;
            if (ix < 0 || ix >= HEAD_GRID)
              continue;
            const uint8_t *pixel = input + (iy * HEAD_GRID + ix) * HEAD_IC;
            const int tap = (ky * 3 + kx) * HEAD_IC;
            s0 = dot48(pixel, k0 + tap, s0);
            s1 = dot48(pixel, k1 + tap, s1);
            s2 = dot48(pixel, k2 + tap, s2);
            s3 = dot48(pixel, k3 + tap, s3);
          }
        }
        cell[oc] = s0;
        cell[oc + 1] = s1;
        cell[oc + 2] = s2;
        cell[oc + 3] = s3;
      }
      /* HEAD_CH=25 → one leftover channel. */
      {
        int32_t s = bias[24];
        const int8_t *k = weights + 24 * HEAD_KERN;
        for (ky = 0; ky < 3; ++ky) {
          int iy = oy + ky - 1;
          if (iy < 0 || iy >= HEAD_GRID)
            continue;
          for (kx = 0; kx < 3; ++kx) {
            int ix = ox + kx - 1;
            if (ix < 0 || ix >= HEAD_GRID)
              continue;
            s = dot48(input + (iy * HEAD_GRID + ix) * HEAD_IC,
                      k + (ky * 3 + kx) * HEAD_IC, s);
          }
        }
        cell[24] = s;
      }
    }
  }
}

static void detector_head_entry(void *arg)
{
  pi_cl_team_fork(NUM_CORES, detector_head_core, arg);
}

/* FC single-core head — cluster head after backbone wedged the next
 * pi_cluster_send_task_to_cl under JTAG_HYPER boot. */
static void detector_head_fc(head_job_t *j)
{
  const int8_t *weights = j->w;
  const int32_t *bias = j->bias;
  const uint8_t *input = j->feat;
  int32_t *output = j->out;
  int oy, ox, oc, ky, kx;

  for (oy = 0; oy < HEAD_GRID; ++oy) {
    *(volatile uint32_t *)0x1b00082c = (uint32_t)oy;
    for (ox = 0; ox < HEAD_GRID; ++ox) {
      int32_t *cell = output + (oy * HEAD_GRID + ox) * HEAD_CH;
      for (oc = 0; oc + 3 < HEAD_CH; oc += 4) {
        int32_t s0 = bias[oc];
        int32_t s1 = bias[oc + 1];
        int32_t s2 = bias[oc + 2];
        int32_t s3 = bias[oc + 3];
        const int8_t *k0 = weights + oc * HEAD_KERN;
        const int8_t *k1 = k0 + HEAD_KERN;
        const int8_t *k2 = k1 + HEAD_KERN;
        const int8_t *k3 = k2 + HEAD_KERN;
        for (ky = 0; ky < 3; ++ky) {
          int iy = oy + ky - 1;
          if (iy < 0 || iy >= HEAD_GRID)
            continue;
          for (kx = 0; kx < 3; ++kx) {
            int ix = ox + kx - 1;
            if (ix < 0 || ix >= HEAD_GRID)
              continue;
            const uint8_t *pixel = input + (iy * HEAD_GRID + ix) * HEAD_IC;
            const int tap = (ky * 3 + kx) * HEAD_IC;
            s0 = dot48(pixel, k0 + tap, s0);
            s1 = dot48(pixel, k1 + tap, s1);
            s2 = dot48(pixel, k2 + tap, s2);
            s3 = dot48(pixel, k3 + tap, s3);
          }
        }
        cell[oc] = s0;
        cell[oc + 1] = s1;
        cell[oc + 2] = s2;
        cell[oc + 3] = s3;
      }
      {
        int32_t s = bias[24];
        const int8_t *k = weights + 24 * HEAD_KERN;
        for (ky = 0; ky < 3; ++ky) {
          int iy = oy + ky - 1;
          if (iy < 0 || iy >= HEAD_GRID)
            continue;
          for (kx = 0; kx < 3; ++kx) {
            int ix = ox + kx - 1;
            if (ix < 0 || ix >= HEAD_GRID)
              continue;
            s = dot48(input + (iy * HEAD_GRID + ix) * HEAD_IC,
                      k + (ky * 3 + kx) * HEAD_IC, s);
          }
        }
        cell[24] = s;
      }
    }
  }
}

void network_run_head_fc(const uint8_t *features, int32_t *output)
{
  static head_job_t job;

  if (!conv4_w_copied) {
    memcpy(conv4_w_l2, letter20_dory_weights + HEAD_WEIGHT_OFFSET, HEAD_WEIGHT_BYTES);
    conv4_w_copied = 1;
  }
  job.feat = features;
  job.out = output;
  job.w = (const int8_t *)conv4_w_l2;
  job.bias = (const int32_t *)(conv4_w_l2 + HEAD_BIAS_OFF);
  *(volatile uint32_t *)0x1b000828 = 0x4EAD00FCu;
  detector_head_fc(&job);
  *(volatile uint32_t *)0x1b000828 = 0x4EAD00FDu;
}

void network_run_head(const uint8_t *features, int32_t *output)
{
  static head_job_t job;
  static struct pi_cluster_task task;

  if (!conv4_w_copied) {
    memcpy(conv4_w_l2, letter20_dory_weights + HEAD_WEIGHT_OFFSET, HEAD_WEIGHT_BYTES);
    conv4_w_copied = 1;
  }
  if (network_cluster_ensure_open())
    return;

  job.feat = features;
  job.out = output;
  job.w = (const int8_t *)conv4_w_l2;
  job.bias = (const int32_t *)(conv4_w_l2 + HEAD_BIAS_OFF);

  memset(&task, 0, sizeof(task));
  pi_cluster_task(&task, detector_head_entry, &job);
  task.stack_size = 3500;
  task.slave_stack_size = 3400;
  *(volatile uint32_t *)0x1b000828 = 0x4EAD0001u;
  pi_cluster_send_task_to_cl(network_cluster_device(), &task);
  *(volatile uint32_t *)0x1b000828 = 0x4EAD0002u;
}

/* Called from FC before network_run — copy weights once. */
void network_prepare_inline_head(void)
{
  if (!conv4_w_copied) {
    memcpy(conv4_w_l2, letter20_dory_weights + HEAD_WEIGHT_OFFSET, HEAD_WEIGHT_BYTES);
    conv4_w_copied = 1;
  }
}

/* Must run inside network_run_cluster (already on cluster). Avoids a second
 * pi_cluster_send_task_to_cl that wedges at 4EAD0001 under JTAG_HYPER. */
void network_cluster_run_head_inline(const uint8_t *features, int32_t *output)
{
  head_job_t job;
  job.feat = features;
  job.out = output;
  job.w = (const int8_t *)conv4_w_l2;
  job.bias = (const int32_t *)(conv4_w_l2 + HEAD_BIAS_OFF);
  *(volatile uint32_t *)0x1b000828 = 0x4EAD0010u;
  pi_cl_team_fork(NUM_CORES, detector_head_core, &job);
  *(volatile uint32_t *)0x1b000828 = 0x4EAD0011u;
}
