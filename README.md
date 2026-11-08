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

- **ESP firmware** (`src/usb`, env `s3-usb`): running on the inverter since 2026-11-08.
  Registration, polling, HA discovery and the power limit verified on the real inverter.
- **Linux**: [`tools/host`](tools/host/README.md) has a CLI and `goodwe-mqtt`, a service
  that does the same from any Linux board on the inverter's USB port. It proved the shared
  libraries first and stays useful for experiments.

`lib/aa55` (protocol, bus master), `lib/goodwe_ha` (HA discovery, control entities, and
`ha::Bridge`, which ties them to the inverter) and `lib/status_led` are shared by both, so
the ESP firmware is only WiFi, MQTT, OTA and USB glue.

```
pio test -e native     # library tests, including the bridge against a simulated inverter
```

## Hardware

- **Board**: dual-USB ESP32-S3 in the DevKitC-1 layout (the one in use is an N16R8 clone
  marked "678AB V0.0.1"). With the module facing up, the left USB-C is the native port and
  goes to the inverter; the right one ("COM", CH343 USB-serial) is for power, flashing and
  logs.
- **VBUS**: as host the S3 must put 5 V on the native port; the inverter's port supplies
  none. On this board the native port's VBUS sits behind a diode; bridge the "USB-OTG"
  pads on the back to connect it to the 5 V rail. "IN-OUT" on the front is for the 5Vin
  pin; leave it open.
- **Cable**: USB-C to USB-B ("printer cable"); the inverter's port is USB-B.
- **LED**: the on-board WS2812 on GPIO48 shows the inverter's state (see `platformio.ini`).

The inverter's USB port is a HID device `0084:0041`: full speed, one interface, a single
interrupt IN endpoint `0x81` (32-byte packets) and no OUT endpoint, so frames go out as
`SET_REPORT` control transfers (`CC 99 <len>` + frame, padded to 72 bytes). The firmware
uses ESP-IDF's raw `usb_host` API from the Arduino core (2.0.17, IDF 4.4); no HID driver.

### Building

```
cp secrets.ini.example secrets.ini   # WiFi, MQTT broker, optional OTA password
pio run -e s3-usb -t upload          # first time over the COM port
pio device monitor                   # logs on the COM port (opening it resets the ESP)
pio run -e s3-usb -t upload --upload-port goodwe-esp.local   # later, over WiFi (OTA)
```

Logs also go to MQTT topic `goodwe-esp/log`. Same entities as `goodwe-mqtt` with
`--allow-control` (`-DALLOW_CONTROL=0` drops the control entities), so only one of the two
should be on the inverter at a time.

### Mounting

The board sits outside the inverter at the capped USB port; seal the open port and the
board against the weather:

- M25×1.5 cable gland for the open USB port. Check the cap's thread first. Get one that
  seals a 4–5 mm cable (reducing insert) and whose bore passes the USB-C plug: the
  USB-B end stays in the inverter's socket, so thread the cable C end first from that side.
- Weatherproof enclosure for the board, and 5 V into the COM port.

## Protocol

Framing and checksum follow [GoodWeLogger](https://github.com/jantenhove/GoodWeLogger):
`AA 55 src dst ctrl func len data… sumHi sumLo`, where the sum is a 16-bit sum of all
preceding bytes.

## Docs

- [docs/protocol.md](docs/protocol.md): AA55 command table, including curtailment (`03 1E`).
- [docs/reverse-engineering.md](docs/reverse-engineering.md): how that table was recovered
  from GoodWe's EzExplorer.
