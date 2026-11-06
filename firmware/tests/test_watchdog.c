#include "test_harness.h"
#include "test_support.h"

static bool delivering(void)
{
    return host_stim_delivered_ua() > 0;
}

static void a_hang_longer_than_2_s_resets_and_disables_output(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(delivering, 5000) != UINT64_MAX);
    const size_t records_before = compliance_count(&vs_core()->log);

    host_hang_ms(2500);
    CHECK_EQ(host_wdt_resets(), 1);
    CHECK_EQ(host_stim_delivered_ua(), 0);
    CHECK(!host_stim()->boost_on);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_WATCHDOG_RESET), 1);
    CHECK(compliance_count(&vs_core()->log) > records_before);

    /* After the reset the device is back to normal operation. */
    host_run_ms(3100);
    CHECK(support_status().mode == VS_MODE_CIRCULATION);
    CHECK_EQ(host_wdt_resets(), 1);
}

static void a_short_stall_does_not_reset(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_run_ms(5000);
    host_hang_ms(900);
    host_run_ms(5000);
    host_hang_ms(900);
    CHECK_EQ(host_wdt_resets(), 0);
}

static void clinician_selection_survives_a_reset(void)
{
    support_boot_with_mode(VS_MODE_METABOLIC);
    host_run_ms(4000);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
    host_hang_ms(2500);
    CHECK_EQ(host_wdt_resets(), 1);
    CHECK_EQ(support_status().selected, VS_MODE_METABOLIC);
    host_run_ms(3100);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
}

static void a_latched_halt_survives_a_reset(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    host_sense_set_impedance(6000);
    host_run_ms(6000);
    CHECK(support_status().stim_locked);

    host_sense_set_impedance(1500);
    host_hang_ms(2500);
    CHECK_EQ(host_wdt_resets(), 1);
    CHECK(support_status().stim_locked);
    CHECK_EQ(support_status().impedance_status, VS_IMPEDANCE_CRITICAL);
    host_run_ms(10000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);

    CHECK(vs_cmd_reenable_stimulation());
    host_hang_ms(2500);
    CHECK_EQ(host_wdt_resets(), 2);
    CHECK(!support_status().stim_locked);
    host_run_ms(3100);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
}

int main(void)
{
    RUN_TEST(a_hang_longer_than_2_s_resets_and_disables_output);
    RUN_TEST(a_short_stall_does_not_reset);
    RUN_TEST(clinician_selection_survives_a_reset);
    RUN_TEST(a_latched_halt_survives_a_reset);
    return th_finish();
}
