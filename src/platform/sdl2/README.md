# Apple-1 Emulator - SDL2 build

Cross-platform build using SDL2 instead of Win32/Direct2D.
Works on Linux, macOS, and Windows (via MSYS2/MinGW).

## Requirements

### Linux
```sh
sudo apt install libsdl2-dev build-essential
```

### macOS
```sh
brew install sdl2
```

### Windows (MSYS2 MinGW64)
```sh
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc make
```

## Build

```sh
cd src/platform/sdl2
make
```

Produces a binary named `apple1` (or `apple1.exe` on Windows).

## Run

The binary expects `roms/` and `settings.ini` to live next to it.
Easiest is to copy them from the project root:

```sh
cp -r ../../../roms .
./apple1
```

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| F1  | CLEAR SCREEN |
| F2  | RESET (Wozmon) |
| F3  | Toggle scanlines |
| F4  | Toggle vignette |
| F5  | Toggle dot artifact |
| F6  | Cycle phosphor (White → Green → Amber) |
| F7  | Toggle teletype pacing |
| F11 | Toggle fullscreen |
| Esc | Send ESC to Apple-1 |
| Drag-and-drop a file onto the window to load it |

## Notes

- No menubar (use F-keys). Settings are saved to `settings.ini`.
- No debugger window in the SDL2 build (yet).
- File loading is via drag-and-drop only (no native file dialog).
