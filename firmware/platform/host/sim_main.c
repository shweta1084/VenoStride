#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"
#include "host.h"
#include "host_trace.h"
#include "vs_core.h"

/*
 * Host simulator: boots the firmware core against the host HAL, optionally
 * replays an IMU CSV trace, and prints status and every compliance record.
 */

typedef struct {
    const char *scenario;
    bool loop;
    double duration_s;
    bool has_mode;
    vs_mode_t mode;
    bool has_pin;
    char pin[CMD_PIN_LENGTH];
    int current_ma;
    long impedance_ohm;
    double battery_pct;
    const char *config_path;
    double status_every_s;
    bool dump_log;
    bool has_epoch;
    unsigned long epoch;
    bool rtc_invalid;
} sim_options_t;

static void usage(void)
{
    printf("usage: venostride_sim [options]\n"
           "  --scenario FILE      IMU CSV trace (ax,ay,az,gx,gy,gz,temp per 10 ms); default: standing still\n"
           "  --loop               repeat the scenario instead of holding its last row\n"
           "  --duration-s N       virtual seconds to run (default: scenario length, or 60)\n"
           "  --mode standby|2|3   clinician mode selection after boot\n"
           "  --pin XXXX           PIN used for the mode selection (default: configured PIN)\n"
           "  --current MA         therapy current, clamped to 15-30 mA\n"
           "  --impedance OHM      electrode impedance (default 1500)\n"
           "  --battery PCT        starting battery percentage (default 100)\n"
           "  --config FILE        key=value config (see venostride_sim.example.cfg)\n"
           "  --status-every-s N   status line period (default 1; 0 = off)\n"
           "  --epoch S            RTC UTC seconds at boot (default 2026-11-04T00:00:00Z)\n"
           "  --rtc-invalid        boot with the RTC not running\n"
           "  --dump-log           print the whole compliance log at the end\n");
}

static bool parse_mode(const char *v, vs_mode_t *out)
{
    if (strcmp(v, "standby") == 0 || strcmp(v, "0") == 0 || strcmp(v, "1") == 0) {
        *out = VS_MODE_STANDBY;
    } else if (strcmp(v, "2") == 0) {
        *out = VS_MODE_CIRCULATION;
    } else if (strcmp(v, "3") == 0) {
        *out = VS_MODE_METABOLIC;
    } else {
        return false;
    }
    return true;
}

static bool copy_pin(const char *v, char out[CMD_PIN_LENGTH])
{
    if (strlen(v) != CMD_PIN_LENGTH) {
        return false;
    }
    memcpy(out, v, CMD_PIN_LENGTH);
    return true;
}

static bool parse_args(int argc, char **argv, sim_options_t *o)
{
    memset(o, 0, sizeof(*o));
    o->current_ma = -1;
    o->impedance_ohm = -1;
    o->battery_pct = -1.0;
    o->status_every_s = 1.0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(a, "--loop") == 0) {
            o->loop = true;
            continue;
        }
        if (strcmp(a, "--dump-log") == 0) {
            o->dump_log = true;
            continue;
        }
        if (strcmp(a, "--rtc-invalid") == 0) {
            o->rtc_invalid = true;
            continue;
        }
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            return false;
        }
        if (v == NULL) {
            fprintf(stderr, "missing value for %s\n", a);
            return false;
        }
        i++;
        if (strcmp(a, "--scenario") == 0) {
            o->scenario = v;
        } else if (strcmp(a, "--duration-s") == 0) {
            o->duration_s = strtod(v, NULL);
        } else if (strcmp(a, "--mode") == 0) {
            if (!parse_mode(v, &o->mode)) {
                fprintf(stderr, "unknown mode '%s'\n", v);
                return false;
            }
            o->has_mode = true;
        } else if (strcmp(a, "--pin") == 0) {
            if (!copy_pin(v, o->pin)) {
                fprintf(stderr, "PIN must be %u characters\n", CMD_PIN_LENGTH);
                return false;
            }
            o->has_pin = true;
        } else if (strcmp(a, "--current") == 0) {
            o->current_ma = atoi(v);
        } else if (strcmp(a, "--impedance") == 0) {
            o->impedance_ohm = strtol(v, NULL, 10);
        } else if (strcmp(a, "--battery") == 0) {
            o->battery_pct = strtod(v, NULL);
        } else if (strcmp(a, "--config") == 0) {
            o->config_path = v;
        } else if (strcmp(a, "--status-every-s") == 0) {
            o->status_every_s = strtod(v, NULL);
        } else if (strcmp(a, "--epoch") == 0) {
            o->epoch = strtoul(v, NULL, 10);
            o->has_epoch = true;
        } else {
            fprintf(stderr, "unknown option %s\n", a);
            return false;
        }
    }
    return true;
}

