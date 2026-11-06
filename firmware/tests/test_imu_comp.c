#include <string.h>

#include "imu_comp.h"
#include "test_harness.h"
#include "test_support.h"

static hal_imu_sample_t sample_at(float temp_c)
{
    hal_imu_sample_t s;
    memset(&s, 0, sizeof(s));
    s.accel_g[2] = 1.0f;
    s.gyro_dps[0] = 0.5f;
    s.temp_c = temp_c;
    return s;
}

/* Test-only coefficients; real ones come from hardware calibration. */
static imu_comp_coeffs_t test_coeffs(void)
{
    imu_comp_coeffs_t k;
    imu_comp_identity(&k);
    k.ref_temp_c = 25.0f;
    k.accel_bias_g_per_c[2] = 0.002f;
    k.gyro_bias_dps_per_c[0] = 0.05f;
    return k;
}

static void identity_coefficients_change_nothing_across_a_wide_span(void)
{
    imu_comp_t ic;
    imu_comp_init(&ic, NULL);
    for (float t = 20.0f; t <= 40.0f; t += 0.5f) {
        hal_imu_sample_t s = sample_at(t);
        imu_comp_apply(&ic, &s);
        CHECK_NEAR(s.accel_g[2], 1.0f, 1e-6);
        CHECK_NEAR(s.gyro_dps[0], 0.5f, 1e-6);
    }
    CHECK(imu_comp_thermal_active(&ic));
}

static void thermal_correction_waits_for_a_span_above_10_c(void)
{
    const imu_comp_coeffs_t k = test_coeffs();
    imu_comp_t ic;
    imu_comp_init(&ic, &k);

    hal_imu_sample_t s = sample_at(25.0f);
    imu_comp_apply(&ic, &s);
    s = sample_at(35.0f); /* span exactly 10 C: not yet */
    imu_comp_apply(&ic, &s);
    CHECK(!imu_comp_thermal_active(&ic));
    CHECK_NEAR(s.accel_g[2], 1.0f, 1e-6);

    s = sample_at(36.0f);
    imu_comp_apply(&ic, &s);
    CHECK(imu_comp_thermal_active(&ic));
    CHECK_NEAR(s.accel_g[2], 1.0f - 0.002f * 11.0f, 1e-5);
    CHECK_NEAR(s.gyro_dps[0], 0.5f - 0.05f * 11.0f, 1e-5);
}

static void static_offset_applies_without_a_span(void)
{
    imu_comp_coeffs_t k;
    imu_comp_identity(&k);
    k.accel_offset_g[0] = 0.01f;
    imu_comp_t ic;
    imu_comp_init(&ic, &k);
    hal_imu_sample_t s = sample_at(25.0f);
    imu_comp_apply(&ic, &s);
    CHECK_NEAR(s.accel_g[0], -0.01f, 1e-6);
}

static void configured_coefficients_reach_the_core(void)
{
    vs_config_t cfg;
    vs_config_defaults(&cfg);
    cfg.imu_comp = test_coeffs();
    host_reset();
    host_boot(&cfg);
    CHECK_NEAR(vs_core()->imu_comp.coeffs.accel_bias_g_per_c[2], 0.002f, 1e-9);

    /* Defaults ship identity: no invented calibration. */
    vs_config_defaults(&cfg);
    for (int i = 0; i < 3; i++) {
        CHECK(cfg.imu_comp.accel_offset_g[i] == 0.0f);
        CHECK(cfg.imu_comp.accel_bias_g_per_c[i] == 0.0f);
        CHECK(cfg.imu_comp.gyro_bias_dps_per_c[i] == 0.0f);
    }
}

static void temperature_ramp_does_not_disturb_standing_detection(void)
{
    trace_t t;
    trace_segment_t seg;
    host_reset();
    host_boot(NULL);
    support_imu_forever(&t, &seg, TRACE_STAND);
    t.temp_c = 20.0f;
    t.temp_slope_c_per_s = 1.0f;
    host_run_ms(3500);
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_STANDING);
    host_run_ms(10000);
    CHECK(imu_comp_thermal_active(&vs_core()->imu_comp));
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_STANDING);
}

int main(void)
{
    RUN_TEST(identity_coefficients_change_nothing_across_a_wide_span);
    RUN_TEST(thermal_correction_waits_for_a_span_above_10_c);
    RUN_TEST(static_offset_applies_without_a_span);
    RUN_TEST(configured_coefficients_reach_the_core);
    RUN_TEST(temperature_ramp_does_not_disturb_standing_detection);
    return th_finish();
}
