# Pulse-marker analyzer

Use with Factory Timer v6.7 presentation diagnostic.

Wiring for ESP01/ESP02:

- ESP01 GPIO33 -> analyzer GPIO4
- ESP02 GPIO33 -> analyzer GPIO5
- direct common GND between analyzer, ESP01 and ESP02

The timer emits one 20 us positive pulse after every display commit. The analyzer uses GPIO_INTR_POSEDGE and applies a 100000 us per-channel refractory window to reject ringing/chatter.

For a 20 second countdown, expected count is 21 events per device (START + 20 one-second commits), with dropped=0. `filtered` may be nonzero if the wiring produces extra threshold crossings; those are intentionally rejected.
