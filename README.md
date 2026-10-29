# goodwe-esp

ESP32 (XIAO C3 or S3) replacement for the HF-A11 WiFi module inside a GoodWe GW3600-DS.

The HF-A11 does not decode anything: its 66-byte `/Acceptor/Datalog` upload is
`00` + 16-byte serial + `01 00 2e` + the inverter's 46-byte AA55 running-info reply.
So the ESP only has to speak AA55 on the inverter UART and publish the result itself,
which removes the DNS spoof and the `goodwe_logging_proxy` container.

## Status

Phase 3: a receive-only sniffer. It logs both directions as raw hex plus decoded AA55
frames over USB serial.

```
pio test -e native     # protocol library tests
pio run -e c3 -t upload && pio device monitor
```

## Wiring (sniffer)

HF-A11 header: 2x7, **2 mm pitch**. Measure the pins on the carrier before connecting
anything. The A11 is a 3.3 V part, but the manual does not say whether the pins tolerate 5 V.

| A11 pin | Signal               | XIAO C3 | XIAO S3 |
|---------|----------------------|---------|---------|
| 1       | GND                  | GND     | GND     |
| 3       | UART_TXD (A11 → inv) | GPIO3   | GPIO1   |
| 4       | UART_RXD (inv → A11) | GPIO4   | GPIO2   |

Pin numbers follow the High-Flying manual (pin 1 GND, pin 2 3.3 V, then 3/4 TXD/RXD).
Find pin 1 on the carrier with a meter (continuity to the inverter's ground) rather than
trusting the silkscreen. Power the XIAO from USB, not from pin 2. Never pull pin 3 up: it is a boot strap on the
A11 with an internal 1 kΩ pull-down.

## Open questions for the sniff

- Is the line 9600 8N1? `AA55_BAUD` in `platformio.ini`; if the raw dump is garbage, try others.
- Who is the master: does the A11 send discovery / register / `01 01` polls?
- What address does the inverter get, and what is the `01 00` in the upload envelope?
- Does the A11 send anything else (ID info, settings reads) at startup?

## Protocol

Framing and checksum follow [GoodWeLogger](https://github.com/jantenhove/GoodWeLogger):
`AA 55 src dst ctrl func len data… sumHi sumLo`, where the sum is a 16-bit sum of all
preceding bytes.

## Docs

- [docs/protocol.md](docs/protocol.md): AA55 command table, including curtailment (`03 1E`).
- [docs/reverse-engineering.md](docs/reverse-engineering.md): how that table was recovered
  from GoodWe's EzExplorer.
