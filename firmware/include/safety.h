#ifndef VS_SAFETY_H
#define VS_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#include "compliance.h"
#include "vs_types.h"

/* From the brief (sections 4 and 6). */
#define SAFETY_IMPEDANCE_PERIOD_MS     30000u
#define SAFETY_IMPEDANCE_WARN_OHM      3000u
#define SAFETY_IMPEDANCE_WARN_COUNT    2u
#define SAFETY_IMPEDANCE_CRIT_OHM      5000u
#define SAFETY_CHARGE_PERIOD_MS        60000u
#define SAFETY_CHARGE_IMBALANCE_NC     500

/* Do not compute impedance until the commanded current has reached this. Not a fault threshold. */
#define SAFETY_MIN_MEASURE_UA          5000u

typedef struct {
    bool taken;
    uint32_t impedance_ohm;
    uint32_t current_ua;
} safety_measurement_t;

typedef struct {
    compliance_t *log;
    vs_impedance_status_t impedance_status;
    uint8_t high_count;
    uint32_t last_impedance_ohm;
    uint64_t next_impedance_ms;
    uint64_t next_charge_ms;
    bool latched;
    vs_stop_reason_t latched_reason;
} safety_t;

void safety_init(safety_t *s, compliance_t *log);

/* Arms both measurements for the first pulse of a new session. */
void safety_session_start(safety_t *s, uint64_t now_ms);

/*
 * Runs any due impedance / charge-balance check while pulses are flowing.
 * Returns VS_STOP_NONE, or the reason stimulation must be cut immediately (the fault is then latched).
 */
vs_stop_reason_t safety_tick(safety_t *s, uint64_t now_ms, uint32_t output_ua, safety_measurement_t *meas);

bool safety_permits_stim(const safety_t *s);
vs_impedance_status_t safety_impedance_status(const safety_t *s);

/* Explicit clinician/user re-enable after a latched halt. Returns false if nothing was latched. */
bool safety_reenable(safety_t *s);

/* Boot-time restore of a halt that was still latched when the device reset. */
void safety_restore_latch(safety_t *s, vs_stop_reason_t reason);

#endif
