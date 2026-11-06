#include "host.h"

#include <string.h>

#include "battery.h"
#include "vs_core.h"

static struct {
    uint64_t now_ms;

    host_imu_source_fn imu_fn;
    void *imu_ctx;
    hal_imu_sample_t imu_static;
    bool imu_fail;

    host_stim_state_t stim;

    uint32_t impedance_ohm;
    hal_sense_status_t sense_status;
    int32_t imbalance_nc;
    bool current_override;
    uint32_t current_override_ua;
    bool sense_fail;

    double remaining_mah;
    bool charging;
    double charge_ma;
    double consumed_mah;
    double consumed_by_mode[3];
    bool powered_off;

    uint32_t rtc_base_s;
    bool rtc_running;

    uint8_t flash[VS_FLASH_LOG_SIZE];
    uint32_t flash_violations;

    bool wdt_started;
    uint32_t wdt_period_ms;
    uint64_t wdt_last_kick_ms;
    uint32_t wdt_resets;
    hal_reset_reason_t reset_reason;

    vs_config_t cfg;
    bool has_cfg;
} H;

/* ---------------- host control ---------------- */

void host_reset(void)
{
    memset(&H, 0, sizeof(H));
    memset(H.flash, 0xFF, sizeof(H.flash));
    H.imu_static.accel_g[2] = 1.0f;
    H.imu_static.temp_c = 25.0f;
    H.impedance_ohm = 1500;
    H.sense_status = HAL_SENSE_OK;
    H.remaining_mah = HOST_BATTERY_CAPACITY_MAH;
    H.rtc_base_s = 1793750400u; /* 2026-11-04T00:00:00Z */
    H.rtc_running = true;
    H.reset_reason = HAL_RESET_POWER_ON;
}

void host_boot(const vs_config_t *cfg)
{
    if (cfg != NULL) {
        H.cfg = *cfg;
        H.has_cfg = true;
    } else {
        H.has_cfg = false;
    }
    H.reset_reason = HAL_RESET_POWER_ON;
    H.powered_off = false;
    vs_core_init(H.has_cfg ? &H.cfg : NULL);
}

static double load_ma(void)
{
    if (!H.stim.boost_on) {
        return HOST_STANDBY_MA;
    }
    const vs_mode_t mode = session_live_mode(&vs_core()->session);
    return HOST_STIM_BASELINE_MA + (mode == VS_MODE_METABOLIC ? HOST_MODE3_STIM_MA : HOST_MODE2_STIM_MA);
}

static void integrate_battery(uint32_t dt_ms)
{
    const double hours = (double)dt_ms / 3600000.0;
    const double used = load_ma() * hours;
    H.consumed_mah += used;
    const vs_mode_t mode = H.stim.boost_on ? session_live_mode(&vs_core()->session) : VS_MODE_STANDBY;
    H.consumed_by_mode[mode] += used;
    H.remaining_mah -= used;
    if (H.charging) {
        H.remaining_mah += H.charge_ma * hours;
    }
    if (H.remaining_mah > HOST_BATTERY_CAPACITY_MAH) {
        H.remaining_mah = HOST_BATTERY_CAPACITY_MAH;
    }
}

static void watchdog_reset(void)
{
    H.stim.boost_on = false;
    H.stim.train_on = false;
    H.stim.amplitude_ua = 0;
    H.wdt_resets++;
    H.wdt_started = false;
    H.reset_reason = HAL_RESET_WATCHDOG;
    vs_core_init(H.has_cfg ? &H.cfg : NULL);
}

static void check_watchdog(void)
{
    if (H.wdt_started && H.now_ms - H.wdt_last_kick_ms > H.wdt_period_ms) {
        watchdog_reset();
    }
}

void host_run_ms(uint64_t ms)
{
    const uint64_t end = H.now_ms + ms;
    while (H.now_ms < end && !H.powered_off) {
        H.now_ms += VS_TICK_MS;
        integrate_battery(VS_TICK_MS);
        check_watchdog();
        vs_core_tick();
    }
    if (H.powered_off) {
        H.now_ms = end;
    }
}

void host_hang_ms(uint64_t ms)
{
    const uint64_t end = H.now_ms + ms;
    while (H.now_ms < end) {
        H.now_ms += VS_TICK_MS;
        integrate_battery(VS_TICK_MS);
        check_watchdog();
    }
}

uint64_t host_now_ms(void)
{
    return H.now_ms;
}

void host_imu_set_static(float ax, float ay, float az, float temp_c)
{
    H.imu_fn = NULL;
    H.imu_static.accel_g[0] = ax;
    H.imu_static.accel_g[1] = ay;
    H.imu_static.accel_g[2] = az;
    memset(H.imu_static.gyro_dps, 0, sizeof(H.imu_static.gyro_dps));
    H.imu_static.temp_c = temp_c;
}

void host_imu_set_source(host_imu_source_fn fn, void *ctx)
{
    H.imu_fn = fn;
    H.imu_ctx = ctx;
}

void host_imu_set_fail(bool fail)
{
    H.imu_fail = fail;
}

void host_sense_set_impedance(uint32_t ohm)
{
    H.impedance_ohm = ohm;
}

void host_sense_set_status(hal_sense_status_t status)
{
    H.sense_status = status;
}

void host_sense_set_imbalance_nc(int32_t extra_nc)
{
    H.imbalance_nc = extra_nc;
}

void host_sense_set_current_ua(uint32_t current_ua)
{
    H.current_override = true;
    H.current_override_ua = current_ua;
}

