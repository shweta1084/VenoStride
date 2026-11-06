#include "test_harness.h"
#include "test_support.h"

static bool is_running(void)
{
    return support_status().mode != VS_MODE_STANDBY;
}

static void imbalance_above_half_microcoulomb_halts(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_sense_set_imbalance_nc(600);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(3000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK(!host_stim()->boost_on);
    CHECK(support_status().stim_locked);
    compliance_record_t rec;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_CHARGE_IMBALANCE, &rec));
    CHECK_EQ(rec.v32, 600);
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION, &rec));
    CHECK_EQ(rec.v16, VS_STOP_CHARGE_BALANCE);
}

static void imbalance_within_tolerance_keeps_running(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_sense_set_imbalance_nc(-400);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(5u * 60u * 1000u);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_CHARGE_IMBALANCE), 0);
}

static void checked_every_60_s(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(is_running, 5000) != UINT64_MAX);
    host_run_ms(5000); /* first check done during the first burst */
    host_sense_set_imbalance_nc(-800);
    host_run_ms(40000); /* the 30 s burst passes without a charge check */
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    host_run_ms(25000); /* the 60 s burst runs the next check */
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_CHARGE_IMBALANCE), 1);
}

int main(void)
{
    RUN_TEST(imbalance_above_half_microcoulomb_halts);
    RUN_TEST(imbalance_within_tolerance_keeps_running);
    RUN_TEST(checked_every_60_s);
    return th_finish();
}
