# Per-second capture changes

The original analyzer was correct for one START edge per trial but unsuitable
for the v6.6 display-commit stream for two reasons:

1. GPIO interrupts were `GPIO_INTR_POSEDGE`, so every falling toggle was missed.
2. `edge_mask` suppressed all later edges from a channel after its first event.

This version uses `GPIO_INTR_ANYEDGE` and records every event while `armed`.
`seen_mask` is retained only for the legacy SUMMARY presence mask.

Capture integrity is explicit:

- `edge_counts[5]` counts every ISR event accepted while armed.
- `dropped_events` increments if the ISR queue is full.
- `ANZ|COUNTS|...` is emitted at END.
- `ANZ|STATUS|...` exposes live counts during a long test.

The `ANZ|EDGE` wire format itself was intentionally left unchanged so existing
log parsers that consume the original fields do not need a format change.
