# Analyzer S3 v8 — dual-device qualification

Six-channel ESP32-S3 logic analyzer build for the Factory Timer 30-minute
ESP01/ESP02 RTC/display qualification.

See `V8_DUAL_30MIN_QUALIFICATION.md` for wiring, capture procedure, calibration,
and analysis.

Default inputs:

- GPIO4  ESP01_SQW
- GPIO5  ESP01_COMMIT
- GPIO6  ESP01_REFRESH
- GPIO7  ESP02_SQW
- GPIO15 ESP02_COMMIT
- GPIO16 ESP02_REFRESH

Protocol remains `ANZ|EDGE|...`; v8 extends READY/STATUS/COUNTS to six channels.
