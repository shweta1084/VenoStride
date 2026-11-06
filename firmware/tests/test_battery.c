#include "battery.h"
#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static void curve_maps_voltage_to_whole_percentages(void)
{
    CHECK_EQ(battery_percentage_from_mv(4200), 100);
    CHECK_EQ(battery_percentage_from_mv(4300), 100);
    CHECK_EQ(battery_percentage_from_mv(3000), 0);
    CHECK_EQ(battery_percentage_from_mv(2900), 0);
    CHECK_EQ(battery_percentage_from_mv(3790), 50);
    for (int pct = 0; pct <= 100; pct++) {
        CHECK_EQ(battery_percentage_from_mv(battery_mv_from_percentage((float)pct)), pct);
    }
}

static void battery_characteristic_reads_an_integer_percentage(void)
{
    host_reset();
    host_boot(NULL);
    host_battery_set_percentage(57.4);
    host_run_ms(1100);
    const uint8_t pct = vs_cmd_battery_percentage();
    CHECK(pct >= 56 && pct <= 57);
    CHECK(pct <= 100);
    CHECK_EQ(support_status().battery_percentage, pct);
    CHECK_EQ(VS_UUID_BATTERY, 0x2A19);
}

static void alerts_fire_once_at_20_and_5_percent(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_battery_set_percentage(21.5);
    host_run_ms(2000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_20), 0);
    host_battery_set_percentage(19.5);
    host_run_ms(2000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_20), 1);
    host_run_ms(60000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_20), 1);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_5), 0);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

static void stimulation_stops_below_5_percent_and_resumes_above_7(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_battery_set_percentage(4.5);
    host_run_ms(1100);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_5), 1);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_STIM_CUTOFF), 1);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_LOW_BATTERY);

    host_battery_set_percentage(6.0);
    host_run_ms(5000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    host_battery_set_percentage(8.0);
    host_run_ms(1100);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

static void below_3v_keeps_sensing_until_2v8_then_shuts_down(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_battery_set_percentage(-1.0); /* into the reserve below 3.0 V */
    host_run_ms(1100);
    CHECK(vs_core()->battery.mv < 3000);
    CHECK(vs_core()->battery.mv >= 2800);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(!host_powered_off());

    host_battery_set_percentage(-1.9);
    host_run_ms(1100);
    CHECK(host_powered_off());
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_SHUTDOWN), 1);
}

static void charging_edges_are_logged(void)
{
    host_reset();
    host_boot(NULL);
    host_battery_set_percentage(40.0);
    host_run_ms(2000);
    host_battery_set_charging(true, 200.0);
    host_run_ms(2000);
    CHECK_EQ(support_count_records(COMPLIANCE_CHARGING, CHARGING_STARTED), 1);
    host_run_ms(60u * 60u * 1000u);
    host_battery_set_charging(false, 0.0);
    host_run_ms(2000);
    CHECK_EQ(support_count_records(COMPLIANCE_CHARGING, CHARGING_ENDED), 1);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_CHARGING, CHARGING_ENDED, &rec));
    CHECK(rec.v16 > 40);
    CHECK(rec.v32 > 3790);
}

int main(void)
{
    RUN_TEST(curve_maps_voltage_to_whole_percentages);
    RUN_TEST(battery_characteristic_reads_an_integer_percentage);
    RUN_TEST(alerts_fire_once_at_20_and_5_percent);
    RUN_TEST(stimulation_stops_below_5_percent_and_resumes_above_7);
    RUN_TEST(below_3v_keeps_sensing_until_2v8_then_shuts_down);
    RUN_TEST(charging_edges_are_logged);
    return th_finish();
}
