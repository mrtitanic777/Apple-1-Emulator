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
2. The disk boots. **Writes are saved back to the `.dsk` file**, so files you
   create in the guest OS persist.
3. The last disk you mounted **auto-mounts on the next launch** (unless the
   Cassette card is selected). **Disk II → Eject** unmounts.

If disk reads are unreliable on your machine (some host schedulers are
timing-sensitive), switch **Settings → Disk II Latch** from *Bit-level* to
*Byte-level* for deterministic timing.

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
