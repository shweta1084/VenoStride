#include "posture.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

static float lpf_alpha(void)
{
    const float dt = (float)VS_TICK_MS / 1000.0f;
    const float rc = 1.0f / (2.0f * PI_F * POSTURE_LPF_CUTOFF_HZ);
    return dt / (rc + dt);
}

void posture_init(posture_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = VS_POSTURE_UNKNOWN;
}

static void clear_gyro_history(posture_t *p)
{
    memset(p->gyro_sign, 0, sizeof(p->gyro_sign));
    memset(p->gyro_change_fill, 0, sizeof(p->gyro_change_fill));
}

/* Records sign reversals per gyro axis; true when one axis reverses more than twice within 1 s. */
static bool gyro_direction_walk(posture_t *p, const float gyro_dps[3])
{
    bool walking = false;
    for (int axis = 0; axis < 3; axis++) {
        const float g = gyro_dps[axis];
        if (fabsf(g) < POSTURE_GYRO_DEADBAND_DPS) {
            continue;
        }
        const int8_t sign = g > 0.0f ? 1 : -1;
        if (p->gyro_sign[axis] != 0 && sign != p->gyro_sign[axis]) {
            uint32_t *hist = p->gyro_changes[axis];
            if (p->gyro_change_fill[axis] < POSTURE_GYRO_CHANGES_WALK) {
                hist[p->gyro_change_fill[axis]++] = p->sample_index;
            } else {
                memmove(&hist[0], &hist[1], (POSTURE_GYRO_CHANGES_WALK - 1u) * sizeof(hist[0]));
                hist[POSTURE_GYRO_CHANGES_WALK - 1u] = p->sample_index;
            }
            if (p->gyro_change_fill[axis] == POSTURE_GYRO_CHANGES_WALK &&
                hist[POSTURE_GYRO_CHANGES_WALK - 1u] - hist[0] <= POSTURE_GYRO_WINDOW_SAMPLES) {
                walking = true;
            }
        }
        p->gyro_sign[axis] = sign;
    }
    return walking;
}

vs_posture_t posture_update(posture_t *p, const hal_imu_sample_t *s)
{
    const float alpha = lpf_alpha();

    if (!p->initialised) {
        memcpy(p->gravity, s->accel_g, sizeof(p->gravity));
        p->initialised = true;
        p->state = VS_POSTURE_IDLE;
    } else {
        for (int i = 0; i < 3; i++) {
            p->gravity[i] += alpha * (s->accel_g[i] - p->gravity[i]);
        }
    }
    p->sample_index++;

    const float gmag = sqrtf(p->gravity[0] * p->gravity[0] + p->gravity[1] * p->gravity[1] +
                             p->gravity[2] * p->gravity[2]);
    float unit[3] = {0.0f, 0.0f, 1.0f};
    if (gmag > 1e-3f) {
        for (int i = 0; i < 3; i++) {
            unit[i] = p->gravity[i] / gmag;
        }
    }

    float dyn[3];
    for (int i = 0; i < 3; i++) {
        dyn[i] = s->accel_g[i] - p->gravity[i];
    }
    p->dynamic_mag_g = sqrtf(dyn[0] * dyn[0] + dyn[1] * dyn[1] + dyn[2] * dyn[2]);
    const float vertical = dyn[0] * unit[0] + dyn[1] * unit[1] + dyn[2] * unit[2];
    p->vertical_lpf_g += alpha * (fabsf(vertical) - p->vertical_lpf_g);

    /* The insole's +Z axis points up when the foot is flat. */
    float cos_tilt = unit[2];
    if (cos_tilt > 1.0f) {
        cos_tilt = 1.0f;
    } else if (cos_tilt < -1.0f) {
        cos_tilt = -1.0f;
    }
    p->tilt_deg = acosf(cos_tilt) * 180.0f / PI_F;
    const bool upright = p->tilt_deg <= POSTURE_TILT_LIMIT_DEG;

    const float gyro_mag = sqrtf(s->gyro_dps[0] * s->gyro_dps[0] + s->gyro_dps[1] * s->gyro_dps[1] +
                                 s->gyro_dps[2] * s->gyro_dps[2]);
    const bool still = p->dynamic_mag_g < POSTURE_STILL_ACCEL_G && gyro_mag < POSTURE_STILL_GYRO_DPS;
    p->still_count = still ? p->still_count + 1u : 0u;

    const bool gyro_walk = gyro_direction_walk(p, s->gyro_dps);

    /* Walking has priority from every state: it is the safety-critical exit. */
    if (p->dynamic_mag_g > POSTURE_WALK_THRESHOLD_G || gyro_walk) {
        p->state = VS_POSTURE_WALKING;
        p->walk_quiet_count = 0;
        p->stand_count = 0;
        return p->state;
    }

    if (p->state == VS_POSTURE_WALKING) {
        if (p->dynamic_mag_g < POSTURE_STAND_THRESHOLD_G) {
            p->walk_quiet_count++;
        } else {
            p->walk_quiet_count = 0;
        }
        if (p->walk_quiet_count >= POSTURE_WALK_EXIT_SAMPLES) {
            p->state = VS_POSTURE_IDLE;
            p->stand_count = 0;
            clear_gyro_history(p);
        }
        return p->state;
    }

    if (p->state == VS_POSTURE_SITTING) {
        if (!still) {
            p->state = VS_POSTURE_IDLE;
            p->stand_count = 0;
        }
        return p->state;
    }

    if (!upright && p->still_count >= POSTURE_SIT_STILL_SAMPLES) {
        p->state = VS_POSTURE_SITTING;
        p->stand_count = 0;
        return p->state;
    }

    if (upright && p->vertical_lpf_g < POSTURE_STAND_THRESHOLD_G) {
        if (p->stand_count < POSTURE_STAND_SAMPLES) {
            p->stand_count++;
        }
    } else {
        p->stand_count = 0;
    }

    p->state = p->stand_count >= POSTURE_STAND_SAMPLES ? VS_POSTURE_STANDING : VS_POSTURE_IDLE;
    return p->state;
}
