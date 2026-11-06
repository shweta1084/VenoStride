#ifndef VS_COMPLIANCE_H
#define VS_COMPLIANCE_H

#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/*
 * Compliance log: fixed 16-byte records in a 32 KB flash ring (brief section 9.3).
 * When the writer enters a page it erases it, dropping that page's (oldest) records.
 *
 * Record payloads by type:
 *   SESSION_START    arg = mode,             v16 = target current mA,   v32 = 0
 *   SESSION_END      arg = mode,             v16 = vs_stop_reason_t,    v32 = duration s
 *   IMPEDANCE        arg = impedance kind,   v16 = impedance ohm,       v32 = output current uA
 *   CHARGING         arg = charging edge,    v16 = battery percentage,  v32 = battery mV
 *   SAFETY_ALERT     arg = alert code,       v16 = detail,              v32 = detail
 *   MODE_TRANSITION  arg = from mode,        v16 = to mode,             v32 = transition reason
 */

typedef enum {
    COMPLIANCE_SESSION_START = 1,
    COMPLIANCE_SESSION_END = 2,
    COMPLIANCE_IMPEDANCE = 3,
    COMPLIANCE_CHARGING = 4,
    COMPLIANCE_SAFETY_ALERT = 5,
    COMPLIANCE_MODE_TRANSITION = 6
} compliance_type_t;

typedef enum {
    IMPEDANCE_KIND_PERIODIC = 0,       /* 30 s measurement */
    IMPEDANCE_KIND_HABITUATION = 1,    /* 30 min window average, no change */
    IMPEDANCE_KIND_HABITUATION_STEP = 2 /* 30 min window average, +1 mA applied (v32 = new setting uA) */
} compliance_impedance_kind_t;

typedef enum {
    CHARGING_STARTED = 0,
    CHARGING_ENDED = 1
} compliance_charging_edge_t;

typedef enum {
    ALERT_IMPEDANCE_WARNING = 1,   /* v16 = ohm */
    ALERT_IMPEDANCE_CRITICAL,      /* v16 = ohm */
    ALERT_CHARGE_IMBALANCE,        /* v32 = |net charge| nC */
    ALERT_SENSE_FAILURE,           /* v16 = hal_sense_status_t */
    ALERT_BATTERY_LOW_20,          /* v16 = percentage */
    ALERT_BATTERY_LOW_5,           /* v16 = percentage */
    ALERT_BATTERY_STIM_CUTOFF,     /* v16 = percentage, v32 = mV */
    ALERT_BATTERY_SHUTDOWN,        /* v32 = mV */
    ALERT_IMU_FAULT,
    ALERT_IMU_RECOVERED,
    ALERT_WATCHDOG_RESET,
    ALERT_RTC_INVALID,
    ALERT_AUTH_FAILED,             /* v16 = consecutive failures */
    ALERT_AUTH_LOCKOUT,            /* v32 = lockout ms */
    ALERT_STIM_REENABLED           /* v16 = vs_stop_reason_t that was latched */
} compliance_alert_t;

/* MODE_TRANSITION reasons; values below 0x100 are vs_stop_reason_t. */
#define TRANSITION_STANDING_DETECTED 0x100u
#define TRANSITION_CLINICIAN_SELECT  0x200u /* from/to are the selected therapy, not the live mode */
#define TRANSITION_BOOT              0x300u /* power-on or reset entry into Standby */

#define COMPLIANCE_RECORD_SIZE      16u
#define COMPLIANCE_SLOTS            (VS_FLASH_LOG_SIZE / COMPLIANCE_RECORD_SIZE)
#define COMPLIANCE_SLOTS_PER_PAGE   (VS_FLASH_PAGE_SIZE / COMPLIANCE_RECORD_SIZE)

typedef struct {
    uint32_t seq;
    uint32_t timestamp;   /* UTC seconds, VS_TIMESTAMP_UNKNOWN when the RTC is invalid */
    uint8_t type;
    uint8_t arg;
    uint16_t v16;
    uint32_t v32;
} compliance_record_t;

typedef struct {
    uint32_t next_seq;
    uint32_t write_slot;
    uint32_t count;
    uint32_t timestamp;
} compliance_t;

/* Recovers the ring head from flash contents. */
void compliance_init(compliance_t *c);
void compliance_format(compliance_t *c);

void compliance_set_time(compliance_t *c, uint32_t utc_s);
uint32_t compliance_append(compliance_t *c, compliance_type_t type, uint8_t arg, uint16_t v16, uint32_t v32);

uint32_t compliance_count(const compliance_t *c);
uint32_t compliance_oldest_seq(const compliance_t *c);
uint32_t compliance_newest_seq(const compliance_t *c);

/* Copies records with seq >= from_seq, oldest first. Returns the number copied. */
size_t compliance_read(const compliance_t *c, uint32_t from_seq, compliance_record_t *out, size_t max);

#endif
