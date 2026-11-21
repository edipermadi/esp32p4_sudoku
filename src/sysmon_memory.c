/**
 * @file sysmon_memory.c
 * @ingroup sysmon_memory
 * @brief The heaps: size, free, low-water mark, largest block.
 */
#include "sysmon_memory.h"
#include "esp_heap_caps.h"

/* (documented in sysmon_memory.h) */
void sysmon_heap(uint32_t caps, bool with_min, sysmon_heap_t *out)
{
    out->total = heap_caps_get_total_size(caps);
    out->free = heap_caps_get_free_size(caps);
    out->min_free = with_min ? heap_caps_get_minimum_free_size(caps) : 0;
    out->largest = with_min ? heap_caps_get_largest_free_block(caps) : 0;   /* (internal: ~0.1 ms) */
}
