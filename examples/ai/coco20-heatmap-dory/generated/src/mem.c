#include "mem.h"
#include "pmsis.h"
#include <string.h>
void mem_init(void) {}
struct pi_device *get_ram_ptr(void) { return NULL; }
void *ram_malloc(size_t size) { (void)size; return NULL; }
void ram_free(void *ptr, size_t size) { (void)ptr; (void)size; }
void ram_read(void *dest, void *src, size_t size) { memcpy(dest, src, size); }
void ram_write(void *dest, void *src, size_t size) { memcpy(dest, src, size); }
void *cl_ram_malloc(size_t size) { (void)size; return NULL; }
void cl_ram_free(void *ptr, size_t size) { (void)ptr; (void)size; }
void cl_ram_read(void *dest, void *src, size_t size) { memcpy(dest, src, size); }
void cl_ram_write(void *dest, void *src, size_t size) { memcpy(dest, src, size); }
size_t load_file_to_ram(const void *dest, const char *filename) { (void)dest; (void)filename; return 0; }
