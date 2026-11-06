#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static void imu_timeout_forces_standby_with_stimulation_off(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(1000);
    CHECK(host_stim_delivered_ua() > 0);

    host_imu_set_fail(true);
    host_run_ms(VS_TICK_MS);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(!host_stim()->boost_on);
    CHECK(support_status().imu_fault);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_IMU_FAULT);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMU_FAULT), 1);

    host_run_ms(10000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(vs_core()->posture_state, VS_POSTURE_UNKNOWN);

    /* Recovery: 1 s of good reads, then a fresh 3 s standing detection. */
    host_imu_set_fail(false);
    host_run_ms(1000);
    CHECK(!support_status().imu_fault);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMU_RECOVERED), 1);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    host_run_ms(3100);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

static void sense_line_failure_halts_and_latches(void)
{
    const hal_sense_status_t failures[] = {HAL_SENSE_OPEN, HAL_SENSE_SHORT};
    for (int i = 0; i < 2; i++) {
        support_boot_with_mode(VS_MODE_CIRCULATION);
        host_sense_set_status(failures[i]);
        CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
        host_run_ms(3000);
        CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
        CHECK(support_status().stim_locked);
        compliance_record_t rec;
        CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_SENSE_FAILURE, &rec));
        CHECK_EQ(rec.v16, failures[i]);
        CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
        CHECK_EQ(rec.v16, VS_STOP_SENSE_FAILURE);
        host_run_ms(30000);
        CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    }
}

static void failed_sense_read_halts_and_latches(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_sense_set_fail(true);
    host_run_ms(3000);
    CHECK(support_status().stim_locked);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(!host_stim()->boost_on);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_SENSE_FAILURE, &rec));
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_SENSE_FAILURE);
    host_run_ms(30000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(support_status().stim_locked);
}

static void zero_measured_current_halts_and_latches(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_sense_set_current_ua(0);
    host_run_ms(3000);
    CHECK(support_status().stim_locked);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(!host_stim()->boost_on);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_SENSE_FAILURE, &rec));
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_SENSE_FAILURE);
    host_run_ms(30000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(support_status().stim_locked);
}

static void measured_current_below_5_ma_is_not_a_sense_fault(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_sense_set_current_ua(1000); /* valid, non-zero, below the 5 mA measurement gate */
    host_run_ms(3000);
    CHECK(!support_status().stim_locked);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_SENSE_FAILURE), 0);
}

static void rtc_failure_keeps_running_with_unknown_timestamps(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_run_ms(1000);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_MODE_TRANSITION, -1, &rec));
    CHECK(rec.timestamp != VS_TIMESTAMP_UNKNOWN);

    host_rtc_set_running(false);
    host_run_ms(5000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_RTC_INVALID), 1);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK(support_find_last_record(COMPLIANCE_SESSION_START, -1, &rec));
    CHECK_EQ(rec.timestamp, VS_TIMESTAMP_UNKNOWN);
}

static void implausible_rtc_date_is_treated_as_unknown(void)
{
    host_reset();
    host_rtc_set_epoch(1000u); /* 1970 */
    host_boot(NULL);
    host_run_ms(4000);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_RTC_INVALID), 1);
    CHECK(!vs_core()->rtc_valid);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_START, -1, &rec));
    CHECK_EQ(rec.timestamp, VS_TIMESTAMP_UNKNOWN);
}

int main(void)
{
    RUN_TEST(imu_timeout_forces_standby_with_stimulation_off);
    RUN_TEST(sense_line_failure_halts_and_latches);
    RUN_TEST(failed_sense_read_halts_and_latches);
    RUN_TEST(zero_measured_current_halts_and_latches);
    RUN_TEST(measured_current_below_5_ma_is_not_a_sense_fault);
    RUN_TEST(rtc_failure_keeps_running_with_unknown_timestamps);
    RUN_TEST(implausible_rtc_date_is_treated_as_unknown);
    return th_finish();
}
