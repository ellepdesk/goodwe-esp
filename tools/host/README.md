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

- `0084:0041` is GoodWeUSBLogger's default, not yet confirmed on this inverter. Check with
  `lsusb` (Linux) or `system_profiler SPUSBDataType` (macOS), and pass `--hid=VID:PID`
  if it differs.
- Linux needs access to the hidraw node, e.g. `/etc/udev/rules.d/99-goodwe.rules`:
  `KERNEL=="hidraw*", ATTRS{idVendor}=="0084", ATTRS{idProduct}=="0041", MODE="0660", GROUP="plugdev"`
  then `sudo udevadm control --reload && sudo udevadm trigger`.
- If writes fail (likely on macOS), the device may want full-size output reports: retry
  with `--report-size 8` or `--report-size 64`.

## Without hardware

`fake_inverter.py` simulates an inverter on a pseudo-terminal:

```
python3 fake_inverter.py &          # prints e.g. /dev/ttys004
./goodwe --serial /dev/ttys004 --poll 2 monitor
python3 fake_inverter.py --nak &    # answer execute commands with NAK
```
