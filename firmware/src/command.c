#include "command.h"

#include <string.h>

#include "hal.h"
#include "version.h"
#include "vs_core.h"

static bool pin_well_formed(const char pin[CMD_PIN_LENGTH])
{
    if (pin == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < CMD_PIN_LENGTH; i++) {
        if (pin[i] < '0' || pin[i] > '9') {
            return false;
        }
    }
    return true;
}

static bool pin_equal(const char a[CMD_PIN_LENGTH], const char b[CMD_PIN_LENGTH])
{
    uint8_t diff = 0;
    for (uint32_t i = 0; i < CMD_PIN_LENGTH; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

void cmd_auth_init(cmd_auth_t *auth, const char pin[CMD_PIN_LENGTH])
{
    memset(auth, 0, sizeof(*auth));
    memcpy(auth->pin, pin, CMD_PIN_LENGTH);
}

bool cmd_auth_locked(const cmd_auth_t *auth, uint64_t now_ms)
{
    return auth->locked && now_ms < auth->locked_until_ms;
}

cmd_result_t cmd_auth_check(cmd_auth_t *auth, const char pin[CMD_PIN_LENGTH], uint64_t now_ms, compliance_t *log)
{
    if (auth->locked && now_ms >= auth->locked_until_ms) {
        auth->locked = false;
        auth->failures = 0;
    }
    if (auth->locked) {
        compliance_append(log, COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED, auth->failures,
                          (uint32_t)(auth->locked_until_ms - now_ms));
        return CMD_LOCKED_OUT;
    }
    if (pin_well_formed(pin) && pin_equal(pin, auth->pin)) {
        auth->failures = 0;
        return CMD_OK;
    }

    auth->failures++;
    compliance_append(log, COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_FAILED, auth->failures, 0);
    if (auth->failures >= CMD_PIN_MAX_FAILURES) {
        auth->locked = true;
        auth->locked_until_ms = now_ms + CMD_PIN_LOCKOUT_MS;
        compliance_append(log, COMPLIANCE_SAFETY_ALERT, ALERT_AUTH_LOCKOUT, auth->failures, CMD_PIN_LOCKOUT_MS);
    }
    return CMD_BAD_PIN;
}

void vs_cmd_get_status(vs_device_status_t *out)
{
    const vs_core_t *c = vs_core();
    memset(out, 0, sizeof(*out));
    out->mode = session_live_mode(&c->session);
    out->battery_percentage = battery_percentage(&c->battery);
    out->impedance_status = safety_impedance_status(&c->safety);
    out->selected = c->session.selected;
    out->posture = c->posture_state;
    out->current_ma = session_effective_current_ma(&c->session);
    out->output_ua = stim_output_ua(&c->stim);
    out->daily_cap_reached = c->session.daily_cap_reached;
    out->mode3_remaining_s = session_mode3_remaining_ms(&c->session) / 1000u;
    out->stim_locked = !safety_permits_stim(&c->safety);
    out->auth_locked = cmd_auth_locked(&c->auth, c->now_ms);
    out->imu_fault = c->imu_fault;
}

cmd_result_t vs_cmd_select_mode(vs_mode_t mode, const char pin[CMD_PIN_LENGTH])
{
    vs_core_t *c = vs_core();
    const uint64_t now = hal_time_ms();
    const cmd_result_t auth = cmd_auth_check(&c->auth, pin, now, &c->log);
    if (auth != CMD_OK) {
        return auth;
    }
    if (mode != VS_MODE_STANDBY && mode != VS_MODE_CIRCULATION && mode != VS_MODE_METABOLIC) {
        return CMD_INVALID;
    }
    compliance_append(&c->log, COMPLIANCE_MODE_TRANSITION, (uint8_t)c->session.selected, (uint16_t)mode,
                      TRANSITION_CLINICIAN_SELECT);
    session_select(&c->session, mode);
    return CMD_OK;
}

uint8_t vs_cmd_set_current(uint8_t current_ma)
{
    vs_core_t *c = vs_core();
    session_set_current(&c->session, current_ma, hal_time_ms());
    return c->session.user_current_ma;
}

size_t vs_cmd_read_compliance(uint32_t from_seq, compliance_record_t *out, size_t max)
{
    return compliance_read(&vs_core()->log, from_seq, out, max);
}

uint8_t vs_cmd_battery_percentage(void)
{
    return battery_percentage(&vs_core()->battery);
}

bool vs_cmd_reenable_stimulation(void)
{
    return safety_reenable(&vs_core()->safety);
}

const char *vs_cmd_firmware_version(void)
{
    return VS_FIRMWARE_VERSION;
}

const char *vs_cmd_build_hash(void)
{
    return VS_BUILD_HASH;
}
