#include "test_harness.h"
#include "test_support.h"

#define DAY0_UTC   1793750400u /* 2026-11-04T00:00:00Z */
#define HOUR_MS    (60ull * 60u * 1000u)
#define MIN_MS     (60ull * 1000u)

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static bool is_standby(void)
{
    return support_status().mode == VS_MODE_STANDBY;
}

/* Fresh device whose RTC reads the given UTC time, Mode 3 selected, standing. */
static void boot_mode3_at(uint32_t utc_s, const vs_config_t *cfg)
{
    host_reset();
    host_rtc_set_epoch(utc_s);
    host_boot(cfg);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "0000"), CMD_OK);
}

static void runs_7_hz_with_30_s_on_30_s_off(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
    host_run_ms(5000);
    CHECK_EQ(host_stim_delivered_ua(), 20000);
    CHECK_EQ(host_stim()->frequency_hz, 7);
    CHECK_EQ(host_stim()->pulse_width_us, 200);

    uint32_t on = 0;
    uint32_t total = 0;
    for (int i = 0; i < 12000; i++) {
        host_run_ms(VS_TICK_MS);
        const uint64_t since = host_now_ms() - vs_core()->session.session_start_ms;
        const bool window = since % 60000u < 30000u;
        CHECK(window ? host_stim_delivered_ua() == 20000 : host_stim_delivered_ua() == 0);
        on += host_stim_delivered_ua() > 0 ? 1u : 0u;
        total++;
    }
    CHECK_NEAR((double)on / total, 0.5, 0.01);
}

static void clinician_frequency_is_clamped_to_5_to_10_hz(void)
{
    const uint8_t requested[] = {9, 12, 3};
    const uint8_t expected[] = {9, 10, 5};
    for (int i = 0; i < 3; i++) {
        vs_config_t cfg;
        vs_config_defaults(&cfg);
        cfg.mode3_frequency_hz = requested[i];
        boot_mode3_at(DAY0_UTC + 8u * 3600u, &cfg);
        CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
        host_run_ms(1000);
        CHECK_EQ(host_stim()->frequency_hz, expected[i]);
    }
}

static void auto_pause_every_30_min_for_2_min(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    const uint64_t start = vs_core()->session.session_start_ms;

    host_run_ms(start + 30u * MIN_MS - host_now_ms() - 10u);
    CHECK_EQ(vs_core()->session.state, SESSION_ACTIVE);
    host_run_ms(10);
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSE_DOWN);
    host_run_ms(500);
    CHECK_NEAR(stim_envelope_ua(&vs_core()->stim), 10000, 300);
    host_run_ms(600);
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSED);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(host_stim()->boost_on);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);

    const uint64_t paused_at = vs_core()->session.state_start_ms;
    host_run_ms(paused_at + 2u * MIN_MS - 10u - host_now_ms());
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSED);
    host_run_ms(10);
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSE_UP);
    host_run_ms(1000);
    CHECK_NEAR(host_stim_delivered_ua(), 10000, 300); /* pattern restarts in its on phase */
    host_run_ms(1100);
    CHECK_EQ(vs_core()->session.state, SESSION_ACTIVE);
    CHECK_EQ(host_stim_delivered_ua(), 20000);
}

static void habituation_adds_1_ma_only_after_a_10_percent_rise(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    host_sense_set_impedance(1500);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(5000); /* baseline measured during ramp-up */
    CHECK_EQ(vs_core()->session.baseline_ohm, 1500);

    host_sense_set_impedance(1600); /* +6.7 %: no change */
    host_run_ms(30u * MIN_MS);
    CHECK_EQ(vs_core()->session.increment_ma, 0);
    CHECK_EQ(support_count_records(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_HABITUATION), 1);

    host_sense_set_impedance(1700); /* +13 % */
    host_run_ms(32u * MIN_MS);
    CHECK_EQ(vs_core()->session.increment_ma, 1);
    CHECK_EQ(support_status().current_ma, 21);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_HABITUATION_STEP, &rec));
    CHECK_EQ(rec.v32, 21000);
    host_run_ms(3u * MIN_MS);
    CHECK_EQ(host_stim()->max_amplitude_ua, 21000);
}

