#include "vs_core.h"

#include <string.h>

#include "hal.h"

static vs_core_t g_core;

void vs_config_defaults(vs_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg->clinician_pin, "0000", CMD_PIN_LENGTH);
    cfg->default_current_ma = VS_CURRENT_DEFAULT_MA;
    cfg->mode3_frequency_hz = VS_MODE3_FREQUENCY_DEFAULT_HZ;
    imu_comp_identity(&cfg->imu_comp);
}

vs_core_t *vs_core(void)
{
    return &g_core;
}

static void new_day(vs_core_t *c)
{
    session_new_day(&c->session);
    c->last_day_reset_ms = c->now_ms;
}

static void update_clock(vs_core_t *c)
{
    uint32_t epoch = 0;
    const bool valid = hal_rtc_get_utc(&epoch) && epoch >= VS_RTC_MIN_VALID_S && epoch < VS_RTC_MAX_VALID_S;

    if (!valid) {
        compliance_set_time(&c->log, VS_TIMESTAMP_UNKNOWN);
        if (c->rtc_valid || !c->rtc_checked) {
            compliance_append(&c->log, COMPLIANCE_SAFETY_ALERT, ALERT_RTC_INVALID, 0, epoch);
        }
        c->rtc_valid = false;
        c->rtc_checked = true;
        /* Without a trustworthy date, the Mode 3 cap still resets at most once per 24 h of uptime. */
        if (c->now_ms - c->last_day_reset_ms >= VS_DAY_MS) {
            new_day(c);
        }
        return;
    }

    c->rtc_valid = true;
    c->rtc_checked = true;
    c->utc_s = epoch;
    compliance_set_time(&c->log, epoch);

    const uint32_t day = epoch / VS_UTC_DAY_S;
    if (!c->utc_day_known) {
        c->utc_day_known = true;
        c->utc_day = day;
    } else if (day > c->utc_day) {
        c->utc_day = day;
        new_day(c);
    }
}

#define RESTORE_OPEN_SESSION_MARGIN_S 60u
#define RESTORE_CHUNK 32u

typedef struct {
    uint32_t day_start_s;
    uint32_t day_end_s;
    vs_mode_t selected;
    vs_stop_reason_t latched;
    uint32_t mode3_used_s;
    bool open;
    uint32_t open_start_s;
    uint32_t last_seen_s;
} restore_t;

static uint32_t overlap_today(const restore_t *r, uint32_t from_s, uint32_t to_s)
{
    const uint32_t a = from_s > r->day_start_s ? from_s : r->day_start_s;
    const uint32_t b = to_s < r->day_end_s ? to_s : r->day_end_s;
    return b > a ? b - a : 0u;
}

static void close_open_session(restore_t *r, uint32_t end_s)
{
    if (r->open) {
        /* Never earlier than the newest record: a clock step backwards must not drop logged time. */
        if (end_s < r->last_seen_s) {
            end_s = r->last_seen_s;
        }
        r->mode3_used_s += overlap_today(r, r->open_start_s, end_s);
        r->open = false;
    }
}

static void restore_record(restore_t *r, const compliance_record_t *rec)
{
    const bool known = rec->timestamp != VS_TIMESTAMP_UNKNOWN;
    switch (rec->type) {
    case COMPLIANCE_MODE_TRANSITION:
        if (rec->v32 == TRANSITION_CLINICIAN_SELECT) {
            r->selected = (vs_mode_t)rec->v16;
        } else if (rec->v32 == TRANSITION_BOOT) {
            /* That run ended at this boot. Its own timestamp covers a silent stretch such as the auto-pause. */
            const uint32_t end_s = known ? rec->timestamp : r->last_seen_s + RESTORE_OPEN_SESSION_MARGIN_S;
            close_open_session(r, end_s);
        }
        break;
    case COMPLIANCE_SAFETY_ALERT:
        if (rec->arg == ALERT_IMPEDANCE_CRITICAL) {
            r->latched = VS_STOP_IMPEDANCE;
        } else if (rec->arg == ALERT_CHARGE_IMBALANCE) {
            r->latched = VS_STOP_CHARGE_BALANCE;
        } else if (rec->arg == ALERT_SENSE_FAILURE) {
            r->latched = VS_STOP_SENSE_FAILURE;
        } else if (rec->arg == ALERT_STIM_REENABLED) {
            r->latched = VS_STOP_NONE;
        }
        break;
    case COMPLIANCE_SESSION_START:
        if (rec->arg == VS_MODE_METABOLIC && known) {
            close_open_session(r, rec->timestamp);
            r->open = true;
            r->open_start_s = rec->timestamp;
            r->last_seen_s = rec->timestamp;
        }
        break;
    case COMPLIANCE_SESSION_END:
        if (rec->arg == VS_MODE_METABOLIC) {
            if (r->open) {
                r->mode3_used_s += overlap_today(r, r->open_start_s, r->open_start_s + rec->v32);
                r->open = false;
            } else if (known && rec->timestamp >= r->day_start_s && rec->timestamp < r->day_end_s) {
                r->mode3_used_s += rec->v32;
            }
        }
        break;
    default:
        break;
    }
    if (r->open && known && rec->timestamp > r->last_seen_s) {
        r->last_seen_s = rec->timestamp;
    }
}

/*
 * RAM does not survive a reset, but the compliance log does. Rebuild the clinician
 * selection, any halt still awaiting re-enable, and today's Mode 3 usage from it so a
 * reset or power cycle cannot lift the daily cap or clear a latched fault.
 */
