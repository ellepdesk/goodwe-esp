# Host CLI

Runs the same `lib/aa55` code as the ESP firmware (framing, registration, polling,
execute commands) on Linux or macOS. It talks to the inverter over:

- **USB HID**: the inverter's own USB port. Frames go out as `CC 99 <len>` + AA55 in an
  output report, per GoodWeUSBLogger.
- **Serial**: a USB-UART adapter on the A11 header, or an RS485 adapter. Plain AA55.

## Build

```
brew install hidapi                         # macOS
sudo apt install libhidapi-dev pkg-config   # Debian/Ubuntu
make
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
- Replies arrive in 8-byte input reports. After the frame, the rest of the device's
  buffer comes back too: zeros or stale bytes from an earlier frame. The parser skips them.
- **While the USB port is in use, the WiFi module stops uploading**, so the logging proxy
  and Home Assistant get no data until it is unplugged.
- Linux needs access to the hidraw node, e.g. `/etc/udev/rules.d/99-goodwe.rules`:
  `KERNEL=="hidraw*", ATTRS{idVendor}=="0084", ATTRS{idProduct}=="0041", MODE="0660", GROUP="plugdev"`
  then `sudo udevadm control --reload && sudo udevadm trigger`.
- Without `libhidapi-dev` (or root), build hidapi's hidraw backend from source; it only
  needs `libudev-dev`:
  ```
  gcc -O2 -c -I hidapi/hidapi hidapi/linux/hid.c -o hid.o
  g++ -O2 -std=c++17 -I../../lib/aa55/src -I hidapi/hidapi -DAA55_BAUD_DEFAULT=9600 \
      -o goodwe goodwe.cpp ../../lib/aa55/src/*.cpp hid.o -ludev
  ```

## Without hardware

`fake_inverter.py` simulates an inverter on a pseudo-terminal:

```
python3 fake_inverter.py &          # prints e.g. /dev/ttys004
./goodwe --serial /dev/ttys004 --poll 2 monitor
python3 fake_inverter.py --nak &    # answer execute commands with NAK
```
