# Analyzer v8 — two-device 30-minute qualification

Analyzer v8 extends v7 from three to six high-impedance inputs. It records the
physical DS3231 SQW, GPIO33 presentation commit, and GPIO16 refresh adoption for
ESP01 and ESP02 simultaneously.

## Wiring

All three boards must share GND.

| Analyzer S3 input | Connect to | Timer-side pin |
|---|---|---|
| GPIO4  | ESP01 DS3231 SQW | RTC SQW / ESP01 GPIO27 node |
| GPIO5  | ESP01 COMMIT | ESP01 GPIO33 |
| GPIO6  | ESP01 REFRESH | ESP01 GPIO16 |
| GPIO7  | ESP02 DS3231 SQW | RTC SQW / ESP02 GPIO27 node |
| GPIO15 | ESP02 COMMIT | ESP02 GPIO33 |
| GPIO16 | ESP02 REFRESH | ESP02 GPIO16 |

The analyzer enables neither pull-up nor pull-down. The OKY3393 RTC modules
already provide the SQW pull-up; the analyzer must not add another one.

## Capture

Use one analyzer trial for the entire qualification. Start capture before the
controller synchronizes the devices and stop it after the 30-minute countdown.

Example serial commands:

```
CLEAR
BEGIN|1|1
```

After the run:

```
STATUS
END|1|1
```

Save the complete analyzer serial log, both timer UART logs, and the controller
CSV from the same run.

For a 30-minute countdown expect approximately 1801 COMMIT edges and 1801
REFRESH edges per timer (boundary 0 plus 1800 second boundaries). SQW is
ANYEDGE, so each RTC contributes roughly 3600 SQW edges over 30 minutes plus
whatever warm-up portion was captured. `dropped=0` is required.

## Fixed run conditions

- Use v6.18-A on both timer devices.
- Set the intended shipping brightness/content from power-on and do not change
  either during warm-up or qualification.
- Both disciplines must be LOCKED.
- `queue_drops=0`, `inferred_missing=0` on both timers.
- Require `|d(rate_ppm)/dt| < 0.01 ppm/min` for several minutes on both devices
  before starting the qualification countdown.
- Do not enable the firmware SQW trace ring.

## Analyzer channel-order skew

ESP-IDF's per-pin GPIO ISR service dispatches simultaneously pending GPIOs
sequentially. The resulting fixed channel-order offset is small compared with a
millisecond acceptance limit, and median-centering removes it from spread
statistics, but it matters when interpreting raw microsecond offsets.

Before the qualification, a one-time fan-out calibration is recommended:
connect the same pulse source to all six analyzer inputs, capture at least ~30
transitions, then run:

```
python tools/calibrate_channel_skew.py calibration.log
```

Pass the reported pair biases to the qualification analyzer if raw offsets are
needed.

## Analysis

Without calibration:

```
python tools/analyze_dual_qualification.py analyzer_v8.log
```

With calibration, for example:

```
python tools/analyze_dual_qualification.py analyzer_v8.log \
  --sqw-pair-bias-us 12 \
  --commit-pair-bias-us 10 \
  --refresh-pair-bias-us 9
```

The script reports:

- COMMIT pair baseline plus centered P95/P99/max and counts >1/2/5 ms.
- REFRESH pair baseline plus centered P95/P99/max and counts >1/2/5 ms.
- Each physical SQW period mean/RMS.
- Initial/final SQW phase and robust relative RTC drift in ppm.
- Analyzer SUMMARY/COUNTS integrity records.

COMMIT/REFRESH percentiles are median-centered only; no global linear trend is
removed before quoting them. Long-term SQW drift is reported separately.
