#ifndef VS_NRF52832_PERIPHERAL_MAP_H
#define VS_NRF52832_PERIPHERAL_MAP_H

/*
 * nRF52832 resource plan for the HAL in include/hal.h. NOT BUILT.
 *
 * The brief names the parts (nRF52832, MPU6050, boost + current-controlled
 * output stage, LiPo) but no schematic, so every GPIO is unassigned. Fill
 * these in from the board schematic; do not guess.
 */

#define VS_PIN_UNASSIGNED 0xFFu

/* MPU6050 on TWIM0 (I2C, 400 kHz), data-ready interrupt on GPIOTE. */
#define VS_PIN_IMU_SCL          VS_PIN_UNASSIGNED
#define VS_PIN_IMU_SDA          VS_PIN_UNASSIGNED
#define VS_PIN_IMU_INT          VS_PIN_UNASSIGNED
#define VS_IMU_I2C_ADDR         0x68u /* AD0 low; 0x69 if AD0 is tied high */

/* Stimulator: boost enable GPIO, amplitude DAC (SPI or PWM+RC, board dependent),
 * biphasic H-bridge phases driven by TIMER1 + PPI so pulse edges do not depend on CPU latency. */
#define VS_PIN_BOOST_EN         VS_PIN_UNASSIGNED
#define VS_PIN_STIM_PHASE_A     VS_PIN_UNASSIGNED
#define VS_PIN_STIM_PHASE_B     VS_PIN_UNASSIGNED
#define VS_PIN_STIM_DAC_CS      VS_PIN_UNASSIGNED
/* Hardware cutoff line, must force the output stage off independently of firmware state. */
#define VS_PIN_STIM_KILL        VS_PIN_UNASSIGNED

/* Electrode sense on SAADC, sampled on the pulse plateau via PPI from TIMER1. */
#define VS_SAADC_CH_ELECTRODE_V 0u
#define VS_SAADC_CH_SENSE_I     1u
#define VS_SAADC_CH_BATTERY     2u
#define VS_PIN_CHARGE_STAT      VS_PIN_UNASSIGNED

/* Timebase: RTC1 (32.768 kHz) for hal_time_ms and the 10 ms tick; UTC kept in RAM, set over BLE. */
/* Watchdog: WDT with CRV for 2 s, RR0 kicked from the main loop. RESETREAS distinguishes DOG from power-on. */
/* Compliance log: 8 pages of internal flash via NVMC (SoftDevice: sd_flash_* instead), placed by the linker. */
/* BLE: SoftDevice S132 GATT server exposing the UUIDs in include/command.h. */

#endif
