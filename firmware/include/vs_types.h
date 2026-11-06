#ifndef VS_TYPES_H
#define VS_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Values cited from the VenoStride Firmware + Software Brief v1.0 (2026-07-12). */

#define VS_TICK_MS                    10u      /* 100 Hz IMU sample period */

#define VS_CURRENT_MIN_MA             15u
#define VS_CURRENT_MAX_MA             30u      /* absolute ceiling, brief section 6 req. 1 */
#define VS_CURRENT_DEFAULT_MA         20u
#define VS_CURRENT_MAX_UA             (VS_CURRENT_MAX_MA * 1000u)
#define VS_PULSE_WIDTH_US             200u

#define VS_RAMP_UP_MS                 5000u
#define VS_RAMP_DOWN_MS               2000u

#define VS_MODE2_FREQUENCY_HZ         5u
#define VS_MODE2_ON_MS                3000u
#define VS_MODE2_OFF_MS               27000u

#define VS_MODE3_FREQUENCY_DEFAULT_HZ 7u
#define VS_MODE3_FREQUENCY_MIN_HZ     5u
#define VS_MODE3_FREQUENCY_MAX_HZ     10u
#define VS_MODE3_ON_MS                30000u
#define VS_MODE3_OFF_MS               30000u
#define VS_MODE3_BLOCK_MS             (30u * 60u * 1000u)
#define VS_MODE3_PAUSE_RAMP_DOWN_MS   1000u
#define VS_MODE3_PAUSE_MS             (2u * 60u * 1000u)
#define VS_MODE3_PAUSE_RAMP_UP_MS     2000u
#define VS_MODE3_DAILY_CAP_MS         (4u * 60u * 60u * 1000u)
#define VS_MODE3_MAX_INCREMENT_MA     8u
#define VS_HABITUATION_RISE_PERCENT   10u

#define VS_DAY_MS                     (24u * 60u * 60u * 1000u)
#define VS_UTC_DAY_S                  86400u
#define VS_TIMESTAMP_UNKNOWN          0xFFFFFFFFu

typedef enum {
    VS_MODE_STANDBY = 0,
    VS_MODE_CIRCULATION = 1,  /* Mode 2 */
    VS_MODE_METABOLIC = 2     /* Mode 3 */
} vs_mode_t;

typedef enum {
    VS_IMPEDANCE_NORMAL = 0,
    VS_IMPEDANCE_WARNING = 1,
    VS_IMPEDANCE_CRITICAL = 2
} vs_impedance_status_t;

typedef enum {
    VS_POSTURE_UNKNOWN = 0,
    VS_POSTURE_IDLE,
    VS_POSTURE_STANDING,
    VS_POSTURE_WALKING,
    VS_POSTURE_SITTING
} vs_posture_t;

/* Why a stimulation session ended; stored in SESSION_END and MODE_TRANSITION records. */
typedef enum {
    VS_STOP_NONE = 0,
    VS_STOP_WALKING,
    VS_STOP_USER,
    VS_STOP_SITTING,
    VS_STOP_DAILY_CAP,
    VS_STOP_LOW_BATTERY,
    VS_STOP_IMPEDANCE,
    VS_STOP_CHARGE_BALANCE,
    VS_STOP_SENSE_FAILURE,
    VS_STOP_IMU_FAULT,
    VS_STOP_SHUTDOWN
} vs_stop_reason_t;

typedef struct {
    float ref_temp_c;
    float accel_offset_g[3];
    float accel_bias_g_per_c[3];
    float gyro_bias_dps_per_c[3];
} imu_comp_coeffs_t;

typedef struct {
    char clinician_pin[4];
    uint8_t default_current_ma;
    uint8_t mode3_frequency_hz;
    imu_comp_coeffs_t imu_comp;
} vs_config_t;

void vs_config_defaults(vs_config_t *cfg);

static inline uint32_t vs_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif
