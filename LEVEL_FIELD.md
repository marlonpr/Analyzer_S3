# Analyzer v4 edge-level field

Each `ANZ|EDGE` record now appends the sampled GPIO level immediately after the ISR-entry timestamp:

```
ANZ|EDGE|run|trial|channel|device|timestamp_us|level
```

`level=1` is a rising transition and `level=0` is a falling transition for a clean toggle marker. The timestamp is still taken first on ISR entry; reading the level happens afterward, so the added diagnostic does not move the recorded edge time.

This field is intended to quantify the observed ~3.9 us rising/falling polarity term and to detect parity mismatches between devices. The bundled `analyze_pair.py` accepts both the old 7-field records and the new 8-field records.
