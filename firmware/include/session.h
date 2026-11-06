#ifndef VS_SESSION_H
#define VS_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "compliance.h"
#include "stim.h"
#include "vs_types.h"

typedef enum {
    SESSION_IDLE = 0,       /* Mode 1 standby: outputs off */
    SESSION_RAMP_UP,
    SESSION_ACTIVE,
    SESSION_PAUSE_DOWN,     /* Mode 3 auto-pause: 1 s ramp to zero */
    SESSION_PAUSED,         /* Mode 3 auto-pause: 2 min off */
    SESSION_PAUSE_UP,       /* Mode 3 auto-pause: 2 s ramp back */
    SESSION_RAMP_DOWN       /* normal exit: 2 s ramp to zero, then boost off */
} session_state_t;

typedef struct {
    stim_t *stim;
    compliance_t *log;

    session_state_t state;
    vs_mode_t mode;              /* therapy of the running session */
    vs_mode_t selected;          /* clinician selection; VS_MODE_STANDBY disables therapy */
    vs_stop_reason_t stop_reason;
    uint8_t user_current_ma;
    uint8_t mode3_frequency_hz;

    uint64_t session_start_ms;
    uint64_t state_start_ms;
    uint64_t last_tick_ms;
    bool ticked;

    /* Mode 3 bookkeeping */
    uint32_t mode3_used_ms;      /* since the last daily reset */
    bool daily_cap_reached;
    uint32_t block_elapsed_ms;   /* stimulation time since session start or the last auto-pause */
    uint8_t increment_ma;
    bool increment_reset_pending;
    uint32_t baseline_ohm;
    uint64_t window_sum_ohm;
    uint32_t window_count;
} session_t;

void session_init(session_t *s, stim_t *stim, compliance_t *log, const vs_config_t *cfg);

void session_select(session_t *s, vs_mode_t mode);
void session_set_current(session_t *s, uint8_t current_ma, uint64_t now_ms);

void session_tick(session_t *s, uint64_t now_ms, vs_posture_t posture, bool permitted);

/* Immediate cut (walking or a safety fault). Returns true if a session was running. */
bool session_safety_stop(session_t *s, vs_stop_reason_t reason, uint64_t now_ms);

void session_on_impedance(session_t *s, uint32_t ohm);

/* Called at 00:00 UTC (or every 24 h of uptime while the RTC is invalid). */
void session_new_day(session_t *s);

/* Boot-time restore of state that must survive a reset (rebuilt from the compliance log). */
void session_restore(session_t *s, vs_mode_t selected, uint32_t mode3_used_ms);

bool session_running(const session_t *s);
vs_mode_t session_live_mode(const session_t *s);
uint8_t session_effective_current_ma(const session_t *s);
uint32_t session_mode3_remaining_ms(const session_t *s);

#endif
