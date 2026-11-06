#include "host_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"

bool host_trace_load(host_trace_t *trace, const char *path)
{
    memset(trace, 0, sizeof(*trace));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    size_t capacity = 1024;
    trace->samples = (hal_imu_sample_t *)malloc(capacity * sizeof(hal_imu_sample_t));
    if (trace->samples == NULL) {
        fclose(f);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        hal_imu_sample_t s;
        const int n = sscanf(line, "%f,%f,%f,%f,%f,%f,%f", &s.accel_g[0], &s.accel_g[1], &s.accel_g[2],
                             &s.gyro_dps[0], &s.gyro_dps[1], &s.gyro_dps[2], &s.temp_c);
        if (n != 7) {
            continue; /* header or blank line */
        }
        if (trace->count == capacity) {
            capacity *= 2;
            hal_imu_sample_t *grown = (hal_imu_sample_t *)realloc(trace->samples, capacity * sizeof(hal_imu_sample_t));
            if (grown == NULL) {
                fclose(f);
                host_trace_free(trace);
                return false;
            }
            trace->samples = grown;
        }
        trace->samples[trace->count++] = s;
    }
    fclose(f);
    if (trace->count == 0) {
        host_trace_free(trace);
        return false;
    }
    return true;
}

void host_trace_free(host_trace_t *trace)
{
    free(trace->samples);
    memset(trace, 0, sizeof(*trace));
}

static void trace_source(uint64_t t_ms, hal_imu_sample_t *out, void *ctx)
{
    const host_trace_t *trace = (const host_trace_t *)ctx;
    size_t index = (size_t)((t_ms - trace->start_ms) / VS_TICK_MS);
    if (index >= trace->count) {
        index = trace->loop ? index % trace->count : trace->count - 1u;
    }
    *out = trace->samples[index];
}

void host_trace_install(host_trace_t *trace, bool loop)
{
    trace->loop = loop;
    /* The first firmware tick after install happens at now + 10 ms and must read row 0. */
    trace->start_ms = host_now_ms() + VS_TICK_MS;
    host_imu_set_source(trace_source, trace);
}

uint64_t host_trace_duration_ms(const host_trace_t *trace)
{
    return (uint64_t)trace->count * VS_TICK_MS;
}
