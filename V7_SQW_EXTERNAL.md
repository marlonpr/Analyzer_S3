# Analyzer v7 — external SQW correlation

GPIO4 <- physical DS3231 SQW / timer GPIO27 node
GPIO5 <- timer GPIO33 ISR publish marker
GPIO6 <- timer GPIO16 refresh-adoption marker
common ground required

There is deliberately no GPIO17 SQW-ISR marker. v6.16 firmware dumps its raw internal SQW timestamps after the run; `tools/analyze_sqw_latency.py` pairs those timestamps with the analyzer's physical rising edges using an offset+rate fit.
