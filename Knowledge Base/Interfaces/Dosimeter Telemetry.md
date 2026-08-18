---
type: interface-spec
status: review
signal: DOSIMETER_ADC
---

# Dosimeter telemetry

## Electrical input

The dosimeter output is connected to `PA4 / ADC1_IN4` and is expected to remain between
0 V and 3.3 V. The front end has 25x gain, and the final PA4 signal has a nominal
sensitivity of 2.5 mV/rad. It remains to be confirmed whether the signal represents
accumulated dose, dose rate, or another detector quantity.

The nominal conversion is usable immediately, but raw ADC code and measured voltage
remain authoritative until a physical calibration is recorded.

## Acquisition

- ADC: ADC1 channel 4.
- Trigger: timer-driven, initial raw sample rate 1 kHz.
- Sample time: use a long sample time selected after measuring source impedance.
- Transfer: DMA circular buffer.
- Filtering: reject out-of-range samples, average or decimate over 100 ms windows, then
  apply a configurable low-pass filter.
- Publication: 1 Hz default, configurable from 0.1 to 10 Hz.
- Compensation: periodically sample internal VREFINT and use the calibrated VDDA estimate
  when converting ADC codes to voltage.

## Telemetry fields

- sample timestamp in microseconds;
- raw mean, minimum, maximum, and standard deviation in ADC counts;
- calculated input voltage in microvolts;
- calibrated radiation value and units when calibration is valid;
- gain, zero offset, sensitivity, and calibration revision;
- flags for saturation, underrange, ADC overrun, stale sample, and uncalibrated value.

## Calibration model

Store a versioned two-point linear model in MCU flash:

`radiation = (voltage - zero_offset) / volts_per_unit`

Use `volts_per_unit = 0.0025 V/rad` as the factory-default nominal coefficient. With a
zero-volt offset, 3.3 V corresponds to approximately 1320 rad. A calibrated coefficient
and zero offset supersede the nominal values and clear the `NOMINAL_CALIBRATION` flag.

Include the nearest valid dosimeter sample in every thermal frame metadata record while
also publishing the independent low-rate telemetry stream.
