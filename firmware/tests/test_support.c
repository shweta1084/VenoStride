#include "test_support.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

static float noise(uint32_t *seed, float amplitude)
{
    *seed = *seed * 1664525u + 1013904223u;
    const float u = (float)(*seed >> 8) / (float)(1u << 24);
    return (u * 2.0f - 1.0f) * amplitude;
}

void trace_init(trace_t *t, const trace_segment_t *segments, size_t count)
{
    memset(t, 0, sizeof(*t));
    t->segments = segments;
    t->count = count;
    t->start_ms = host_now_ms();
    t->tilt_deg = 60.0f;
    t->temp_c = 25.0f;
    t->seed = 12345u;
}

static trace_kind_t kind_at(const trace_t *t, uint64_t rel_ms)
{
    uint64_t acc = 0;
    for (size_t i = 0; i < t->count; i++) {
        if (t->segments[i].duration_ms == 0 || rel_ms < acc + t->segments[i].duration_ms) {
            return t->segments[i].kind;
        }
        acc += t->segments[i].duration_ms;
    }
    return t->count > 0 ? t->segments[t->count - 1].kind : TRACE_STAND;
}

void trace_sample(uint64_t t_ms, hal_imu_sample_t *out, void *ctx)
{
    trace_t *t = (trace_t *)ctx;
    const uint64_t rel = t_ms - t->start_ms;
    const float ts = (float)rel / 1000.0f;
    memset(out, 0, sizeof(*out));
    out->temp_c = t->temp_c + t->temp_slope_c_per_s * ts;

    switch (kind_at(t, rel)) {
    case TRACE_STAND:
        out->accel_g[2] = 1.0f;
        break;
    case TRACE_WALK: {
        const float w = 2.0f * PI_F * 1.8f * ts;
        out->accel_g[0] = 0.4f * sinf(w + 1.0f);
        out->accel_g[2] = 1.0f + 1.0f * sinf(w);
        out->gyro_dps[1] = 200.0f * sinf(w);
        break;
    }
    case TRACE_TILTED: {
        const float rad = t->tilt_deg * PI_F / 180.0f;
        out->accel_g[1] = sinf(rad);
        out->accel_g[2] = cosf(rad);
        break;
    }
    }
    for (int i = 0; i < 3; i++) {
        out->accel_g[i] += noise(&t->seed, t->noise_g);
    }
}

void support_imu_forever(trace_t *storage, trace_segment_t *segment_storage, trace_kind_t kind)
{
    segment_storage->kind = kind;
    segment_storage->duration_ms = 0;
    trace_init(storage, segment_storage, 1);
    host_imu_set_source(trace_sample, storage);
}

void support_boot_with_mode(vs_mode_t mode)
{
    host_reset();
    host_boot(NULL);
    vs_cmd_select_mode(mode, "0000");
}

uint64_t support_run_until(bool (*pred)(void), uint64_t timeout_ms)
{
    const uint64_t start = host_now_ms();
    while (host_now_ms() - start < timeout_ms) {
        if (pred()) {
            return host_now_ms() - start;
        }
        host_run_ms(VS_TICK_MS);
    }
    return pred() ? host_now_ms() - start : UINT64_MAX;
}

size_t support_count_records(compliance_type_t type, int arg)
{
    compliance_record_t buf[64];
    size_t total = 0;
    uint32_t from = 0;
    for (;;) {
        const size_t n = vs_cmd_read_compliance(from, buf, 64);
        for (size_t i = 0; i < n; i++) {
            if (buf[i].type == (uint8_t)type && (arg < 0 || buf[i].arg == (uint8_t)arg)) {
                total++;
            }
        }
        if (n < 64) {
            break;
        }
        from = buf[n - 1].seq + 1u;
    }
    return total;
}

bool support_find_last_record(compliance_type_t type, int arg, compliance_record_t *out)
{
    compliance_record_t buf[64];
    bool found = false;
    uint32_t from = 0;
    for (;;) {
        const size_t n = vs_cmd_read_compliance(from, buf, 64);
        for (size_t i = 0; i < n; i++) {
            if (buf[i].type == (uint8_t)type && (arg < 0 || buf[i].arg == (uint8_t)arg)) {
                *out = buf[i];
                found = true;
            }
        }
        if (n < 64) {
            break;
        }
        from = buf[n - 1].seq + 1u;
    }
    return found;
}

vs_device_status_t support_status(void)
{
    vs_device_status_t st;
    vs_cmd_get_status(&st);
    return st;
}
