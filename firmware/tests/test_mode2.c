#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

/* Boots standing in Mode 2 and returns once the session has just started (t = 3 s). */
static void start_mode2(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
}

static void ramps_to_20_ma_over_5_s_and_pulses_only_in_the_3_s_window(void)
{
    start_mode2();
    const stim_t *st = &vs_core()->stim;

    host_run_ms(2500);
    CHECK_NEAR(stim_envelope_ua(st), 10000, 200);
    CHECK(host_stim_delivered_ua() > 0);
    CHECK_EQ(host_stim()->frequency_hz, 5);
    CHECK_EQ(host_stim()->pulse_width_us, 200);

    host_run_ms(2500); /* 5.0 s in, inside the 27 s off period: only 3 s of ON time so far */
    CHECK_EQ(stim_envelope_ua(st), 12000);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(host_stim()->boost_on);

    host_run_ms(55000); /* the ramp completed 2 s into the second burst; start of the third */

    /* Sample every tick over two full 30 s cycles and check the duty pattern. */
    uint32_t on_ticks = 0;
    uint32_t total_ticks = 0;
    for (int i = 0; i < 6000; i++) {
        host_run_ms(VS_TICK_MS);
        const uint64_t since_start = host_now_ms() - vs_core()->session.session_start_ms;
        const bool window = since_start % 30000u < 3000u;
        const uint32_t out = host_stim_delivered_ua();
        CHECK(window ? out == 20000 : out == 0);
        on_ticks += out > 0 ? 1u : 0u;
        total_ticks++;
    }
    CHECK_NEAR((double)on_ticks / total_ticks, 0.10, 0.002);
    CHECK_EQ(host_stim()->max_amplitude_ua, 20000);
}

static void selecting_standby_ramps_down_over_2_s_then_disables_boost(void)
{
    start_mode2();
    host_run_ms(61000); /* inside the third on-window at 20 mA */
    CHECK_EQ(host_stim_delivered_ua(), 20000);

    CHECK_EQ(vs_cmd_select_mode(VS_MODE_STANDBY, "0000"), CMD_OK);
    host_run_ms(1000);
    CHECK_NEAR(host_stim_delivered_ua(), 10000, 200);
    CHECK(host_stim()->boost_on);
    host_run_ms(1020);
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);

    compliance_record_t end;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &end));
    CHECK_EQ(end.v16, VS_STOP_USER);
    CHECK_EQ(end.v32, 63);

    /* Standby selection keeps therapy off while standing. */
    host_run_ms(10000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
}

/* Cumulative ON time of the 3 s / 27 s pattern up to (not including) the tick at rel_ms. */
static uint32_t on_time_before(uint64_t rel_ms)
{
    const uint64_t phase = rel_ms % 30000u;
    return (uint32_t)((rel_ms / 30000u) * 3000u + (phase < 3000u ? phase : 3000u));
}

static void entry_ramp_advances_only_during_on_periods(void)
{
    start_mode2();
    const stim_t *st = &vs_core()->stim;
    const uint64_t start = vs_core()->session.session_start_ms;
    CHECK_EQ(host_now_ms(), start);

    const int failures_before = th_failures;
    uint32_t off_envelope = 0;
    for (uint64_t rel = 0; rel < 3u * 30000u; rel += VS_TICK_MS) {
        if (rel > 0) {
            host_run_ms(VS_TICK_MS);
        }
        const bool window = rel % 30000u < 3000u;
        const uint32_t on = on_time_before(rel);
        const uint32_t expected = (on >= VS_RAMP_UP_MS ? VS_RAMP_UP_MS : on) * 20000u / VS_RAMP_UP_MS;
        CHECK_EQ(stim_envelope_ua(st), expected);
        CHECK_EQ(host_stim_delivered_ua(), window ? expected : 0u);
        if (!window) {
            if (rel % 30000u == 3000u) {
                off_envelope = stim_envelope_ua(st);
            }
            CHECK_EQ(stim_envelope_ua(st), off_envelope); /* held through the whole off period */
        }
        if (th_failures > failures_before) {
            printf("  first mismatch at %llu ms into the session\n", (unsigned long long)rel);
            return;
        }
    }

    /* Spot values: end of burst 1, start and end of the ramp in burst 2, burst 3. */
    CHECK_EQ(on_time_before(2990), 2990);
    CHECK_EQ(on_time_before(30000), 3000);
    CHECK_EQ(on_time_before(32000), 5000);
    CHECK_EQ(host_stim()->max_amplitude_ua, 20000);
}

