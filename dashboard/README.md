# DHVANI ground-station dashboard

Laptop dashboard for the DHVANI sonar payload. It reads the
board's serial telemetry (one line per ping, 115200 8N1 over the ST-LINK virtual
COM port), plots every value live, and labels the water condition with a
RandomForest classifier.

## Quick start

```bash
pip install -r requirements.txt

streamlit run app.py                                  # replay the demo log, no board needed
streamlit run app.py -- COM9                          # live from the board (close CubeIDE's console / PuTTY first)
streamlit run app.py -- replay:sample/log_bench_sample.txt   # replay a specific file
```

The browser opens at http://localhost:8501.

## Files

| File | What it does |
| --- | --- |
| `app.py` | The Streamlit dashboard (live serial or replay) |
| `telemetry.py` | Parser for the board's telemetry line; shared by `app.py` and `capture.py` |
| `capture.py` | Logs raw serial lines to a file for later replay: `python capture.py COM9` |
| `ml/generate_dataset.py` | Builds the synthetic training set `data/dhvani_synthetic_dataset.csv` |
| `ml/train.py` | Trains the classifier → `dhvani_model.joblib` |
| `ml/evaluate.py` | Evaluates it → `dhvani_model_report.txt` |
| `ml/generate_demo_log.py` | Builds `sample/log_demo.txt`, the looping demo telemetry |
| `sample/log_demo.txt` | Synthetic demo telemetry cycling through the four water conditions |
| `sample/log_bench_sample.txt` | A few **real** lines captured from the board on the bench |
| `.streamlit/config.toml` | Dark ocean-blue theme |

## Telemetry line format

```
AUD-LFM  T=24.4C c=1495 clarity=4092 (clear=3900) tds=803ppm f=2000-17000Hz dur=4ms amp=1669 | I=11.70mA E=6.4mJ batt=86% NORMAL
```

mode · water temperature °C · sound speed m/s · clarity 0–4095 (0 = muddy) · TDS ppm ·
chirp band Hz · pulse ms · amplitude in DAC counts (Vpp = 2·amp·3.3/4095) · current mA ·
energy per ping mJ · battery budget % · power mode `NORMAL / ECONOMY / SURVIVE`.

## What the ML model is — and is not

The classifier uses exactly three features, `temp_c`, `tds_ppm`, `clarity`, and
predicts `clear_fresh`, `clear_reef`, `muddy_river` or `muddy_estuary`.

**It is trained and evaluated on synthetic data** (`ml/generate_dataset.py`).
The 98.5 % in `dhvani_model_report.txt` says the pipeline works; it is not a
real-world accuracy figure. Replacing the synthetic set with labelled bench
captures (via `capture.py`, one file per condition) and re-running
`ml/train.py` and `ml/evaluate.py` is the next step.

Clarity on the board comes from a dial standing in for a turbidity sensor;
temperature, salinity and power are real measurements. The dashboard footer says so on screen.
