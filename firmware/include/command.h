#ifndef VS_COMMAND_H
#define VS_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compliance.h"
#include "vs_types.h"

/*
 * Application side of the BLE interface (brief section 5). The radio/GATT
 * binding calls these; the host simulator calls them in-process.
 */

#define VS_BLE_DEVICE_NAME        "VenoStride"
#define VS_UUID_SERVICE           0x181Au
#define VS_UUID_DEVICE_STATUS     0x2A58u
#define VS_UUID_MODE_SELECT       0x0001u
#define VS_UUID_CURRENT_ADJUST    0x0002u
#define VS_UUID_COMPLIANCE_DATA   0x0003u
#define VS_UUID_BATTERY           0x2A19u   /* carries battery_percentage 0-100 */

#define CMD_PIN_LENGTH            4u
#define CMD_PIN_MAX_FAILURES      5u
#define CMD_PIN_LOCKOUT_MS        (15u * 60u * 1000u)

typedef enum {
    CMD_OK = 0,
    CMD_BAD_PIN,
    CMD_LOCKED_OUT,
    CMD_INVALID
} cmd_result_t;

typedef struct {
    char pin[CMD_PIN_LENGTH];
    uint8_t failures;
    bool locked;
    uint64_t locked_until_ms;
} cmd_auth_t;

typedef struct {
    vs_mode_t mode;                         /* live mode */
    uint8_t battery_percentage;
    vs_impedance_status_t impedance_status;
    vs_mode_t selected;
    vs_posture_t posture;
    uint8_t current_ma;                     /* effective setpoint incl. anti-habituation */
    uint32_t output_ua;                     /* amplitude being delivered right now */
    bool daily_cap_reached;
    uint32_t mode3_remaining_s;
    bool stim_locked;                       /* latched fault awaiting explicit re-enable */
    bool auth_locked;
    bool imu_fault;
} vs_device_status_t;

void cmd_auth_init(cmd_auth_t *auth, const char pin[CMD_PIN_LENGTH]);
/* Checks a clinician PIN, applying the 5-strike / 15-minute lockout and logging every attempt. */
cmd_result_t cmd_auth_check(cmd_auth_t *auth, const char pin[CMD_PIN_LENGTH], uint64_t now_ms, compliance_t *log);
bool cmd_auth_locked(const cmd_auth_t *auth, uint64_t now_ms);

/* ---- characteristic handlers (implemented against the core singleton) ---- */
void vs_cmd_get_status(vs_device_status_t *out);
cmd_result_t vs_cmd_select_mode(vs_mode_t mode, const char pin[CMD_PIN_LENGTH]);
/* Returns the current actually applied (15-30 mA). */
uint8_t vs_cmd_set_current(uint8_t current_ma);
size_t vs_cmd_read_compliance(uint32_t from_seq, compliance_record_t *out, size_t max);
uint8_t vs_cmd_battery_percentage(void);

/* Explicit re-enable after an impedance, charge-balance or sense-circuit halt. */
bool vs_cmd_reenable_stimulation(void);

const char *vs_cmd_firmware_version(void);
const char *vs_cmd_build_hash(void);

#endif
