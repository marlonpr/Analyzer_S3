# Analyzer v6 — one-device SQW ISR diagnostic

Default channel labels:

- GPIO4: ESP01_SQW (wire directly to DS3231 SQW / ESP GPIO27 node)
- GPIO5: ESP01_SQW_ISR (wire to ESP01 GPIO17)
- GPIO6: ESP01_COMMIT (ESP01 GPIO33)
- GPIO7: ESP01_REFRESH (ESP01 GPIO16)
- GPIO15: ESP02_SQW (optional physical SQW comparison)

Use level=1 edges from ESP01_SQW as the physical DS3231 rising edge. Pair each with the next ESP01_SQW_ISR edge. The difference is GPIO ISR service latency plus the first esp_timer_get_time() call and direct marker write.
