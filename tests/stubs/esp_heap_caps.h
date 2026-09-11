#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM (1<<10)
#define MALLOC_CAP_INTERNAL (1<<11)
void *heap_caps_malloc(size_t size, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
