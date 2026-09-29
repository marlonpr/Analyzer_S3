# Analyzer v6 — commit, refresh, and DS3231 SQW

Default wiring for the two-device v6.15 diagnostic:

| Analyzer input | Signal | Connect to |
|---|---|---|
| GPIO4 | ESP01_COMMIT | ESP01 GPIO33 |
| GPIO5 | ESP02_COMMIT | ESP02 GPIO33 |
| GPIO6 | ESP01_REFRESH | ESP01 GPIO16 |
| GPIO7 | ESP02_REFRESH | ESP02 GPIO16 |
| GPIO15 | ESP01_SQW | ESP01 GPIO27 / its DS3231 SQW node |
| GPIO16 | ESP02_SQW | ESP02 GPIO27 / its DS3231 SQW node |

Connect all grounds. Do **not** tie the two SQW nodes together; each goes only to its own analyzer input.

Analyzer inputs now disable both internal pull-up and pull-down resistors. This is important for the open-drain SQW nodes; each timer ESP32 already enables its own SQW pull-up.

`ANZ|EDGE` keeps the final sampled level field. For SQW comparison use level=1 (physical rising edges), which corresponds to the positive-edge interrupt used by `rtc_discipline`.
