# AA55 command set, from EzExplorer 1.0.1.8

Recovered by static disassembly of `EzExplorer.exe` (GoodWe's 2015 USB tool, archived
copy of `goodwe.com.cn/enAdmin/File/EzExplorer_1.0.1.8_Setup.exe`, SHA-1 matching the
Wayback index). Nothing was executed. Command names come from the log strings in the
function that calls each frame builder.

## Framing

`AA 55 <src> <dst> <ctrl> <func> <len> <data…> <sum hi> <sum lo>`

- PC/logger address is `0x80`; `0x7F` is broadcast (registration).
- The checksum is a 16-bit sum of every preceding byte, header included (`0x401a60`).
- Over USB HID, a frame is prefixed with `CC 99 <len>` (from GoodWeUSBLogger; EzExplorer's
  HID layer was not traced).

## Execute replies

Execute commands (ctrl `0x03`) are answered with a one-byte frame:
`AA 55 <inv> 80 … 01 <ack> <sum>`. EzExplorer checks the checksum and the swapped
addresses, then reads the data byte:

- `0x06` (ASCII ACK): success.
- `0x15` (ASCII NAK): rejected; EzExplorer retries, giving up after 8 attempts.

The reply's ctrl/func bytes are not checked. Some handlers first refuse with
"Inverter in grid scheduling state!".

## Commands

| ctrl | func | len | Meaning (EzExplorer log text) | Data |
|------|------|-----|-------------------------------|------|
| 00 | 00 | 0 | Offline query / discovery (to 0x7F) | |
| 00 | 01 | 17 | Allocate address (to 0x7F) | 16-byte serial + address |
| 00 | 02 | 0 | Remove registration | |
| 01 | 00 | 0 | Read (unidentified) | |
| 01 | 01 | 0 | Read running info | |
| 01 | 02 | 0 | Read ID info | |
| 01 | 03 | 0 | Read settings ("Get Inverter Setting Info") | |
| 02 | 06 | 12 | Set inverter parameters (PvStartVolt, StartDelayTime, …) | 6 × u16 |
| 03 | 00 | 0 | Clear E-Total and H-Total | |
| 03 | 01 | 1 | Set safety country | country index 0–13 |
| 03 | 02 | 6 | Set RTC | 6 bytes, presumably Y M D h m s |
| 03 | 03 | 1 | Set country sub-variant ("Set Italian") | 0–4 |
| 03 | 1B | 0 | Start inverter | |
| 03 | 1C | 0 | Stop inverter | |
| 03 | 1E | 1 | **Adjust real power** | % of rated power, 0–100 |
| 03 | 1F | 1 | Adjust reactive power (power factor) | dialog range −0.99…−0.9, 0.9…1 |
| 03 | 20 | 0 | Start reactive power curve | |
| 03 | 21 | 0 | Stop reactive power curve | |
| 03 | 22 | 6 | Set EzBee wireless params | network id, channel |
| 03 | 27 | 1 | Unidentified | |
| 03 | 28 | 2 | Set function switch | 2 bytes |

### Real power adjust

Builder `0x5506c0`: `AA 55 80 <addr> 03 1E 01 <pct> <sum hi> <sum lo>`.
The dialog (IDD 191) validates the input with `DDV_MinMaxByte(0, 100)`.

## Running info (01/81)

Field layout as in `decode_running_info`. EzExplorer gives names for two of the fields.

Work mode (map filled at `0x483632`): 0 Wait, 1 Normal, 2 Fault, 3 Permanent Fault.

Error code, a 32-bit big-endian bitmask (tested bit by bit in `0x425b00`):

| Bit | Error | Bit | Error |
|---|---|---|---|
| 0 | GFCI Device Failure | 15 | PV Over Voltage |
| 1 | AC HCT Failure | 16 | External Fan Failure |
| 2 | TBD | 17 | Vac Failure |
| 3 | DCI Consistency Failure | 18 | Isolation Failure |
| 4 | GFCI Consistency Failure | 19 | DC Injection High |
| 9 | Utility Loss | 22 | Fac Consistency Failure |
| 10 | Ground I Failure | 23 | Vac Consistency Failure |
| 11 | DC Bus High | 25 | Relay Check Failure |
| 12 | Internal Fan Failure | 29 | Fac Failure |
| 13 | Over Temperature | 30 | EEPROM R/W Failure |
| 14 | Auto Test Failure | 31 | Internal Communication Failure |

GoodWe's protocol PDF (Table 3-7) has the same table except bit 12 "Internal Version
Unmatch" (EzExplorer: Internal Fan Failure) and bit 16 "Fan Failure" (EzExplorer: External
Fan Failure). The PDF's Work Mode table (3-6) didn't survive text extraction.