static void habituation_caps_at_8_ma_and_resets_next_day(void)
{
    /* A session that starts at 21:00 UTC crosses midnight, so the cap allows ~7 h of stimulation. */
    boot_mode3_at(DAY0_UTC + 21u * 3600u, NULL);
    host_sense_set_impedance(1500);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(5000);
    host_sense_set_impedance(2000);
    host_run_ms(7u * HOUR_MS);
    CHECK_EQ(vs_core()->session.increment_ma, VS_MODE3_MAX_INCREMENT_MA);
    CHECK_EQ(support_count_records(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_HABITUATION_STEP), 8);
    CHECK_EQ(host_stim()->max_amplitude_ua, 28000);

    /* Walk off, next day: the new session starts back at the user setting. */
    vs_cmd_select_mode(VS_MODE_STANDBY, "0000");
    CHECK(support_run_until(is_standby, 5000) != UINT64_MAX);
    host_rtc_set_epoch(DAY0_UTC + VS_UTC_DAY_S + 23u * 3600u);
    host_run_ms(10);
    host_rtc_set_epoch(DAY0_UTC + 2u * VS_UTC_DAY_S + 8u * 3600u);
    host_run_ms(10);
    vs_cmd_select_mode(VS_MODE_METABOLIC, "0000");
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    CHECK_EQ(vs_core()->session.increment_ma, 0);
    CHECK_EQ(support_status().current_ma, 20);
}

static void habituation_never_exceeds_30_ma(void)
{
    boot_mode3_at(DAY0_UTC + 21u * 3600u, NULL);
    vs_cmd_set_current(26);
    host_sense_set_impedance(1500);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(5000);
    host_sense_set_impedance(2000);
    host_run_ms(7u * HOUR_MS);
    CHECK_EQ(vs_core()->session.increment_ma, 4);
    CHECK_EQ(host_stim()->max_amplitude_ua, 30000);
}

static void daily_cap_stops_at_4_h_and_resets_at_midnight_utc(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(4u * HOUR_MS - 3000u);
    CHECK(support_status().mode == VS_MODE_METABOLIC);
    CHECK(support_status().mode3_remaining_s <= 3);
    host_run_ms(3010);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(!host_stim()->boost_on);
    const vs_device_status_t st = support_status();
    CHECK(st.daily_cap_reached);
    CHECK_EQ(st.mode3_remaining_s, 0);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_METABOLIC, &rec));
    CHECK_EQ(rec.v16, VS_STOP_DAILY_CAP);
    CHECK_EQ(rec.v32, 4u * 3600u);

    /* Still standing at 12:00 .. 23:59 UTC: no more Mode 3 today, and no command can lift the cap. */
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "0000"), CMD_OK);
    host_run_ms(12u * HOUR_MS - 10000u);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);

    host_run_ms(20000); /* crosses 00:00 UTC */
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
    CHECK(!support_status().daily_cap_reached);
}

