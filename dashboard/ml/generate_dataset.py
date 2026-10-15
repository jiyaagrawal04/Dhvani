"""
DHVANI synthetic development dataset generator.

IMPORTANT / LIMITATION:
This dataset is 100% SYNTHETIC. It is NOT real sensor data collected from the
DHVANI AUV or its STM32 payload. It exists only so the ML training/evaluation
pipeline and the Ground Station Dashboard can be built and exercised before a
real labeled dataset from the physical sensors is available.

Do not use metrics derived from this dataset to make any claim about
real-world classification accuracy.

Features (match the physical DHVANI sensors exactly):
    temp_c    - water temperature, degrees Celsius
    tds_ppm   - total dissolved solids / salinity proxy, parts per million
    clarity   - optical clarity sensor reading (unitless sensor counts)

Label (4 water conditions):
    clear_fresh    - high clarity, low TDS
    clear_reef     - high clarity, higher TDS
    muddy_river    - low clarity, low TDS
    muddy_estuary  - low clarity, higher TDS
"""

import numpy as np
import pandas as pd

RANDOM_SEED = 42
N_ROWS = 10000
OUTPUT_PATH = "data/dhvani_synthetic_dataset.csv"

CLASSES = ["clear_fresh", "clear_reef", "muddy_river", "muddy_estuary"]


def generate_class_samples(rng, label, n):
    """Generate n synthetic samples for a given water-condition label.

    Distributions overlap at the edges on purpose (real noisy sensors,
    temperature variation, and transitional water conditions) so the
    classes are not perfectly separable.
    """
    if label == "clear_fresh":
        # Clear freshwater: high clarity, low TDS
        temp_c = rng.normal(22.0, 3.0, n)
        tds_ppm = rng.normal(250, 90, n)
        clarity = rng.normal(4200, 350, n)
    elif label == "clear_reef":
        # Clear shallow reef / clear salty water: high clarity, higher TDS
        temp_c = rng.normal(27.0, 2.5, n)
        tds_ppm = rng.normal(900, 150, n)
        clarity = rng.normal(4000, 400, n)
    elif label == "muddy_river":
        # Muddy river: low clarity, low TDS
        temp_c = rng.normal(24.0, 3.5, n)
        tds_ppm = rng.normal(300, 100, n)
        clarity = rng.normal(1200, 400, n)
    elif label == "muddy_estuary":
        # Muddy estuary: low clarity, higher TDS
        temp_c = rng.normal(26.0, 3.0, n)
        tds_ppm = rng.normal(850, 180, n)
        clarity = rng.normal(1350, 450, n)
    else:
        raise ValueError(f"Unknown label: {label}")

    # Realistic measurement noise on top of the class distribution
    temp_c = temp_c + rng.normal(0, 0.3, n)
    tds_ppm = tds_ppm + rng.normal(0, 20, n)
    clarity = clarity + rng.normal(0, 80, n)

    # Clip to physically plausible sensor ranges
    temp_c = np.clip(temp_c, 5, 40)
    tds_ppm = np.clip(tds_ppm, 0, 2000)
    clarity = np.clip(clarity, 0, 5000)

    return temp_c, tds_ppm, clarity


def main():
    rng = np.random.default_rng(RANDOM_SEED)

    rows_per_class = N_ROWS // len(CLASSES)
    remainder = N_ROWS - rows_per_class * len(CLASSES)

    frames = []
    for i, label in enumerate(CLASSES):
        n = rows_per_class + (1 if i < remainder else 0)
        temp_c, tds_ppm, clarity = generate_class_samples(rng, label, n)
        frames.append(
            pd.DataFrame(
                {
                    "temp_c": temp_c,
                    "tds_ppm": tds_ppm,
                    "clarity": clarity,
                    "label": label,
                }
            )
        )

    df = pd.concat(frames, ignore_index=True)

    # Shuffle rows so the CSV isn't grouped by class
    df = df.sample(frac=1.0, random_state=RANDOM_SEED).reset_index(drop=True)

    # Round sensor values to realistic sensor precision
    df["temp_c"] = df["temp_c"].round(1)
    df["tds_ppm"] = df["tds_ppm"].round(0).astype(int)
    df["clarity"] = df["clarity"].round(0).astype(int)

    df.to_csv(OUTPUT_PATH, index=False)

    print("DHVANI SYNTHETIC DEVELOPMENT DATASET")
    print("=" * 50)
    print("WARNING: This is SYNTHETIC data, not real sensor data.")
    print(f"Rows generated : {len(df)}")
    print(f"Columns        : {list(df.columns)}")
    print("Class distribution:")
    print(df["label"].value_counts())
    print(f"Missing values : {df.isnull().sum().sum()}")
    print(f"Saved to       : {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
