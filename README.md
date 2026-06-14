# Apple-1 Emulator

A faithful Apple-1 emulator in C++. Cycle-accurate MOS 6502, Wozmon, Integer
BASIC, real Apple Cassette Interface support — verified bit-for-bit against
Woz's 1976 ACI ROM — plus a **read/write Disk II controller** with DOS support
and a built-in debugger with single-step, breakpoints, and a live memory view.

The primary build is a native **Windows GUI** (Win32 + Direct2D/DirectWrite)
with a CRT-style display. A cross-platform **SDL2** build is also included for
Linux, macOS, and Windows/MSYS2.

## Features

- **Cycle-accurate MOS 6502** CPU core
- **Apple-1 memory map** with the real PIA address-decoding quirk (only the
  low 2 bits are decoded, so all of `$D0xx` mirrors four registers — Integer
  BASIC depends on this)
- **Integer BASIC** at `$E000-$EFFF` (loaded from `basic.rom`)
- **Wozmon** monitor at `$FF00-$FFFF`
- **Expansion slot (one IO card at a time):**
  - **Cassette (ACI)** — generate and decode real cassette WAV audio through
    Woz's original 1976 ACI ROM
  - **Disk 1 (Disk II + OneDos)** — read/write Disk II controller; disk writes
    are persisted back to the `.dsk` file so a guest OS can actually save. The
    last-used disk auto-mounts on the next launch.
- **RAM expansion** — run with 8 KB (stock), 16 KB, 24 KB, or 32 KB total
- **Debugger** — separate window with registers, flags, disassembly, a memory
  inspector, and tape/disk diagnostics; pause/resume, single-step,
  step-over-JSR, run-to-RTS, and a breakpoints manager (add / enable / disable /
  delete)
- **CRT display effects** — scanlines, dot artifact, vignette, teletype pacing,
  White / Green / Amber phosphor, and 1x–7x window scaling
- **Disk II latch model** — switch between cycle-accurate (per-bit) and
  deterministic (per-byte) nibble timing

### Memory map

```
$0000-$1FFF  RAM (8 KB on-board; expandable up to $7FFF / 32 KB total)
$C000-$C1FF  Expansion slot — depends on the selected IO Card:
               Cassette : ACI registers $C000-$C0FF, ACI ROM $C100-$C1FF
               Disk 1   : Disk II soft switches $C000-$C00F, OneDos ROM $C100-$C1FF
               None     : unmapped
$D000-$D0FF  PIA (keyboard + display); low 2 bits decoded -> 4 mirrored regs
$E000-$EFFF  Integer BASIC (from basic.rom)
$FF00-$FFFF  Wozmon
```

## Build

### Windows (Visual Studio 2022)

Open `vs/apple1.sln`, select **Release | x64**, and build. The binary lands at
`build/Release/apple1.exe`. Requires the C++17 toolset (v143). No external
dependencies — it links only the Windows SDK (Direct2D, DirectWrite, COM).

### Cross-platform (SDL2)

```sh
cd src/platform/sdl2
make
```

See [src/platform/sdl2/README.md](src/platform/sdl2/README.md) for the SDL2
build's requirements and its (different) key bindings.

## Run

The emulator looks for a `roms/` directory in its working directory, so the
simplest approach is to run it from the project root, or copy `roms/` next to
the built `apple1.exe`.

```
roms/
    wozmon.rom    (256 B,  required)
    char.rom      (512 B,  required — 2513 character generator)
    basic.rom     (4 KB,   optional — Integer BASIC at $E000)
    aci.rom       (256 B,  optional — used by the Cassette IO card)
    ONEDOS.ROM    (512 B,  optional — used by the Disk 1 IO card)
```

`cassettes/` holds sample `.wav` tapes and `disks/` holds a sample ONEDOS
`.dsk` image.

## Using the emulator

When the window opens you're talking to a real Apple-1. **Just type** — input
is uppercase ASCII like the original hardware. Press **Enter** for Wozmon's `\`
prompt.

- **Wozmon basics:** `FF00.FF1F` dumps memory; `E000R` runs whatever is at
  `$E000` (i.e. BASIC); `addr: bytes` writes bytes.
- **BASIC:** `E000R` then Enter. Try `PRINT 2+2`, or
  `10 FOR I=1 TO 5 : 20 PRINT I*I : 30 NEXT I : RUN`.
- **Reset:** **F2** jumps to Wozmon and preserves memory.

### The IO card (Cassette vs. Disk)

The Apple-1 had one expansion slot, and so does this emulator — pick what's in
it under **Expansions → IO Card**:

- **Cassette (default)** — the ACI cassette interface is active. Loading or
  staging a tape, or the ACI ROM polling for one, pops a file dialog for a
  `.wav`.
