# goodwe-esp

An ESP32-S3 on the GoodWe GW3600-DS's external USB port, replacing what the built-in
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

Board: a dual-USB ESP32-S3 (DevKitC-1 layout). Its native USB-C port, the one marked "USB"
or "OTG", runs as a USB host to the inverter's HID device `0084:0041`. The other USB-C
port, marked "COM" or "UART", has a USB-serial bridge and stays free for power, flashing
and logs. The C3 has no USB OTG controller, so it drops out.

1. **Descriptors.** Record `lsusb -v -d 0084:0041` from Linux (Orange Pi or Septimus):
   speed (low or full), and whether there is an interrupt OUT endpoint or output reports
   go as `SET_REPORT` control transfers. Known so far: 8-byte input reports, 72-byte output
   reports, no report IDs.
2. **Power.** As host the S3 must put 5 V on VBUS; the inverter's port supplies none. On
   these boards the native port's VBUS is usually isolated by a diode; many clones have a
   solder jumper (often marked "IN-OUT" or "USB-OTG") that connects it to the 5 V rail.
   Close it, then power the board through the COM port from a 5 V supply at the inverter,
   and measure 5 V on the native port's VBUS before plugging in the inverter. Cable: USB-C
   to USB-B ("printer cable"); the inverter's port is USB-B.
3. **USB transport.** ESP-IDF's raw `usb_host` API, which ships with the current Arduino
   core (2.0.17, IDF 4.4), so no HID driver is needed: open the device, claim interface 0,
   keep an IN transfer on the interrupt endpoint, and send `CC 99 <len>` + frame, padded to
   72 bytes, via the OUT endpoint or `SET_REPORT`. Handle detach: the DS powers off at
   night and the device disappears with it. If IDF 4.4 can't handle a low-speed device,
   move to Arduino 3 (IDF 5).
4. **Firmware.** The transport feeds `aa55::Master`; `lib/goodwe_ha` provides discovery,
   state and the power limit, exactly as in `goodwe-mqtt`. Add WiFi, an MQTT client
   (PubSubClient) and ArduinoOTA.
5. **Logs and updates.** Over the COM port (serial console and flashing) while on the
   bench. Once mounted, use OTA, plus an MQTT log topic so the COM port needn't be
   reachable.
6. **Mounting.** The board sits outside the inverter at the capped USB port. Seal the open port and the board against the weather.

### Shopping list

- Dual-USB ESP32-S3 board (DevKitC-1 layout: native "USB/OTG" port plus "COM" port)
- USB-C to USB-B cable ("printer cable"), native port to the inverter
- 5 V USB power supply at the inverter, into the COM port
- M25×1.5 cable gland for the open USB port. Check the cap's thread first. Get one that
  seals a 4–5 mm cable (reducing insert) and whose bore passes the USB-C plug: the
  USB-B end stays in the inverter's socket, so thread the cable C end first from that side.
- Weatherproof enclosure for the board

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
