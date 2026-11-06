#ifndef VS_TEST_SUPPORT_H
#define VS_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "compliance.h"
#include "hal.h"
#include "host.h"
#include "vs_core.h"

/* ---- synthetic IMU traces ---- */

typedef enum {
    TRACE_STAND,       /* upright, still, optional noise */
    TRACE_WALK,        /* ~1.8 Hz gait: >0.5 g dynamic, oscillating gyro */
    TRACE_TILTED       /* gravity rotated away from +Z, still */
} trace_kind_t;

typedef struct {
    trace_kind_t kind;
    uint32_t duration_ms;   /* 0 = forever */
} trace_segment_t;

typedef struct {
    const trace_segment_t *segments;
    size_t count;
    uint64_t start_ms;
    float noise_g;
    float tilt_deg;
    float temp_c;
    float temp_slope_c_per_s;
    uint32_t seed;
} trace_t;

void trace_init(trace_t *t, const trace_segment_t *segments, size_t count);
void trace_sample(uint64_t t_ms, hal_imu_sample_t *out, void *ctx);

/* Installs a single-kind forever trace. */
void support_imu_forever(trace_t *storage, trace_segment_t *segment_storage, trace_kind_t kind);

/* ---- device helpers ---- */

/* Fresh hardware, power-on boot, clinician selects the mode with PIN 0000. */
void support_boot_with_mode(vs_mode_t mode);

/* Runs until pred() is true or timeout; returns elapsed ms or UINT64_MAX on timeout. */
uint64_t support_run_until(bool (*pred)(void), uint64_t timeout_ms);

size_t support_count_records(compliance_type_t type, int arg);
bool support_find_last_record(compliance_type_t type, int arg, compliance_record_t *out);

vs_device_status_t support_status(void);

#endif
