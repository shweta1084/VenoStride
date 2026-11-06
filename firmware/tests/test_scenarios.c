#include <stdio.h>

#include "host_trace.h"
#include "test_harness.h"
#include "test_support.h"

static bool load(host_trace_t *trace, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", VS_SCENARIO_DIR, name);
    const bool ok = host_trace_load(trace, path);
    if (!ok) {
        printf("  cannot load %s\n", path);
    }
    return ok;
}

static void stand_csv_starts_mode2_after_three_seconds(void)
{
    host_trace_t trace;
    host_reset();
    host_boot(NULL);
    CHECK(load(&trace, "stand.csv"));
    if (trace.count == 0) {
        return;
    }
    host_trace_install(&trace, false);
    host_run_ms(2900);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    host_run_ms(200);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    host_trace_free(&trace);
}

static void stand_walk_stand_csv_cuts_on_walking_and_resumes(void)
{
    host_trace_t trace;
    host_reset();
    host_boot(NULL);
    CHECK(load(&trace, "stand_walk_stand.csv"));
    if (trace.count == 0) {
        return;
    }
    host_trace_install(&trace, false);
    host_run_ms(5900);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK(host_stim_delivered_ua() > 0);

    host_run_ms(300);
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_WALKING);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    compliance_record_t end;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &end));
    CHECK_EQ(end.v16, VS_STOP_WALKING);

    /* Walking ends at 11 s; 2 s to leave WALKING plus 3 s of standing. */
    host_run_ms(11000);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    host_trace_free(&trace);
}

static void sit_csv_never_stimulates(void)
{
    host_trace_t trace;
    host_reset();
    host_boot(NULL);
    CHECK(load(&trace, "sit.csv"));
    if (trace.count == 0) {
        return;
    }
    host_trace_install(&trace, false);
    uint32_t max_output = 0;
    for (int i = 0; i < 6 * 60 * 10; i++) {
        host_run_ms(100);
        if (host_stim_delivered_ua() > max_output) {
            max_output = host_stim_delivered_ua();
        }
    }
    CHECK_EQ(max_output, 0);
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_SITTING);
    host_trace_free(&trace);
}

static void noisy_stand_csv_still_starts_therapy(void)
{
    host_trace_t trace;
    host_reset();
    host_boot(NULL);
    CHECK(load(&trace, "noisy_stand.csv"));
    if (trace.count == 0) {
        return;
    }
    host_trace_install(&trace, false);
    host_run_ms(6000);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    host_trace_free(&trace);
}

static void temp_span_csv_activates_compensation_and_keeps_standing(void)
{
    host_trace_t trace;
    host_reset();
    host_boot(NULL);
    CHECK(load(&trace, "temp_span.csv"));
    if (trace.count == 0) {
        return;
    }
    host_trace_install(&trace, false);
    host_run_ms(host_trace_duration_ms(&trace));
    CHECK(imu_comp_thermal_active(&vs_core()->imu_comp));
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_STANDING);
    host_trace_free(&trace);
}

int main(void)
{
    RUN_TEST(stand_csv_starts_mode2_after_three_seconds);
    RUN_TEST(stand_walk_stand_csv_cuts_on_walking_and_resumes);
    RUN_TEST(sit_csv_never_stimulates);
    RUN_TEST(noisy_stand_csv_still_starts_therapy);
    RUN_TEST(temp_span_csv_activates_compensation_and_keeps_standing);
    return th_finish();
}
