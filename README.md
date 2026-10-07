# Analyzer 10.1 — three physical COMMIT channels

This revision fixes bootstrap grouping while retaining V10 qualification and its
START-only physical gate. Alternating same-channel pairs qualify at
995000..1005000 us; cross-channel association stays at 5000 us. Pending groups
now have `TRAIN_MAX + association window` grace, letting each channel's next
edge retrospectively qualify the original START before its group expires.
Original timestamps are preserved. Real incomplete groups/dropped input fail.
Host test `tests/host/negative_test.cpp` proves that on the saved capture: a
removed START edge, a 10 ms late START and a skipped mid-train COMMIT each
produce MISSING and a final FAIL, while the unmodified capture passes.

The first three boundaries in each train must have max-min span <=100 us.
Later boundaries report INFO. A gap >1.5 s starts another train and resets the
START gate. Multiple countdowns in one BEGIN/END capture are supported.

## Wiring and physical identity

| Channel | Analyzer ESP32-S3 GPIO | Default timer | Optional qualification timer |
|---|---:|---|---|
| 1 | 5 | ESP01 | ESP01 |
| 2 | 15 | ESP04 | ESP04 |
| 3 | 16 | ESP03 | ESP03 |

Timer COMMIT output is GPIO33; connect common ground. This build is configured
for the restored long-run mapping: channel 1 = ESP01, channel 2 = ESP04,
channel 3 = ESP03. ESP02 and ESP05 are controller-only for this physical capture.
The historical ESP01/ESP02/ESP03 fixture remains included only for replay tests.

Default build in an ESP-IDF environment:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

For the physically wired ESP01/ESP04/ESP03 trio:

```powershell
idf.py -B build-0143 -D SDKCONFIG=sdkconfig.0143 -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.v10_0143" set-target esp32s3
idf.py -B build-0143 -D SDKCONFIG=sdkconfig.0143 build
idf.py -B build-0143 -D SDKCONFIG=sdkconfig.0143 -p COMx flash monitor
```

GPIO option names retain their earlier ESP01/02/03 spelling for compatibility;
channel-name options define the physical EDGE/QUALIFIED/SKEW/COUNTS labels.
The READY banner also prints the configured device name for each GPIO.

## Capture and replay

Send `BEGIN|1|1` with CR, LF or CRLF; expect `ANZ|ACK|BEGIN|1|1`. After the
countdown send `END|1|1`. Save all EDGE/QUALIFIED/SKEW/COUNTS/TRAIN/TRIPWIRE lines.

The original 2026-10-07 capture is included unmodified:

```powershell
python .\tools\check_v10_log.py .\tests\host\fixtures\20261007_original_v10_capture.txt --expect-start-ranges 46,48,68
```

| Train | True START span | Worst three-boundary gate span | Complete boundaries |
|---|---:|---:|---:|
| 1 | 46 us | 46 us | 31 |
| 2 | 48 us | 50 us | 31 |
| 3 | 68 us | 68 us | 31 |

Python replay and actual analyzer source compiled against host IDF shims produce
zero incomplete groups and final PASS. Original unpatched analyzer source
produces nine incomplete groups on the same capture. This proves grouping;
30 s trains do not prove a 40-minute RTC rate result. The ESP-IDF target build
and hardware preflight remain pending.