static bool parse_vec3(const char *v, float out[3])
{
    return sscanf(v, "%f , %f , %f", &out[0], &out[1], &out[2]) == 3;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

static bool load_config(const char *path, vs_config_t *cfg)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open config %s\n", path);
        return false;
    }
    char line[256];
    int line_no = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        line_no++;
        char *s = trim(line);
        if (*s == '\0' || *s == '#') {
            continue;
        }
        char *eq = strchr(s, '=');
        if (eq == NULL) {
            fprintf(stderr, "%s:%d: expected key=value\n", path, line_no);
            ok = false;
            continue;
        }
        *eq = '\0';
        const char *key = trim(s);
        const char *val = trim(eq + 1);
        bool good = true;
        if (strcmp(key, "pin") == 0) {
            good = copy_pin(val, cfg->clinician_pin);
        } else if (strcmp(key, "default_current_ma") == 0) {
            cfg->default_current_ma = (uint8_t)atoi(val);
        } else if (strcmp(key, "mode3_frequency_hz") == 0) {
            cfg->mode3_frequency_hz = (uint8_t)atoi(val);
        } else if (strcmp(key, "imu_ref_temp_c") == 0) {
            cfg->imu_comp.ref_temp_c = (float)strtod(val, NULL);
        } else if (strcmp(key, "imu_accel_offset_g") == 0) {
            good = parse_vec3(val, cfg->imu_comp.accel_offset_g);
        } else if (strcmp(key, "imu_accel_bias_g_per_c") == 0) {
            good = parse_vec3(val, cfg->imu_comp.accel_bias_g_per_c);
        } else if (strcmp(key, "imu_gyro_bias_dps_per_c") == 0) {
            good = parse_vec3(val, cfg->imu_comp.gyro_bias_dps_per_c);
        } else {
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path, line_no, key);
            good = false;
        }
        if (!good) {
            fprintf(stderr, "%s:%d: bad value for '%s'\n", path, line_no, key);
            ok = false;
        }
    }
    fclose(f);
    return ok;
}

static const char *mode_name(vs_mode_t m)
{
    switch (m) {
    case VS_MODE_STANDBY: return "Standby";
    case VS_MODE_CIRCULATION: return "Mode2";
    case VS_MODE_METABOLIC: return "Mode3";
    }
    return "?";
}

static const char *posture_name(vs_posture_t p)
{
    switch (p) {
    case VS_POSTURE_UNKNOWN: return "UNKNOWN";
    case VS_POSTURE_IDLE: return "IDLE";
    case VS_POSTURE_STANDING: return "STANDING";
    case VS_POSTURE_WALKING: return "WALKING";
    case VS_POSTURE_SITTING: return "SITTING";
    }
    return "?";
}

static const char *impedance_name(vs_impedance_status_t s)
{
    switch (s) {
    case VS_IMPEDANCE_NORMAL: return "NORMAL";
    case VS_IMPEDANCE_WARNING: return "WARNING";
    case VS_IMPEDANCE_CRITICAL: return "CRITICAL";
    }
    return "?";
}

static const char *stop_name(uint32_t r)
{
    static const char *names[] = {"none", "walking", "user", "sitting", "daily-cap", "low-battery",
                                  "impedance", "charge-balance", "sense-failure", "imu-fault", "shutdown"};
    return r < sizeof(names) / sizeof(names[0]) ? names[r] : "?";
}

