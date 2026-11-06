#ifndef VS_HOST_TRACE_H
#define VS_HOST_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/* IMU replay from CSV rows (ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, temp_c), one row per 10 ms. */
typedef struct {
    hal_imu_sample_t *samples;
    size_t count;
    uint64_t start_ms;
    bool loop;
} host_trace_t;

bool host_trace_load(host_trace_t *trace, const char *path);
void host_trace_free(host_trace_t *trace);

/* Feeds the trace to the IMU from the current virtual time; past the end it loops or holds the last row. */
void host_trace_install(host_trace_t *trace, bool loop);

uint64_t host_trace_duration_ms(const host_trace_t *trace);

#endif
