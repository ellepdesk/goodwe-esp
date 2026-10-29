"""Simulated GoodWe inverter on a pseudo-terminal, for testing the CLI without hardware.

    python3 fake_inverter.py [--nak]
prints the pty path to pass to `goodwe --serial`. Answers registration, running-info
polls (with data from a real 2023 capture) and execute commands (ACK, or NAK with --nak).
"""
import os
import pty
import sys
import tty

SERIAL = b"00000DSU00000000"
INFO = bytes.fromhex(
    "071c0513002f00170919002e138a04550001013900000000"
    "0002da2900007ffc00100000000000000e3900000023"
)
OWN = 0x80


def frame(src, dst, ctrl, func, data=b""):
    body = bytes([0xAA, 0x55, src, dst, ctrl, func, len(data)]) + data
    s = sum(body) & 0xFFFF
    return body + bytes([s >> 8, s & 0xFF])


def frames(fd):
    buf = b""
    while True:
        chunk = os.read(fd, 256)
        if not chunk:
            return
        buf += chunk
        while True:
            i = buf.find(b"\xaa\x55")
            if i < 0 or len(buf) < i + 7:
                break
            n = 9 + buf[i + 6]
            if len(buf) < i + n:
                break
            f, buf = buf[i:i + n], buf[i + n:]
            if (sum(f[:-2]) & 0xFFFF) == int.from_bytes(f[-2:], "big"):
                yield f


def main():
    nak = "--nak" in sys.argv
    master, slave = pty.openpty()
    tty.setraw(slave)
    print(os.ttyname(slave), flush=True)
    address = None
    for f in frames(master):
        src, dst, ctrl, func, data = f[2], f[3], f[4], f[5], f[7:-2]
        print(f"rx {f.hex(' ')}", file=sys.stderr, flush=True)
        reply = None
        if ctrl == 0x00 and func == 0x00 and address is None:
            reply = frame(0x7F, OWN, 0x00, 0x80, SERIAL)
        elif ctrl == 0x00 and func == 0x01 and data[:16] == SERIAL:
            address = data[16]
            reply = frame(address, OWN, 0x00, 0x81)
        elif ctrl == 0x00 and func == 0x02:
            address = None
        elif dst == address and ctrl == 0x01 and func == 0x01:
            reply = frame(address, OWN, 0x01, 0x81, INFO)
        elif dst == address and ctrl == 0x03:
            reply = frame(address, OWN, 0x03, func | 0x80, bytes([0x15 if nak else 0x06]))
        if reply:
            os.write(master, reply)


if __name__ == "__main__":
    main()
