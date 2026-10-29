# Reverse engineering EzExplorer

How the AA55 command table in [protocol.md](protocol.md) was recovered from GoodWe's
EzExplorer, the Windows USB tool for inverters of the GW3600-DS generation. The analysis
was entirely static: the installer and program were unpacked and disassembled on macOS
and never executed. Scripts are in [`tools/re`](../tools/re).

## Why

The ESP replacement for the HF-A11 will be the bus master on the inverter's AA55 serial
link. The public projects (GoodWeLogger, GoodWeUSBLogger, Goodwe2PVoutput) only use
registration and the read commands. The question was whether the protocol also has
control commands, curtailment in particular. EzExplorer talks the same protocol over
USB HID and has the menus to drive them.

## 1. Getting the installer

The manuals point to `www.goodwe.com.cn`, which no longer resolves, and GoodWe's current
download search (`POST en.goodwe.com/api/searchkeys.html`) returns `null` for
"EzExplorer". Download aggregators list `ezexplorer_1.0.1.8_setup.exe` (2015-05-29, ~21 MB)
but block scripted downloads or have dead links.

The Wayback Machine's CDX index has GoodWe's own copy:

```
curl "http://web.archive.org/cdx/search/cdx?url=goodwe.com.cn&matchType=domain&output=txt&filter=urlkey:.*xplorer.*"
…/enadmin/file/ezexplorer_1.0.1.8_setup.exe 20150325023344 …/EzExplorer_1.0.1.8_Setup.exe application/octet-stream 200 RSTJ2MC4XV6LPGFRW7GMRT7GTUEG3NWG 21096063
```

Fetched with the `id_` suffix, which returns the original bytes without the Wayback
rewriting:

```
curl -L -o EzExplorer_1.0.1.8_Setup.exe \
  "http://web.archive.org/web/20150325023344id_/http://www.goodwe.com.cn/enAdmin/File/EzExplorer_1.0.1.8_Setup.exe"
```

The CDX digest is a base32 SHA-1. The download's SHA-1 in base32 is
`RSTJ2MC4XV6LPGFRW7GMRT7GTUEG3NWG`, a match, so this is the file GoodWe served in 2015.
In hex: `8ca69d305cbd7cb798b1b7ccc8cfe69d086db6c6`.

## 2. Unpacking

`file` says PE32 GUI; its strings say InstallShield (2004-era runtime, with MSI
handlers). 7-Zip can't open it. The payload is not in the PE image: `pefile` puts the end
of the last section at offset 466 948, and the 21.6 MB after that is an overlay with a
simple layout. Each entry is four NUL-terminated strings followed by raw bytes:

```
"data1.cab\0" "Disk1\data1.cab\0" "0.0.0.0\0" "596046\0" <596046 bytes>
```

[`extract_overlay.py`](../tools/re/extract_overlay.py) walks those entries; the last one
ends exactly at the end of the file. The overlay holds `data1.hdr`, `data1.cab`,
`data2.cab` (InstallShield cabinets, magic `ISc(`), `setup.exe`, `setup.inx` and a few
support files. Nothing is encrypted.

`unshield -d app x data1.cab` (Homebrew) unpacks 508 files, including `EzExplorer_ENG/`:

| File | Notes |
|------|-------|
| `EzExplorer.exe` | 8.4 MB, PE32 x86, MSVC 9 / MFC 9 (`mfc90u.dll`). SHA-1 `b4a45b0067a89fd2f754df93edfc3102fa5756eb` |
| `GWCommonU.dll` | small helper; imports only file I/O |
| `Language/English.ini` | UI strings, `KEY=Text` |
| `TeeChart8.ocx`, `ToolkitPro1201vc90U.dll` | charting and UI libraries |

Native C++ rules out decompiling .NET metadata with ILSpy; this needs a disassembler.

## 3. Is there a curtailment feature at all?

The language file answers that before any disassembly:

```
ID_MAIN_INVERTER_START=Start
ID_MAIN_INVERTER_STOP=Stop
ID_MAIN_INVERTER_REAL_POWER_ADJUST=Real Power Adjust
ID_MAIN_INVERTER_REACTIVE_POWER_ADJUST=Reactive Power Adjust
IDD191_Title=Real Power Adjust
IDC_STATIC_REAL_POWER=%Rated Power
IDD198_Title=Reactive Power Adjust
IDC_STATIC_REACTIVE_POWER=Input Range: [-0.99,-0.9]∪[0.9,1]
```

So there is a "Real Power Adjust" dialog (resource IDD 191) that takes a percentage of
rated power.

## 4. Finding the protocol code

`rabin2 -i` shows that `EzExplorer.exe` imports `HID.DLL` (`HidD_GetAttributes`,
`HidP_GetCaps`, …), `SETUPAPI` and `ReadFile`/`CreateFileW`, so the USB code is in the
main executable, not a DLL.

