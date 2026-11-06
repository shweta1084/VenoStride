#include "safety.h"

#include <string.h>

#include "hal.h"

void safety_init(safety_t *s, compliance_t *log)
{
    memset(s, 0, sizeof(*s));
    s->log = log;
    s->impedance_status = VS_IMPEDANCE_NORMAL;
}

void safety_session_start(safety_t *s, uint64_t now_ms)
{
    s->next_impedance_ms = now_ms;
    s->next_charge_ms = now_ms;
}

static vs_stop_reason_t latch(safety_t *s, vs_stop_reason_t reason)
{
    s->latched = true;
    s->latched_reason = reason;
    return reason;
}

static uint16_t sat16(uint32_t v)
{
    return v > 0xFFFFu ? 0xFFFFu : (uint16_t)v;
}

static vs_stop_reason_t check_impedance(safety_t *s, const hal_sense_sample_t *sense, safety_measurement_t *meas)
{
    const uint64_t ohm64 = ((uint64_t)sense->voltage_mv * 1000u) / sense->current_ua;
    const uint32_t ohm = ohm64 > UINT32_MAX ? UINT32_MAX : (uint32_t)ohm64;
    s->last_impedance_ohm = ohm;
    meas->taken = true;
    meas->impedance_ohm = ohm;
    meas->current_ua = sense->current_ua;
    compliance_append(s->log, COMPLIANCE_IMPEDANCE, IMPEDANCE_KIND_PERIODIC, sat16(ohm), sense->current_ua);

    if (ohm64 > SAFETY_IMPEDANCE_CRIT_OHM) {
        s->impedance_status = VS_IMPEDANCE_CRITICAL;
        compliance_append(s->log, COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_CRITICAL, sat16(ohm), 0);
        return latch(s, VS_STOP_IMPEDANCE);
    }
    if (ohm64 > SAFETY_IMPEDANCE_WARN_OHM) {
        if (s->high_count < 0xFFu) {
            s->high_count++;
        }
        if (s->high_count >= SAFETY_IMPEDANCE_WARN_COUNT && s->impedance_status != VS_IMPEDANCE_WARNING) {
            s->impedance_status = VS_IMPEDANCE_WARNING;
            compliance_append(s->log, COMPLIANCE_SAFETY_ALERT, ALERT_IMPEDANCE_WARNING, sat16(ohm), 0);
        }
    } else {
        s->high_count = 0;
        s->impedance_status = VS_IMPEDANCE_NORMAL;
    }
    return VS_STOP_NONE;
}

static vs_stop_reason_t check_charge(safety_t *s, const hal_sense_sample_t *sense)
{
    const int64_t net = (int64_t)sense->phase1_charge_nc + (int64_t)sense->phase2_charge_nc;
    const uint64_t magnitude = net < 0 ? (uint64_t)(-net) : (uint64_t)net;
    if (magnitude > (uint64_t)SAFETY_CHARGE_IMBALANCE_NC) {
        const uint32_t logged = magnitude > UINT32_MAX ? UINT32_MAX : (uint32_t)magnitude;
        compliance_append(s->log, COMPLIANCE_SAFETY_ALERT, ALERT_CHARGE_IMBALANCE, 0, logged);
        return latch(s, VS_STOP_CHARGE_BALANCE);
    }
    return VS_STOP_NONE;
}

vs_stop_reason_t safety_tick(safety_t *s, uint64_t now_ms, uint32_t output_ua, safety_measurement_t *meas)
{
    meas->taken = false;
    if (output_ua < SAFETY_MIN_MEASURE_UA) {
        return VS_STOP_NONE;
    }
    const bool impedance_due = now_ms >= s->next_impedance_ms;
    const bool charge_due = now_ms >= s->next_charge_ms;
    if (!impedance_due && !charge_due) {
        return VS_STOP_NONE;
    }

    hal_sense_sample_t sense;
    memset(&sense, 0, sizeof(sense));
    const bool read_ok = hal_sense_read(&sense);
    if (!read_ok || sense.status != HAL_SENSE_OK || sense.current_ua == 0u) {
        const uint16_t status = (uint16_t)(sense.status == HAL_SENSE_OK ? HAL_SENSE_OPEN : sense.status);
        compliance_append(s->log, COMPLIANCE_SAFETY_ALERT, ALERT_SENSE_FAILURE, status, 0);
        return latch(s, VS_STOP_SENSE_FAILURE);
    }

    vs_stop_reason_t stop = VS_STOP_NONE;
    if (impedance_due) {
        s->next_impedance_ms = now_ms + SAFETY_IMPEDANCE_PERIOD_MS;
        stop = check_impedance(s, &sense, meas);
    }
    if (stop == VS_STOP_NONE && charge_due) {
        s->next_charge_ms = now_ms + SAFETY_CHARGE_PERIOD_MS;
        stop = check_charge(s, &sense);
    }
    return stop;
}

bool safety_permits_stim(const safety_t *s)
{
    return !s->latched;
}

vs_impedance_status_t safety_impedance_status(const safety_t *s)
{
    return s->impedance_status;
}

void safety_restore_latch(safety_t *s, vs_stop_reason_t reason)
{
    if (reason == VS_STOP_NONE) {
        return;
    }
    latch(s, reason);
    if (reason == VS_STOP_IMPEDANCE) {
        s->impedance_status = VS_IMPEDANCE_CRITICAL;
    }
}

bool safety_reenable(safety_t *s)
{
    if (!s->latched) {
        return false;
    }
    compliance_append(s->log, COMPLIANCE_SAFETY_ALERT, ALERT_STIM_REENABLED, (uint16_t)s->latched_reason, 0);
    s->latched = false;
    s->latched_reason = VS_STOP_NONE;
    s->impedance_status = VS_IMPEDANCE_NORMAL;
    s->high_count = 0;
    return true;
}
