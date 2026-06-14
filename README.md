# Apple-1 Emulator

A faithful Apple-1 emulator in C++. Cycle-accurate MOS 6502, Wozmon, Integer
BASIC, real Apple Cassette Interface support — verified bit-for-bit against
Woz's 1976 ACI ROM — plus a Disk II controller and a built-in debugger with
single-step, breakpoints, and a live memory view.

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
- **Apple Cassette Interface (ACI)** — generate and decode real cassette WAV
  audio through Woz's original 1976 ACI ROM
- **Disk II controller** with ONEDOS support (mount `.dsk` images)
- **Debugger**: pause/resume, single-step, step-over-JSR, breakpoints, and a
  register + disassembly + memory panel
- **CRT display effects**: scanlines, dot artifact, vignette, teletype pacing,
  and White / Green / Amber phosphor

### Memory map

```
$0000-$1FFF  RAM (8 KB on-board)
$C000-$C00F  Disk II soft switches (when a .dsk is mounted)
$C000-$C0FF  ACI tape data port (when no disk is mounted)
$C100-$C1FF  ACI ROM (cassette mini-monitor)
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
    basic.rom     (4 KB,   optional — enables Integer BASIC at $E000)
    aci.rom       (256 B,  optional — enables the ACI cassette via $C100)
    ONEDOS.ROM    (256 B,  optional — Disk II / ONEDOS)
```

`cassettes/` holds sample `.wav` tapes and `disks/` holds a sample `.dsk`
image you can load.

## Controls

The GUI is fully menu-driven; these are the keyboard shortcuts:

| Key      | Action                                    |
|----------|-------------------------------------------|
| Ctrl+O   | Open a file (`.txt`, `.bin`, or `.wav`)   |
| F1       | Clear screen                              |
| F2       | Reset (jump to Wozmon; memory preserved)  |
| F3       | Mount a `.dsk` image (Disk II)            |
| F5       | Debugger: pause / resume CPU              |
| F6       | Debugger: single-step one instruction     |
| F7       | Debugger: step over JSR                    |
| F8       | Debugger: toggle breakpoint at PC          |
| F9       | Debugger: goto address in memory view      |

Additional menus cover **Cassette** (save a memory range as a WAV),
**Disk II** (eject), **Debugger** (show window, clear breakpoints),
**View** (display scale 1x–7x), and **Settings** (scanlines, dot artifact,
teletype pacing, vignette, phosphor color).

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
Standard 140 KB Apple Disk II image, mounted via **Disk II → Mount** (F3).

## Quick tour

1. Run `apple1.exe`. Press Enter to get Wozmon's `\` prompt.
2. Inspect the Wozmon ROM: `FF00.FF1F`
3. Run BASIC: `E000R`, then try `PRINT 2+2` or
   `10 FOR I=1 TO 5 : 20 PRINT I*I : 30 NEXT I : RUN`.
4. F2 to reset back to Wozmon.
5. F5 to open the debugger; F6 to single-step.

## Verification

The cassette format was validated by routing a generated WAV through the real
ACI ROM (256 bytes of Woz's 1976 assembly). All 40 bytes of HELLO WORLD
round-trip perfectly: generated WAV → ACI ROM decode → memory.

## License

The emulator source is released under the [MIT License](LICENSE). The ROM
images under `roms/` and the disk image under `disks/` are historical Apple-1
firmware and third-party software, included for preservation and
interoperability; they are not covered by the MIT license.
