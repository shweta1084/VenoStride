#include "stim.h"

#include <string.h>

#include "hal.h"
#include "vs_types.h"

static uint32_t clip_ua(uint32_t ua)
{
    return ua > VS_CURRENT_MAX_UA ? VS_CURRENT_MAX_UA : ua;
}

static void push_outputs(stim_t *st, bool train, uint32_t amplitude_ua)
{
    amplitude_ua = clip_ua(amplitude_ua);
    if (amplitude_ua != st->output_ua) {
        hal_stim_set_amplitude_ua(amplitude_ua);
        st->output_ua = amplitude_ua;
    }
    if (train != st->train_on) {
        hal_stim_set_train(train, st->pattern.frequency_hz, st->pattern.pulse_width_us);
        st->train_on = train;
    }
}

void stim_init(stim_t *st)
{
    memset(st, 0, sizeof(*st));
}

void stim_begin(stim_t *st, const stim_pattern_t *pattern, uint64_t now_ms)
{
    st->pattern = *pattern;
    st->envelope_ua = 0;
    st->ramp_from_ua = 0;
    st->ramp_to_ua = 0;
    st->ramp_duration_ms = 0;
    st->ramp_start_ms = now_ms;
    st->ramp_gated = false;
    st->ramp_on_elapsed_ms = 0;
    st->was_in_burst = false;
    st->last_tick_ms = now_ms;
    st->pattern_start_ms = now_ms;
    if (!st->boost_on) {
        hal_stim_boost_enable(true);
        st->boost_on = true;
    }
}

void stim_restart_pattern(stim_t *st, uint64_t now_ms)
{
    st->pattern_start_ms = now_ms;
}

void stim_ramp_to(stim_t *st, uint32_t target_ua, uint32_t duration_ms, uint64_t now_ms)
{
    st->ramp_from_ua = st->envelope_ua;
    st->ramp_to_ua = clip_ua(target_ua);
    st->ramp_start_ms = now_ms;
    st->ramp_duration_ms = duration_ms;
    st->ramp_gated = false;
    st->ramp_on_elapsed_ms = 0;
    if (duration_ms == 0) {
        st->envelope_ua = st->ramp_to_ua;
    }
}

void stim_ramp_to_gated(stim_t *st, uint32_t target_ua, uint32_t on_duration_ms, uint64_t now_ms)
{
    stim_ramp_to(st, target_ua, on_duration_ms, now_ms);
    st->ramp_gated = on_duration_ms > 0;
}

static uint64_t ramp_elapsed_ms(const stim_t *st, uint64_t now_ms)
{
    return st->ramp_gated ? st->ramp_on_elapsed_ms : now_ms - st->ramp_start_ms;
}

void stim_retarget(stim_t *st, uint32_t target_ua, uint64_t now_ms)
{
    const uint64_t elapsed = ramp_elapsed_ms(st, now_ms);
    const uint32_t remaining = elapsed >= st->ramp_duration_ms ? 0u : (uint32_t)(st->ramp_duration_ms - elapsed);
    if (st->ramp_gated) {
        stim_ramp_to_gated(st, target_ua, remaining, now_ms);
    } else {
        stim_ramp_to(st, target_ua, remaining, now_ms);
    }
}

void stim_set_level(stim_t *st, uint32_t level_ua, uint64_t now_ms)
{
    stim_ramp_to(st, level_ua, 0, now_ms);
}

bool stim_ramp_done(const stim_t *st, uint64_t now_ms)
{
    return st->envelope_ua == st->ramp_to_ua && ramp_elapsed_ms(st, now_ms) >= st->ramp_duration_ms;
}

void stim_cutoff(stim_t *st)
{
    hal_stim_cutoff();
    st->boost_on = false;
    st->train_on = false;
    st->output_ua = 0;
    st->envelope_ua = 0;
    st->ramp_from_ua = 0;
    st->ramp_to_ua = 0;
    st->ramp_duration_ms = 0;
}

void stim_disable(stim_t *st)
{
    push_outputs(st, false, 0);
    if (st->boost_on) {
        hal_stim_boost_enable(false);
        st->boost_on = false;
    }
    st->envelope_ua = 0;
    st->ramp_to_ua = 0;
    st->ramp_from_ua = 0;
    st->ramp_duration_ms = 0;
}

static void update_envelope(stim_t *st, uint64_t now_ms)
{
    const uint64_t elapsed = ramp_elapsed_ms(st, now_ms);
    if (st->ramp_duration_ms == 0 || elapsed >= st->ramp_duration_ms) {
        st->envelope_ua = st->ramp_to_ua;
        return;
    }
    const int64_t span = (int64_t)st->ramp_to_ua - (int64_t)st->ramp_from_ua;
    st->envelope_ua = (uint32_t)((int64_t)st->ramp_from_ua + span * (int64_t)elapsed / (int64_t)st->ramp_duration_ms);
}

void stim_tick(stim_t *st, uint64_t now_ms)
{
    if (!st->boost_on) {
        return;
    }
    const uint64_t period = (uint64_t)st->pattern.on_ms + st->pattern.off_ms;
    const bool in_burst = period == 0 || (now_ms - st->pattern_start_ms) % period < st->pattern.on_ms;

    /* An interval counts as ON time when it began inside a burst. */
    if (st->ramp_gated && st->was_in_burst) {
        st->ramp_on_elapsed_ms += (uint32_t)(now_ms - st->last_tick_ms);
    }
    st->was_in_burst = in_burst;
    st->last_tick_ms = now_ms;

    update_envelope(st, now_ms);
    const uint32_t amplitude = in_burst ? st->envelope_ua : 0u;
    push_outputs(st, amplitude > 0u, amplitude);
}

uint32_t stim_output_ua(const stim_t *st)
{
    return st->train_on ? st->output_ua : 0u;
}

uint32_t stim_envelope_ua(const stim_t *st)
{
    return st->envelope_ua;
}

bool stim_pulsing(const stim_t *st)
{
    return st->train_on && st->output_ua > 0u;
}

bool stim_boost_on(const stim_t *st)
{
    return st->boost_on;
}
