#ifndef VS_HAL_H
#define VS_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Hardware boundary. The core never touches registers; each platform
 * (platform/host, platform/nrf52832) provides these functions.
 */

/* ---- time ---- */
uint64_t hal_time_ms(void);

/* ---- IMU (MPU6050) ---- */
typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    float temp_c;
} hal_imu_sample_t;

/* Returns false on I2C timeout / communication loss. */
bool hal_imu_read(hal_imu_sample_t *out);

/* ---- stimulator output stage (boost converter + current DAC + pulse timer) ---- */
void hal_stim_boost_enable(bool enable);
void hal_stim_set_amplitude_ua(uint32_t amplitude_ua);
void hal_stim_set_train(bool enable, uint8_t frequency_hz, uint16_t pulse_width_us);
/* Immediate hardware cutoff: amplitude 0, pulse train off, boost off. */
void hal_stim_cutoff(void);

/* ---- current / voltage sense on the electrode path ---- */
typedef enum {
    HAL_SENSE_OK = 0,
    HAL_SENSE_OPEN,
    HAL_SENSE_SHORT
} hal_sense_status_t;

typedef struct {
    hal_sense_status_t status;
    uint32_t voltage_mv;        /* electrode voltage during the pulse plateau */
    uint32_t current_ua;        /* measured current during the pulse plateau */
    int32_t phase1_charge_nc;   /* first (positive) phase charge */
    int32_t phase2_charge_nc;   /* second (negative) phase charge */
} hal_sense_sample_t;

bool hal_sense_read(hal_sense_sample_t *out);

/* ---- battery / power ---- */
uint32_t hal_battery_mv(void);
bool hal_power_is_charging(void);
void hal_power_shutdown(void);

/* ---- RTC ---- */
/* Returns false if the RTC is not running. Plausibility is judged by the core. */
bool hal_rtc_get_utc(uint32_t *epoch_s);

/* ---- compliance flash region (VS_FLASH_LOG_SIZE bytes, VS_FLASH_PAGE_SIZE pages) ---- */
#define VS_FLASH_PAGE_SIZE 4096u
#define VS_FLASH_LOG_PAGES 8u
#define VS_FLASH_LOG_SIZE  (VS_FLASH_PAGE_SIZE * VS_FLASH_LOG_PAGES)

void hal_flash_read(uint32_t offset, void *dst, size_t len);
/* NOR semantics: bits can only be cleared; target must be erased first. */
void hal_flash_write(uint32_t offset, const void *src, size_t len);
void hal_flash_erase_page(uint32_t page_index);

/* ---- watchdog ---- */
typedef enum {
    HAL_RESET_POWER_ON = 0,
    HAL_RESET_WATCHDOG
} hal_reset_reason_t;

void hal_wdt_start(uint32_t period_ms);
void hal_wdt_kick(void);
hal_reset_reason_t hal_reset_reason(void);

#endif
