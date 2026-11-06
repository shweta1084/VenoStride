#ifndef VS_CORE_H
#define VS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "battery.h"
#include "command.h"
#include "compliance.h"
#include "imu_comp.h"
#include "posture.h"
#include "safety.h"
#include "session.h"
#include "stim.h"
#include "vs_types.h"

#define VS_WDT_PERIOD_MS          2000u
#define VS_WDT_KICK_MS            1000u
#define VS_IMU_RECOVERY_SAMPLES   100u        /* 1 s of good reads before standing detection resumes */
#define VS_RTC_MIN_VALID_S        1704067200u /* 2024-01-01T00:00:00Z */
#define VS_RTC_MAX_VALID_S        4102444800u /* 2100-01-01T00:00:00Z */

typedef struct {
    vs_config_t cfg;
    compliance_t log;
    imu_comp_t imu_comp;
    posture_t posture;
    vs_posture_t posture_state;
    stim_t stim;
    session_t session;
    safety_t safety;
    battery_t battery;
    cmd_auth_t auth;

    bool imu_fault;
    uint32_t imu_good_count;

    bool rtc_valid;
    bool rtc_checked;
    uint32_t utc_s;
    bool utc_day_known;
    uint32_t utc_day;
    uint64_t last_day_reset_ms;

    uint64_t now_ms;
    uint64_t last_kick_ms;
    bool powered_off;
} vs_core_t;

/* Boot: outputs off, log recovered from flash, watchdog armed, Standby. */
void vs_core_init(const vs_config_t *cfg);

/* One 10 ms step, driven by the IMU data-ready interrupt (or the host simulator). */
void vs_core_tick(void);

vs_core_t *vs_core(void);

#endif
