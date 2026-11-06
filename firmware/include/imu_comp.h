#ifndef VS_IMU_COMP_H
#define VS_IMU_COMP_H

#include <stdbool.h>

#include "hal.h"
#include "vs_types.h"

/* Thermal compensation becomes active once the observed temperature span exceeds this (brief section 3). */
#define IMU_COMP_SPAN_THRESHOLD_C 10.0f

typedef struct {
    imu_comp_coeffs_t coeffs;
    float min_temp_c;
    float max_temp_c;
    bool seen_sample;
    bool thermal_active;
} imu_comp_t;

/* All-zero coefficients: no correction. Real values come from hardware calibration. */
void imu_comp_identity(imu_comp_coeffs_t *coeffs);

void imu_comp_init(imu_comp_t *ic, const imu_comp_coeffs_t *coeffs);
void imu_comp_set_coeffs(imu_comp_t *ic, const imu_comp_coeffs_t *coeffs);

/* Corrects the sample in place: static offset always, thermal slope once active. */
void imu_comp_apply(imu_comp_t *ic, hal_imu_sample_t *sample);

bool imu_comp_thermal_active(const imu_comp_t *ic);

#endif