static void restore_from_log(vs_core_t *c)
{
    restore_t r;
    memset(&r, 0, sizeof(r));
    r.selected = VS_MODE_CIRCULATION;
    r.day_start_s = c->utc_day * VS_UTC_DAY_S;
    r.day_end_s = r.day_start_s + VS_UTC_DAY_S;

    compliance_record_t buf[RESTORE_CHUNK];
    uint32_t from = 0;
    size_t n;
    do {
        n = compliance_read(&c->log, from, buf, RESTORE_CHUNK);
        for (size_t i = 0; i < n; i++) {
            restore_record(&r, &buf[i]);
        }
        if (n > 0) {
            from = buf[n - 1].seq + 1u;
        }
    } while (n == RESTORE_CHUNK);
    /* Still open: this boot has not been logged yet. Count through the current UTC time, not the last record. */
    if (c->rtc_valid) {
        close_open_session(&r, c->utc_s);
    } else {
        close_open_session(&r, r.last_seen_s + RESTORE_OPEN_SESSION_MARGIN_S);
    }

    safety_restore_latch(&c->safety, r.latched);
    /* Without a valid date, today's usage is unknowable: assume the cap is spent. */
    const uint32_t used_ms = c->rtc_valid ? r.mode3_used_s * 1000u : VS_MODE3_DAILY_CAP_MS;
    session_restore(&c->session, r.selected, used_ms);
}

void vs_core_init(const vs_config_t *cfg)
{
    vs_core_t *c = &g_core;
    memset(c, 0, sizeof(*c));
    hal_stim_cutoff();

    if (cfg != NULL) {
        c->cfg = *cfg;
    } else {
        vs_config_defaults(&c->cfg);
    }
    c->now_ms = hal_time_ms();
    c->last_day_reset_ms = c->now_ms;

    compliance_init(&c->log);
    update_clock(c);

    imu_comp_init(&c->imu_comp, &c->cfg.imu_comp);
    posture_init(&c->posture);
    c->posture_state = VS_POSTURE_UNKNOWN;
    stim_init(&c->stim);
    session_init(&c->session, &c->stim, &c->log, &c->cfg);
    safety_init(&c->safety, &c->log);
    battery_init(&c->battery, &c->log);
    cmd_auth_init(&c->auth, c->cfg.clinician_pin);

    restore_from_log(c);
    compliance_append(&c->log, COMPLIANCE_MODE_TRANSITION, VS_MODE_STANDBY, VS_MODE_STANDBY, TRANSITION_BOOT);
    if (hal_reset_reason() == HAL_RESET_WATCHDOG) {
        compliance_append(&c->log, COMPLIANCE_SAFETY_ALERT, ALERT_WATCHDOG_RESET, 0, 0);
    }

    hal_wdt_start(VS_WDT_PERIOD_MS);
    hal_wdt_kick();
    c->last_kick_ms = c->now_ms;
}

static void read_imu(vs_core_t *c)
{
    hal_imu_sample_t sample;
    if (!hal_imu_read(&sample)) {
        c->imu_good_count = 0;
        if (!c->imu_fault) {
            c->imu_fault = true;
            compliance_append(&c->log, COMPLIANCE_SAFETY_ALERT, ALERT_IMU_FAULT, 0, 0);
            session_safety_stop(&c->session, VS_STOP_IMU_FAULT, c->now_ms);
        }
        c->posture_state = VS_POSTURE_UNKNOWN;
        return;
    }

    if (c->imu_fault) {
        if (++c->imu_good_count < VS_IMU_RECOVERY_SAMPLES) {
            c->posture_state = VS_POSTURE_UNKNOWN;
            return;
        }
        c->imu_fault = false;
        posture_init(&c->posture);
        compliance_append(&c->log, COMPLIANCE_SAFETY_ALERT, ALERT_IMU_RECOVERED, 0, 0);
    }

    imu_comp_apply(&c->imu_comp, &sample);
    c->posture_state = posture_update(&c->posture, &sample);
}

void vs_core_tick(void)
{
    vs_core_t *c = &g_core;
    if (c->powered_off) {
        return;
    }
    c->now_ms = hal_time_ms();
    update_clock(c);

    read_imu(c);
    if (c->posture_state == VS_POSTURE_WALKING && session_running(&c->session)) {
        session_safety_stop(&c->session, VS_STOP_WALKING, c->now_ms);
    }

    battery_tick(&c->battery, c->now_ms);
    if (battery_needs_shutdown(&c->battery)) {
        session_safety_stop(&c->session, VS_STOP_SHUTDOWN, c->now_ms);
        c->powered_off = true;
        hal_power_shutdown();
        return;
    }
    if (!battery_permits_stim(&c->battery) && session_running(&c->session)) {
        session_safety_stop(&c->session, VS_STOP_LOW_BATTERY, c->now_ms);
    }

    safety_measurement_t meas;
    const vs_stop_reason_t fault = safety_tick(&c->safety, c->now_ms, stim_output_ua(&c->stim), &meas);
    if (fault != VS_STOP_NONE) {
        session_safety_stop(&c->session, fault, c->now_ms);
    } else if (meas.taken) {
        session_on_impedance(&c->session, meas.impedance_ohm);
    }

    const bool permitted = safety_permits_stim(&c->safety) && battery_permits_stim(&c->battery) && !c->imu_fault;
    const bool was_running = session_running(&c->session);
    session_tick(&c->session, c->now_ms, c->posture_state, permitted);
    if (!was_running && session_running(&c->session)) {
        safety_session_start(&c->safety, c->now_ms);
    }
    stim_tick(&c->stim, c->now_ms);

    if (c->now_ms - c->last_kick_ms >= VS_WDT_KICK_MS) {
        hal_wdt_kick();
        c->last_kick_ms = c->now_ms;
    }
}
