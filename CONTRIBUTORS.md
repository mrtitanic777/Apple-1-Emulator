# Contributors

## Logan Greer — author

Creator and maintainer of the Apple-1 Emulator: the cycle-accurate 6502 core,
Wozmon and Integer BASIC integration, the Apple Cassette Interface, the
Win32/Direct2D GUI, the SDL2 cross-platform build, and the original Disk II
controller.

## Phillip Allison — [@philtimmes](https://github.com/philtimmes)

Contributed the read/write Disk II ("DOS") feature set and debugger
enhancements:

- **Read/write Disk II + DOS persistence.** Extended the Disk II controller
  from read-only to a full read/write path: it captures sector writes,
  denibbles the GCR data, splices it back into the disk image, and writes the
  changes out to the `.dsk` file — so a guest OS (ONEDOS) can actually save.
  Added last-disk auto-mount and the matching 512-byte `ONEDOS.ROM` and
  `ONEDOS.DSK`.
- **Breakpoints manager.** A modal dialog to add, list, enable/disable, and
  delete breakpoints, wired into the debugger window.
- **Application icon.** `apple1.ico`, plus `make_icon.ps1`, which generates it.
- **Updated BASIC ROM** (`roms/basic.rom`).

### Files Phillip authored

- `src/platform/windows/breakpoints_dialog.cpp`
- `src/platform/windows/breakpoints_dialog.h`
- `src/platform/windows/make_icon.ps1`
- `src/platform/windows/apple1.ico`

### Files Phillip modified

`src/app.cpp`, `src/app.h`, `src/bus.cpp`, `src/bus.h`, `src/cpu6502.h`,
`src/debugger.cpp`, `src/debugger.h`, `src/disk_ii.cpp`, `src/disk_ii.h`,
`src/platform/sdl2/sdl_app.cpp`,
`src/platform/windows/debugger_renderer.cpp`,
`src/platform/windows/debugger_renderer.h`,
`src/platform/windows/debugger_window.cpp`,
`src/platform/windows/main.cpp`,
`src/platform/windows/main_window.cpp`,
`src/platform/windows/main_window.h`,
`src/platform/windows/resource.rc`,
`src/platform/windows/resource.h`,
`src/roms.cpp`, `src/roms.h`, `src/settings.cpp`, `src/settings.h`,
`vs/apple1.vcxproj`

Each file Phillip touched also carries a credit header at the top.
