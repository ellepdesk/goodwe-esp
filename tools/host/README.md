# Host tools

Two programs running the same `lib/aa55` code as the ESP firmware (framing, registration,
polling, execute commands) on Linux or macOS:

- `goodwe`: CLI for sniffing, monitoring and one-off commands.
- `goodwe-mqtt`: service that publishes to Home Assistant, the Linux counterpart of the
  ESP firmware (see below).

Both talk to the inverter over:

- **USB HID**: the inverter's own USB port. Frames go out as `CC 99 <len>` + AA55 in an
  output report, per GoodWeUSBLogger.
- **Serial**: a USB-UART adapter on the A11 header, or an RS485 adapter. Plain AA55.

## Build

```
brew install hidapi mosquitto                                 # macOS
sudo apt install build-essential libhidapi-dev libmosquitto-dev pkg-config    # Debian/Ubuntu/Armbian
make                # both; `make goodwe` needs only hidapi
```

## Use

```
./goodwe --hid monitor                       # register, print running info every 10 s
./goodwe --hid --raw monitor                 # ... plus every byte in and out
./goodwe --serial /dev/ttyUSB0 sniff         # passive: print frames, send nothing
./goodwe --hid --allow-control power 100     # real power limit, % of rated
./goodwe --hid --allow-control stop          # / start
```

`power`, `start` and `stop` exit 0 on ACK and 1 on NAK or no reply. See
[../../docs/protocol.md](../../docs/protocol.md) before sending anything else.

## USB HID notes

Verified on a GW3600-DS from Ubuntu (kernel 7.0, hidraw), 2026-10-30:

- The device is `0084:0041` ("HID USB 1.6"). The report descriptor has 8-byte input
  and 72-byte output reports, no report IDs. Writes work both padded (`--report-size 72`)
  and unpadded over hidraw; other platforms may need the padding.
- Full speed, with a single interrupt IN endpoint (`0x81`, 32-byte packets) and no OUT
  endpoint, so output reports go as `SET_REPORT` control transfers (2026-11-04, NanoPi
  NEO3).
- Replies arrive in 8-byte input reports. After the frame, the rest of the device's
  buffer comes back too: zeros or stale bytes from an earlier frame. The parser skips them.
- **While the USB port is in use, the WiFi module stops uploading**, so the logging proxy
  and Home Assistant get no data until it is unplugged.
- Linux needs access to the hidraw node: copy [99-goodwe.rules](99-goodwe.rules) to
  `/etc/udev/rules.d/`, then `sudo udevadm control --reload && sudo udevadm trigger`.
- Without `libhidapi-dev` (or root), build hidapi's hidraw backend from source; it only
  needs `libudev-dev`:
  ```
  gcc -O2 -c -I hidapi/hidapi hidapi/linux/hid.c -o hid.o
  g++ -O2 -std=c++17 -I../../lib/aa55/src -I hidapi/hidapi -DAA55_BAUD_DEFAULT=9600 \
      -o goodwe goodwe.cpp transport.cpp ../../lib/aa55/src/*.cpp hid.o -ludev
  ```

## goodwe-mqtt

Registers the inverter, polls it every 10 s and publishes to MQTT with Home Assistant
discovery. Topics, unique IDs and state fields are the same as goodwe_logging_proxy's
(shared code in `lib/goodwe_ha`), so HA keeps the existing entities and their history when
this takes over from the proxy.

```
./goodwe-mqtt --hid --broker 192.168.1.10                  # sensors only
./goodwe-mqtt --hid --broker 192.168.1.10 --allow-control  # + Power Limit, Stop/Start
```

- **Power Limit** (`--allow-control`): a number entity, 0–100 % of rated power, sent with
  execute `03 1E`. The inverter can't report its limit, so the service re-sends the last
  setting (kept as a retained MQTT message) after every registration and each time the
  inverter goes back to Normal, e.g. in the morning. It only sends in Normal mode, the only
  mode this was tested in, and gives up after 3 failed attempts per trigger. Without
  `--allow-control` the entity is removed and nothing is ever sent.
- **Power Limit (W)**: the same limit in watts, 0 to rated power. Rated power comes from
  the serial (`1 3600 DS…` → 3600 W) or `--rated-power`. The inverter takes whole
  percentages, so a request is rounded down to one: 2000 W on a 3600 W model becomes 55 %
  = 1980 W, and the W entity then shows 1980. The service republishes a W request as the
  retained percentage command, so both entities show one setting and a restart restores it.
- **Stop/Start Inverter** (`--allow-control`): two buttons for maintenance, **disabled by
  default** in HA (enable them on the device page) and listed under Configuration, so they
  stay off dashboards. Buttons rather than a switch: a stopped inverter reports plain Wait,
  so there is no on/off state to show. Start takes ~40 s to reach Normal. Untested whether
  a stop survives the nightly power-off, so don't leave it stopped overnight.
- **Night**: the DS switches off without PV, and its USB device disappears with it. The
  service logs that once and reopens the port when the inverter comes back. Sensors go
  unavailable after 10 minutes (`expire_after`), as with the proxy.
- **Poll interval** (`--poll`, whole seconds, default 10): 1 s is the useful minimum; the
  inverter refreshes AC power about once a second and answers a read in ~190 ms (see
  [protocol.md](../../docs/protocol.md#polling-rate-gw3600-ds-over-usb-2026-11-04)). Each
  poll updates 16 sensors in HA, so below a few seconds consider excluding most of them
  from the recorder. Power-limit commands are sent right away, not on the next poll.
- **The WiFi module stops uploading while USB is in use**, so this replaces the proxy rather
  than running alongside it. To roll back, unplug the USB cable.

### Install (Debian/Armbian)

```
make goodwe-mqtt
sudo install goodwe-mqtt /usr/local/bin/
sudo cp 99-goodwe.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
echo 'GOODWE_MQTT_ARGS=--hid --report-size 72 --broker 192.168.1.10 --allow-control' | sudo tee /etc/default/goodwe-mqtt
sudo cp goodwe-mqtt.service /etc/systemd/system/
sudo systemctl enable --now goodwe-mqtt
journalctl -fu goodwe-mqtt
```

## Without hardware

`fake_inverter.py` simulates an inverter on a pseudo-terminal:

```
python3 fake_inverter.py &          # prints e.g. /dev/ttys004
./goodwe --serial /dev/ttys004 --poll 2 monitor
python3 fake_inverter.py --nak &    # answer execute commands with NAK
./goodwe-mqtt --serial /dev/ttys004 --poll 2 --allow-control   # needs a broker on localhost
```
