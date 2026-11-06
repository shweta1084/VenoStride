#ifndef VS_STIM_H
#define VS_STIM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t frequency_hz;
    uint16_t pulse_width_us;
    uint32_t on_ms;
    uint32_t off_ms;
} stim_pattern_t;

typedef struct {
    bool boost_on;
    stim_pattern_t pattern;
    uint64_t pattern_start_ms;
    uint32_t envelope_ua;
    uint32_t ramp_from_ua;
    uint32_t ramp_to_ua;
    uint64_t ramp_start_ms;
    uint32_t ramp_duration_ms;
    bool ramp_gated;            /* ramp progress counts ON-phase time only */
    uint32_t ramp_on_elapsed_ms;
    bool was_in_burst;
    uint64_t last_tick_ms;
    bool train_on;
    uint32_t output_ua;
} stim_t;

void stim_init(stim_t *st);

/* Enables the boost converter and starts the duty pattern in its ON phase with a zero envelope. */
void stim_begin(stim_t *st, const stim_pattern_t *pattern, uint64_t now_ms);
void stim_restart_pattern(stim_t *st, uint64_t now_ms);

/* Linear envelope ramp from the present level. Targets above 30 mA are clipped. */
void stim_ramp_to(stim_t *st, uint32_t target_ua, uint32_t duration_ms, uint64_t now_ms);
/* Same ramp, but it advances only while the duty pattern is ON and holds its progress through OFF periods. */
void stim_ramp_to_gated(stim_t *st, uint32_t target_ua, uint32_t on_duration_ms, uint64_t now_ms);
/* Changes the target of the ramp in progress, keeping its remaining time and gating. */
void stim_retarget(stim_t *st, uint32_t target_ua, uint64_t now_ms);
/* Jumps the envelope to a level (DAC updated on the next stim_tick). */
void stim_set_level(stim_t *st, uint32_t level_ua, uint64_t now_ms);
bool stim_ramp_done(const stim_t *st, uint64_t now_ms);

/* Safety stop: outputs off in this call, no ramp. */
void stim_cutoff(stim_t *st);
/* Normal stop once a ramp-down has reached zero: train and boost off. */
void stim_disable(stim_t *st);

void stim_tick(stim_t *st, uint64_t now_ms);

uint32_t stim_output_ua(const stim_t *st);   /* amplitude being delivered (0 between bursts) */
uint32_t stim_envelope_ua(const stim_t *st);
bool stim_pulsing(const stim_t *st);
bool stim_boost_on(const stim_t *st);

#endif
