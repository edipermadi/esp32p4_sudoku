/**
 * @file sysmon_memory.h
 * @ingroup sysmon_memory
 * @brief The heaps: size, free, low-water mark, largest block.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_memory Memory
 * @ingroup sysmon_core
 * @brief The heaps: size, free, low-water mark, largest block.
 * @{
 */

/** @brief A heap's numbers. */
typedef struct {
    size_t total;                       /**< its size in bytes (0: no such heap) */
    size_t free;                        /**< free now */
    size_t min_free;                    /**< the least it has had free (with_min only) */
    size_t largest;                     /**< the largest free block (with_min only) */
} sysmon_heap_t;
/**
 * @brief A heap's size and free space.
 * @param caps      which heap: MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM, ...
 * @param with_min  also the low-water mark and the largest free block. Not for PSRAM: its low-water
 *                  mark reads 0 from boot on (the core registers it that way), and its largest block
 *                  walks the whole 28 MB heap with interrupts off (4 ms: the screen blinks).
 * @param[out] out  the numbers
 */
void sysmon_heap(uint32_t caps, bool with_min, sysmon_heap_t *out);

/** @} */

#ifdef __cplusplus
}
#endif