Data bytes 32–43, between the hour counter and today's energy, are labelled as last-fault
values in both EzExplorer and GoodWeUSBLogger: temperature, PV1 V, PV2 V, line V, line Hz
and GFCI. The 2023 GW3600-DS capture (3042 records) doesn't fit those labels:

| Bytes | Observed |
|---|---|
| 32–33 | always `0x0010` |
| 34–35 | `0x0800` only in morning Wait, else 0 |
| 36–39 | always 0 |
| 40–41 | changes every record; ≈ PV1 V in Wait, ~360 V while producing (÷10): DC bus voltage |
| 42–43 | alternates 0 / 20 between records |

### Real power adjust: verified on the GW3600-DS (2026-10-30, over USB)

The inverter ACKs `03 1E` (`AA 55 80 11 03 1E 01 64 02 16` → `AA 55 11 80 03 9E 01 06 02 38`)
and limits output to the given % of rated power:

| Setting | AC output |
|---|---|
| 100 % (before) | 268–278 W |
| 5 % | 185–189 W from the next poll (≤ 10 s); PV2 current to 0 A |
| 100 % (restored) | 359 W at once |

A 10 % test earlier was inconclusive: production (~290 W) stayed below the 360 W limit.

The limit can't be read back. Replies to `01 00`, `01 01`, `01 02` and `01 03` were
compared at 100 % and 50 %: only the live measurements in running info changed.

## Read replies (GW3600-DS, firmware V1.16)

Layouts per GoodWe's "Inverter.VS.EzFamily PROTOCOL V1.1" (in the GoodWeUSBLogger repo), Tables 3-8 and 3-9.

| Read | Reply |
|---|---|
| `01 00` | 23 bytes `00 01 02 03 04 07 0a 0d 0e 0f 10 11 12 13 14 15 16 17 18 19 1c 1f 20`: the running-info data indexes (PDF Table 3-3) this model fills. Missing are exactly the L2/L3 fields `05 06 08 09 0b 0c 1a 1b 1d 1e` of a single-phase unit. |
| `01 02` | firmware `V1.16` (5), model `GW3600-DS` (10), manufacturer, blank here (16), serial (16), nominal PV voltage `3600` = 360.0 V (4), internal version, all `00` (12), safety country `0x14` (1) |
| `01 03` | 6 × u16 BE: 1500, 30, 1840, 2530, 4803, 5097 = PV start 150.0 V, start delay 30 s, Vac 184.0–253.0 V, Fac 48.03–50.97 Hz |

## Firmware updater (not traced)

EzExplorer has a firmware flasher ("Update Program" in the Inverter menu; window "Flash",
log "Welcome to use EzFlash!"). This section is from its strings and the 2011 help page
"Update program of inverter" (USB mode); the frames it sends were not disassembled.

- Dialogs: `GWInverterFlashDlg` (USB) plus wireless, dual-wireless and three-phase
  wireless variants for the old EzBee radio ("Wireless Update Program").
- Automatic mode takes the first `*.mot` in `C:\FlashFile`; manual mode picks a `*.out` or
  `*.mot`. The help only says the file is a "compiled special file" from GoodWe. No
  firmware files ship with the installer, and none were found online.

Two update routines:

| Routine | File | Log sequence |
|---|---|---|
| `0x453560` master, `0x453b20` slave, `0x4541f0` program/verify | `.out` (TI COFF, as for a C2000 DSP) | "Master cpu update in progress...", erase with three acknowledged steps ("erase ack information1–3"), "Programming Start", "Programming complete, start verify", "Verify complete!", then the same for "Slave cpu". Any failure: "Please Reset Device for Reflash!" |
| `0x457ac0` setup, `0x457f60` transfer | `.mot` (Motorola S-record, as for Renesas MCUs) | maps the file to `tmp.bin`, handshake ("Did not receive hand-shaking information"), acknowledge, transfer with "%d %%" and "%d seconds remaining", "Firmware Update Completed!" |

The `.mot` setup builds a parameter table using address `0x11`, the address the GW3600-DS
gets on registration. Which routine applies to which model is unknown.

The two-CPU split matches the protocol PDF's error bit 12, "Internal Version Unmatch"
("master and slave firmware version is unmatch"). The GW3600-DS reports `V1.16` with an
empty internal version (see `01 02` above); the help's Property screenshot shows a
GW4600-SS on `E1.00`.

**Don't run any of this against the inverter**: an interrupted master/slave flash asks
for a reset and reflash, and there is no known firmware file to reflash with.

## Not yet known

- Whether the limit persists across restarts, and whether every write wears flash. Don't
  send it every poll; send only on change until this is known.
- The meaning of the `03 28` function-switch bits; it may gate features such as power
  limiting.

**Never send `03 01`, `03 03` or `02 06` without knowing the exact values: they change
grid-protection settings.**
