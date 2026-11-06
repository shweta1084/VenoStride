#include "session.h"

#include <string.h>

void session_init(session_t *s, stim_t *stim, compliance_t *log, const vs_config_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->stim = stim;
    s->log = log;
    s->state = SESSION_IDLE;
    s->mode = VS_MODE_STANDBY;
    s->selected = VS_MODE_CIRCULATION;
    s->user_current_ma = (uint8_t)vs_clamp_u32(cfg->default_current_ma, VS_CURRENT_MIN_MA, VS_CURRENT_MAX_MA);
    s->mode3_frequency_hz = (uint8_t)vs_clamp_u32(cfg->mode3_frequency_hz, VS_MODE3_FREQUENCY_MIN_HZ,
                                                  VS_MODE3_FREQUENCY_MAX_HZ);
}

static uint32_t effective_ma(const session_t *s, vs_mode_t mode)
{
    uint32_t ma = s->user_current_ma;
    if (mode == VS_MODE_METABOLIC) {
        ma += s->increment_ma;
    }
    return ma > VS_CURRENT_MAX_MA ? VS_CURRENT_MAX_MA : ma;
}

static uint32_t target_ua(const session_t *s)
{
    return effective_ma(s, s->mode) * 1000u;
}

static void set_state(session_t *s, session_state_t state, uint64_t now_ms)
{
    s->state = state;
    s->state_start_ms = now_ms;
}

static bool mode3_time_left(const session_t *s)
{
    return !s->daily_cap_reached && s->mode3_used_ms + VS_RAMP_DOWN_MS < VS_MODE3_DAILY_CAP_MS;
}

static void start_session(session_t *s, uint64_t now_ms)
{
    stim_pattern_t pattern;
    s->mode = s->selected;
    if (s->mode == VS_MODE_METABOLIC) {
        pattern.frequency_hz = s->mode3_frequency_hz;
        pattern.on_ms = VS_MODE3_ON_MS;
        pattern.off_ms = VS_MODE3_OFF_MS;
        if (s->increment_reset_pending) {
            s->increment_ma = 0;
            s->increment_reset_pending = false;
        }
        s->baseline_ohm = 0;
        s->window_sum_ohm = 0;
        s->window_count = 0;
    } else {
        pattern.frequency_hz = VS_MODE2_FREQUENCY_HZ;
        pattern.on_ms = VS_MODE2_ON_MS;
        pattern.off_ms = VS_MODE2_OFF_MS;
    }
    pattern.pulse_width_us = VS_PULSE_WIDTH_US;

    s->session_start_ms = now_ms;
    s->block_elapsed_ms = 0;
    s->stop_reason = VS_STOP_NONE;

    compliance_append(s->log, COMPLIANCE_MODE_TRANSITION, VS_MODE_STANDBY, (uint16_t)s->mode,
                      TRANSITION_STANDING_DETECTED);
    compliance_append(s->log, COMPLIANCE_SESSION_START, (uint8_t)s->mode, (uint16_t)effective_ma(s, s->mode), 0);

    stim_begin(s->stim, &pattern, now_ms);
    if (s->mode == VS_MODE_CIRCULATION) {
        stim_ramp_to_gated(s->stim, target_ua(s), VS_RAMP_UP_MS, now_ms);
    } else {
        stim_ramp_to(s->stim, target_ua(s), VS_RAMP_UP_MS, now_ms);
    }
    set_state(s, SESSION_RAMP_UP, now_ms);
}

static void end_session(session_t *s, vs_stop_reason_t reason, uint64_t now_ms)
{
    const uint32_t duration_s = (uint32_t)((now_ms - s->session_start_ms) / 1000u);
    compliance_append(s->log, COMPLIANCE_SESSION_END, (uint8_t)s->mode, (uint16_t)reason, duration_s);
    compliance_append(s->log, COMPLIANCE_MODE_TRANSITION, (uint8_t)s->mode, VS_MODE_STANDBY, (uint32_t)reason);
    s->stop_reason = reason;
    s->mode = VS_MODE_STANDBY;
    set_state(s, SESSION_IDLE, now_ms);
}

static void begin_ramp_down(session_t *s, vs_stop_reason_t reason, uint64_t now_ms)
{
    s->stop_reason = reason;
    stim_ramp_to(s->stim, 0, VS_RAMP_DOWN_MS, now_ms);
    set_state(s, SESSION_RAMP_DOWN, now_ms);
}

static void evaluate_habituation(session_t *s)
{
    if (s->window_count == 0 || s->baseline_ohm == 0) {
        return;
    }
    const uint32_t avg = (uint32_t)(s->window_sum_ohm / s->window_count);
    const bool rose = (uint64_t)avg * 100u > (uint64_t)s->baseline_ohm * (100u + VS_HABITUATION_RISE_PERCENT);
    uint8_t kind = IMPEDANCE_KIND_HABITUATION;
    if (rose && s->increment_ma < VS_MODE3_MAX_INCREMENT_MA &&
        s->user_current_ma + s->increment_ma + 1u <= VS_CURRENT_MAX_MA) {
        s->increment_ma++;
        kind = IMPEDANCE_KIND_HABITUATION_STEP;
    }
    compliance_append(s->log, COMPLIANCE_IMPEDANCE, kind, (uint16_t)(avg > 0xFFFFu ? 0xFFFFu : avg), target_ua(s));
    s->window_sum_ohm = 0;
    s->window_count = 0;
}

void session_select(session_t *s, vs_mode_t mode)
{
    s->selected = mode;
}

