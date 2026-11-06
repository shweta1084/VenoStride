# VenoStride firmware (host build)

Firmware logic for the VenoStride insole stimulator, written as a portable C11 core behind a small hardware abstraction layer (HAL). There is no board yet, so this repository builds and tests the firmware on a PC. A virtual clock runs a 4-hour Mode 3 cap or a 36-hour battery drain in seconds.

The engineering brief (`Firmware + Software Brief.docx`) is the source of truth. Where it contradicts itself, the choices made are listed in the plan and summarised under [Behaviour](#behaviour) below.

No phone app, no BLE radio stack, no flashing project.

## Layout

| Path | Contents |
| --- | --- |
| `firmware/include/` | Public types (`vs_types.h`), HAL interface (`hal.h`), module headers |
| `firmware/src/` | The core: posture, IMU temperature compensation, stimulation engine, session state machine, safety, battery, compliance log, command API, status codec |
| `firmware/platform/host/` | HAL fakes with a virtual clock, CSV IMU replay, the `venostride_sim` CLI |
| `firmware/platform/nrf52832/` | Stub HAL and peripheral map for the future board binding (not built by default) |
| `firmware/tests/` | One test executable per requirement area, with an in-repo assert harness |
| `firmware/scenarios/` | IMU traces (stand, stand-walk-stand, sit, noisy stand, temperature span) and their generator |

## Build and test

Requirements: CMake 3.20 or newer, and a C11 compiler (MSVC 2022, GCC or Clang). Ninja is optional.

On Windows, open an **x64 Native Tools Command Prompt for VS 2022** and run:

```bat
cmake -S firmware -B %USERPROFILE%\vs_build -G Ninja
cmake --build %USERPROFILE%\vs_build
ctest --test-dir %USERPROFILE%\vs_build --output-on-failure
```

Without Ninja, use the Visual Studio generator and pass the configuration:

```bat
cmake -S firmware -B %USERPROFILE%\vs_build
cmake --build %USERPROFILE%\vs_build --config Release
ctest --test-dir %USERPROFILE%\vs_build -C Release --output-on-failure
```

On Linux or macOS, the same commands work with `-B build`.

Notes:

- Keep the build directory outside OneDrive. OneDrive sync can lock freshly linked executables and make test runs fail with "Access is denied".
- Single-config generators default to `Release`. The runtime tests simulate about 125 hours, which takes about 10 seconds optimised and noticeably longer in Debug.
- Warnings are errors (`/W4 /WX` or `-Wall -Wextra -Werror`).
- `-DVS_CHECK_NRF_STUBS=ON` also compiles the nRF52832 stubs as a syntax check.
- The version string is `VS_FIRMWARE_VERSION` in `firmware/CMakeLists.txt`. The build hash is the git short hash, or `nogit` outside a repository.

## Simulator

`venostride_sim` boots the firmware, applies the clinician commands you pass, replays an IMU trace, and prints each compliance record as it is written, plus periodic status lines.

```bat
venostride_sim --scenario firmware\scenarios\stand_walk_stand.csv --mode 2
venostride_sim --scenario firmware\scenarios\stand.csv --loop --mode 3 --current 22 --duration-s 7200 --status-every-s 600
venostride_sim --mode 2 --impedance 6000 --duration-s 10
venostride_sim --mode 3 --pin 1234
venostride_sim --config firmware\platform\host\venostride_sim.example.cfg --mode 3 --dump-log
```

| Option | Meaning |
| --- | --- |
| `--scenario FILE` | IMU CSV, one row per 10 ms: `ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c`. Without it the IMU reports a still, upright foot. |
| `--loop` | Repeat the scenario instead of holding its last row |
| `--duration-s N` | Virtual seconds to run (default: scenario length, or 60) |
| `--mode standby\|2\|3` | Clinician mode selection after boot |
| `--pin XXXX` | PIN for that selection (default: the configured PIN, `0000`) |
| `--current MA` | Therapy current; clamped to 15–30 mA |
| `--impedance OHM` | Electrode impedance (default 1500 Ω) |
| `--battery PCT` | Starting state of charge (default 100) |
| `--config FILE` | `key=value` file: `pin`, `default_current_ma`, `mode3_frequency_hz`, and the IMU compensation coefficients |
| `--status-every-s N` | Status line period (default 1, 0 turns it off) |
| `--epoch S` | RTC UTC seconds at boot (default 2026-11-04T00:00:00Z) |
| `--rtc-invalid` | Boot with the RTC stopped |
| `--dump-log` | Print the whole compliance log at the end |

To use your own config, copy `venostride_sim.example.cfg` to `venostride_sim.cfg`. Local `*.cfg` files are git-ignored. To regenerate the scenario CSVs, run `python firmware/scenarios/generate_scenarios.py`.

## Behaviour

These are the rules the code and tests implement. Where the brief disagrees with itself, the resolution is noted.

- **Posture at 100 Hz.**
  - Gravity is tracked by a 0.5 Hz low-pass filter, and decisions use acceleration with gravity removed.
  - Standing means vertical dynamic acceleration below 0.3 g for 3 s, with the foot within 15° of upright.
  - Walking means dynamic acceleration above 0.5 g, or more than 2 gyro direction changes within 1 s. Leaving walking takes 2 s below 0.3 g, followed by a fresh 3 s standing check.
  - Sitting or off-foot means tilted more than 15° and still for 5 minutes.
- **Stop behaviour.**
  - Walking and every safety fault cut output in the same 10 ms tick. This follows the brief's IEC 20 ms requirement, which overrides its "100 ms ramp" and "2 s ramp on walking" sentences.
  - A normal exit ramps to 0 over 2 s. Normal exits are a mode change, the daily cap, and sitting.
  - Entry ramps from 0 to the target over 5 s. Mode 2 measures those 5 s as stimulation ON-time; Mode 3 measures them as wall-clock time.
  - The default current is 20 mA, and nothing can exceed 30 mA.
- **Mode 2:** 5 Hz, 200 µs biphasic pulses, 3 s on and 27 s off, with no daily cap. The 5 s entry ramp counts cumulative ON-time only. It pauses for each 27 s OFF period, holds the level it has reached, and resumes when the next ON period begins. The envelope reaches the target once the ON-time adds up to 5 s, which is partway through the second burst. The host simulator advances in 10 ms ticks that land on the duty-cycle boundaries, so this count is exact in the simulator. Carrying that exact ON-time accounting across irregular or delayed ticks is a hardware-integration item.
- **Mode 3:**
  - Runs at 7 Hz by default; the clinician config can set 5–10 Hz. The duty cycle is 30 s on and 30 s off.
  - Its entry ramp runs on wall-clock time, and it keeps advancing through the OFF periods.
  - Every 30 minutes of session time it pauses for 2 minutes. A pause ramps down over 1 s and back up over 2 s.
  - It has a 4-hour daily cap on session time, which resets at 00:00 UTC.
  - Anti-habituation adds 1 mA when the 30-minute average impedance is more than 10% above the session baseline. The increase is capped at +8 mA, never exceeds 30 mA, and resets on the next day's session.
- **Safety.**
  - Impedance is measured every 30 s while at least 5 mA is being delivered. If the due time falls between bursts, the reading is taken at the next pulse.
  - Two readings above 3 kΩ raise an alert and stimulation continues. One reading above 5 kΩ halts stimulation.
  - Net charge of one biphasic pulse is checked every 60 s. More than 0.5 µC of absolute net charge raises an alert and latches a halt. Details are under [Charge balance verification](#charge-balance-verification).
  - A sense-circuit failure (open, short, or no current) halts stimulation.
  - Halts latch until `vs_cmd_reenable_stimulation()` is called.
- **Battery.**
  - The firmware uses a generic LiPo curve that maps 3.0 V to 0% and 4.2 V to 100%. This is a placeholder until the real cell is characterised.
  - Alerts fire at 20% and 5%.
  - Stimulation stops below 5% or below 3.0 V and resumes at 7%. The device shuts down below 2.8 V.
  - UUID `0x2A19` reports `battery_percentage` from 0 to 100, not volts.
- **Watchdog:** 2 s period, kicked every 1 s. On reset, stimulation is off at boot.
- **Faults:**
  - Losing the IMU forces Standby. Recovery needs 100 good samples.
  - An invalid RTC keeps the device running and stamps log records as unknown time.
- **Clinician authentication:**
  - A four-digit PIN, compared in constant time.
  - Five failures lock mode changes for 15 minutes. Every failure is logged.
  - The brief's one-byte "2-bit password" is not accepted as authentication.
- **Compliance log:**
  - A 32 KB ring of 16-byte records in eight 4 KB flash pages. The oldest page is erased when the writer wraps.
  - There are six record types: `SESSION_START`, `SESSION_END` (with duration and stop reason), `IMPEDANCE`, `CHARGING`, `SAFETY_ALERT`, and `MODE_TRANSITION`. Field meanings are documented in `firmware/include/compliance.h`.
- **State that survives a reset.**
  - RAM is lost on a watchdog or power-cycle reset, so at boot the core rebuilds safety-relevant state from the compliance log.
  - It restores the clinician's mode selection.
  - It restores any latched halt that was not followed by a re-enable.
  - It restores today's Mode 3 usage from the records still in the ring. An unfinished session is closed at the current UTC time, bounded to the current UTC day, so the automatic pause is counted even though it writes no impedance records.
  - If the RTC is invalid at boot, the Mode 3 cap is treated as already used until 24 h of uptime has passed.
  - While the relevant records are still in the ring, a reset cannot clear a halt or refill the daily cap. See the ring-buffer limitation below.
- **IMU temperature compensation:**
  - Every sample carries the MPU6050 temperature, and coefficients (offset and per-°C slope) come from config.
  - The slope applies once the observed temperature span exceeds 10 °C.
  - The shipped coefficients are identity. No calibration values have been invented.

### Charge balance verification

The firmware performs a per-pulse charge-balance check every 60 seconds using current-sense feedback. It calculates the net charge from the positive and negative phases and triggers a safety alert and latched stimulation halt when the absolute net charge exceeds 0.5 µC.

The host implementation simulates phase charge measurements using the commanded current and pulse width. Actual hardware measurement and calibration have not yet been validated.

The following requirements remain unresolved and are not currently implemented as firmware checks:

- Charge-balance tolerance of ±5%.
- Phase-duration matching tolerance of ±5%.

Accumulated charge tracking is not included because it is not explicitly required by the current engineering brief.

### Battery budget check

The host fake drains a 425 mAh cell using the brief's current figures:

- Standby draws 5 mA.
- With the boost converter on, the draw is 11.8 mA plus 0.046 mA in Mode 2 or 0.32 mA in Mode 3.

Results from `test_runtime`:

| Case | Result | Brief |
| --- | --- | --- |
| Standby until empty | 85.0 h | ~85 h |
| Mode 2 until the 5% cutoff, divided by 0.95 | 35.9 h | ~36 h |
| One 4-hour Mode 3 session | 48.5 mAh, equivalent to 35.1 h continuous | ~35 h |

This checks that the firmware's duty behaviour matches the budget. It does not measure a real cell.

## Unresolved, by design

- **Device Status (`0x2A58`) byte layout.** The brief gives two layouts that cannot both be true: "1 B mode + 2 B battery + 1 B impedance" versus "bits 0–1 mode, bits 2–7 battery", and 6 bits cannot hold 0–100. `status_codec.c` is the single place this will be decided. Until then it returns `STATUS_CODEC_UNRESOLVED`.
- **On-air encoding of the four-digit PIN** in the mode-select write (`0x0001`). The command API takes a structured `{mode, pin[4]}`, and the decoder in `status_codec.c` is also left unresolved.
- **The UUIDs are preserved as written:** service `0x181A`, status `0x2A58`, battery `0x2A19`, mode `0x0001`, current `0x0002`, compliance `0x0003`.
- **Changing the PIN over BLE is not implemented.** A runtime PIN change would need persistent storage separate from the compliance ring.
- **Charge-balance tolerance of ±5%, and phase-duration matching within ±5%.** The brief states both. The firmware checks absolute net charge per pulse against 0.5 µC only. See [Charge balance verification](#charge-balance-verification).

## Hardware-only work this repository cannot do

Host tests exercise the firmware against a simulated clock, IMU, and sense circuit. They do not validate the hardware below. None of this has been confirmed on a board:

- The nRF52832 HAL, including the physical emergency stimulation cutoff.
- Actual ADC and current-sense accuracy and tolerances, including behaviour at low stimulation currents. The firmware does not treat a non-zero reading below 5 mA as a fault; 5 mA is only the point at which an impedance reading is taken.
- Impedance measurement accuracy, and per-phase charge measurement accuracy.
- The ±5% phase charge-balance tolerance and the ±5% phase-duration match. Neither is a firmware check yet.
- Battery voltage-to-percentage calibration for the real cell. The host uses a placeholder curve from 3.0 V to 4.2 V.
- Temperature-compensation calibration. Shipped coefficients are zero.
- Physical timing of the safety cutoff and of the watchdog.

The same limit covers the rest of the hardware work:

- Electrical safety and the IEC 60601-1 / 60601-2-10 lab tests. These cover leakage, insulation, output limits, and the 20 ms shutdown measured on the output.
- Verifying output current against the setpoint: DAC linearity, current-limit hardware, and the hardware kill path.
- Measuring real impedance and charge on skin electrodes, including SAADC timing on the pulse plateau.
- MPU6050 temperature calibration per unit, which will produce the real `imu_comp` coefficients.
- Characterising the battery: discharge curve, real currents in each mode, charger behaviour, and the 2.8 V shutdown.
- The BLE stack (SoftDevice GATT), radio range, pairing and bonding, and the iOS/Android apps.
- EMC, thermal behaviour, waterproofing, and mechanical fit in the insole.
- Gait detection accuracy on real users, across shoes, speeds, stairs, and vehicles.
- Board bring-up: `firmware/platform/nrf52832/` lists the intended peripheral for each HAL function. All pins are unassigned until there is a schematic.

### Compliance ring-buffer limitation

The compliance log is a 32 KB ring. Once it is full, entering the next page erases the oldest page, so older records are gone. `compliance_init` can still find the write position and the next sequence number from what remains. It cannot reconstruct runtime state whose only copy was in an erased record. After a reboot that can mean:

- an unfinished Mode 3 session is missing its `SESSION_START`, so today's usage is under-counted and the 4-hour cap can be refilled;
- the alert that latched a safety halt has been overwritten, so the halt is cleared;
- the clinician's mode selection has been overwritten, so the device falls back to Mode 2.

A separate persistent checkpoint, outside the ring, is required before recovery of those three items is robust. It is not implemented.

## Limitations

- This is an engineering model of the specified behaviour, not certified medical device firmware. Passing the host tests does not show that stimulation is electrically safe, that hardware timing is accurate, or that any regulatory requirement has been met.
- The nRF52832 HAL in `firmware/platform/nrf52832/` is a placeholder. It does not drive the radio, the IMU, the stimulator, or the physical emergency cutoff. Those functions are not hardware-validated.
- The posture thresholds are the brief's numbers, tested on synthetic traces only.
- There is no OTA update, no secure boot, and no flash wear levelling beyond the ring's page rotation.
- Times are tick-quantised to 10 ms.
