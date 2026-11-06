"""Regenerates the synthetic IMU traces used by venostride_sim and test_scenarios.

Each row is one 100 Hz sample: ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, temp_c.
Traces are synthetic (no recorded patient data). The simulator holds the last
row once a file ends, so short files can represent long still periods.
"""

import math
import os
import random

RATE_HZ = 100
HERE = os.path.dirname(os.path.abspath(__file__))


def stand(seconds, noise=0.0, temp=25.0, temp_slope=0.0, rng=None, t0=0.0):
    rows = []
    for i in range(int(seconds * RATE_HZ)):
        t = t0 + i / RATE_HZ
        n = (lambda: rng.uniform(-noise, noise)) if rng and noise else (lambda: 0.0)
        sway = 0.02 * math.sin(2 * math.pi * 0.3 * t) if noise else 0.0
        rows.append((n() + sway, n(), 1.0 + n(), 0.0, 0.0, 0.0, temp + temp_slope * t))
    return rows


def walk(seconds, temp=25.0, t0=0.0):
    rows = []
    for i in range(int(seconds * RATE_HZ)):
        t = t0 + i / RATE_HZ
        w = 2 * math.pi * 1.8 * t
        rows.append((0.4 * math.sin(w + 1.0), 0.0, 1.0 + math.sin(w), 0.0, 200.0 * math.sin(w), 0.0, temp))
    return rows


def tilted(seconds, deg=60.0, temp=25.0):
    rad = math.radians(deg)
    return [(0.0, math.sin(rad), math.cos(rad), 0.0, 0.0, 0.0, temp) for _ in range(int(seconds * RATE_HZ))]


def write(name, rows):
    path = os.path.join(HERE, name)
    with open(path, "w", newline="\n") as f:
        f.write("ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c\n")
        for r in rows:
            f.write(",".join(f"{v:.4f}" for v in r) + "\n")
    print(f"{name}: {len(rows)} samples")


def main():
    rng = random.Random(2026)
    write("stand.csv", stand(10))
    write("stand_walk_stand.csv", stand(6) + walk(5, t0=6) + stand(10))
    write("sit.csv", tilted(2))
    write("noisy_stand.csv", stand(12, noise=0.05, rng=rng))
    write("temp_span.csv", stand(20, temp=20.0, temp_slope=0.75))


if __name__ == "__main__":
    main()
