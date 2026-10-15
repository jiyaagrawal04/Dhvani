"""
Generates sample/log_demo.txt: a realistic, looping DHVANI telemetry log
used by the dashboard's replay mode (no STM32 hardware required).

The log cycles through the four water conditions, then repeats clear water with
the battery nearly gone, so the dashboard visibly demonstrates:

    clear + low TDS   -> clear_fresh
    clear + high TDS  -> clear_reef
    muddy + low TDS   -> muddy_river
    muddy + high TDS  -> muddy_estuary

and the ping shrinking in ECONOMY / SURVIVE power modes for the same water.

This is SYNTHETIC demo telemetry, not a real STM32 capture.
"""

import numpy as np

RANDOM_SEED = 7
OUTPUT_PATH = "sample/log_demo.txt"
PINGS_PER_BLOCK = 15

# (label, temp_mean, tds_mean, tds_std, clarity_mean, clarity_std, power_mode, batt_pct)
# The ping itself (band, pulse, amplitude) is derived below from clarity, TDS and
# power mode, following the firmware's measured behaviour on the bench.
BLOCKS = [
    ("clear_fresh", 22.0, 250, 80, 4000, 120, "NORMAL", 91),
    ("clear_reef", 27.0, 900, 140, 3950, 120, "NORMAL", 84),
    ("muddy_river", 24.0, 300, 90, 450, 150, "NORMAL", 72),
    ("muddy_estuary", 26.0, 850, 170, 400, 150, "ECONOMY", 55),
    # same water as block 1, battery nearly gone: shows the battery-aware side on its own
    ("clear_fresh (low battery)", 22.0, 250, 80, 4000, 120, "SURVIVE", 22),
]

CLEAR_THRESHOLD = 3900      # fixed in the firmware, printed as "(clear=3900)"
F_LO_HZ = 2000              # band always starts at 2 kHz
I_MA = 11.7                 # whole payload draws ~12 mA on the bench


def sound_speed(temp_c, tds_ppm, depth_m=0.0):
    """Medwin's equation, as used on the board (TDS ppm -> ppt as the salinity proxy).

    Matches the bench captures: 24.4 C / 803 ppm -> 1495 m/s, 27.4 C / 22 ppm -> 1502 m/s.
    """
    s = tds_ppm / 1000.0
    t = temp_c
    return (1449.2 + 4.6 * t - 0.055 * t**2 + 0.00029 * t**3
            + (1.34 - 0.010 * t) * (s - 35.0) + 0.016 * depth_m)


def ping_from_conditions(clarity, tds_ppm, power_mode):
    """Band, pulse length and amplitude the way the firmware chooses them.

    Fitted to bench lines from the real board:
      clarity 4092 -> 2-17000 Hz, 4 ms      clarity 449 -> 2-5807 Hz, 15 ms
      clarity 1755 -> 2-10043 Hz, 10 ms     clarity 10  -> 2-5000 Hz, 16 ms
      tds 22 ppm -> amp 1213 (1.96 Vpp)     tds 803 ppm -> amp 1669 (2.69 Vpp)
      ECONOMY: amplitude x0.8, pulse x0.7 (bench); SURVIVE: x0.6, x0.5 (approx.)
    """
    x = min(max(clarity / CLEAR_THRESHOLD, 0.0), 1.0)
    f_hi = int(round(5000 + 12000 * x, -2))           # 5-17 kHz with clarity
    dur_ms = 16 - 12 * x                              # 16 ms muddy -> 4 ms clear
    amp = 1200 + 0.58 * tds_ppm                       # louder in salty water
    if power_mode == "ECONOMY":
        amp, dur_ms = amp * 0.8, dur_ms * 0.7
    elif power_mode == "SURVIVE":
        amp, dur_ms = amp * 0.6, dur_ms * 0.5
    return f_hi, max(2, int(round(dur_ms))), min(int(round(amp)), 2047)


def main():
    rng = np.random.default_rng(RANDOM_SEED)
    lines = []

    for (
        label,
        temp_mean,
        tds_mean,
        tds_std,
        clarity_mean,
        clarity_std,
        power_mode,
        batt_mean,
    ) in BLOCKS:
        lines.append(f"# --- block: {label} ---")
        for i in range(PINGS_PER_BLOCK):
            temp_c = np.clip(rng.normal(temp_mean, 0.4), 5, 40)
            tds_ppm = np.clip(rng.normal(tds_mean, tds_std), 0, 2000)
            clarity = np.clip(rng.normal(clarity_mean, clarity_std), 0, 4095)
            c = sound_speed(temp_c, tds_ppm)
            f_hi, dur_ms, amp = ping_from_conditions(clarity, tds_ppm, power_mode)
            i_ma = max(0, I_MA + rng.normal(0, 0.05))
            e_mj = max(0, 5.3 + 0.33 * dur_ms + rng.normal(0, 0.15))   # ~6.4 mJ at 4 ms, ~10.3 mJ at 15 ms
            batt = np.clip(batt_mean - 3.0 * (i / PINGS_PER_BLOCK), 1, 100)

            line = (
                f"AUD-LFM T={temp_c:.1f}C c={c:.0f} clarity={clarity:.0f} "
                f"(clear={CLEAR_THRESHOLD}) tds={tds_ppm:.0f}ppm "
                f"f={F_LO_HZ}-{f_hi}Hz dur={dur_ms}ms amp={amp} | "
                f"I={i_ma:.2f}mA E={e_mj:.1f}mJ batt={batt:.0f}% {power_mode}"
            )
            lines.append(line)

    content = "\n".join(lines) + "\n"
    with open(OUTPUT_PATH, "w") as f:
        f.write(content)

    print(f"Wrote {len(lines)} lines ({len(BLOCKS)} blocks) to {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
