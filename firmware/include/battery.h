#ifndef VS_BATTERY_H
#define VS_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#include "compliance.h"

/* From the brief (section 6, requirement 5). */
#define BATTERY_ALERT_HIGH_PCT       20u
#define BATTERY_ALERT_LOW_PCT        5u
#define BATTERY_STIM_CUTOFF_PCT      5u
#define BATTERY_STIM_CUTOFF_MV       3000u
#define BATTERY_SHUTDOWN_MV          2800u
#define BATTERY_SAMPLE_PERIOD_MS     1000u

/* Engineering placeholders: hysteresis so thresholds do not chatter. */
#define BATTERY_STIM_RESUME_PCT      7u
#define BATTERY_ALERT_REARM_PCT      5u

typedef struct {
    compliance_t *log;
    uint32_t mv;
    uint8_t percentage;
    bool sampled;
    bool alert_high_sent;
    bool alert_low_sent;
    bool stim_inhibited;
    bool charging;
    bool shutdown;
    uint64_t next_sample_ms;
} battery_t;

/*
 * Generic single-cell LiPo open-circuit curve. Placeholder until the pack's
 * measured discharge curve is fitted; 0 % is placed at the 3.0 V stimulation cutoff.
 */
uint8_t battery_percentage_from_mv(uint32_t mv);
uint32_t battery_mv_from_percentage(float percentage);

void battery_init(battery_t *b, compliance_t *log);

/* Samples the ADC once per second; logs alerts and charging edges. */
void battery_tick(battery_t *b, uint64_t now_ms);

uint8_t battery_percentage(const battery_t *b);
uint32_t battery_mv(const battery_t *b);
bool battery_permits_stim(const battery_t *b);
bool battery_needs_shutdown(const battery_t *b);

#endif