- **Disk 1** — the OneDos boot ROM and Disk II controller are mapped. Selecting
  this (or mounting a disk) boots OneDos; if no disk is mounted it prompts you
  for a `.dsk`.
- **None** — slot empty.

### Disks (Disk II + DOS)

1. **Disk II → Mount .dsk…** (or **F3**), or just pick **Expansions → IO Card →
   Disk 1** and you'll be prompted for an image. Mounting a disk automatically
   switches the IO card to Disk 1.
2. From WozMon, type `C100R` to boot the disk's OS (OneDOS on the bundled
   image). **Writes are saved back to the `.dsk` file**, so files you create
   persist.
3. The last disk you mounted **auto-mounts on the next launch** (unless the
   Cassette card is selected). **Disk II → Eject** unmounts.

If disk reads are unreliable on your machine (some host schedulers are
timing-sensitive), switch **Settings → Disk II Latch** from *Bit-level* to
*Byte-level* for deterministic timing.

See **[OneDOS](#onedos)** below for booting, the full command set, the sysdisk
menu, and the calling API.

### Cassettes (ACI)

With the Cassette IO card selected, **Cassette → Save Memory as WAV…** prompts
for a start and end address (hex) and writes a real ACI-format tape. To load,
use **File → Open…** with a `.wav`, or let the ACI ROM prompt you.

## Controls

### Keyboard shortcuts

| Key      | Action                                              |
|----------|-----------------------------------------------------|
| Ctrl+O   | Open a file (`.txt`, `.bin`, or `.wav`)             |
| F1       | Clear screen                                        |
| F2       | Reset (jump to Wozmon; memory preserved)            |
| F3       | Mount a `.dsk` image (Disk II)                      |
| F5       | Debugger: pause / resume CPU                        |
| F6       | Debugger: single-step one instruction               |
| F7       | Debugger: step over JSR                             |
| F8       | Toggle breakpoint at the current PC                 |
| F9       | Goto an address in the debugger's memory view       |

All other keys are sent to the emulated Apple-1 (uppercase ASCII; Enter = CR,
Esc, and Ctrl-letters are passed through).

### Full menu reference

| Menu | Items |
|------|-------|
| **File** | Open… (Ctrl+O) · Exit |
| **CPU** | Clear Screen (F1) · Reset (F2) · Pause/Resume (F5) · Step (F6) · Step Over JSR (F7) |
| **Cassette** | Save Memory as WAV… (prompts start/end address) |
| **Disk II** | Mount .dsk… (F3) · Eject |
| **Debugger** | Show Debugger Window · Toggle Breakpoint at PC (F8) · Goto Memory Address… (F9) · Clear All Breakpoints |
| **View** | Scale Tiny (1x) · Small (2x) · Medium (3x) · Large (5x) · Huge (7x) |
| **Settings** | Scanlines · Dot Artifact · Teletype Pacing · Vignette · Phosphor ▸ White/Green/Amber · Disk II Latch ▸ Bit-level/Byte-level |
| **Expansions** | RAM ▸ None (8KB)/8KB (16KB)/16KB (24KB)/24KB (32KB) · IO Card ▸ None/Cassette/Disk 1 |
| **Help** | About… |

## Debugger

Open it with **Debugger → Show Debugger Window**. The panel shows the CPU
registers and flags, live disassembly at the PC, a memory inspector
(**F9** to jump it to an address), and tape/disk diagnostics. It has on-screen
buttons to **Run Free**, enter **Step** mode, **Run to RTS**, **Step** one
instruction, and **ADD BP…**, which opens the **breakpoints manager** — a
dialog to add a breakpoint by hex address and enable, disable, or delete
existing ones. **F5/F6/F7** drive pause/step/step-over from the main window,
**F8** toggles a breakpoint at the current PC, and **Clear All Breakpoints**
removes them in one shot.

## OneDOS

The bundled `disks/ONEDOS.DSK` ships with **OneDOS**, a small DOS for the
Apple-1 + Disk II contributed by Phillip Allison. It runs in the Disk-1 card's
onboard 4K RAM (`$2000-$2FFF`), leaving all 8K of stock Apple-1 RAM
(`$0000-$1FFF`) free for your own code. OneDOS disks are standard **DOS 3.3**
(35 tracks × 16 sectors, 143,360 bytes, logical order) and interchange with
real Apple II DOS 3.3 disks.

### Booting OneDOS

With the **Disk 1** IO card active and a disk mounted (see
[Disks](#disks-disk-ii--dos) above), type `C100R` at WozMon's `\` prompt. The
boot ROM loads OneDOS and you land at its prompt:

```
<
```

A fresh boot of the shipped disk drops you in the **sysdisk menu** (below); if a
file is bootmarked (see `BM`), it auto-runs first. Any unrecognized command
falls through to WozMon — type `2000R` to return to OneDOS.

### Commands

Type commands right after the `<` prompt. Filenames are **case-sensitive**, up
to **30 characters**, with **no space** between the command and the name
(`BLHELLO`, not `BL HELLO`). `AAAA` (address) and `LEN` (length) are 4-digit hex.

| Command | What it does |
|---|---|
| `CAT` | List every file (paginates every 20; prints a `BYTES FREE` footer). |
| `BLNAME` | **BLOAD** — load file at the address baked into its header. |
| `BLNAME,AAAA,LEN` | **BLOAD override** — load `LEN` bytes into `AAAA` (relocate / partial load). |
| `BRNAME` | **BRUN** — BLOAD then run; returns to `<` when it `RTS`es. |
| `BSNAME,AAAA,LEN` | **BSAVE** — save `LEN` bytes from `AAAA` as binary `NAME` (4-byte header auto-prepended). |
| `DNAME` | **DELETE** `NAME` (slot reused on the next save). |
| `ROLD,NEW` | **RENAME** `OLD` → `NEW` (no collision check on `NEW`). |
| `BMNAME` | **Bootmark** `NAME` as the startup file — auto-BRUN'd on the next `C100R`. |
| `ANAME,MM,ATYPE` | Set file **type** (`MM`, 2 hex) and **auxtype**/load address (`ATYPE`, 4 hex). |
| `I` | **FORMAT** — zero every sector + write a blank VTOC. |
| `IS` | **INIT SYSTEM** — format, then write a bootable OneDOS image. |
| `L` / `S` | Reserved (stubbed). |
| *anything else* | Falls through to WozMon; `2000R` returns to OneDOS. |

`CAT` lists each file as `B 002 HELLO` (type, sector count, name). The `A`
command's type byte sets the CAT letter — `$04`/`$06` → `B` (binary), anything
else → `?`. Example: `AHELLO2,06,1A00` marks HELLO2 as BIN and changes its load
address to `$1A00`, so future `BLHELLO2`/`BRHELLO2` load straight there.

### Typical session

```
C100R                   ; boot OneDOS
  <CAT                  ; what's on the disk?
   B 002 HELLO
  <BRHELLO              ; load and run HELLO
  HELLO FROM ONEDOS!
  <BSPROG,1A00,40       ; save 64 bytes from $1A00 as PROG
  <RPROG,GAME           ; rename PROG -> GAME
  <BMGAME               ; mark GAME as the startup program
  <                     ; reboot...
C100R
  GAME OUTPUT...        ; auto-runs
  <
```

### Saving your own programs

1. Enter code into RAM from WozMon (stock RAM below `$2000` is free), e.g.
   `1A00: A2 00 BD 19 1A ...`
2. At the OneDOS prompt: `BSMYPROG,1A00,32` (saves `$32` = 50 bytes).
3. Run it later with `BRMYPROG`, or make it the startup with `BMMYPROG`.

Programs whose load address is **below `$0B00`** would collide with OneDOS's
working buffers, so OneDOS automatically stages them at `$1000` and copies them
down after the read finishes — just BSAVE with the real target address. The
stage area is `$1000-$1FFF` (4K); anything larger must be split or relocated.

**Binary file format:** a 4-byte header — bytes 0–1 load address, 2–3 length —
followed by the program bytes. `BLOAD` reads it to know where to load;
`BLNAME,AAAA,LEN` overrides it; `BSAVE` writes it from your `,AAAA,LEN`.

### Sysdisk menu

A fresh `C100R` on the shipped disk lands in a bootmarked menu:

```
1) DEMOS          4) DISK CHECKER
2) EXAMPLES       5) DISK COPIER
3) FORMATTER      6) FILE COPIER
                  0) EXIT TO ONEDOS
```

Pick a number to BRUN that utility; each returns to the menu, and `0` drops you
at the `<` prompt. *Note:* DEMOS, EXAMPLES, DISK CHECKER, DISK COPIER, and FILE
COPIER are currently stubs (they print TODO and bounce back); FORMATTER has a
working low-level-format body, but its bit-level write timing hasn't been fully
verified on real hardware.

### Error messages

The DOS 3.3 error code is written to zero page `$69` (`ERRNUM`) before the
message prints, so a program can read it after a call.

| Message | Code | Meaning |
|---|---|---|
| `?` | `$0B` | Syntax error — bad command, missing comma, bad hex |
| `FILE NOT FOUND` | `$06` | No catalog entry matched the name |
| `I/O ERROR` | `$08` | Disk read/write failed |
| `DISK FULL` | `$09` | No free sectors for the file's T/S list or data |

### Recovery

If OneDOS wedges (hung, garbage, weird state), hit **RESET** (F2) and type
`C100R`. The card's RAM is rebuilt from disk on every cold boot, so no bad state
survives — only RAM is clobbered; the disk is untouched.

<details>
<summary><b>Calling OneDOS from your own code — jump table, zero page, memory map</b></summary>

Each export is a fixed 3-byte `JMP` into the resident image, so you just `JSR`
the entry. **C=0 on success, C=1 on error.**

| Addr | Name | Description |
|---|---|---|
| `$2000` | `COLD` | Cold entry (boot1 lands here) |
| `$200D` | `WARM` | Return to the `<` prompt |
| `$2010` | `RWTSRD` | Read sector (TRACK, SECTOR, BUFPTR in ZP) |
| `$2013` | `RWTSWR` | Write sector (TRACK, SECTOR, BUFPTR in ZP) |
| `$2016` | `FOPEN` | Find file by name (PARSEWS, FNAMELEN) |
| `$2019` | `FSEEK` | A = data-sector index into open file |
| `$201C` | `FRDSEC` | Read next file data sector → SECBUF (C=1 on EOF/err) |
| `$201F` | `FWRSEC` | Write SECBUF as next file data sector (C=1 on EOF/err) |
| `$2022` | `RDVTOC` | Reload VTOCBUF from disk |
| `$2025` | `FLIST` | CAT-style listing (paginates; prints BYTES FREE) |
| `$2028` | `FREE` | Count free sectors → A (lo) + X (hi); also ZP `$6B/$6C` |
| `$202B` | `FORMAT` | Zero-fill every sector + blank VTOC (`I`) |
| `$202E` | `INITSYS` | FORMAT + write boot1 + OneDOS image + reserve T0/T1:S0 (`IS`) |
| `$2031` | `BRUN` | BRUN file named in PARSEWS/FNAMELEN; returns when child RTSes |

**Zero-page convention:** `$27` TRACK · `$28` SECTOR (logical) · `$29/$2A`
BUFPTR lo/hi · `$50` TS_TRK · `$51` TS_SEC · `$5C` TSL_IDX. Buffers: `$0280`
PARSEWS (put the filename here) · `$0300` SECBUF · `$0400` TSLBUF · `$0500`
VTOCBUF.

**OneDOS memory map:**

| Range | Use |
|---|---|
| `$0000-$01FF` | Stack, zero page |
| `$0200-$027F` | WozMon input buffer |
| `$0280-$02BD` | PARSEWS / PARSEWS2 (filename parse areas) |
| `$0300-$03FF` | SECBUF (sector buffer) |
| `$0400-$04FF` | TSLBUF (T/S list) |
| `$0500-$05FF` | VTOCBUF |
| `$0800-$08FF` | boot1 |
| `$0900-$0AFF` | write-path nibble buffers |
| `$0B00-$1DFF` | **free for user programs** (sysdisk utilities load here) |
| `$1E00-$1FFF` | MENU parking area (free if sysdisk MENU isn't the bootprog) |
| `$2000-$2FFF` | OneDOS resident (16 sectors) |
| `$C000-$C00F` | Disk II soft switches |
| `$C100-$C1FF` | P6 boot ROM |
| `$C200-$C2FF` | encode/skew tables (WRTAB, SKEW, INV_SKEW) |
| `$D012-$D013` | PIA / display |
| `$E000-$EFFF` | BASIC (if loaded) |
| `$FF00-$FFFF` | WozMon |

</details>

## File formats

### Wozmon text (`.txt`)
Lines of `address: byte byte byte ...`, all hex:
```
1000: A2 00 BD 13 10 20 EF FF
1008: E8 E0 15 F0 03 4C 02 10
```

### Raw binary (`.bin`)
Loaded at `$0300` by default. To load at a specific address, name the file
`something_XXXX.bin` (e.g. `myprog_E000.bin`).

### Cassette WAV (`.wav`)
16-bit mono 44.1 kHz — the real Apple-1 cassette format: a 770 Hz leader tone
followed by tone-coded data bits.

### Disk image (`.dsk`)
Standard 140 KB Apple Disk II image (35 tracks × 16 sectors × 256 bytes, DOS
3.3 logical order), mounted via **Disk II → Mount** (F3). Writes are persisted
back to the file.

## Verification

The cassette format was validated by routing a generated WAV through the real
ACI ROM (256 bytes of Woz's 1976 assembly). All 40 bytes of HELLO WORLD
round-trip perfectly: generated WAV → ACI ROM decode → memory.

## Acknowledgments

The read/write Disk II + DOS support, the debugger's breakpoints manager, and
the application icon were contributed by **Phillip Allison**
([@philtimmes](https://github.com/philtimmes)). See
[CONTRIBUTORS.md](CONTRIBUTORS.md) for the full breakdown of his work.

## License

The emulator source is released under the [MIT License](LICENSE). The ROM
images under `roms/` and the disk image under `disks/` are historical Apple-1
firmware and third-party software, included for preservation and
interoperability; they are not covered by the MIT license.
