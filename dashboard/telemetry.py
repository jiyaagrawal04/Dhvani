"""
Shared DHVANI telemetry line parser.

Expected line format (from STM32 over USB serial):

    AUD-LFM T=24.4C c=1495 clarity=4092 (clear=3900) tds=803ppm \
        f=2000-17000Hz dur=4ms amp=1669 | I=11.70mA E=6.4mJ batt=86% NORMAL

Used identically by:
    - app.py (live serial mode and replay mode)
    - capture.py (raw serial capture / validation)

Invalid or malformed lines return None and must be ignored by callers.
"""

import re

_FIELD_PATTERNS = {
    "temp_c": re.compile(r"T=(-?\d+(?:\.\d+)?)C"),
    "sound_speed": re.compile(r"\bc=(\d+(?:\.\d+)?)\b"),
    "clarity": re.compile(r"\bclarity=(\d+(?:\.\d+)?)\b"),
    "clear_threshold": re.compile(r"\(clear=(\d+(?:\.\d+)?)\)"),
    "tds_ppm": re.compile(r"\btds=(\d+(?:\.\d+)?)ppm"),
    "freq_band": re.compile(r"\bf=(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)Hz"),
    "dur_ms": re.compile(r"\bdur=(\d+(?:\.\d+)?)ms"),
    "amp": re.compile(r"\bamp=(\d+(?:\.\d+)?)\b"),
    "i_ma": re.compile(r"\bI=(\d+(?:\.\d+)?)mA"),
    "e_mj": re.compile(r"\bE=(\d+(?:\.\d+)?)mJ"),
    "batt_pct": re.compile(r"\bbatt=(\d+(?:\.\d+)?)%"),
    "power_mode": re.compile(r"\b(NORMAL|ECONOMY|SURVIVE)\b"),
}

# Fields that MUST be present (and parseable) for a line to be usable by the
# ML pipeline. Everything else is optional display-only telemetry.
_REQUIRED_FOR_ML = ("temp_c", "tds_ppm", "clarity")


def parse_telemetry_line(line):
    """Parse one DHVANI telemetry line into a dict, or return None if invalid.

    Returns a dict with keys:
        mode, temp_c, sound_speed, clarity, clear_threshold, tds_ppm,
        f_lo, f_hi, dur_ms, amp, i_ma, e_mj, batt_pct, power_mode
    """
    if not line:
        return None

    line = line.strip()
    if not line or line.startswith("#"):
        return None

    try:
        mode_match = re.match(r"^(\S+)", line)
        if not mode_match:
            return None
        mode = mode_match.group(1)

        values = {}
        for key, pattern in _FIELD_PATTERNS.items():
            m = pattern.search(line)
            if not m:
                continue
            if key == "freq_band":
                values["f_lo"] = float(m.group(1))
                values["f_hi"] = float(m.group(2))
            elif key == "power_mode":
                values["power_mode"] = m.group(1)
            else:
                values[key] = float(m.group(1))

        # The three ML inputs are mandatory; anything else is best-effort.
        for req in _REQUIRED_FOR_ML:
            if req not in values:
                return None

        record = {
            "mode": mode,
            "temp_c": values.get("temp_c"),
            "sound_speed": values.get("sound_speed"),
            "clarity": values.get("clarity"),
            "clear_threshold": values.get("clear_threshold"),
            "tds_ppm": values.get("tds_ppm"),
            "f_lo": values.get("f_lo"),
            "f_hi": values.get("f_hi"),
            "dur_ms": values.get("dur_ms"),
            "amp": values.get("amp"),
            "i_ma": values.get("i_ma"),
            "e_mj": values.get("e_mj"),
            "batt_pct": values.get("batt_pct"),
            "power_mode": values.get("power_mode"),
        }
        return record
    except (ValueError, IndexError, AttributeError):
        return None
