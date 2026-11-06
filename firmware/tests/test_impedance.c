#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static void start_mode2_with_impedance(uint32_t ohm)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_sense_set_impedance(ohm);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
}

static void measured_every_30_s_while_stimulating(void)
{
    start_mode2_with_impedance(1500);
    host_run_ms(5u * 60u * 1000u);
    const size_t n = support_count_records(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC);
    CHECK(n >= 10 && n <= 11);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC, &rec));
    CHECK_NEAR(rec.v16, 1500, 1);
}

static void two_readings_above_3k_alert_but_keep_stimulating(void)
{
    start_mode2_with_impedance(4000);
    host_run_ms(5000);
    CHECK_EQ(support_status().impedance_status, VS_IMPEDANCE_NORMAL);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_WARNING), 0);

    host_run_ms(30000);
    CHECK_EQ(support_status().impedance_status, VS_IMPEDANCE_WARNING);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_WARNING), 1);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK(host_stim()->boost_on);

    /* One alert per episode; a good reading clears it. */
    host_run_ms(60000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_WARNING), 1);
    host_sense_set_impedance(1500);
    host_run_ms(30000);
    CHECK_EQ(support_status().impedance_status, VS_IMPEDANCE_NORMAL);
}

static void a_single_reading_above_5k_halts_until_re_enabled(void)
{
    start_mode2_with_impedance(1500);
    host_run_ms(5000);
    host_sense_set_impedance(6000);

    bool halted = false;
    for (int i = 0; i < 4000 && !halted; i++) {
        host_run_ms(VS_TICK_MS);
        halted = support_status().mode == VS_MODE_STANDBY;
    }
    CHECK(halted);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(!host_stim()->boost_on);
    const vs_device_status_t st = support_status();
    CHECK_EQ(st.impedance_status, VS_IMPEDANCE_CRITICAL);
    CHECK(st.stim_locked);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_CRITICAL, &rec));
    CHECK_EQ(rec.v16, 6000);
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_IMPEDANCE);

    /* Still standing, electrode reapplied, but no restart without an explicit re-enable. */
    host_sense_set_impedance(1500);
    host_run_ms(60000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);

    CHECK(vs_cmd_reenable_stimulation());
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_STIM_REENABLED), 1);
    host_run_ms(VS_TICK_MS);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK_EQ(support_status().impedance_status, VS_IMPEDANCE_NORMAL);
    CHECK(!vs_cmd_reenable_stimulation());
}

int main(void)
{
    RUN_TEST(measured_every_30_s_while_stimulating);
    RUN_TEST(two_readings_above_3k_alert_but_keep_stimulating);
    RUN_TEST(a_single_reading_above_5k_halts_until_re_enabled);
    return th_finish();
}
