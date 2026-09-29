# Analyzer v5 — commit + refresh adoption

Default channel map for the v6.14 two-device validation:

| Analyzer input | Edge label | Connect from |
|---|---|---|
| GPIO4 | ESP01_COMMIT | ESP01 GPIO33 |
| GPIO5 | ESP02_COMMIT | ESP02 GPIO33 |
| GPIO6 | ESP01_REFRESH | ESP01 GPIO16 |
| GPIO7 | ESP02_REFRESH | ESP02 GPIO16 |
| GPIO15 | AUX | optional |

All grounds must be common. Each `ANZ|EDGE` record retains the sampled level as
the final field, so commit-marker polarity can be modeled explicitly.

The refresh marker toggles when the classic ESP32 core-1 HUB75 refresh loop sees
a newly published active-frame index at the start of a complete scan cycle.
Therefore `REFRESH - COMMIT` measures software publication to refresh adoption.
