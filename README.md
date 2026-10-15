# DHVANI — adaptive software-defined sonar transmitter payload for AUVs

**Designed and built by Jiya Agrawal · 2026**

DHVANI is a low-power, real-time, software-defined sonar transmitter that chooses its own ping — band, pulse length, amplitude and modulation — from live water conditions **and** the remaining battery budget, then sleeps while a timer and DMA stream the waveform out through the chip's own DAC.

| !\[DHVANI payload pod, front](hardware/pod/photos/DHVANI\_Pod\_white\_front.png) | !\[DHVANI architecture](diagrams/DHVANI\_Architecture\_HalfSlide.png) |

|:--:|:--:|> \\\*\\\*Prototype video:\\\*\\\* https://www.youtube.com/watch?v=tk1MluvHB7E

## What it does

* **Water-aware ping.** Temperature (DS18B20), salinity (TDS probe) and clarity (dial standing in for a turbidity sensor — see the honesty note) set the chirp band, pulse length and amplitude for every ping.
* **Battery-aware ping.** An INA219 measures real current and energy per ping; the firmware moves between `NORMAL`, `ECONOMY` and `SURVIVE` modes from the remaining energy budget — mission-budget planning, not just "low battery".
* **Three modulations, two bands.** LFM chirp, geometric chirp and Barker-13 BPSK, each in an audible and an ultrasonic band (`AUD-LFM / AUD-GEO / AUD-BPSK / US-LFM / US-GEO / US-BPSK`), switched with the blue user button. Hann window on chirps, Tukey window on BPSK — for sidelobe suppression and to protect the transducer driver's slew rate.
* **CPU sleeps during the ping.** TIM6 triggers the on-chip 12-bit DAC at 1 MSps through GPDMA; a "CPU busy" line (PE0) on the scope stays low for the whole ping.
* **Ground-station dashboard** on a laptop reads the board's serial telemetry, plots every value live and labels the water condition with a small RandomForest classifier (trained on synthetic data for now — see `dashboard/README.md`).

## Measured results (bench, oscilloscope)

|Quantity|Clear water|Muddy water|
|-|-|-|
|Audible band|2 – 17 kHz|2 – 5.8 kHz|
|Pulse length|3.84 ms|14.8 ms|
|Ultrasonic dominant frequency|179 kHz|110 kHz|

* Amplitude scales with salinity: 1.90 Vpp (fresh) → 2.92 Vpp (salty).
* Whole payload draws about **12 mA**.
* BPSK FFT peak at 8.7 kHz against 8.45 kHz expected.

## Hardware

|Block|Part|Notes|
|-|-|-|
|Controller|NUCLEO-U575ZI-Q (STM32U575, Cortex-M33, 160 MHz)|ultra-low-power family, FPU, GPDMA|
|DAC|on-chip 12-bit DAC, PA4|1 MSps, TIM6 trigger + GPDMA|
|Analog front end|TL074: 2nd-order Sallen-Key Butterworth low-pass (fc ≈ 340 kHz) + unity buffer|47 Ω to scope, 1 kΩ to buzzer; +5 V / −9 V (9 V battery)|
|Temperature|DS18B20 on PF12|4.7 kΩ pull-up|
|Salinity|TDS probe on PC3||
|Clarity|potentiometer on PA3|stand-in for turbidity|
|Power monitor|INA219 on I2C (PB9 / PB8)|wired in place of the JP5 jumper|
|Enclosure|3D-printed PLA pod, AUV hull section|see `hardware/pod`|

Full pin map, values and wiring: `docs/DHVANI\\\_Pins\\\_and\\\_Values\\\_Roadmap.pdf` and `diagrams/DHVANI\\\_Hardware\\\_Wiring.png`.

## Repository map

|Folder|Contents|
|-|-|
|`firmware/`|STM32CubeIDE project for the NUCLEO-U575ZI-Q (waveform generation, sensors, power modes, serial telemetry)|
|`dashboard/`|Streamlit ground-station dashboard, telemetry parser, serial capture tool, ML training / evaluation scripts and the trained model|
|`hardware/pod/`|OpenSCAD source, print-ready STLs, printing and measurement guides, photos of the printed pod|
|`hardware/pcb/`|Nucleo-144 shield PCB design brief and KiCad guide (designed for the roadmap; not fabricated yet)|
|`docs/`|Build guides, complete test guide, pins \& values roadmap, tech stack|
|`diagrams/`|Architecture, process flow, wiring and clear-vs-muddy diagrams (SVG source + PNG)|

Note on `docs/DHVANI\\\_Tech\\\_Stack.pdf` and `docs/DHVANI\\\_Breadboard\\\_Build\\\_v2.pdf`: these describe the earlier external-DAC (DAC0800) build. The final prototype uses the on-chip DAC — `docs/DHVANI\\\_Builtin\\\_DAC\\\_Build\\\_v3.pdf` is the current reference.

## Run it

**Firmware** — open `firmware/` in STM32CubeIDE, build, and flash a NUCLEO-U575ZI-Q over the on-board ST-LINK. Telemetry comes out of the ST-LINK virtual COM port at 115200 8N1, one line per ping.

**Dashboard**

```bash
cd dashboard
pip install -r requirements.txt
streamlit run app.py            # replays the demo log, no board needed
streamlit run app.py -- COM9    # live from the board
```

See `dashboard/README.md` for the telemetry format, the capture tool and how the model is trained.

**Pod** — print `hardware/pod/DHVANI\\\_Pod\\\_lower.stl`, `DHVANI\\\_Pod\\\_upper.stl` and two of `DHVANI\\\_Pod\\\_saddle\\\_PRINT\\\_2.stl` in PLA; settings are in `hardware/pod/README.txt`.

## Design decisions

* **No FPGA.** Its static power draw contradicts the brief's central low-power requirement; a timer + DMA + on-chip DAC does the job with the CPU asleep.
* **No ML inside the adaptation core.** Choosing a ping from water and battery state is a constrained lookup, not a learning problem. ML is used only on the ground station, to classify water condition from telemetry — and that model is currently trained on synthetic data, clearly labelled as such in the code, the report and on screen.
* **Real sensors wherever the brief allows.** Temperature, salinity and current are measured; only clarity uses the dial the brief permits.

## Honesty note

Clarity comes from a potentiometer that stands in for a turbidity sensor (the turbidity sensor we had could not separate clear from muddy water on the bench). Temperature, salinity and power are real measurements. The dashboard says so on screen.

## Roadmap

* Echo receiver and echo-based turbidity estimation
* Dual-band ping and seabed imaging
* Custom shield PCB (KiCad) and a faster external DAC to reach 500 kHz — fabrication planned for December 2026

## Author

Jiya Agrawal

Ground-station dashboard and ML model: Disha Singh

## Licence

MIT — see `LICENSE`.

