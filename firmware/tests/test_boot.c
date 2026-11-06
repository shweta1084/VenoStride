#include <string.h>

#include "test_harness.h"
#include "test_support.h"
#include "version.h"

static void boots_to_standby_with_outputs_off(void)
{
    host_reset();
    host_boot(NULL);
    const vs_device_status_t st = support_status();
    CHECK_EQ(st.mode, VS_MODE_STANDBY);
    CHECK_EQ(st.selected, VS_MODE_CIRCULATION);
    CHECK_EQ(st.output_ua, 0);
    CHECK(!host_stim()->boost_on);
    CHECK(host_stim()->cutoff_count >= 1);
}

static void watchdog_is_kicked_by_the_tick_loop(void)
{
    host_reset();
    host_imu_set_static(0.0f, 0.866f, 0.5f, 25.0f); /* tilted: no therapy, just the idle loop */
    host_boot(NULL);
    host_run_ms(60u * 1000u);
    CHECK_EQ(host_wdt_resets(), 0);
    CHECK_EQ(support_status().mode, VS_MODE_STANDBY);
}

static void version_and_build_hash_are_embedded(void)
{
    CHECK(strcmp(vs_cmd_firmware_version(), VS_FIRMWARE_VERSION) == 0);
    CHECK(strlen(vs_cmd_firmware_version()) > 0);
    CHECK(strlen(vs_cmd_build_hash()) > 0);
}

int main(void)
{
    RUN_TEST(boots_to_standby_with_outputs_off);
    RUN_TEST(watchdog_is_kicked_by_the_tick_loop);
    RUN_TEST(version_and_build_hash_are_embedded);
    return th_finish();
}
