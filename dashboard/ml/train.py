"""
DHVANI water-condition classifier training script.

Trains a RandomForestClassifier on the three DHVANI sensor features:
    temp_c, tds_ppm, clarity
to predict one of four water-condition labels.

IMPORTANT: Trained and evaluated on SYNTHETIC development data only
(see ml/generate_dataset.py). Metrics printed here are NOT real-world
accuracy figures.
"""

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_score,
    recall_score,
)
from sklearn.model_selection import train_test_split

RANDOM_STATE = 42
DATA_PATH = "data/dhvani_synthetic_dataset.csv"
MODEL_PATH = "dhvani_model.joblib"

FEATURES = ["temp_c", "tds_ppm", "clarity"]
TARGET = "label"


def main():
    df = pd.read_csv(DATA_PATH)

    missing = [c for c in FEATURES + [TARGET] if c not in df.columns]
    if missing:
        raise ValueError(f"Dataset missing required columns: {missing}")

    X = df[FEATURES]
    y = df[TARGET]

    print("DHVANI MODEL TRAINING (synthetic-data development run)")
    print("=" * 60)
    print(f"Total rows        : {len(df)}")
    print("Class distribution (full dataset):")
    print(y.value_counts())
    print()

    # Stratified split avoids data leakage between train/test and keeps
    # class proportions consistent in both splits.
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=RANDOM_STATE, stratify=y
    )

    print(f"Train set size    : {len(X_train)}")
    print(f"Test set size     : {len(X_test)}")
    print()

    model = RandomForestClassifier(
        n_estimators=200,
        max_depth=None,
        random_state=RANDOM_STATE,
        n_jobs=-1,
    )
    model.fit(X_train, y_train)

    y_pred = model.predict(X_test)

    accuracy = accuracy_score(y_test, y_pred)
    precision_macro = precision_score(y_test, y_pred, average="macro")
    recall_macro = recall_score(y_test, y_pred, average="macro")
    f1_macro = f1_score(y_test, y_pred, average="macro")

    print("TEST SET METRICS (synthetic-data development evaluation)")
    print("-" * 60)
    print(f"Accuracy          : {accuracy:.4f}")
    print(f"Precision (macro) : {precision_macro:.4f}")
    print(f"Recall (macro)    : {recall_macro:.4f}")
    print(f"F1-score (macro)  : {f1_macro:.4f}")
    print()

    labels_sorted = sorted(y.unique())
    cm = confusion_matrix(y_test, y_pred, labels=labels_sorted)
    print("Confusion matrix (rows=actual, cols=predicted):")
    print("Labels:", labels_sorted)
    print(cm)
    print()

    print("Classification report:")
    print(classification_report(y_test, y_pred))

    print("Feature importance:")
    for feat, imp in sorted(
        zip(FEATURES, model.feature_importances_), key=lambda x: -x[1]
    ):
        print(f"  {feat:10s}: {imp:.4f}")
    print()

    joblib.dump(model, MODEL_PATH)
    print(f"Model saved to: {MODEL_PATH}")

    # Sanity check: confirm predict() and predict_proba() both work
    sample = X_test.iloc[:1]
    pred = model.predict(sample)
    proba = model.predict_proba(sample)
    print()
    print("Sanity check on one test sample:")
    print(f"  predict()       -> {pred}")
    print(f"  predict_proba() -> {proba}")


if __name__ == "__main__":
    main()
