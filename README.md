# Analyzer_v10 — 3 physical devices, 1 Hz-aware pairing

This analyzer intentionally uses **only three physical COMMIT inputs**:

- ESP01
- ESP02
- ESP03

ESP04 and ESP05 are **controller-only devices**. They are not wired to the
analyzer and do not participate in the physical max-min calculation.

## Default wiring

| Timer | Timer GPIO | Analyzer ESP32-S3 GPIO |
|---|---:|---:|
| ESP01 | GPIO33 COMMIT | GPIO5 |
| ESP02 | GPIO33 COMMIT | GPIO15 |
| ESP03 | GPIO33 COMMIT | GPIO16 |

All grounds must be common.

## Why v10 does not pair raw edge #N

Reset/re-arm/START_AT can toggle the COMMIT pin outside the countdown. v9
paired raw ordinal edges, so one isolated edge shifted one board by a full
second.

v10 first decides whether an edge belongs to a genuine 1 Hz countdown train.

A same-channel pair qualifies when:

    995000 us <= delta <= 1005000 us

and the logical level alternates.

Only 1 Hz-qualified edges are allowed into cross-device association.

Qualified edges from the three boards are then associated within:

    5000 us

This is deliberately much wider than the 100 us qualification limit. A real
200 us fault must still be paired and reported as a trip, not silently
discarded.

## Start-only physical gate

For each detected countdown train, only its first three qualified boundaries
are hard-gated:

    max(ESP01, ESP02, ESP03) - min(...) <= 100 us

Later boundaries are still reported, but with:

    gated=0
    result=INFO

This avoids incorrectly failing a long countdown because of legitimate RTC
module-to-module ppm drift.

## Multiple countdowns in one BEGIN/END capture

Supported.

If the gap between completed 1 Hz boundary groups is greater than 1.5 s, v10
starts a new train and resets the three-boundary start gate.

So one Hercules capture may contain, for example:

    BEGIN|1|1<CR>
      20 s countdown #1
      20 s countdown #2
      20 s countdown #3
    END|1|1<CR>

and the analyzer should report three `ANZ|TRAIN|...` records.

## Hercules line endings

CR, LF, and CRLF are accepted.

    BEGIN|1|1<CR>

must answer:

    ANZ|ACK|BEGIN|1|1

## Build

    cd Analyzer_v10_3device_1hz
    idf.py set-target esp32s3
    idf.py menuconfig
    idf.py build
    idf.py -p COMx flash monitor

The ESP-IDF 6.x component requirements are already included:

    REQUIRES esp_driver_gpio esp_timer esp_driver_uart

## Important output

Raw input:

    ANZ|EDGE|...

1 Hz-qualified edge:

    ANZ|QUALIFIED|...

Physical association:

    ANZ|SKEW|...|train=1|boundary=0|...|range_us=59|gated=1|result=PASS

Per-countdown result:

    ANZ|TRAIN|...|train=1|boundaries=21|gated=3|start_worst_range_us=60|...|result=PASS

Final capture result:

    ANZ|TRIPWIRE|...|mode=START_ONLY|limit_us=100|first_boundaries=3|trains=3|...|result=PASS

## ESP04 and ESP05

The analyzer intentionally knows nothing about their COMMIT pins.

For the 5-device stage:

- ESP01/02/03: physical analyzer + controller telemetry
- ESP04/05: controller telemetry only

That means the 100 us **physical** tripwire applies only to ESP01/02/03.
It does not prove physical COMMIT skew for ESP04/05.

If physical validation of ESP04/05 is later required without adding analyzer
inputs, rotate the three analyzer leads for a separate run, for example:

    ESP01 / ESP04 / ESP05

while keeping ESP01 as the physical bridge between captures.
