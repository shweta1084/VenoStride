#ifndef VS_HOST_H
#define VS_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "hal.h"
#include "vs_types.h"

/*
 * Host (Windows/Linux) stand-in for the board. Time is virtual: host_run_ms()
 * advances it 10 ms per firmware tick, so hours of operation run in seconds.
 */

/* Power budget figures quoted in the brief (section 2), used to drain the simulated cell. */
#define HOST_BATTERY_CAPACITY_MAH  425.0
#define HOST_STANDBY_MA            5.0
#define HOST_STIM_BASELINE_MA      11.8
#define HOST_MODE2_STIM_MA         0.046
#define HOST_MODE3_STIM_MA         0.32
/* Charge left below the 3.0 V point before the 2.8 V shutdown, as a fraction of capacity. */
#define HOST_BATTERY_RESERVE_FRAC  0.02

typedef void (*host_imu_source_fn)(uint64_t t_ms, hal_imu_sample_t *out, void *ctx);

typedef struct {
    bool boost_on;
    bool train_on;
    uint32_t amplitude_ua;
    uint8_t frequency_hz;
    uint16_t pulse_width_us;
    uint32_t cutoff_count;
    uint64_t last_cutoff_ms;
    uint32_t max_amplitude_ua;
} host_stim_state_t;

/* Fresh hardware: time 0, erased flash, full battery, standing IMU, 1.5 kOhm electrodes, RTC valid. */
void host_reset(void);

/* Power-on boot of the firmware with the given config (NULL for defaults). */
void host_boot(const vs_config_t *cfg);

/* Runs firmware ticks for the given virtual time. Stops early if the device powered off. */
void host_run_ms(uint64_t ms);
/* Advances time without ticking firmware (simulated hang); the watchdog may reset the device. */
void host_hang_ms(uint64_t ms);
uint64_t host_now_ms(void);

/* ---- IMU ---- */
void host_imu_set_static(float ax, float ay, float az, float temp_c);
void host_imu_set_source(host_imu_source_fn fn, void *ctx);
void host_imu_set_fail(bool fail);

/* ---- electrode sense ---- */
void host_sense_set_impedance(uint32_t ohm);
void host_sense_set_status(hal_sense_status_t status);
void host_sense_set_imbalance_nc(int32_t extra_nc);
/* Replaces the measured current. The commanded output is unchanged. Cleared by host_reset(). */
void host_sense_set_current_ua(uint32_t current_ua);
/* Makes hal_sense_read() return false. Cleared by host_reset(). */
void host_sense_set_fail(bool fail);

/* ---- stimulator observation ---- */
const host_stim_state_t *host_stim(void);
uint32_t host_stim_delivered_ua(void);

/* ---- battery ---- */
void host_battery_set_percentage(double pct);
void host_battery_set_charging(bool charging, double charge_ma);
double host_battery_percentage(void);
double host_battery_consumed_mah(void);
double host_battery_consumed_mah_in_mode(vs_mode_t mode);
bool host_powered_off(void);

/* ---- RTC ---- */
void host_rtc_set_epoch(uint32_t epoch_s_now);
void host_rtc_set_running(bool running);

/* ---- flash / watchdog ---- */
uint32_t host_flash_violations(void);
uint32_t host_wdt_resets(void);

#endif
