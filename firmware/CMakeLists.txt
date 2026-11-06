cmake_minimum_required(VERSION 3.20)
project(venostride_firmware C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(VS_CHECK_NRF_STUBS "Compile the nRF52832 HAL stubs on the host as a syntax check" OFF)

set(VS_FIRMWARE_VERSION "v0.1.0-host-2026-11-04")

find_package(Git QUIET)
set(VS_BUILD_HASH "nogit")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE VS_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE VS_GIT_RESULT)
    if(VS_GIT_RESULT EQUAL 0 AND VS_GIT_HASH)
        set(VS_BUILD_HASH "${VS_GIT_HASH}")
    endif()
endif()
configure_file(include/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

if(MSVC)
    add_compile_options(/W4 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Werror -Wno-missing-field-initializers)
endif()

set(VS_CORE_SOURCES
    src/battery.c
    src/command.c
    src/compliance.c
    src/imu_comp.c
    src/posture.c
    src/safety.c
    src/session.c
    src/status_codec.c
    src/stim.c
    src/vs_core.c)

# Portable firmware core linked with the host HAL fakes.
add_library(venostride_host STATIC ${VS_CORE_SOURCES} platform/host/hal_host.c platform/host/host_trace.c)
target_include_directories(venostride_host PUBLIC
    include
    platform/host
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(NOT MSVC)
    target_link_libraries(venostride_host PUBLIC m)
endif()

add_executable(venostride_sim platform/host/sim_main.c)
target_link_libraries(venostride_sim PRIVATE venostride_host)

if(VS_CHECK_NRF_STUBS)
    add_library(nrf52832_stub_check OBJECT platform/nrf52832/hal_nrf52832.c)
    target_include_directories(nrf52832_stub_check PRIVATE include platform/nrf52832)
endif()

enable_testing()

add_library(vs_test_support STATIC tests/test_support.c)
target_link_libraries(vs_test_support PUBLIC venostride_host)
target_compile_definitions(vs_test_support PUBLIC
    VS_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenarios")

set(VS_TESTS
    test_boot
    test_posture
    test_imu_comp
    test_mode2
    test_walking_cutoff
    test_current_limit
    test_impedance
    test_charge_balance
    test_battery
    test_faults
    test_watchdog
    test_mode3
    test_compliance
    test_command
    test_transitions
    test_runtime
    test_scenarios)

foreach(t ${VS_TESTS})
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE vs_test_support)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
set_tests_properties(test_runtime test_mode3 PROPERTIES TIMEOUT 1800)

add_test(NAME sim_smoke
    COMMAND venostride_sim --scenario ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/stand_walk_stand.csv
            --mode 2 --status-every-s 0 --duration-s 30)
set_tests_properties(sim_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "SESSION_END +Mode2 after [0-9]+ s \\(walking\\)")
