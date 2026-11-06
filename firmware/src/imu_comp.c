#include "imu_comp.h"

#include <string.h>

void imu_comp_identity(imu_comp_coeffs_t *coeffs)
{
    memset(coeffs, 0, sizeof(*coeffs));
}

void imu_comp_init(imu_comp_t *ic, const imu_comp_coeffs_t *coeffs)
{
    memset(ic, 0, sizeof(*ic));
    imu_comp_set_coeffs(ic, coeffs);
}

void imu_comp_set_coeffs(imu_comp_t *ic, const imu_comp_coeffs_t *coeffs)
{
    if (coeffs != NULL) {
        ic->coeffs = *coeffs;
    } else {
        imu_comp_identity(&ic->coeffs);
    }
}

void imu_comp_apply(imu_comp_t *ic, hal_imu_sample_t *sample)
{
    if (!ic->seen_sample) {
        ic->min_temp_c = sample->temp_c;
        ic->max_temp_c = sample->temp_c;
        ic->seen_sample = true;
    } else {
        if (sample->temp_c < ic->min_temp_c) {
            ic->min_temp_c = sample->temp_c;
        }
        if (sample->temp_c > ic->max_temp_c) {
            ic->max_temp_c = sample->temp_c;
        }
    }
    if (ic->max_temp_c - ic->min_temp_c > IMU_COMP_SPAN_THRESHOLD_C) {
        ic->thermal_active = true;
    }

    const float dt = sample->temp_c - ic->coeffs.ref_temp_c;
    for (int i = 0; i < 3; i++) {
        sample->accel_g[i] -= ic->coeffs.accel_offset_g[i];
        if (ic->thermal_active) {
            sample->accel_g[i] -= ic->coeffs.accel_bias_g_per_c[i] * dt;
            sample->gyro_dps[i] -= ic->coeffs.gyro_bias_dps_per_c[i] * dt;
        }
    }
}

bool imu_comp_thermal_active(const imu_comp_t *ic)
{
    return ic->thermal_active;
}
