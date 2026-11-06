#include <math.h>
#include <string.h>

#include "posture.h"
#include "test_harness.h"
#include "test_support.h"

#define PI_F 3.14159265f

static hal_imu_sample_t upright(float tilt_deg)
{
    hal_imu_sample_t s;
    memset(&s, 0, sizeof(s));
    const float rad = tilt_deg * PI_F / 180.0f;
    s.accel_g[1] = sinf(rad);
    s.accel_g[2] = cosf(rad);
    s.temp_c = 25.0f;
    return s;
}

static hal_imu_sample_t gait(uint32_t i)
{
    hal_imu_sample_t s = upright(0.0f);
    const float w = 2.0f * PI_F * 1.8f * (float)i / 100.0f;
    s.accel_g[0] = 0.4f * sinf(w + 1.0f);
    s.accel_g[2] = 1.0f + sinf(w);
    s.gyro_dps[1] = 200.0f * sinf(w);
    return s;
}

/* Feeds n copies of a sample; returns the sample index (1-based) where `want` first appeared, or 0. */
static uint32_t feed(posture_t *p, hal_imu_sample_t s, uint32_t n, vs_posture_t want)
{
    uint32_t first = 0;
    for (uint32_t i = 1; i <= n; i++) {
        if (posture_update(p, &s) == want && first == 0) {
            first = i;
        }
    }
    return first;
}

static void standing_needs_three_seconds_of_quiet(void)
{
    posture_t p;
    posture_init(&p);
    const hal_imu_sample_t s = upright(0.0f);
    for (uint32_t i = 1; i < POSTURE_STAND_SAMPLES; i++) {
        CHECK(posture_update(&p, &s) != VS_POSTURE_STANDING);
    }
    CHECK_EQ(posture_update(&p, &s), VS_POSTURE_STANDING);
}

static void foot_placement_within_15_degrees_still_stands(void)
{
    posture_t p;
    posture_init(&p);
    CHECK(feed(&p, upright(12.0f), 400, VS_POSTURE_STANDING) == POSTURE_STAND_SAMPLES);

    posture_init(&p);
    CHECK_EQ(feed(&p, upright(20.0f), 1000, VS_POSTURE_STANDING), 0);
}

static void walking_is_flagged_within_100_ms_of_gait_onset(void)
{
    posture_t p;
    posture_init(&p);
    feed(&p, upright(0.0f), 400, VS_POSTURE_STANDING);
    uint32_t detected = 0;
    for (uint32_t i = 0; i < 100 && detected == 0; i++) {
        const hal_imu_sample_t s = gait(i);
        if (posture_update(&p, &s) == VS_POSTURE_WALKING) {
            detected = i + 1;
        }
    }
    CHECK(detected > 0);
    CHECK(detected <= 10);
}

static void walking_holds_for_two_quiet_seconds_then_standing_needs_three_more(void)
{
    posture_t p;
    posture_init(&p);
    feed(&p, upright(0.0f), 400, VS_POSTURE_STANDING);
    for (uint32_t i = 0; i < 500; i++) {
        const hal_imu_sample_t s = gait(i);
        posture_update(&p, &s);
    }
    CHECK_EQ(p.state, VS_POSTURE_WALKING);

    const hal_imu_sample_t still = upright(0.0f);
    uint32_t left_walking = 0;
    uint32_t standing = 0;
    for (uint32_t i = 1; i <= 1000; i++) {
        const vs_posture_t st = posture_update(&p, &still);
        if (left_walking == 0 && st != VS_POSTURE_WALKING) {
            left_walking = i;
        }
        if (standing == 0 && st == VS_POSTURE_STANDING) {
            standing = i;
        }
    }
    CHECK(left_walking >= POSTURE_WALK_EXIT_SAMPLES);
    CHECK(left_walking <= POSTURE_WALK_EXIT_SAMPLES + 50);
    CHECK_EQ(standing - left_walking, POSTURE_STAND_SAMPLES);
}

static void gyro_direction_changes_alone_flag_walking(void)
{
    posture_t p;
    posture_init(&p);
    feed(&p, upright(0.0f), 400, VS_POSTURE_STANDING);
    uint32_t detected = 0;
    for (uint32_t i = 0; i < 200 && detected == 0; i++) {
        hal_imu_sample_t s = upright(0.0f);
        s.gyro_dps[0] = 100.0f * sinf(2.0f * PI_F * 2.0f * (float)i / 100.0f);
        if (posture_update(&p, &s) == VS_POSTURE_WALKING) {
            detected = i + 1;
        }
    }
    CHECK(detected > 0);
    CHECK(detected <= 100);

    /* One reversal per second is not walking. */
    posture_init(&p);
    feed(&p, upright(0.0f), 400, VS_POSTURE_STANDING);
    bool walked = false;
    for (uint32_t i = 0; i < 1000; i++) {
        hal_imu_sample_t s = upright(0.0f);
        s.gyro_dps[0] = 100.0f * sinf(2.0f * PI_F * 0.5f * (float)i / 100.0f);
        walked |= posture_update(&p, &s) == VS_POSTURE_WALKING;
    }
    CHECK(!walked);
}

static void tilted_and_still_for_five_minutes_is_sitting(void)
{
    posture_t p;
    posture_init(&p);
    const hal_imu_sample_t s = upright(60.0f);
    const uint32_t sitting_at = feed(&p, s, POSTURE_SIT_STILL_SAMPLES + 10, VS_POSTURE_SITTING);
    CHECK_EQ(sitting_at, POSTURE_SIT_STILL_SAMPLES);
    CHECK(p.state == VS_POSTURE_SITTING);

    /* Motion leaves SITTING. */
    hal_imu_sample_t moved = s;
    moved.gyro_dps[2] = 20.0f;
    CHECK_EQ(posture_update(&p, &moved), VS_POSTURE_IDLE);
}

static void noisy_standing_is_still_standing(void)
{
    trace_t t;
    trace_segment_t seg = {TRACE_STAND, 0};
    trace_init(&t, &seg, 1);
    t.noise_g = 0.05f;
    posture_t p;
    posture_init(&p);
    uint32_t standing = 0;
    for (uint32_t i = 0; i < 600 && standing == 0; i++) {
        hal_imu_sample_t s;
        trace_sample(t.start_ms + (uint64_t)i * VS_TICK_MS, &s, &t);
        if (posture_update(&p, &s) == VS_POSTURE_STANDING) {
            standing = i + 1;
        }
    }
    CHECK(standing > 0);
    CHECK(standing <= 400);
}

int main(void)
{
    RUN_TEST(standing_needs_three_seconds_of_quiet);
    RUN_TEST(foot_placement_within_15_degrees_still_stands);
    RUN_TEST(walking_is_flagged_within_100_ms_of_gait_onset);
    RUN_TEST(walking_holds_for_two_quiet_seconds_then_standing_needs_three_more);
    RUN_TEST(gyro_direction_changes_alone_flag_walking);
    RUN_TEST(tilted_and_still_for_five_minutes_is_sitting);
    RUN_TEST(noisy_standing_is_still_standing);
    return th_finish();
}