static const char *alert_name(uint8_t a)
{
    static const char *names[] = {"?", "impedance-warning", "impedance-critical", "charge-imbalance",
                                  "sense-failure", "battery-20%", "battery-5%", "battery-stim-cutoff",
                                  "battery-shutdown", "imu-fault", "imu-recovered", "watchdog-reset",
                                  "rtc-invalid", "auth-failed", "auth-lockout", "stim-reenabled"};
    return a < sizeof(names) / sizeof(names[0]) ? names[a] : "?";
}

static const char *result_name(cmd_result_t r)
{
    switch (r) {
    case CMD_OK: return "OK";
    case CMD_BAD_PIN: return "BAD_PIN";
    case CMD_LOCKED_OUT: return "LOCKED_OUT";
    case CMD_INVALID: return "INVALID";
    }
    return "?";
}

static void print_record(const compliance_record_t *r)
{
    char when[32];
    if (r->timestamp == VS_TIMESTAMP_UNKNOWN) {
        snprintf(when, sizeof(when), "t=unknown");
    } else {
        snprintf(when, sizeof(when), "utc=%lu", (unsigned long)r->timestamp);
    }
    printf("  #%-6lu %-14s ", (unsigned long)r->seq, when);
    switch (r->type) {
    case COMPLIANCE_SESSION_START:
        printf("SESSION_START   %s at %u mA\n", mode_name((vs_mode_t)r->arg), r->v16);
        break;
    case COMPLIANCE_SESSION_END:
        printf("SESSION_END     %s after %lu s (%s)\n", mode_name((vs_mode_t)r->arg), (unsigned long)r->v32,
               stop_name(r->v16));
        break;
    case COMPLIANCE_IMPEDANCE: {
        static const char *kinds[] = {"periodic", "habituation", "habituation-step"};
        printf("IMPEDANCE       %u ohm at %lu uA (%s)\n", r->v16, (unsigned long)r->v32,
               r->arg < 3 ? kinds[r->arg] : "?");
        break;
    }
    case COMPLIANCE_CHARGING:
        printf("CHARGING        %s at %u%% / %lu mV\n", r->arg == CHARGING_STARTED ? "started" : "ended", r->v16,
               (unsigned long)r->v32);
        break;
    case COMPLIANCE_SAFETY_ALERT:
        printf("SAFETY_ALERT    %s (%u, %lu)\n", alert_name(r->arg), r->v16, (unsigned long)r->v32);
        break;
    case COMPLIANCE_MODE_TRANSITION: {
        const char *why = r->v32 == TRANSITION_STANDING_DETECTED ? "standing detected"
                          : r->v32 == TRANSITION_CLINICIAN_SELECT ? "clinician select"
                          : r->v32 == TRANSITION_BOOT              ? "boot"
                                                                    : stop_name(r->v32);
        printf("MODE_TRANSITION %s -> %s (%s)\n", mode_name((vs_mode_t)r->arg), mode_name((vs_mode_t)r->v16), why);
        break;
    }
    default:
        printf("type %u\n", r->type);
        break;
    }
}

/* Prints records with seq >= *next and advances it. */
static void print_new_records(uint32_t *next)
{
    compliance_record_t buf[32];
    for (;;) {
        const size_t n = vs_cmd_read_compliance(*next, buf, 32);
        for (size_t i = 0; i < n; i++) {
            print_record(&buf[i]);
            *next = buf[i].seq + 1u;
        }
        if (n < 32) {
            return;
        }
    }
}

static void print_status(void)
{
    vs_device_status_t st;
    vs_cmd_get_status(&st);
    printf("[%9.2f s] mode=%-7s sel=%-7s posture=%-8s out=%5.1f mA set=%2u mA batt=%3u%% imp=%-8s "
           "m3_left=%5lus%s%s%s\n",
           (double)host_now_ms() / 1000.0, mode_name(st.mode), mode_name(st.selected), posture_name(st.posture),
           (double)host_stim_delivered_ua() / 1000.0, st.current_ma, st.battery_percentage,
           impedance_name(st.impedance_status), (unsigned long)st.mode3_remaining_s,
           st.stim_locked ? " STIM-LOCKED" : "", st.auth_locked ? " AUTH-LOCKED" : "",
           st.imu_fault ? " IMU-FAULT" : "");
}

