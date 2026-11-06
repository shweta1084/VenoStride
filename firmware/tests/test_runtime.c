#include "test_harness.h"
#include "test_support.h"

/*
 * Battery-life claims from the brief, checked against the host power model
 * (HOST_* figures in host.h). These validate the firmware's duty behaviour
 * (boost kept on through off-windows and pauses, Standby keeps it off),
 * not the cell: real numbers need hardware.
 */

#define HOUR_MS (3600ull * 1000ull)

static bool session_ended(void)
{
    return !session_running(&vs_core()->session);
}

static bool running(void)
{
    return session_running(&vs_core()->session);
}

static void standby_lasts_about_85_h(void)
{
    host_reset();
    host_boot(NULL);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_STANDBY, "0000"), CMD_OK);

    uint64_t empty_ms = 0;
    while (!host_powered_off() && host_now_ms() < 100u * HOUR_MS) {
        host_run_ms(60000);
        if (empty_ms == 0 && host_battery_percentage() <= 0.0) {
            empty_ms = host_now_ms();
        }
    }
    const double empty_h = (double)empty_ms / (double)HOUR_MS;
    const double off_h = (double)host_now_ms() / (double)HOUR_MS;
    printf("  standby: 0%% at %.2f h, shutdown at %.2f h\n", empty_h, off_h);
    CHECK_NEAR(empty_h, 85.0, 85.0 * 0.03);
    CHECK(host_powered_off());
    CHECK(off_h > empty_h && off_h < empty_h + 2.0);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_SHUTDOWN), 1);
    CHECK_EQ(host_stim()->max_amplitude_ua, 0);
}

static void mode2_lasts_about_36_h(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    const uint64_t start = host_now_ms();

    while (!session_ended() && host_now_ms() - start < 40u * HOUR_MS) {
        host_run_ms(60000);
    }
    CHECK(support_run_until(session_ended, 60000) != UINT64_MAX);
    const double to_cutoff_h = (double)(host_now_ms() - start) / (double)HOUR_MS;
    const double projected_h = to_cutoff_h / 0.95;
    printf("  mode 2: stimulation cut at %.2f h (%.1f%% left), projected full drain %.2f h\n", to_cutoff_h,
           host_battery_percentage(), projected_h);
    CHECK_NEAR(projected_h, 36.0, 36.0 * 0.03);

    compliance_record_t end;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &end));
    CHECK_EQ(end.v16, VS_STOP_LOW_BATTERY);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_STIM_CUTOFF), 1);
    CHECK(!host_stim()->boost_on);
}

static void mode3_draw_matches_35_h_and_4_h_uses_48_mah(void)
{
    support_boot_with_mode(VS_MODE_METABOLIC);
    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    const uint64_t start = host_now_ms();
    const double before = host_battery_consumed_mah_in_mode(VS_MODE_METABOLIC);
    CHECK(support_run_until(session_ended, 5u * HOUR_MS) != UINT64_MAX);
    const double hours = (double)(host_now_ms() - start) / (double)HOUR_MS;
    const double used = host_battery_consumed_mah_in_mode(VS_MODE_METABOLIC) - before;
    const double continuous_h = HOST_BATTERY_CAPACITY_MAH / (used / hours);
    printf("  mode 3: %.3f h session used %.2f mAh, continuous equivalent %.2f h\n", hours, used, continuous_h);

    CHECK_NEAR(hours, 4.0, 0.01);
    CHECK_NEAR(used, 48.4, 0.5);
    CHECK_NEAR(continuous_h, 35.0, 35.0 * 0.03);
}

int main(void)
{
    RUN_TEST(standby_lasts_about_85_h);
    RUN_TEST(mode2_lasts_about_36_h);
    RUN_TEST(mode3_draw_matches_35_h_and_4_h_uses_48_mah);
    return th_finish();
}
