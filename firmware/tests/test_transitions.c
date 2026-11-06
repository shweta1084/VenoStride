#include "test_harness.h"
#include "test_support.h"

static uint32_t g_seed = 0xC0FFEEu;

static uint32_t rand_below(uint32_t n)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return (g_seed >> 8) % n;
}

static bool in_standby(void)
{
    return support_status().mode == VS_MODE_STANDBY;
}

static bool running(void)
{
    return session_running(&vs_core()->session);
}

/* Runs tick by tick, checking the output never exceeds the setpoint. */
static void run_checked(uint64_t ms, uint32_t limit_ua)
{
    for (uint64_t t = 0; t < ms; t += VS_TICK_MS) {
        host_run_ms(VS_TICK_MS);
        if (host_stim_delivered_ua() > limit_ua) {
            CHECK(host_stim_delivered_ua() <= limit_ua);
            return;
        }
    }
}

typedef struct {
    uint32_t starts;
    uint32_t ends;
    bool open;
    bool ok;
} log_walk_t;

/* Checks START/END pairing and their adjacent MODE_TRANSITION records over [from, newest]. */
static void walk_log(log_walk_t *w, uint32_t from)
{
    compliance_record_t buf[64];
    compliance_record_t prev = {0};
    bool have_prev = false;
    for (;;) {
        const size_t n = vs_cmd_read_compliance(from, buf, 64);
        for (size_t i = 0; i < n; i++) {
            const compliance_record_t *r = &buf[i];
            if (have_prev && r->seq != prev.seq + 1u) {
                w->ok = false;
            }
            if (r->type == COMPLIANCE_SESSION_START) {
                if (w->open || !have_prev || prev.type != COMPLIANCE_MODE_TRANSITION ||
                    prev.v32 != TRANSITION_STANDING_DETECTED || prev.v16 != r->arg) {
                    w->ok = false;
                }
                w->open = true;
                w->starts++;
            } else if (r->type == COMPLIANCE_SESSION_END) {
                if (!w->open) {
                    w->ok = false;
                }
                w->open = false;
                w->ends++;
            } else if (r->type == COMPLIANCE_MODE_TRANSITION && r->v32 < TRANSITION_STANDING_DETECTED) {
                if (!have_prev || prev.type != COMPLIANCE_SESSION_END || r->v16 != VS_MODE_STANDBY) {
                    w->ok = false;
                }
            }
            prev = *r;
            have_prev = true;
        }
        if (n < 64) {
            break;
        }
        from = buf[n - 1].seq + 1u;
    }
}

static void a_thousand_standby_mode2_round_trips(void)
{
    host_reset();
    host_boot(NULL);
    CHECK_EQ(vs_cmd_select_mode(VS_MODE_STANDBY, "0000"), CMD_OK);
    host_run_ms(100);

    log_walk_t w = {0, 0, false, true};
    uint32_t sessions_seen = 0;
    for (int i = 0; i < 1000; i++) {
        const uint32_t from = compliance_newest_seq(&vs_core()->log) + 1u;
        CHECK_EQ(vs_cmd_select_mode(VS_MODE_CIRCULATION, "0000"), CMD_OK);
        run_checked(rand_below(40000u / VS_TICK_MS) * VS_TICK_MS, 20000u);
        if (running()) {
            sessions_seen++;
        }
        CHECK_EQ(vs_cmd_select_mode(VS_MODE_STANDBY, "0000"), CMD_OK);
        CHECK(support_run_until(in_standby, VS_RAMP_DOWN_MS + 50u) != UINT64_MAX);
        CHECK_EQ(host_stim_delivered_ua(), 0);
        CHECK(!host_stim()->boost_on);
        CHECK_EQ(stim_envelope_ua(&vs_core()->stim), 0);
        run_checked(rand_below(1000u / VS_TICK_MS) * VS_TICK_MS, 0u);
        walk_log(&w, from);
        if (th_failures > 0) {
            return;
        }
    }
    CHECK(w.ok);
    CHECK(!w.open);
    CHECK_EQ(w.starts, w.ends);
    CHECK_EQ(w.starts, sessions_seen);
    CHECK(sessions_seen > 900);
    CHECK(host_stim()->max_amplitude_ua <= 20000u);
    CHECK_EQ(host_flash_violations(), 0);
    printf("  %u sessions started and ended\n", (unsigned)sessions_seen);
}

static void switching_mode2_to_mode3_while_standing(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    host_run_ms(6000);
    const uint32_t from = compliance_newest_seq(&vs_core()->log) + 1u;

    CHECK_EQ(vs_cmd_select_mode(VS_MODE_METABOLIC, "0000"), CMD_OK);
    host_run_ms(VS_TICK_MS);
    CHECK_EQ(vs_core()->session.state, SESSION_RAMP_DOWN);
    CHECK_EQ(support_status().mode, VS_MODE_CIRCULATION);
    run_checked(VS_RAMP_DOWN_MS + 20u, 20000u);
    CHECK_EQ(support_status().mode, VS_MODE_METABOLIC);
    host_run_ms(VS_RAMP_UP_MS + 100u);
    CHECK_EQ(host_stim()->frequency_hz, VS_MODE3_FREQUENCY_DEFAULT_HZ);
    CHECK_EQ(host_stim_delivered_ua(), 20000u);

    compliance_record_t r[8];
    const size_t n = vs_cmd_read_compliance(from, r, 8);
    CHECK(n >= 5);
    CHECK_EQ(r[0].type, COMPLIANCE_MODE_TRANSITION); /* clinician select 2 -> 3 */
    CHECK_EQ(r[0].v32, TRANSITION_CLINICIAN_SELECT);
    CHECK_EQ(r[1].type, COMPLIANCE_SESSION_END);
    CHECK_EQ(r[1].v16, VS_STOP_USER);
    CHECK_EQ(r[2].type, COMPLIANCE_MODE_TRANSITION);
    CHECK_EQ(r[2].arg, VS_MODE_CIRCULATION);
    CHECK_EQ(r[2].v16, VS_MODE_STANDBY);
    CHECK_EQ(r[3].type, COMPLIANCE_MODE_TRANSITION);
    CHECK_EQ(r[3].v16, VS_MODE_METABOLIC);
    CHECK_EQ(r[4].type, COMPLIANCE_SESSION_START);
    CHECK_EQ(r[4].arg, VS_MODE_METABOLIC);
}

static void reselecting_during_ramp_down_finishes_the_ramp_first(void)
{
    support_boot_with_mode(VS_MODE_CIRCULATION);
    CHECK(support_run_until(running, 5000) != UINT64_MAX);
    host_run_ms(60500); /* inside the third burst, after the entry ramp */
    CHECK_EQ(host_stim_delivered_ua(), 20000u);

    vs_cmd_select_mode(VS_MODE_STANDBY, "0000");
    host_run_ms(1000);
    const uint32_t mid = host_stim_delivered_ua();
    CHECK(mid > 0 && mid < 20000u);
    vs_cmd_select_mode(VS_MODE_CIRCULATION, "0000");
    host_run_ms(500);
    CHECK(host_stim_delivered_ua() < mid);
    host_run_ms(1000);
    CHECK_EQ(support_count_records(COMPLIANCE_SESSION_END, -1), 1);
    CHECK_EQ(support_count_records(COMPLIANCE_SESSION_START, -1), 2);
    CHECK(running());
}

int main(void)
{
    RUN_TEST(a_thousand_standby_mode2_round_trips);
    RUN_TEST(switching_mode2_to_mode3_while_standing);
    RUN_TEST(reselecting_during_ramp_down_finishes_the_ramp_first);
    return th_finish();
}
