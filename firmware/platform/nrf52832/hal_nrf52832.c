/*
 * nRF52832 binding of include/hal.h. NOT BUILT and not a flashing project:
 * each function records which peripheral it will use. Every body is a safe
 * placeholder (no output, sensors reported as failed) so that if this file
 * were linked by mistake the core would stay in Standby.
 */

#include <string.h>

#include "hal.h"
#include "peripheral_map.h"

/* RTC1 overflow-extended counter, 32768 Hz -> ms. */
uint64_t hal_time_ms(void)
{
    return 0;
}

/* TWIM0 burst read of ACCEL_XOUT_H..GYRO_ZOUT_L (14 bytes incl. TEMP_OUT).
 * Scale: +/-2 g -> 16384 LSB/g, +/-250 dps -> 131 LSB/dps, temp = raw/340 + 36.53 C.
 * Return false on TWIM error or timeout. */
bool hal_imu_read(hal_imu_sample_t *out)
{
    (void)out;
    return false;
}

/* VS_PIN_BOOST_EN; wait for boost rail settle before the first pulse. */
void hal_stim_boost_enable(bool enable)
{
    (void)enable;
}

/* Output DAC setpoint. The core already clips to 30 mA; the hardware current limit must too. */
void hal_stim_set_amplitude_ua(uint32_t amplitude_ua)
{
    (void)amplitude_ua;
}

/* TIMER1 + PPI + GPIOTE: period 1/frequency, phase A pulse_width_us, interphase gap, phase B pulse_width_us. */
void hal_stim_set_train(bool enable, uint8_t frequency_hz, uint16_t pulse_width_us)
{
    (void)enable;
    (void)frequency_hz;
    (void)pulse_width_us;
}

/* Drive VS_PIN_STIM_KILL, stop TIMER1, clear the DAC, drop boost. Must complete within one tick. */
void hal_stim_cutoff(void)
{
}

/* SAADC on the pulse plateau (PPI-triggered from TIMER1); per-phase charge integrated from the current samples. */
bool hal_sense_read(hal_sense_sample_t *out)
{
    out->status = HAL_SENSE_OPEN;
    out->voltage_mv = 0;
    out->current_ua = 0;
    out->phase1_charge_nc = 0;
    out->phase2_charge_nc = 0;
    return false;
}

/* SAADC VS_SAADC_CH_BATTERY through the board divider. */
uint32_t hal_battery_mv(void)
{
    return 0;
}

/* VS_PIN_CHARGE_STAT from the charger IC. */
bool hal_power_is_charging(void)
{
    return false;
}

/* Cut stimulation, then System OFF (wake on charger connect). */
void hal_power_shutdown(void)
{
}

/* UTC is held in RAM against RTC1 and set by the BLE client; false until set. */
bool hal_rtc_get_utc(uint32_t *epoch_s)
{
    *epoch_s = 0;
    return false;
}

/* NVMC (or sd_flash_write / sd_flash_page_erase with the SoftDevice) on the linker-reserved log region. */
void hal_flash_read(uint32_t offset, void *dst, size_t len)
{
    (void)offset;
    memset(dst, 0xFF, len);
}

void hal_flash_write(uint32_t offset, const void *src, size_t len)
{
    (void)offset;
    (void)src;
    (void)len;
}

void hal_flash_erase_page(uint32_t page_index)
{
    (void)page_index;
}

/* WDT: CRV = period_ms * 32.768, RREN0, run while CPU sleeps; it cannot be stopped once started. */
void hal_wdt_start(uint32_t period_ms)
{
    (void)period_ms;
}

void hal_wdt_kick(void)
{
}

/* POWER->RESETREAS: DOG bit -> HAL_RESET_WATCHDOG; clear the register after reading. */
hal_reset_reason_t hal_reset_reason(void)
{
    return HAL_RESET_POWER_ON;
}
