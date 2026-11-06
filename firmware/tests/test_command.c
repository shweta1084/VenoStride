#include <string.h>

#include "status_codec.h"
#include "test_harness.h"
#include "test_support.h"

static bool running(void)
{
    return session_running(&vs_core()->session);
}

static void boot_with_pin(const char *pin)
{
    vs_config_t cfg;
    vs_config_defaults(&cfg);
    memcpy(cfg.clinician_pin, pin, CMD_PIN_LENGTH);
    host_reset();
    host_boot(&cfg);
}

static void uuids_match_the_brief(void)
{
    CHECK_EQ(VS_UUID_SERVICE, 0x181A);
    CHECK_EQ(VS_UUID_DEVICE_STATUS, 0x2A58);
    CHECK_EQ(VS_UUID_MODE_SELECT, 0x0001);
    CHECK_EQ(VS_UUID_CURRENT_ADJUST, 0x0002);
    CHECK_EQ(VS_UUID_COMPLIANCE_DATA, 0x0003);
    CHECK_EQ(VS_UUID_BATTERY, 0x2A19);
    CHECK(strcmp(VS_BLE_DEVICE_NAME, "VenoStride") == 0);
}

static void correct_pin_selects_the_mode_and_is_logged(void)
{
    boot_with_pin("4821");
    CHECK_EQ(support_status().selected, VS_MODE_CIRCULATION);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4821"), CMD_OK);
    CHECK_EQ(support_status().selected, VS_MODE_METABOLIC);

    compliance_record_t r;
    CHECK(support_find_last_record(COMPLIANCE_MODE_TRANSITION, VS_MODE_CIRCULATION, &r));
    CHECK_EQ(r.v16, VS_MODE_METABOLIC);
    CHECK_EQ(r.v32, TRANSITION_CLINICIAN_SELECT);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED), 0);
}

static void wrong_or_malformed_pins_are_rejected(void)
{
    boot_with_pin("4821");
    const char two_digit[CMD_PIN_LENGTH] = {'4', '8', 0, 0};
    const char with_letter[CMD_PIN_LENGTH] = {'4', '8', '2', 'A'};
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4822"), CMD_BAD_PIN);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, two_digit), CMD_BAD_PIN);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, with_letter), CMD_BAD_PIN);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, NULL), CMD_BAD_PIN);
    CHECK_EQ(support_status().selected, VS_MODE_CIRCULATION);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED), 4);

    compliance_record_t r;
    CHECK(support_find_last_record(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED, &r));
    CHECK_EQ(r.v16, 4); /* consecutive failures */
}

static void a_pin_that_is_not_four_digits_never_matches(void)
{
    /* A malformed configured PIN must not let a matching malformed attempt through. */
    const char weak[CMD_PIN_LENGTH] = {'1', '2', 0, 0};
    boot_with_pin(weak);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, weak), CMD_BAD_PIN);
}

static void a_2_bit_password_is_not_authentication(void)
{
    /* Section 5.1's one-byte write carries a 2-bit password; none of its four values may authenticate. */
    boot_with_pin("0000");
    for (char v = 0; v < 4; v++) {
        const char raw[CMD_PIN_LENGTH] = {v, 0, 0, 0};
        const char digit[CMD_PIN_LENGTH] = {(char)('0' + v), 0, 0, 0};
        CHECK(vs_cmd_select_mode(VS_MODE_METABOLIC, raw) != CMD_OK);
        CHECK(vs_cmd_select_mode(VS_MODE_METABOLIC, digit) != CMD_OK);
    }
    CHECK_EQ(support_status().selected, VS_MODE_CIRCULATION);
}

static void five_failures_lock_out_for_15_minutes(void)
{
    boot_with_pin("4821");
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "0000"), CMD_BAD_PIN);
    }
    CHECK(!support_status().auth_locked);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "0000"), CMD_BAD_PIN);
    CHECK(support_status().auth_locked);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_LOCKOUT), 1);

    /* Even the right PIN is refused while locked, and the attempt is logged. */
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4821"), CMD_LOCKED_OUT);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED), 6);
    CHECK_EQ(support_status().selected, VS_MODE_CIRCULATION);

    host_run_ms(CMD_PIN_LOCKOUT_MS - 1000u);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4821"), CMD_LOCKED_OUT);
    host_run_ms(1000);
    CHECK(!support_status().auth_locked);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4821"), CMD_OK);
    CHECK_EQ(support_status().selected, VS_MODE_METABOLIC);
}

static void a_correct_pin_resets_the_failure_count(void)
{
    boot_with_pin("4821");
    for (int i = 0; i < 4; i++) {
        vs_cmd_select_mode(VS_MODE_METABOLIC, "1111");
    }
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "4821"), CMD_OK);
    for (int i = 0; i < 4; i++) {
        vs_cmd_select_mode(VS_MODE_CIRCULATION, "1111");
    }
    CHECK(!support_status().auth_locked);
    CHECK_EQ(support_count_records(COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_LOCKOUT), 0);
}

