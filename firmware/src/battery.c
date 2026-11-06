#include "battery.h"

#include <string.h>

#include "hal.h"

typedef struct {
    uint8_t pct;
    uint16_t mv;
} curve_point_t;

static const curve_point_t k_curve[] = {
    {0, 3000},  {5, 3500},  {10, 3610}, {20, 3670}, {30, 3710}, {40, 3750},
    {50, 3790}, {60, 3840}, {70, 3900}, {80, 3980}, {90, 4080}, {100, 4200},
};
#define CURVE_POINTS (sizeof(k_curve) / sizeof(k_curve[0]))

uint8_t battery_percentage_from_mv(uint32_t mv)
{
    if (mv <= k_curve[0].mv) {
        return 0;
    }
    if (mv >= k_curve[CURVE_POINTS - 1].mv) {
        return 100;
    }
    for (size_t i = 1; i < CURVE_POINTS; i++) {
        if (mv < k_curve[i].mv) {
            const uint32_t lo_mv = k_curve[i - 1].mv;
            const uint32_t hi_mv = k_curve[i].mv;
            const uint32_t lo_pct = k_curve[i - 1].pct;
            const uint32_t hi_pct = k_curve[i].pct;
            return (uint8_t)(lo_pct + (hi_pct - lo_pct) * (mv - lo_mv) / (hi_mv - lo_mv));
        }
    }
    return 100;
}

uint32_t battery_mv_from_percentage(float percentage)
{
    if (percentage <= 0.0f) {
        return k_curve[0].mv;
    }
    if (percentage >= 100.0f) {
        return k_curve[CURVE_POINTS - 1].mv;
    }
    for (size_t i = 1; i < CURVE_POINTS; i++) {
        if (percentage <= (float)k_curve[i].pct) {
            const float lo_pct = k_curve[i - 1].pct;
            const float hi_pct = k_curve[i].pct;
            const float frac = (percentage - lo_pct) / (hi_pct - lo_pct);
            /* Round up so that the inverse lookup lands on the same whole percentage. */
            const float mv = (float)k_curve[i - 1].mv + frac * (float)(k_curve[i].mv - k_curve[i - 1].mv);
            return (uint32_t)(mv + 0.999f);
        }
    }
    return k_curve[CURVE_POINTS - 1].mv;
}

void battery_init(battery_t *b, compliance_t *log)
{
    memset(b, 0, sizeof(*b));
    b->log = log;
}

void battery_tick(battery_t *b, uint64_t now_ms)
{
    if (b->sampled && now_ms < b->next_sample_ms) {
        return;
    }
    b->next_sample_ms = now_ms + BATTERY_SAMPLE_PERIOD_MS;

    b->mv = hal_battery_mv();
    b->percentage = battery_percentage_from_mv(b->mv);

    const bool charging = hal_power_is_charging();
    if (b->sampled && charging != b->charging) {
        compliance_append(b->log, COMPLIANCE_CHARGING, charging ? CHARGING_STARTED : CHARGING_ENDED,
                          b->percentage, b->mv);
    } else if (!b->sampled && charging) {
        compliance_append(b->log, COMPLIANCE_CHARGING, CHARGING_STARTED, b->percentage, b->mv);
    }
    b->charging = charging;
    b->sampled = true;

    if (b->percentage > BATTERY_ALERT_HIGH_PCT + BATTERY_ALERT_REARM_PCT) {
        b->alert_high_sent = false;
    }
    if (b->percentage > BATTERY_ALERT_LOW_PCT + BATTERY_ALERT_REARM_PCT) {
        b->alert_low_sent = false;
    }
    if (b->percentage <= BATTERY_ALERT_HIGH_PCT && !b->alert_high_sent) {
        b->alert_high_sent = true;
        compliance_append(b->log, COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_20, b->percentage, b->mv);
    }
    if (b->percentage <= BATTERY_ALERT_LOW_PCT && !b->alert_low_sent) {
        b->alert_low_sent = true;
        compliance_append(b->log, COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_LOW_5, b->percentage, b->mv);
    }

    const bool below_cutoff = b->mv < BATTERY_STIM_CUTOFF_MV || b->percentage < BATTERY_STIM_CUTOFF_PCT;
    if (!b->stim_inhibited && below_cutoff) {
        b->stim_inhibited = true;
        compliance_append(b->log, COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_STIM_CUTOFF, b->percentage, b->mv);
    } else if (b->stim_inhibited && b->mv >= BATTERY_STIM_CUTOFF_MV && b->percentage >= BATTERY_STIM_RESUME_PCT) {
        b->stim_inhibited = false;
    }

    if (!b->shutdown && b->mv < BATTERY_SHUTDOWN_MV) {
        b->shutdown = true;
        compliance_append(b->log, COMPLIANCE_SAFETY_ALERT, ALERT_BATTERY_SHUTDOWN, 0, b->mv);
    }
}

uint8_t battery_percentage(const battery_t *b)
{
    return b->percentage;
}

uint32_t battery_mv(const battery_t *b)
{
    return b->mv;
}

bool battery_permits_stim(const battery_t *b)
{
    return b->sampled && !b->stim_inhibited;
}

bool battery_needs_shutdown(const battery_t *b)
{
    return b->shutdown;
}