void session_set_current(session_t *s, uint8_t current_ma, uint64_t now_ms)
{
    s->user_current_ma = (uint8_t)vs_clamp_u32(current_ma, VS_CURRENT_MIN_MA, VS_CURRENT_MAX_MA);
    switch (s->state) {
    case SESSION_ACTIVE:
        stim_set_level(s->stim, target_ua(s), now_ms);
        break;
    case SESSION_RAMP_UP:
    case SESSION_PAUSE_UP:
        stim_retarget(s->stim, target_ua(s), now_ms);
        break;
    default:
        break;
    }
}

bool session_safety_stop(session_t *s, vs_stop_reason_t reason, uint64_t now_ms)
{
    stim_cutoff(s->stim);
    if (s->state == SESSION_IDLE) {
        return false;
    }
    end_session(s, reason, now_ms);
    return true;
}

void session_on_impedance(session_t *s, uint32_t ohm)
{
    if (s->mode != VS_MODE_METABOLIC || s->state == SESSION_IDLE) {
        return;
    }
    if (s->baseline_ohm == 0) {
        s->baseline_ohm = ohm;
    }
    s->window_sum_ohm += ohm;
    s->window_count++;
}

void session_new_day(session_t *s)
{
    s->mode3_used_ms = 0;
    s->daily_cap_reached = false;
    s->increment_reset_pending = true;
}

void session_restore(session_t *s, vs_mode_t selected, uint32_t mode3_used_ms)
{
    s->selected = selected;
    s->mode3_used_ms = mode3_used_ms > VS_MODE3_DAILY_CAP_MS ? VS_MODE3_DAILY_CAP_MS : mode3_used_ms;
    s->daily_cap_reached = false;
    s->daily_cap_reached = !mode3_time_left(s);
}

void session_tick(session_t *s, uint64_t now_ms, vs_posture_t posture, bool permitted)
{
    const uint32_t dt = s->ticked ? (uint32_t)(now_ms - s->last_tick_ms) : 0u;
    s->last_tick_ms = now_ms;
    s->ticked = true;

    if (s->state == SESSION_IDLE) {
        const bool therapy = s->selected == VS_MODE_CIRCULATION ||
                             (s->selected == VS_MODE_METABOLIC && mode3_time_left(s));
        if (posture == VS_POSTURE_STANDING && permitted && therapy) {
            start_session(s, now_ms);
        }
        return;
    }

    if (s->mode == VS_MODE_METABOLIC) {
        s->mode3_used_ms += dt;
    }
    if (s->state == SESSION_RAMP_UP || s->state == SESSION_ACTIVE) {
        s->block_elapsed_ms += dt;
    }

    switch (s->state) {
    case SESSION_RAMP_UP:
        if (stim_ramp_done(s->stim, now_ms)) {
            set_state(s, SESSION_ACTIVE, now_ms);
        }
        break;
    case SESSION_PAUSE_DOWN:
        if (stim_ramp_done(s->stim, now_ms)) {
            set_state(s, SESSION_PAUSED, now_ms);
        }
        break;
    case SESSION_PAUSED:
        if (now_ms - s->state_start_ms >= VS_MODE3_PAUSE_MS) {
            stim_restart_pattern(s->stim, now_ms);
            stim_ramp_to(s->stim, target_ua(s), VS_MODE3_PAUSE_RAMP_UP_MS, now_ms);
            set_state(s, SESSION_PAUSE_UP, now_ms);
        }
        break;
    case SESSION_PAUSE_UP:
        if (stim_ramp_done(s->stim, now_ms)) {
            set_state(s, SESSION_ACTIVE, now_ms);
        }
        break;
    case SESSION_RAMP_DOWN:
        if (stim_ramp_done(s->stim, now_ms)) {
            stim_disable(s->stim);
            end_session(s, s->stop_reason, now_ms);
        }
        return;
    default:
        break;
    }

    if (s->mode == VS_MODE_METABOLIC && s->mode3_used_ms + VS_RAMP_DOWN_MS >= VS_MODE3_DAILY_CAP_MS) {
        s->daily_cap_reached = true;
        begin_ramp_down(s, VS_STOP_DAILY_CAP, now_ms);
        return;
    }
    if (s->selected != s->mode) {
        begin_ramp_down(s, VS_STOP_USER, now_ms);
        return;
    }
    if (posture == VS_POSTURE_SITTING) {
        begin_ramp_down(s, VS_STOP_SITTING, now_ms);
        return;
    }
    if (s->mode == VS_MODE_METABOLIC && s->block_elapsed_ms >= VS_MODE3_BLOCK_MS) {
        evaluate_habituation(s);
        s->block_elapsed_ms = 0;
        stim_ramp_to(s->stim, 0, VS_MODE3_PAUSE_RAMP_DOWN_MS, now_ms);
        set_state(s, SESSION_PAUSE_DOWN, now_ms);
    }
}

bool session_running(const session_t *s)
{
    return s->state != SESSION_IDLE;
}

vs_mode_t session_live_mode(const session_t *s)
{
    return s->state == SESSION_IDLE ? VS_MODE_STANDBY : s->mode;
}

uint8_t session_effective_current_ma(const session_t *s)
{
    return (uint8_t)effective_ma(s, s->mode == VS_MODE_STANDBY ? s->selected : s->mode);
}

uint32_t session_mode3_remaining_ms(const session_t *s)
{
    if (s->daily_cap_reached || s->mode3_used_ms >= VS_MODE3_DAILY_CAP_MS) {
        return 0;
    }
    return VS_MODE3_DAILY_CAP_MS - s->mode3_used_ms;
}
