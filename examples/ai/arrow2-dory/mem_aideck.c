#include "mem.h"
#include "pmsis.h"
#include "bsp/bsp.h"
#include <stdint.h>
#include <string.h>

#ifdef ARROW2_EMBED_WEIGHTS
#include "embedded_weights.h"
#else
#include "bsp/fs.h"
#include "bsp/fs/readfs.h"
#include "bsp/flash.h"
#include "bsp/ram.h"
#ifdef USE_HYPERFLASH
#include "bsp/flash/hyperflash.h"
typedef struct pi_hyperflash_conf flash_conf_t;
#define flash_conf_init(conf) pi_hyperflash_conf_init(conf)
#else
typedef struct pi_default_flash_conf flash_conf_t;
#define flash_conf_init(conf) pi_default_flash_conf_init(conf)
#endif
#ifdef USE_HYPERRAM
#include "bsp/ram/hyperram.h"
typedef struct pi_hyperram_conf ram_conf_t;
#define ram_conf_init(conf) pi_hyperram_conf_init(conf)
#else
typedef struct pi_default_ram_conf ram_conf_t;
#define ram_conf_init(conf) pi_default_ram_conf_init(conf)
#endif
#endif

#define L2_EXT_POOL_BYTES 24576
static uint8_t l2_ext_pool[L2_EXT_POOL_BYTES];
static size_t l2_ext_used;

#ifndef ARROW2_EMBED_WEIGHTS
#define BUFFER_SIZE 128
static uint8_t buffer[BUFFER_SIZE];
static struct pi_device flash;
static flash_conf_t flash_conf;
static struct pi_device fs;
static struct pi_readfs_conf fs_conf;
static struct pi_device ram;
static ram_conf_t ram_conf;
#endif

int mem_init(void) {
  l2_ext_used = 0;
#ifndef ARROW2_EMBED_WEIGHTS
  flash_conf_init(&flash_conf);
  pi_open_from_conf(&flash, &flash_conf);
  if (pi_flash_open(&flash)) {
    return -1;
  }

  pi_readfs_conf_init(&fs_conf);
  fs_conf.fs.flash = &flash;
  pi_open_from_conf(&fs, &fs_conf);
  if (pi_fs_mount(&fs)) {
    return -2;
  }

  ram_conf_init(&ram_conf);
  pi_open_from_conf(&ram, &ram_conf);
  if (pi_ram_open(&ram)) {
    return -3;
  }
#endif
  return 0;
}

struct pi_device *get_ram_ptr() {
#ifdef ARROW2_EMBED_WEIGHTS
  return NULL;
#else
  return &ram;
#endif
}

void *ram_malloc(size_t size) {
  size = (size + 3u) & ~3u;
  if (l2_ext_used + size > L2_EXT_POOL_BYTES) {
    return NULL;
  }
  void *ptr = &l2_ext_pool[l2_ext_used];
  l2_ext_used += size;
  return ptr;
}

void ram_free(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
}

void ram_read(void *dest, void *src, const size_t size) {
  memcpy(dest, src, size);
}

void ram_write(void *dest, void *src, const size_t size) {
  memcpy(dest, src, size);
}

void *cl_ram_malloc(size_t size) {
  return ram_malloc(size);
}

void cl_ram_free(void *ptr, size_t size) {
  ram_free(ptr, size);
}

void cl_ram_read(void *dest, void *src, const size_t size) {
  memcpy(dest, src, size);
}

void cl_ram_write(void *dest, void *src, const size_t size) {
  memcpy(dest, src, size);
}

size_t load_file_to_ram(const void *dest, const char *filename) {
#ifdef ARROW2_EMBED_WEIGHTS
  const uint8_t *src = NULL;
  size_t size = 0;
  if (!strcmp(filename, "BNReluConvolution0_weights.hex")) {
    src = BNReluConvolution0_weights;
    size = BNReluConvolution0_weights_len;
  } else if (!strcmp(filename, "BNReluConvolution2_weights.hex")) {
    src = BNReluConvolution2_weights;
    size = BNReluConvolution2_weights_len;
  } else if (!strcmp(filename, "BNReluConvolution4_weights.hex")) {
    src = BNReluConvolution4_weights;
    size = BNReluConvolution4_weights_len;
  } else if (!strcmp(filename, "FullyConnected6_weights.hex")) {
    src = FullyConnected6_weights;
    size = FullyConnected6_weights_len;
  } else if (!strcmp(filename, "inputs.hex")) {
    src = inputs;
    size = inputs_len;
  } else {
    return 0;
  }
  memcpy((void *)dest, src, size);
  return size;
#else
  pi_fs_file_t *fd = pi_fs_open(&fs, filename, 0);
  if (fd == NULL) {
    printf("ERROR: Cannot open file %s!\n", filename);
    return 0;
  }

  const size_t size = fd->size;
  size_t offset = 0;
  do {
    const size_t read_bytes = pi_fs_read(fd, buffer, BUFFER_SIZE);
    ram_write((void *)((const uint8_t *)dest + offset), buffer, read_bytes);
    offset += read_bytes;
  } while (offset < size);
  return offset;
#endif
}
