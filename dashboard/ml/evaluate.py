"""
DHVANI model evaluation script.

Loads the trained dhvani_model.joblib and re-evaluates it on a held-out
stratified test split of the synthetic development dataset.

IMPORTANT: This is a "Synthetic-data development evaluation".
These numbers describe model performance on SYNTHETIC data only and
must NOT be reported as real-world sensor accuracy.
"""

import joblib
import pandas as pd
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
)
from sklearn.model_selection import train_test_split

RANDOM_STATE = 42
DATA_PATH = "data/dhvani_synthetic_dataset.csv"
MODEL_PATH = "dhvani_model.joblib"
REPORT_PATH = "dhvani_model_report.txt"

FEATURES = ["temp_c", "tds_ppm", "clarity"]
TARGET = "label"


def main():
    model = joblib.load(MODEL_PATH)
    df = pd.read_csv(DATA_PATH)

    X = df[FEATURES]
    y = df[TARGET]

    # Same split parameters as training, so this is the identical held-out
    # test set the model never saw during fit().
    _, X_test, _, y_test = train_test_split(
        X, y, test_size=0.2, random_state=RANDOM_STATE, stratify=y
    )

    y_pred = model.predict(X_test)

    accuracy = accuracy_score(y_test, y_pred)
    f1_macro = f1_score(y_test, y_pred, average="macro")
    f1_weighted = f1_score(y_test, y_pred, average="weighted")
    report = classification_report(y_test, y_pred)
    labels_sorted = sorted(y.unique())
    cm = confusion_matrix(y_test, y_pred, labels=labels_sorted)

    lines = []
    lines.append("DHVANI MODEL EVALUATION REPORT")
    lines.append("Synthetic-data development evaluation")
    lines.append(
        "NOTE: Evaluated on SYNTHETIC sensor data (see ml/generate_dataset.py)."
    )
    lines.append(
        "These figures are NOT a measurement of real-world sensor accuracy."
    )
    lines.append("=" * 60)
    lines.append(f"Model path        : {MODEL_PATH}")
    lines.append(f"Test set size     : {len(X_test)}")
    lines.append(f"Accuracy          : {accuracy:.4f}")
    lines.append(f"Macro F1-score    : {f1_macro:.4f}")
    lines.append(f"Weighted F1-score : {f1_weighted:.4f}")
    lines.append("")
    lines.append("Classification report:")
    lines.append(report)
    lines.append("Confusion matrix (rows=actual, cols=predicted):")
    lines.append(f"Labels: {labels_sorted}")
    lines.append(str(cm))

    report_text = "\n".join(lines)
    print(report_text)

    with open(REPORT_PATH, "w") as f:
        f.write(report_text + "\n")

    print()
    print(f"Report saved to: {REPORT_PATH}")


if __name__ == "__main__":
    main()