void host_sense_set_fail(bool fail)
{
    H.sense_fail = fail;
}

const host_stim_state_t *host_stim(void)
{
    return &H.stim;
}

uint32_t host_stim_delivered_ua(void)
{
    return H.stim.boost_on && H.stim.train_on ? H.stim.amplitude_ua : 0u;
}

void host_battery_set_percentage(double pct)
{
    H.remaining_mah = HOST_BATTERY_CAPACITY_MAH * pct / 100.0;
}

void host_battery_set_charging(bool charging, double charge_ma)
{
    H.charging = charging;
    H.charge_ma = charge_ma;
}

double host_battery_percentage(void)
{
    return H.remaining_mah / HOST_BATTERY_CAPACITY_MAH * 100.0;
}

double host_battery_consumed_mah(void)
{
    return H.consumed_mah;
}

double host_battery_consumed_mah_in_mode(vs_mode_t mode)
{
    return H.consumed_by_mode[mode];
}

bool host_powered_off(void)
{
    return H.powered_off;
}

void host_rtc_set_epoch(uint32_t epoch_s_now)
{
    H.rtc_base_s = epoch_s_now - (uint32_t)(H.now_ms / 1000u);
}

void host_rtc_set_running(bool running)
{
    H.rtc_running = running;
}

uint32_t host_flash_violations(void)
{
    return H.flash_violations;
}

uint32_t host_wdt_resets(void)
{
    return H.wdt_resets;
}

/* ---------------- HAL implementation ---------------- */

uint64_t hal_time_ms(void)
{
    return H.now_ms;
}

bool hal_imu_read(hal_imu_sample_t *out)
{
    if (H.imu_fail) {
        return false;
    }
    if (H.imu_fn != NULL) {
        H.imu_fn(H.now_ms, out, H.imu_ctx);
    } else {
        *out = H.imu_static;
    }
    return true;
}

void hal_stim_boost_enable(bool enable)
{
    H.stim.boost_on = enable;
}

void hal_stim_set_amplitude_ua(uint32_t amplitude_ua)
{
    H.stim.amplitude_ua = amplitude_ua;
    if (amplitude_ua > H.stim.max_amplitude_ua) {
        H.stim.max_amplitude_ua = amplitude_ua;
    }
}

void hal_stim_set_train(bool enable, uint8_t frequency_hz, uint16_t pulse_width_us)
{
    H.stim.train_on = enable;
    H.stim.frequency_hz = frequency_hz;
    H.stim.pulse_width_us = pulse_width_us;
}

void hal_stim_cutoff(void)
{
    H.stim.boost_on = false;
    H.stim.train_on = false;
    H.stim.amplitude_ua = 0;
    H.stim.cutoff_count++;
    H.stim.last_cutoff_ms = H.now_ms;
}

bool hal_sense_read(hal_sense_sample_t *out)
{
    memset(out, 0, sizeof(*out));
    if (H.sense_fail) {
        return false;
    }
    out->status = H.sense_status;
    if (H.sense_status != HAL_SENSE_OK) {
        return true;
    }
    const uint32_t ua = H.current_override ? H.current_override_ua : host_stim_delivered_ua();
    out->current_ua = ua;
    out->voltage_mv = (uint32_t)(((uint64_t)ua * H.impedance_ohm) / 1000u);
    const int32_t phase_nc = (int32_t)(((uint64_t)ua * H.stim.pulse_width_us) / 1000u);
    out->phase1_charge_nc = phase_nc;
    out->phase2_charge_nc = -(phase_nc + H.imbalance_nc);
    return true;
}

uint32_t hal_battery_mv(void)
{
    if (H.remaining_mah > 0.0) {
        return battery_mv_from_percentage((float)host_battery_percentage());
    }
    const double reserve = HOST_BATTERY_CAPACITY_MAH * HOST_BATTERY_RESERVE_FRAC;
    double frac = -H.remaining_mah / reserve;
    if (frac > 1.0) {
        frac = 1.0;
    }
    return (uint32_t)(3000.0 - 250.0 * frac);
}

bool hal_power_is_charging(void)
{
    return H.charging;
}

void hal_power_shutdown(void)
{
    H.powered_off = true;
    H.stim.boost_on = false;
    H.stim.train_on = false;
    H.stim.amplitude_ua = 0;
}

bool hal_rtc_get_utc(uint32_t *epoch_s)
{
    if (!H.rtc_running) {
        *epoch_s = 0;
        return false;
    }
    *epoch_s = H.rtc_base_s + (uint32_t)(H.now_ms / 1000u);
    return true;
}

void hal_flash_read(uint32_t offset, void *dst, size_t len)
{
    memcpy(dst, &H.flash[offset], len);
}

void hal_flash_write(uint32_t offset, const void *src, size_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        if (H.flash[offset + i] != 0xFF) {
            H.flash_violations++;
        }
        H.flash[offset + i] &= s[i];
    }
}

void hal_flash_erase_page(uint32_t page_index)
{
    memset(&H.flash[page_index * VS_FLASH_PAGE_SIZE], 0xFF, VS_FLASH_PAGE_SIZE);
}

void hal_wdt_start(uint32_t period_ms)
{
    H.wdt_started = true;
    H.wdt_period_ms = period_ms;
    H.wdt_last_kick_ms = H.now_ms;
}

void hal_wdt_kick(void)
{
    H.wdt_last_kick_ms = H.now_ms;
}

hal_reset_reason_t hal_reset_reason(void)
{
    return H.reset_reason;
}
