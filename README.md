# goodwe-esp

A XIAO ESP32-S3 on the GoodWe GW3600-DS's external USB port, replacing what the built-in
HF-A11 WiFi module does: it polls the inverter and publishes to Home Assistant over MQTT,
with power limiting. No DNS spoof, no `goodwe_logging_proxy`, and the inverter stays closed.

The HF-A11 does not decode anything: its 66-byte `/Acceptor/Datalog` upload is
`00` + 16-byte serial + `01 00 2e` + the inverter's 46-byte AA55 running-info reply. The
USB port speaks the same AA55 protocol (verified with the host CLI, 2026-10-30), and the
WiFi module stops uploading while USB is in use, so the ESP takes over cleanly. Unplug it
and the A11 resumes: that is the rollback.

## Status

- **Linux**: [`tools/host`](tools/host/README.md) has a CLI and `goodwe-mqtt`, a service
  that already does what the firmware will do, from any Linux board on the inverter's USB
  port. This proves the shared libraries on the real inverter first.
- **ESP firmware**: only the phase-3 UART sniffer so far (below, now a fallback). The USB
  host firmware is next.

`lib/aa55` (protocol, bus master) and `lib/goodwe_ha` (HA discovery, power-limit logic)
are shared by both.

```
pio test -e native     # protocol library tests
```

## Plan: USB host on the S3

The S3 has a USB OTG controller (the C3 doesn't, so the C3 drops out). Its USB-C port runs
as a USB host to the inverter's HID device `0084:0041`.

1. **Descriptors.** Record `lsusb -v -d 0084:0041` from Linux (Orange Pi or Septimus):
   speed (low or full), and whether there is an interrupt OUT endpoint or output reports
   go as `SET_REPORT` control transfers. Known so far: 8-byte input reports, 72-byte output
   reports, no report IDs.
2. **Power.** As host the S3 must put 5 V on VBUS; the inverter's port supplies none.
   Feed the XIAO's 5V pin from a supply at the inverter and check, with the schematic and
   a meter, that it reaches the USB-C VBUS. If it doesn't, use an OTG Y-cable that injects
   5 V. Cable: USB-C (host) to whatever the inverter's port is.
3. **USB transport.** ESP-IDF's raw `usb_host` API, which ships with the current Arduino
   core (2.0.17, IDF 4.4), so no HID driver is needed: open the device, claim interface 0,
   keep an IN transfer on the interrupt endpoint, and send `CC 99 <len>` + frame, padded to
   72 bytes, via the OUT endpoint or `SET_REPORT`. Handle detach: the DS powers off at
   night and the device disappears with it. If IDF 4.4 can't handle a low-speed device,
   move to Arduino 3 (IDF 5).
4. **Firmware.** The transport feeds `aa55::Master`; `lib/goodwe_ha` provides discovery,
   state and the power limit, exactly as in `goodwe-mqtt`. Add WiFi, an MQTT client
   (PubSubClient) and ArduinoOTA.
5. **Logs and updates.** The USB port is busy, so logging goes over WiFi (an MQTT log
   topic), with UART0 on the XIAO's TX/RX pins and a USB-UART adapter as fallback. Flash
   over USB once, before plugging into the inverter, then use OTA.
6. **Mounting.** The XIAO sits outside the inverter at the capped USB port. Use its u.FL
   antenna, since the A11 needed an external one too. Seal the open port and the board
   against the weather.

## Fallback: A11 header sniffer

`src/main.cpp` listens passively on the A11's UART. It's only needed if the USB route
fails, e.g. to drive the inverter UART directly instead.

```
pio run -e c3 -t upload && pio device monitor
```

HF-A11 header: 2x7, **2 mm pitch**. Measure the pins on the carrier before connecting
anything. The A11 is a 3.3 V part, but the manual does not say whether the pins tolerate 5 V.

| A11 pin | Signal               | XIAO C3 | XIAO S3 |
|---------|----------------------|---------|---------|
| 1       | GND                  | GND     | GND     |
| 3       | UART_TXD (A11 → inv) | GPIO3   | GPIO1   |
| 4       | UART_RXD (inv → A11) | GPIO4   | GPIO2   |

Pin numbers follow the High-Flying manual (pin 1 GND, pin 2 3.3 V, then 3/4 TXD/RXD).
Find pin 1 on the carrier with a meter (continuity to the inverter's ground) rather than
trusting the silkscreen. Power the XIAO from USB, not from pin 2. Never pull pin 3 up: it
is a boot strap on the A11 with an internal 1 kΩ pull-down.

Open questions for a sniff: is the line 9600 8N1 (`AA55_BAUD` in `platformio.ini`), does
the A11 act as master (discovery, register, `01 01` polls), and what else does it send at
startup?

## Protocol

Framing and checksum follow [GoodWeLogger](https://github.com/jantenhove/GoodWeLogger):
`AA 55 src dst ctrl func len data… sumHi sumLo`, where the sum is a 16-bit sum of all
preceding bytes.

## Docs

- [docs/protocol.md](docs/protocol.md): AA55 command table, including curtailment (`03 1E`).
- [docs/reverse-engineering.md](docs/reverse-engineering.md): how that table was recovered
  from GoodWe's EzExplorer.
