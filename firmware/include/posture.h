#ifndef VS_POSTURE_H
#define VS_POSTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal.h"
#include "vs_types.h"

/* From the brief (section 3). */
#define POSTURE_STAND_THRESHOLD_G    0.3f
#define POSTURE_STAND_SAMPLES        300u          /* 3 s at 100 Hz */
#define POSTURE_WALK_THRESHOLD_G     0.5f
#define POSTURE_WALK_EXIT_SAMPLES    200u          /* < 0.3 g for 2 s leaves WALKING */
#define POSTURE_GYRO_CHANGES_WALK    3u            /* "more than 2" direction changes */
#define POSTURE_GYRO_WINDOW_SAMPLES  100u          /* within 1 s */
#define POSTURE_TILT_LIMIT_DEG       15.0f
#define POSTURE_SIT_STILL_SAMPLES    (5u * 60u * 100u)  /* 5 min still */
#define POSTURE_LPF_CUTOFF_HZ        0.5f

/* Engineering placeholders, not in the brief; tune on hardware. */
#define POSTURE_GYRO_DEADBAND_DPS    30.0f
#define POSTURE_STILL_ACCEL_G        0.05f
#define POSTURE_STILL_GYRO_DPS       5.0f

typedef struct {
    vs_posture_t state;
    bool initialised;
    float gravity[3];          /* low-passed acceleration, the gravity estimate */
    float vertical_lpf_g;      /* low-passed |vertical dynamic acceleration| */
    float dynamic_mag_g;       /* |a - gravity| for the latest sample */
    float tilt_deg;
    uint32_t sample_index;
    uint32_t stand_count;
    uint32_t walk_quiet_count;
    uint32_t still_count;
    int8_t gyro_sign[3];
    uint32_t gyro_changes[3][POSTURE_GYRO_CHANGES_WALK];
    uint8_t gyro_change_fill[3];
} posture_t;

void posture_init(posture_t *p);
vs_posture_t posture_update(posture_t *p, const hal_imu_sample_t *sample);

#endif
