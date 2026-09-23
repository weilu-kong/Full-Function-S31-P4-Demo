#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t int_free;
    size_t int_min;
    size_t int_largest;
    size_t psram_free;
    size_t psram_min;
    size_t psram_largest;
    size_t psram_simd_largest;
    uint32_t task_count;
} app_heap_snapshot_t;

void app_health_capture_heap(app_heap_snapshot_t *out);
void app_health_log_heap(const char *tag);

#ifdef __cplusplus
}
#endif