static void an_unknown_mode_is_invalid(void)
{
    boot_with_pin("4821");
    const size_t transitions = support_count_records(COMPLIANCE_MODE_TRANSITION, -1);
    CHECK_EQ(vs_cmd_select_mode((vs_mode_t)3, "4821"), CMD_INVALID);
    CHECK_EQ(support_count_records(COMPLIANCE_MODE_TRANSITION, -1), transitions);
    CHECK_EQ(support_status().selected, VS_MODE_CIRCULATION);
}

static void current_adjust_is_clamped_to_15_to_30_ma(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK_EQ(vs_cmd_set_current(5), VS_CURRENT_MIN_MA);
    CHECK_EQ(vs_cmd_set_current(0), VS_CURRENT_MIN_MA);
    CHECK_EQ(vs_cmd_set_current(45), VS_CURRENT_MAX_MA);
    CHECK_EQ(vs_cmd_set_current(255), VS_CURRENT_MAX_MA);
    CHECK_EQ(vs_cmd_set_current(24), 24);
    CHECK_EQ(support_status().current_ma, 24);

    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    host_run_ms(61000); /* inside the third 3 s burst, after the entry ramp */
    CHECK_EQ(host_stim_delivered_ua(), 24000u);
    CHECK_EQ(vs_cmd_set_current(16), 16);
    host_run_ms(10);
    CHECK_EQ(host_stim_delivered_ua(), 16000u);
}

static void status_reports_live_state(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    vs_device_status_t st = support_status();
    CHECK_EQ(st.mode, VS_MODE_STANDBY);
    CHECK_EQ(st.selected, VS_MODE_CIRCULATION);
    CHECK_EQ(st.current_ma, VS_CURRENT_DEFAULT_MA);
    CHECK_EQ(st.output_ua, 0);
    CHECK(!st.stim_locked);
    CHECK(!st.imu_fault);

    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    host_run_ms(61000);
    st = support_status();
    CHECK_EQ(st.mode, VS_MODE_CIRCULATION);
    CHECK_EQ(st.posture, VS_POSTURE_STANDING);
    CHECK_EQ(st.impedance_status, VS_IMPEDANCE_NORMAL);
    CHECK_EQ(st.output_ua, 20000u);
    CHECK_EQ(st.mode3_remaining_s, VS_MODE3_DAILY_CAP_MS / 1000u);
    CHECK(st.battery_percentage <= 100);
}

static void battery_characteristic_is_a_percentage(void)
{
    const double levels[] = {100.0, 73.0, 41.0, 12.0, 3.0};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        host_reset();
        host_battery_set_percentage(levels[i]);
        host_boot(NULL);
        host_run_ms(1100);
        const uint8_t pct = vs_cmd_battery_percentage();
        CHECK(pct <= 100);
        CHECK_NEAR(pct, levels[i], 1.0);
        CHECK_EQ(support_status().battery_percentage, pct);
    }
}

static void compliance_characteristic_reads_from_a_sequence(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    compliance_record_t r[8];
    const size_t n = vs_cmd_read_compliance(0, r, 8);
    CHECK(n >= 2);
    CHECK_EQ(r[0].type, COMPLIANCE_MODE_TRANSITION);
    CHECK_EQ(r[0].v32, TRANSITION_BOOT);
    const size_t tail = vs_cmd_read_compliance(r[1].seq, r, 8);
    CHECK_EQ(tail, n - 1);
}

static void wire_layouts_stay_unresolved(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    vs_device_status_t st = support_status();
    uint8_t buf[16];
    size_t written = 99;
    CHECK_EQ(status_codec_encode_device_status(&st, buf, sizeof(buf), &written), STATUS_CODEC_UNRESOLVED);
    CHECK_EQ(written, 0);

    vs_mode_t mode = VS_MODE_STANDBY;
    char pin[CMD_PIN_LENGTH];
    const uint8_t in[2] = {0x02, 0x00};
    CHECK_EQ(status_codec_decode_mode_select(in, sizeof(in), &mode, pin), STATUS_CODEC_UNRESOLVED);
}

static void firmware_version_is_reported(void)
{
    CHECK(strncmp(vs_cmd_firmware_version(), "v", 1) == 0);
    CHECK(strlen(vs_cmd_build_hash()) > 0);
}

int main(void)
{
    RUN_TEST(uuids_match_the_brief);
    RUN_TEST(correct_pin_selects_the_mode_and_is_logged);
    RUN_TEST(wrong_or_malformed_pins_are_rejected);
    RUN_TEST(a_pin_that_is_not_four_digits_never_matches);
    RUN_TEST(a_2_bit_password_is_not_authentication);
    RUN_TEST(five_failures_lock_out_for_15_minutes);
    RUN_TEST(a_correct_pin_resets_the_failure_count);
    RUN_TEST(an_unknown_mode_is_invalid);
    RUN_TEST(current_adjust_is_clamped_to_15_to_30_ma);
    RUN_TEST(status_reports_live_state);
    RUN_TEST(battery_characteristic_is_a_percentage);
    RUN_TEST(compliance_characteristic_reads_from_a_sequence);
    RUN_TEST(wire_layouts_stay_unresolved);
    RUN_TEST(firmware_version_is_reported);
    return th_finish();
}
