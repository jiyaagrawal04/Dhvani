"""
DHVANI serial capture utility.

Standalone helper (independent of the Streamlit dashboard) for connecting to
the STM32 ground-station serial link and logging raw telemetry lines to a
file, e.g. for later replay with:

    streamlit run app.py -- replay:sample/my_capture.txt

Usage:
    python capture.py COM9 [output_file] [--baud 115200]

If output_file is omitted, a timestamped file is created under sample/.
"""

import argparse
import sys
import time
from datetime import datetime

import serial

from telemetry import parse_telemetry_line

DEFAULT_BAUD = 115200


def main():
    parser = argparse.ArgumentParser(description="Capture DHVANI serial telemetry to a log file.")
    parser.add_argument("port", help="Serial port, e.g. COM9")
    parser.add_argument(
        "output",
        nargs="?",
        default=None,
        help="Output log file path (default: sample/capture_<timestamp>.txt)",
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate (default 115200)")
    args = parser.parse_args()

    output_path = args.output or f"sample/capture_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"

    print(f"DHVANI capture: {args.port} @ {args.baud} baud -> {output_path}")
    print("Press Ctrl+C to stop.")

    valid_count = 0
    invalid_count = 0

    with open(output_path, "a", buffering=1) as out_file:
        while True:
            try:
                with serial.Serial(args.port, args.baud, timeout=1) as ser:
                    print(f"Connected to {args.port}.")
                    while True:
                        try:
                            raw = ser.readline().decode("utf-8", errors="ignore")
                        except serial.SerialException:
                            raise
                        if not raw:
                            continue
                        line = raw.strip()
                        if not line:
                            continue

                        record = parse_telemetry_line(line)
                        if record is None:
                            invalid_count += 1
                            continue

                        valid_count += 1
                        out_file.write(line + "\n")
                        print(
                            f"[{valid_count} ok / {invalid_count} dropped] {line}"
                        )
            except serial.SerialException as exc:
                print(f"Serial error ({exc}). Retrying in 2s...")
                time.sleep(2)
            except KeyboardInterrupt:
                print(f"\nStopped. {valid_count} valid lines written to {output_path}.")
                sys.exit(0)


if __name__ == "__main__":
    main()
