# v8 changes from v7

- Six inputs instead of three: SQW/COMMIT/REFRESH for ESP01 and ESP02.
- Existing ESP01 default pins remain GPIO4/5/6.
- ESP02 defaults are GPIO7/15/16.
- Six-bit seen mask (`0x3F` when all channels are observed).
- READY, STATUS and COUNTS report all six channels.
- Output formatting buffer enlarged for six-channel status records.
- Added 30-minute qualification analysis and channel-skew calibration tools.
- Capture mechanism remains high-impedance ANYEDGE with the same IRAM GPIO ISR
  service used by v7.