No Ghidra or Java was available, so the approach was Capstone over the code sections
plus small pattern scripts:

1. [`scan.py`](../tools/re/scan.py) disassembles every executable section linearly
   (`skipdata` on) and pickles `(address, size, mnemonic, operands)` for about 476k
   instructions. That is fine for an unoptimised MSVC debug-style build like this one,
   where functions are contiguous and padded with `int3`.
2. [`find.py`](../tools/re/find.py) looks for a `mov byte ptr [...], 0xaa` followed within
   a few instructions by a `0x55` store. That gives 76 sites.

The first site shows the shape every command builder shares:

```
0052d44e  mov    byte ptr [eax + 0x1fc], 0xaa
0052d45b  mov    byte ptr [ecx + 0x1fd], 0x55
0052d468  mov    byte ptr [edx + 0x1fe], 0x80     ; src: PC
0052d475  mov    byte ptr [eax + 0x1ff], 0x7f     ; dst: broadcast
0052d482  mov    byte ptr [ecx + 0x200], 0        ; ctrl
0052d48f  mov    byte ptr [edx + 0x201], 0        ; func
0052d49c  mov    byte ptr [eax + 0x202], 0        ; len
0052d4a3  push   7
0052d4b1  push   ecx                              ; frame start
0052d4b8  call   0x401a60                         ; checksum(frame, 7)
0052d4d4  mov    byte ptr [eax + 0x203], dl       ; sum >> 8
0052d4ea  mov    byte ptr [edx + 0x204], cl       ; sum & 0xff
```

[`frames.py`](../tools/re/frames.py) takes each `0xaa` store's displacement as the frame
base and collects the next stores at base+2 … base+6 (src, dst, ctrl, func, len) and the
data bytes. That yields a table of `(src, dst, ctrl, func, len)` tuples. Register
operands (`cl`, `dl`) are runtime values, usually the inverter address or a parameter.
Some sites at `0x533xxx` and `0x4f1xxx` don't fit the pattern: they start `09 01 …` or
have `0xaa` in a data position, so they are probably a different protocol (EzBee wireless)
or data tables. They were not pursued.

### The checksum

```
00401a60  …
00401a89  mov    eax, dword ptr [ebp + 8]      ; buf
00401a8c  add    eax, dword ptr [ebp - 8]      ; + i
00401a8f  movzx  ecx, byte ptr [eax]
00401a92  movzx  edx, word ptr [ebp - 4]
00401a96  add    edx, ecx                      ; sum += buf[i]
00401a98  mov    word ptr [ebp - 4], dx
…
00401a9e  mov    ax, word ptr [ebp - 4]        ; return 16-bit sum
```

This is a plain 16-bit sum over the bytes before it, header included. It's the same as
GoodWeLogger's and as `aa55::checksum`.

## 5. Naming the commands

The `(ctrl, func)` tuples alone don't say what a command does. Two passes named them.

[`callers.py`](../tools/re/callers.py) finds each builder's function start (the
`push ebp; mov ebp, esp` before it) and every `call` to it, following one level of
`jmp` thunks. Most control commands have two callers, apparently one per transport (USB
and the EzBee/RS485 path); Start and Function Switch have one. The country command has
one caller per country,
each pushing a constant index (0–13), which is a strong hint that it is a "set country"
command.

[`strs.py`](../tools/re/strs.py) lists every immediate in a caller's body that points
into a data section and decodes it as UTF-16 or ASCII. The UI handlers log their outcome,
so each one names itself:

| Builder | ctrl/func/len | Caller strings |
|---------|---------------|----------------|
| `0x54fd90` | 03/00/0 | `Success: Clear E-Total and H-Total for Inverter(%s)!` |
| `0x54ff20` | 03/02/6 | `Success: Set RTC for Inverter(%s)!` |
| `0x550300` | 03/01/1 | `Success: Set Italy for Inverter(%s)!` (one caller per country) |
| `0x5500f0` | 03/03/1 | `Success: Set Italian for Inverter(%s)!` (indices 0–4) |
| `0x550510` | 03/28/2 | `Success: Set Function Switch for Inverter(%s)!` |
| `0x5506c0` | 03/1E/1 | `Success: Adjust Real Power for Inverter(%s)!` |
| `0x550860` | 03/1F/1 | `Success: Adjust Reactive Power for Inverter(%s)!` |
| `0x550c00` | 03/20/0 | `Success: Start Reactive Power Curve for Inverter(%s)!` |
| `0x550d90` | 03/21/0 | `Success: Stop Reactive Power Curve for Inverter(%s)!` |
| `0x54ac00` | 03/1B/0 | `Success: Start Inverter(%s)!` |
| `0x54ad40` | 03/1C/0 | `Success: Stop Inverter(%s)!` |
| `0x54fa00` | 02/06/12 | `Success: Set Inverter(%s) Param!`, `Get Inverter Setting Info` |
| `0x552fd0` | 03/22/6 | `Broadcast Set Wireless Param` |

