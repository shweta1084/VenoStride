#include <math.h>
#include <string.h>

#include "test_harness.h"
#include "test_support.h"

#define PI_F 3.14159265f

typedef struct {
    uint64_t onset_ms;
    float amp_g;
    float freq_hz;
    float phase;
    bool impulse_only;
} gait_ctx_t;

static void gait_source(uint64_t t_ms, hal_imu_sample_t *out, void *ctx)
{
    const gait_ctx_t *g = (const gait_ctx_t *)ctx;
    memset(out, 0, sizeof(*out));
    out->accel_g[2] = 1.0f;
    out->temp_c = 25.0f;
    if (t_ms < g->onset_ms) {
        return;
    }
    if (g->impulse_only) {
        out->accel_g[2] += g->amp_g;
        return;
    }
    const float t = (float)(t_ms - g->onset_ms) / 1000.0f;
    const float w = 2.0f * PI_F * g->freq_hz * t + g->phase;
    out->accel_g[2] += g->amp_g * sinf(w);
    out->gyro_dps[1] = 150.0f * sinf(w);
}

static bool in_on_window_at_full(void)
{
    return host_stim_delivered_ua() == 20000;
}

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

/* Runs tick by tick until WALKING; checks the cut happened in that same tick. Returns ms from onset. */
static uint64_t run_to_walking_and_check_cut(uint64_t onset_ms)
{
    const uint32_t cutoffs_before = host_stim()->cutoff_count;
    for (int i = 0; i < 300; i++) {
        host_run_ms(VS_TICK_MS);
        if (vs_core()->posture_state == VS_POSTURE_WALKING) {
            CHECK_EQ(host_stim_delivered_ua(), 0);
            CHECK(!host_stim()->boost_on);
            CHECK_EQ(host_stim()->cutoff_count, cutoffs_before + 1);
            CHECK_EQ(host_stim()->last_cutoff_ms, host_now_ms());
            return host_now_ms() - onset_ms;
        }
        CHECK(host_stim_delivered_ua() > 0 || host_stim()->boost_on);
    }
    CHECK(!"walking never detected");
    return UINT64_MAX;
}

static void impulse_cuts_output_in_the_same_10_ms_tick(void)
{
    gait_ctx_t g = {UINT64_MAX, 0.8f, 0.0f, 0.0f, true};
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_imu_set_source(gait_source, &g);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(30000);
    CHECK(support_run_until(in_on_window_at_full, 5000) != UINT64_MAX);

    g.onset_ms = host_now_ms() + VS_TICK_MS;
    const uint64_t latency = run_to_walking_and_check_cut(g.onset_ms);
    CHECK_EQ(latency, 0); /* detected on the first impulse sample, outputs cut before the tick returns */
}

static void hundred_gait_profiles_all_cut_on_the_detection_tick(void)
{
    uint64_t worst = 0;
    for (int trial = 0; trial < 100; trial++) {
        gait_ctx_t g;
        g.onset_ms = UINT64_MAX;
        g.amp_g = 0.7f + 1.3f * (float)(trial % 10) / 9.0f;     /* 0.7 - 2.0 g */
        g.freq_hz = 1.2f + 1.2f * (float)(trial / 10) / 9.0f;   /* 1.2 - 2.4 Hz */
        g.phase = (float)(trial % 7) * 0.9f;
        g.impulse_only = false;

        support_boot_with_mode(VS_MODE_CIRCULATION);
        host_imu_set_source(gait_source, &g);
        CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
        host_run_ms(500 + (uint64_t)(trial % 5) * 400); /* vary where in the ramp walking starts */

        g.onset_ms = host_now_ms() + VS_TICK_MS;
        const uint64_t ms = run_to_walking_and_check_cut(g.onset_ms);
        /* Whatever the starting phase, the gait crosses 0.5 g of dynamic acceleration within one cycle. */
        const uint64_t cycle_ms = (uint64_t)(1000.0f / g.freq_hz);
        CHECK(ms <= cycle_ms);
        if (ms != UINT64_MAX && ms > worst) {
            worst = ms;
        }
        compliance_record_t end;
        CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &end));
        CHECK_EQ(end.v16, VS_STOP_WALKING);
    }
    printf("  worst gait-onset-to-cut: %llu ms\n", (unsigned long long)worst);
}

static void walking_during_ramp_down_also_cuts_immediately(void)
{
    gait_ctx_t g = {UINT64_MAX, 1.0f, 1.8f, 0.0f, false};
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_imu_set_source(gait_source, &g);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(1000);
    vs_cmd_select_mode(VS_MODE_STANDBY, "0000");
    host_run_ms(200);
    CHECK_EQ(vs_core()->session.state, SESSION_RAMP_DOWN);

    g.onset_ms = host_now_ms() + VS_TICK_MS;
    run_to_walking_and_check_cut(g.onset_ms);
    CHECK_EQ(vs_core()->session.state, SESSION_IDLE);
}

static void no_restart_until_standing_is_detected_again(void)
{
    trace_t t;
    const trace_segment_t segs[] = {{TRACE_STAND, 4000}, {TRACE_WALK, 3000}, {TRACE_STAND, 0}};
    support_boot_with_mode(VS_MODE_CIRCULATION);
    trace_init(&t, segs, 3);
    host_imu_set_source(trace_sample, &t);
    host_run_ms(4200);
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_WALKING);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);

    /* Walking ends at 7 s: 2 s to leave WALKING, then 3 s to confirm standing. */
    host_run_ms(7000 - 4200 + 4900);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    host_run_ms(600);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

int main(void)
{
    RUN_TEST(impulse_cuts_output_in_the_same_10_ms_tick);
    RUN_TEST(hundred_gait_profiles_all_cut_on_the_detection_tick);
    RUN_TEST(walking_during_ramp_down_also_cuts_immediately);
    RUN_TEST(no_restart_until_standing_is_detected_again);
    return th_finish();
}
