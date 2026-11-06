#include <string.h>

#include "test_harness.h"
#include "test_support.h"

#define RTC_BASE_S 1793750400u /* 2026-11-04 00:00:00Z, the host default */

static compliance_record_t g_buf[COMPLIANCE_SLOTS];

static size_t read_all(void)
{
    return vs_cmd_read_compliance(0, g_buf, COMPLIANCE_SLOTS);
}

static bool running(void)
{
    return session_running(&vs_core()->session);
}

static void ring_is_32_kb_of_16_byte_records(void)
{
    CHECK_EQ(COMPLIANCE_RECORD_SIZE * COMPLIANCE_SLOTS, 32u * 1024u);
    CHECK_EQ(VS_FLASH_LOG_SIZE, 32u * 1024u);
    CHECK_EQ(COMPLIANCE_SLOTS_PER_PAGE * VS_FLASH_LOG_PAGES, COMPLIANCE_SLOTS);
}

static void wrapping_drops_the_oldest_page_and_keeps_seq_contiguous(void)
{
    host_reset();
    host_boot(NULL);
    compliance_t *log = &vs_core()->log;
    const uint32_t first_seq = compliance_newest_seq(log) + 1u;
    const uint32_t appended = 3u * COMPLIANCE_SLOTS + 77u;
    for (uint32_t i = 0; i < appended; i++) {
        compliance_append(log, COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC, (uint16_t)(1000u + i % 500u), i);
    }

    const uint32_t count = compliance_count(log);
    CHECK(count <= COMPLIANCE_SLOTS);
    CHECK(count > COMPLIANCE_SLOTS - COMPLIANCE_SLOTS_PER_PAGE);
    CHECK_EQ(compliance_newest_seq(log), first_seq + appended - 1u);
    CHECK_EQ(compliance_oldest_seq(log), compliance_newest_seq(log) - count + 1u);

    const size_t n = read_all();
    CHECK_EQ(n, count);
    for (size_t i = 0; i < n; i++) {
        CHECK_EQ(g_buf[i].seq, compliance_oldest_seq(log) + (uint32_t)i);
        CHECK_EQ(g_buf[i].type, COMPLIANCE_IMPEDANCE);
        CHECK_EQ(g_buf[i].v32, g_buf[i].seq - first_seq);
    }
    CHECK_EQ(host_flash_violations(), 0);
}

static void reading_from_a_dropped_seq_starts_at_the_oldest(void)
{
    host_reset();
    host_boot(NULL);
    compliance_t *log = &vs_core()->log;
    for (uint32_t i = 0; i < 2u * COMPLIANCE_SLOTS; i++) {
        compliance_append(log, COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC, 1500, i);
    }
    compliance_record_t r[4];
    CHECK_EQ(vs_cmd_read_compliance(1, r, 4), 4);
    CHECK_EQ(r[0].seq, compliance_oldest_seq(log));
    CHECK_EQ(vs_cmd_read_compliance(compliance_newest_seq(log) + 1u, r, 4), 0);
    CHECK_EQ(vs_cmd_read_compliance(compliance_newest_seq(log), r, 4), 1);
}

static void a_rescan_recovers_the_ring_head(void)
{
    host_reset();
    host_boot(NULL);
    compliance_t *log = &vs_core()->log;
    for (uint32_t i = 0; i < COMPLIANCE_SLOTS + 300u; i++) {
        compliance_append(log, COMPLIANCE_CHARGING, CHARGING_STARTED, 50, i);
    }
    const compliance_t before = *log;

    compliance_t scanned;
    compliance_init(&scanned);
    CHECK_EQ(scanned.next_seq, before.next_seq);
    CHECK_EQ(scanned.write_slot, before.write_slot);
    CHECK_EQ(scanned.count, before.count);

    /* A reboot continues the sequence: the next record is the BOOT transition. */
    host_boot(NULL);
    compliance_record_t boot;
    CHECK(support_find_last_record(COMPLIANCE_MODE_TRANSITION, VS_MODE_STANDBY, &boot));
    CHECK_EQ(boot.seq, before.next_seq);
    CHECK_EQ(boot.v32, TRANSITION_BOOT);
    CHECK_EQ(host_flash_violations(), 0);
}

static void a_session_writes_all_six_record_types(void)
{
    static const trace_segment_t segs[] = {
        {TRACE_STAND, 70000},
        {TRACE_WALK, 0},
    };
    trace_t t;
    host_reset();
    host_boot(NULL);
    trace_init(&t, segs, 2);
    host_imu_set_source(trace_sample, &t);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_CIRCULATION, "0000"), CMD_OK);

    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    host_sense_set_impedance(3500); /* above the 3 kOhm warning, below the 5 kOhm halt */
    host_run_ms(75000);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
    host_battery_set_charging(true, 200.0);
    host_run_ms(2000);
    host_battery_set_charging(false, 0.0);
    host_run_ms(2000);

    CHECK(support_count_records(COMPLIANCE_SESSION_START, VS_MODE_CIRCULATION) == 1);
    CHECK(support_count_records(COMPLIANCE_SESSION_END, VS_MODE_CIRCULATION) == 1);
    CHECK(support_count_records(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC) >= 2);
    CHECK(support_count_records(COMPLIANCE_CHARGING, CHARGING_STARTED) == 1);
    CHECK(support_count_records(COMPLIANCE_CHARGING, CHARGING_ENDED) == 1);
    CHECK(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_WARNING) >= 1);
    CHECK(support_count_records(COMPLIANCE_MODE_TRANSITION, -1) >= 3); /* boot, select, start, end */

    compliance_record_t end;
    CHECK(support_find_last_record(COMPLIANCE_SESSION_END, -1, &end));
    CHECK_EQ(end.v16, VS_STOP_WALKING);

    compliance_record_t imp;
    CHECK(support_find_last_record(COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC, &imp));
    CHECK_NEAR(imp.v16, 3500, 50);
    CHECK(imp.v32 >= SAFETY_MIN_MEASURE_UA);

    const size_t n = read_all();
    for (size_t i = 0; i < n; i++) {
        CHECK(g_buf[i].type >= COMPLIANCE_SESSION_START && g_buf[i].type <= COMPLIANCE_MODE_TRANSITION);
        CHECK(g_buf[i].timestamp >= RTC_BASE_S && g_buf[i].timestamp <= RTC_BASE_S + 200u);
        if (i > 0) {
            CHECK_EQ(g_buf[i].seq, g_buf[i - 1].seq + 1u);
            CHECK(g_buf[i].timestamp >= g_buf[i - 1].timestamp);
        }
    }
    CHECK_EQ(host_flash_violations(), 0);
}

static void records_are_stamped_unknown_without_a_valid_rtc(void)
{
    host_reset();
    host_rtc_set_running(false);
    host_boot(NULL);
    host_run_ms(1000);
    compliance_record_t r;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_RTC_INVALID, &r));
    CHECK_EQ(r.timestamp, VS_TIMESTAMP_UNKNOWN);
}

int main(void)
{
    RUN_TEST(ring_is_32_kb_of_16_byte_records);
    RUN_TEST(wrapping_drops_the_oldest_page_and_keeps_seq_contiguous);
    RUN_TEST(reading_from_a_dropped_seq_starts_at_the_oldest);
    RUN_TEST(a_rescan_recovers_the_ring_head);
    RUN_TEST(a_session_writes_all_six_record_types);
    RUN_TEST(records_are_stamped_unknown_without_a_valid_rtc);
    return th_finish();
}