static void mode2_still_runs_after_the_mode3_cap(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(4u * HOUR_MS + 1000u);
    CHECK(support_status().daily_cap_reached);
    vs_cmd_select_mode(VS_MODE_CIRCULATION, "0000");
    host_run_ms(VS_TICK_MS);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

static void cap_is_cumulative_across_sessions(void)
{
    trace_t t;
    const trace_segment_t segs[] = {{TRACE_STAND, (uint32_t)(2u * HOUR_MS)}, {TRACE_WALK, 60000u}, {TRACE_STAND, 0}};
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    trace_init(&t, segs, 3);
    host_imu_set_source(trace_sample, &t);
    host_run_ms(2u * HOUR_MS + 30000u);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    const uint32_t remaining = support_status().mode3_remaining_s;
    CHECK_NEAR(remaining, 2u * 3600u + 3u, 2);

    host_run_ms(30000u + 8000u + 2u * HOUR_MS - 5000u);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
    host_run_ms(10000u);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(support_status().daily_cap_reached);
}

static void a_power_cycle_does_not_reset_the_cap(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(3u * HOUR_MS);

    host_boot(NULL); /* power cycle: RAM lost, flash and RTC kept */
    CHECK_EQ(support_status().selected, VS_MODE_METABOLIC);
    const uint32_t remaining = support_status().mode3_remaining_s;
    CHECK(remaining <= 3600u);
    CHECK(remaining >= 3600u - 120u);

    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(remaining * 1000ull + 1000u);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(support_status().daily_cap_reached);
}

static void power_cycle_late_in_the_auto_pause_keeps_the_cap(void)
{
    boot_mode3_at(DAY0_UTC + 8u * 3600u, NULL);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    const uint64_t start = vs_core()->session.session_start_ms;
    host_run_ms(start + 30u * MIN_MS - host_now_ms());
    CHECK(vs_core()->session.state == SESSION_PAUSE_DOWN || vs_core()->session.state == SESSION_PAUSED);
    const uint64_t pause_by = host_now_ms() + 2000u;
    while (vs_core()->session.state != SESSION_PAUSED && host_now_ms() < pause_by) {
        host_run_ms(VS_TICK_MS);
    }
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSED);

    /* 110 s into the 2 min pause: past the old 60 s margin, with no impedance records written. */
    host_run_ms(110u * 1000u);
    CHECK_EQ(vs_core()->session.state, SESSION_PAUSED);
    const uint32_t used_at_cycle = vs_core()->session.mode3_used_ms;

    host_boot(NULL);
    CHECK_EQ(support_status().selected, VS_MODE_METABOLIC);
    const uint32_t remaining_s = support_status().mode3_remaining_s;
    const uint32_t recovered_ms = VS_MODE3_DAILY_CAP_MS - remaining_s * 1000u;
    CHECK(recovered_ms + 1000u >= used_at_cycle);
    CHECK(recovered_ms <= used_at_cycle + 2000u);
    CHECK(remaining_s * 1000ull <= (uint64_t)VS_MODE3_DAILY_CAP_MS - used_at_cycle + 1000ull);

    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms((uint64_t)remaining_s * 1000ull + 2000u);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(support_status().daily_cap_reached);
}

static void invalid_rtc_at_boot_blocks_mode3_until_24_h_of_uptime(void)
{
    host_reset();
    host_rtc_set_running(false);
    host_boot(NULL);
    vs_cmd_select_mode(VS_MODE_METABOLIC, "0000");
    host_run_ms(60000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(support_status().daily_cap_reached);

    host_run_ms(24u * HOUR_MS);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
}

int main(void)
{
    RUN_TEST(runs_7_hz_with_30_s_on_30_s_off);
    RUN_TEST(clinician_frequency_is_clamped_to_5_to_10_hz);
    RUN_TEST(auto_pause_every_30_min_for_2_min);
    RUN_TEST(habituation_adds_1_ma_only_after_a_10_percent_rise);
    RUN_TEST(habituation_caps_at_8_ma_and_resets_next_day);
    RUN_TEST(habituation_never_exceeds_30_ma);
    RUN_TEST(daily_cap_stops_at_4_h_and_resets_at_midnight_utc);
    RUN_TEST(mode2_still_runs_after_the_mode3_cap);
    RUN_TEST(cap_is_cumulative_across_sessions);
    RUN_TEST(a_power_cycle_does_not_reset_the_cap);
    RUN_TEST(power_cycle_late_in_the_auto_pause_keeps_the_cap);
    RUN_TEST(invalid_rtc_at_boot_blocks_mode3_until_24_h_of_uptime);
    return th_finish();
}