The real-power, reactive-power and reactive-curve handlers also log
`Error:Inverter in grid  scheduling state!`, so the program checks a state first and can
refuse.

## 6. Real power adjust in detail

The builder takes the percentage as its second stack argument:

```
0055079e  mov    byte ptr [ebp - 0x30], 0xaa
005507a2  mov    byte ptr [ebp - 0x2f], 0x55
005507a6  mov    byte ptr [ebp - 0x2e], 0x80
005507aa  mov    cl, byte ptr [ebp - 0x14]       ; inverter address (looked up)
005507ad  mov    byte ptr [ebp - 0x2d], cl
005507b0  mov    byte ptr [ebp - 0x2c], 3        ; ctrl: execute
005507b4  mov    byte ptr [ebp - 0x2b], 0x1e     ; func: real power adjust
005507b8  mov    byte ptr [ebp - 0x2a], 1        ; len
005507bc  mov    dl, byte ptr [ebp + 0xc]        ; arg 2: the value
005507bf  mov    byte ptr [ebp - 0x29], dl
005507c2  push   8
005507c8  …call  0x401a60                       ; checksum over 8 bytes
005507f4  push   0xa
005507fd  call   0x552600                       ; send 10 bytes, wait for reply
```

Its caller passes `byte [ebp-0x690]`, a member of a dialog object built at `ebp-0x704`.
The dialog constructor at `0x466a50` passes resource ID `0xbf` (191, "Real Power Adjust")
to the `CDialog` base. Its `DoDataExchange` is:

```
00466b63  add    ecx, 0x74              ; &m_value (byte)
00466b6a  push   0x4b3                  ; edit control ID
00466b73  call   0x5a2ba6               ; DDX_Text(pDX, 0x4b3, m_value)
00466b78  push   0x64                   ; max 100
00466b7a  push   0                      ; min 0
00466b7f  movzx  ecx, byte ptr [eax + 0x74]
00466b88  call   0x5a2ba0               ; DDV_MinMaxByte(pDX, m_value, 0, 100)
```

So the data byte is the percentage typed into the dialog, an integer from 0 to 100,
with no scaling.

## 7. Reply handling

`0x552600` sends the frame, reads the reply and validates it in this order:

1. The checksum over the first 8 reply bytes must match bytes 8–9. This implies a
   1-byte data field, so a 10-byte reply.
2. The reply must start with `AA 55`.
3. The addresses must be swapped: `reply[2] == request[3]` and `reply[3] == request[2]`.
4. The data byte `reply[7]`:
   - `0x06` (ASCII ACK) → return 0 (success)
   - `0x15` (ASCII NAK) → retry
   - anything else → retry

A loop counter (`[ebp-4]`) gives up with -1 after about 8 attempts. The reply's ctrl/func bytes are
not checked.

## Limits of this analysis

- EzExplorer supports many inverter families, so a command existing here doesn't mean
  the GW3600-DS implements it. A NAK is the expected sign of "unsupported".
- The HID transport layer (`0x518280`/`0x5184b0`) was not traced. The `CC 99 <len>`
  USB prefix comes from GoodWeUSBLogger. The inverter-side UART, which the ESP uses,
  carries bare AA55 frames.
- `03 27`, `03 28` (function switch bits), `01 00` and the 12 bytes of `02 06` are
  unidentified.
- Linear disassembly can misread data as code. Every site used above was checked by
  reading the surrounding listing ([`dump.py`](../tools/re/dump.py)), not just the
  pattern match.

## Reproducing

```
python3 -m venv venv && venv/bin/pip install -r tools/re/requirements.txt
brew install unshield
# fetch the installer as in step 1, then:
venv/bin/python -I tools/re/extract_overlay.py EzExplorer_1.0.1.8_Setup.exe x
unshield -d app x data1.cab            # run inside x/Disk1
venv/bin/python -I tools/re/scan.py app/EzExplorer_ENG/EzExplorer.exe ezx.pkl
venv/bin/python -I tools/re/frames.py ezx.pkl
venv/bin/python -I tools/re/callers.py ezx.pkl 0x55079e   # a 0xaa store site, not a function start
venv/bin/python -I tools/re/strs.py ezx.pkl app/EzExplorer_ENG/EzExplorer.exe 0x4c3060
venv/bin/python -I tools/re/dump.py ezx.pkl 0x5506c0
```

The scripts' addresses are specific to EzExplorer 1.0.1.8 (`EzExplorer_ENG`). The binary
itself is GoodWe's and is not included in this repository.