static void second_burst_resumes_where_the_first_stopped(void)
{
    start_mode2();
    const uint64_t start = vs_core()->session.session_start_ms;

    host_run_ms(start + 2990u - host_now_ms());
    CHECK_EQ(host_stim_delivered_ua(), 11960u); /* last tick of burst 1: 2.99 s of 5 s */
    host_run_ms(10);
    CHECK_EQ(host_stim_delivered_ua(), 0u);
    CHECK_EQ(stim_envelope_ua(&vs_core()->stim), 12000u);
    CHECK_EQ(vs_core()->session.state, SESSION_RAMP_UP);

    host_run_ms(start + 30000u - host_now_ms());
    CHECK_EQ(host_stim_delivered_ua(), 12000u); /* neither restarted from 0 nor jumped to 20 mA */
    host_run_ms(1000);
    CHECK_EQ(host_stim_delivered_ua(), 16000u);
    host_run_ms(990);
    CHECK_EQ(host_stim_delivered_ua(), 19960u);
    host_run_ms(10);
    CHECK_EQ(host_stim_delivered_ua(), 20000u); /* 5 s of cumulative ON time */
    host_run_ms(10);
    CHECK_EQ(vs_core()->session.state, SESSION_ACTIVE);
    host_run_ms(980); /* last tick of burst 2 */
    CHECK_EQ(host_stim_delivered_ua(), 20000u);
}

static void current_change_during_the_off_period_keeps_ramp_progress(void)
{
    start_mode2();
    const uint64_t start = vs_core()->session.session_start_ms;
    host_run_ms(10000); /* off period, 3 s of ON time done, envelope 12 mA */
    CHECK_EQ(vs_cmd_set_current(24), 24);
    host_run_ms(10);
    CHECK_EQ(stim_envelope_ua(&vs_core()->stim), 12000u);
    CHECK_EQ(host_stim_delivered_ua(), 0u);

    host_run_ms(start + 30000u - host_now_ms());
    CHECK_EQ(host_stim_delivered_ua(), 12000u);
    host_run_ms(1000); /* halfway through the remaining 2 s of ON time */
    CHECK_EQ(host_stim_delivered_ua(), 18000u);
    host_run_ms(1000);
    CHECK_EQ(host_stim_delivered_ua(), 24000u);
    host_run_ms(990);
    CHECK_EQ(host_stim()->max_amplitude_ua, 24000u);
}

static void exit_during_a_held_ramp_still_ramps_down_over_2_s(void)
{
    start_mode2();
    host_run_ms(10000); /* ramp held at 12 mA in the off period */
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_STANDBY, "0000"), CMD_OK);
    host_run_ms(10);
    CHECK_EQ(vs_core()->session.state, SESSION_RAMP_DOWN);
    host_run_ms(1000);
    CHECK_NEAR(stim_envelope_ua(&vs_core()->stim), 6000, 100);
    CHECK(host_stim()->boost_on);
    host_run_ms(1020);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(host_stim_delivered_ua(), 0u);
}

static void mode2_has_no_daily_cap(void)
{
    start_mode2();
    host_run_ms(5ull * 60u * 60u * 1000u);
    const vs_device_status_t st = support_status();
    CHECK_EQ(st.mode, VS_MODE_CIRCULATION);
    CHECK(!st.daily_cap_reached);
    CHECK_EQ(st.mode3_remaining_s, 4u * 60u * 60u);
}

static void session_start_is_logged_with_transition(void)
{
    start_mode2();
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_START, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, 20);
    CHECK(support_find_last_record(COMPLIANCE_MODE_TRANSITION, VS_MODE_STANDBY, &rec));
    CHECK_EQ(rec.v16, VS_MODE_CIRCULATION);
    CHECK_EQ(rec.v32, TRANSITION_STANDING_DETECTED);
}

int main(void)
{
    RUN_TEST(ramps_to_20_ma_over_5_s_and_pulses_only_in_the_3_s_window);
    RUN_TEST(selecting_standby_ramps_down_over_2_s_then_disables_boost);
    RUN_TEST(entry_ramp_advances_only_during_on_periods);
    RUN_TEST(second_burst_resumes_where_the_first_stopped);
    RUN_TEST(current_change_during_the_off_period_keeps_ramp_progress);
    RUN_TEST(exit_during_a_held_ramp_still_ramps_down_over_2_s);
    RUN_TEST(mode2_has_no_daily_cap);
    RUN_TEST(session_start_is_logged_with_transition);
    return th_finish();
}
