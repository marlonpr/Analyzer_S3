# ESP32-S3 presentation-edge analyzer

This build is the long-capture version of the original START edge analyzer.
It is intended for the Factory Timer v6.6 display-commit test, where each
Factory Timer toggles its presentation diagnostic GPIO at visible START and
again at every displayed one-second boundary.

## What changed from the original analyzer

- Inputs use `GPIO_INTR_ANYEDGE` instead of rising-edge only.
- Every edge is captured while a trial is armed; there is no one-edge-per-channel filter.
- The original `ANZ|EDGE|...` record format is preserved.
- The edge queue is increased from 64 to 256 entries.
- Per-channel edge counts and a dropped-event counter are reported by `STATUS`
  and at `END`.
- The original `ANZ|SUMMARY|...` record is preserved for compatibility; its
  mask now means "this channel was seen at least once during this trial".

## Default analyzer input pins

| Device | Analyzer GPIO |
|---|---:|
| ESP01 | 4 |
| ESP02 | 5 |
| ESP03 | 6 |
| ESP04 | 7 |
| ESP05 | 15 |

For the current two-device test:

```text
ESP01 GPIO33  -> Analyzer GPIO4
ESP02 GPIO33  -> Analyzer GPIO5
ESP01 GND ----+-> Analyzer GND
ESP02 GND ----+
```

The Factory Timer GPIO33 signal is expected to start low and toggle on each
visible display commit. Therefore both rising and falling edges are meaningful.

## Serial protocol

The protocol remains line based at 115200 baud by default.

Start a long capture:

```text
BEGIN|1|1
```

The analyzer replies:

```text
ANZ|ACK|BEGIN|1|1
```

Each display commit produces the original edge record format:

```text
ANZ|EDGE|1|1|1|ESP01|123456789
ANZ|EDGE|1|1|2|ESP02|123456802
```

The final field is the analyzer `esp_timer_get_time()` timestamp in microseconds.
The Nth ESP01 record is paired with the Nth ESP02 record. Their difference is
the visible presentation spread at that second boundary.

During a run, `STATUS` reports cumulative counts and any queue drops:

```text
STATUS
ANZ|STATUS|armed=1|run=1|trial=1|mask=0x03|count=2|ESP01=900|ESP02=900|ESP03=0|ESP04=0|ESP05=0|dropped=0
```

After the countdown finishes:

```text
END|1|1
```

The analyzer emits the legacy summary plus the new integrity record:

```text
ANZ|SUMMARY|1|1|0x03|2
ANZ|COUNTS|1|1|ESP01=1801|ESP02=1801|ESP03=0|ESP04=0|ESP05=0|dropped=0
```

For a 30-minute countdown, 1801 edges per active device is expected when the
Factory Timer toggles once at visible START and once for each of the 1800 later
second boundaries through 00:00. The two active device counts must match and
`dropped` must be zero before using the capture for timing analysis.

Other commands are unchanged:

```text
PING
STATUS
CLEAR
```

## Important capture rule

Leave the analyzer armed for the entire countdown. Do not send `END` immediately
after START as was done in the old one-edge-per-trial workflow.

## Build

Use the same ESP-IDF target and build procedure as the original Analyzer_S3
project. Configuration names are unchanged, so existing sdkconfig values for
the analyzer input pins remain valid.