int main(int argc, char **argv)
{
    sim_options_t o;
    if (!parse_args(argc, argv, &o)) {
        usage();
        return 2;
    }

    vs_config_t cfg;
    vs_config_defaults(&cfg);
    if (o.config_path != NULL && !load_config(o.config_path, &cfg)) {
        return 2;
    }

    host_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    if (o.scenario != NULL && !host_trace_load(&trace, o.scenario)) {
        fprintf(stderr, "cannot load scenario %s\n", o.scenario);
        return 2;
    }

    host_reset();
    if (o.battery_pct >= 0.0) {
        host_battery_set_percentage(o.battery_pct);
    }
    if (o.impedance_ohm >= 0) {
        host_sense_set_impedance((uint32_t)o.impedance_ohm);
    }
    if (o.has_epoch) {
        host_rtc_set_epoch((uint32_t)o.epoch);
    }
    if (o.rtc_invalid) {
        host_rtc_set_running(false);
    }

    printf("VenoStride host simulator %s (%s)\n", vs_cmd_firmware_version(), vs_cmd_build_hash());
    host_boot(&cfg);
    if (o.scenario != NULL) {
        host_trace_install(&trace, o.loop);
        printf("scenario %s: %.2f s%s\n", o.scenario, (double)host_trace_duration_ms(&trace) / 1000.0,
               o.loop ? ", looping" : "");
    }

    uint32_t next_seq = 0;
    print_new_records(&next_seq);

    if (o.has_mode) {
        const char *pin = o.has_pin ? o.pin : cfg.clinician_pin;
        const cmd_result_t r = vs_cmd_select_mode(o.mode, pin);
        printf("select %s: %s\n", mode_name(o.mode), result_name(r));
    }
    if (o.current_ma >= 0) {
        const int requested = o.current_ma > 255 ? 255 : o.current_ma;
        printf("current %d mA: applied %u mA\n", o.current_ma, vs_cmd_set_current((uint8_t)requested));
    }
    print_new_records(&next_seq);

    double duration_s = o.duration_s;
    if (duration_s <= 0.0) {
        duration_s = o.scenario != NULL && !o.loop ? (double)host_trace_duration_ms(&trace) / 1000.0 : 60.0;
    }
    const uint64_t end_ms = host_now_ms() + (uint64_t)(duration_s * 1000.0);
    const uint64_t status_ms = (uint64_t)(o.status_every_s * 1000.0);
    uint64_t next_status = host_now_ms();

    vs_posture_t last_posture = vs_core()->posture_state;
    while (host_now_ms() < end_ms && !host_powered_off()) {
        host_run_ms(VS_TICK_MS);
        if (vs_core()->posture_state != last_posture) {
            last_posture = vs_core()->posture_state;
            printf("[%9.2f s] posture -> %s\n", (double)host_now_ms() / 1000.0, posture_name(last_posture));
        }
        print_new_records(&next_seq);
        if (status_ms > 0 && host_now_ms() >= next_status) {
            print_status();
            next_status += status_ms;
        }
    }
    if (host_powered_off()) {
        printf("device powered off at %.2f s\n", (double)host_now_ms() / 1000.0);
    }
    if (status_ms == 0 || host_now_ms() + status_ms != next_status) {
        print_status();
    }

    if (o.dump_log) {
        printf("compliance log (%lu records, seq %lu..%lu):\n", (unsigned long)compliance_count(&vs_core()->log),
               (unsigned long)compliance_oldest_seq(&vs_core()->log),
               (unsigned long)compliance_newest_seq(&vs_core()->log));
        uint32_t from = 0;
        print_new_records(&from);
    }
    host_trace_free(&trace);
    return 0;
}
