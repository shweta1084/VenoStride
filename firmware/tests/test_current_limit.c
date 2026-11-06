#include "stim.h"
#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static bool delivering(void)
{
    return host_stim_delivered_ua() > 0;
}

static void a_35_ma_request_is_delivered_as_30_ma(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK_EQ(vs_cmd_set_current(35), 30);
    CHECK_EQ(support_status().current_ma, 30);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(60000 + 1000); /* third burst: the entry ramp takes 5 s of ON time */
    CHECK_EQ(host_stim_delivered_ua(), 30000);
    host_run_ms(60000);
    CHECK_EQ(host_stim()->max_amplitude_ua, 30000);
}

static void requests_below_15_ma_are_raised_to_15(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK_EQ(vs_cmd_set_current(5), 15);
    CHECK_EQ(vs_cmd_set_current(0), 15);
    CHECK_EQ(vs_cmd_set_current(255), 30);
}

static void stim_engine_clips_any_target_above_30_ma(void)
{
    host_reset();
    host_boot(NULL);
    stim_t st;
    stim_init(&st);
    const stim_pattern_t pattern = {5, 200, 1000, 0};
    stim_begin(&st, &pattern, host_now_ms());
    stim_ramp_to(&st, 45000, 0, host_now_ms());
    stim_tick(&st, host_now_ms());
    CHECK_EQ(stim_output_ua(&st), 30000);
    CHECK_EQ(host_stim()->amplitude_ua, 30000);
    stim_cutoff(&st);
}

static void current_change_reaches_the_dac_within_50_ms(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(60000);
    CHECK(support_run_until(delivering, 5000) != UINT64_MAX);
    host_run_ms(500);
    CHECK_EQ(host_stim_delivered_ua(), 20000);

    vs_cmd_set_current(25);
    host_run_ms(VS_TICK_MS);
    CHECK_EQ(host_stim_delivered_ua(), 25000);
}

int main(void)
{
    RUN_TEST(a_35_ma_request_is_delivered_as_30_ma);
    RUN_TEST(requests_below_15_ma_are_raised_to_15);
    RUN_TEST(stim_engine_clips_any_target_above_30_ma);
    RUN_TEST(current_change_reaches_the_dac_within_50_ms);
    return th_finish();
}
