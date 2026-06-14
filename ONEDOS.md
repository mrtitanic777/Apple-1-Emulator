# OneDOS — User Guide

A small DOS for the Apple-1 plus Disk II, written by **Phillip Allison**
([@philtimmes](https://github.com/philtimmes)). It ships on the bundled
`disks/ONEDOS.DSK` and runs under this emulator's **Disk 1** IO card.

OneDOS runs out of the Disk-1 card's onboard 4K RAM (`$2000-$2FFF`), which
leaves all 8K of stock Apple-1 RAM (`$0000-$1FFF`) free for whatever you're
working on.

> New here? In the emulator, select **Expansions → IO Card → Disk 1** (or just
> **Disk II → Mount** a `.dsk`), then from WozMon type `C100R` to boot. See the
> [main README](README.md) for emulator controls.

## Booting

From WozMon (the `\` prompt), type:

```
C100R
```

The boot ROM pulls T0 S0 into `$0800` and jumps in. That's **boot1**, and it
reads in sectors 1..F (in whatever physical order the head hands them over) and
puts each one at `$2000` plus the right page offset for its logical sector
number. Once all 15 are in, `JMP $2000` and OneDOS takes over. The first thing
it does is grab T1 S0 into `$2F00` — that's the 16th sector of the resident
image; the P6 ROM only gets us 15. Then it checks the VTOC for a bootmarked
file and runs it if one's there.

You'll see the OneDOS prompt:

```
<
```

If a startup program is set (see `BM` below) it runs first, prints whatever it
prints, and when it `RTS`es you land at the `<` prompt.

## Commands

All commands are typed immediately after the `<` prompt. Filenames are
**case-sensitive** and up to **30 characters**. Address (`AAAA`) and length
(`LEN`) arguments are **4-digit hex**.

> **No space** between the command letters and the filename. `BLHELLO` is
> canonical. `BL HELLO` also works (the parser skips leading spaces), but the
> no-space form is shorter and preferred.

| Command | What it does |
|---|---|
| `CAT` | List every file on the disk. Pages every 20 entries (press a key to continue) and prints a `XXX,XXX BYTES FREE` footer. |
| `BLNAME` | **BLOAD** — read file `NAME` into memory at the load address baked into its header. |
| `BLNAME,AAAA,LEN` | **BLOAD with override** — read `LEN` bytes from `NAME` into `AAAA`. Use to relocate or load part of a file. |
| `BRNAME` | **BRUN** — BLOAD + `JMP` into the loaded program. Returns to `<` when it `RTS`es. |
| `BSNAME,AAAA,LEN` | **BSAVE** — write `LEN` bytes from `AAAA` to a new binary file `NAME`. A 4-byte header (load addr + length) is prepended automatically. |
| `DNAME` | **DELETE** `NAME`. Frees its data sectors + T/S list and marks the catalog entry deleted (`$FF`); the slot is reused on the next save. |
| `ROLD,NEW` | **RENAME** `OLD` to `NEW` (rewrites the name field only). Errors `FILE NOT FOUND` if `OLD` doesn't exist. `NEW` isn't checked for collisions — reusing a name quietly creates a duplicate entry. |
| `BMNAME` | **Bootmark** `NAME` as the startup file (stored in the VTOC at `$C4-$E3`). On the next `C100R`, `NAME` is auto-BRUN'd before the `<` prompt. |
| `ANAME,MM,ATYPE` | Set `NAME`'s file **type** (`MM`, 2 hex digits) and **auxtype** (`ATYPE`, 4 hex digits = the header load address). |
| `I` | **FORMAT** — zero-fill every sector (560 sectors, ~70 s on real hardware) and write a fresh blank VTOC. Use before `IS`, or to wipe a disk. |
| `IS` | **INIT SYSTEM disk** — FORMAT, then write boot1 + the 16 OneDOS sectors and reserve T0/T1:S0. Produces a bootable disk. |
| `L` / `S` | Reserved (LOAD/SAVE variants; stubbed). |
| *anything else* | Falls through to WozMon (the `\` prompt). Type `2000R` to return to OneDOS. |

### `CAT` output

```
B 002 HELLO
```

`B` = type (binary), `002` = sector count, `HELLO` = filename. The footer
`XXX,XXX BYTES FREE` is decimal (free sectors × 256), always 6 digits with a
thousands separator.

### File types (the `A` command)

| Type byte | CAT letter | Meaning |
|---|---|---|
| `$04` | `B` | DOS 3.3 binary |
| `$06` | `B` | ProDOS-style BIN |
| else | `?` | unknown |

`BLOAD`/`BRUN` ignore the type byte entirely — they load whatever the T/S list
points at — but `CAT` uses it to print a meaningful letter.

```
AHELLO2,06,1A00
```

…marks `HELLO2` as BIN (type `$06`) and changes its header load address to
`$1A00`, so future `BLHELLO2` / `BRHELLO2` load straight to `$1A00`.

## Typical session

```
C100R                   ; boot OneDOS
  <CAT                  ; what's on the disk?
   B 002 HELLO
  <BRHELLO              ; load and run HELLO
  HELLO FROM ONEDOS!
  <BSPROG,1A00,40       ; save 64 bytes from $1A00 as PROG
  <CAT
   B 002 HELLO
   B 002 PROG
  <RPROG,GAME           ; rename PROG -> GAME
  <BMGAME               ; mark GAME as startup
  <                     ; reboot...
C100R
  GAME OUTPUT...        ; auto-runs
  <
```

## Error messages

The DOS 3.3 error code is written to zero page at `$69` (`ERRNUM`) before the
message prints, so a program can read `ERRNUM` after a call to see exactly what
went wrong.

| Message | Code | Meaning |
|---|---|---|
| `?` | `$0B` | **SYNTAX ERROR** — unrecognized command, missing comma, bad hex, etc. |
| `FILE NOT FOUND` | `$06` | Catalog walk completed without matching the name. |
| `I/O ERROR` | `$08` | Disk read or write failed (no addr-field match, decode failure). |
| `DISK FULL` | `$09` | No free sectors for the new file's T/S list or data. |

## Writing a program

1. Type your machine code into RAM using WozMon (e.g. starting at `$1A00`,
   since stock RAM is free below `$2000`):

   ```
   2000R    ; back to OneDOS from WozMon, or
   \        ; from any unknown command at the < prompt
   1A00: A2 00 BD 19 1A F0 08 09 80 ...
   ```

2. Back at the OneDOS prompt, save it:

   ```
   BSMYPROG,1A00,32      ; saves $32 = 50 bytes from $1A00 as MYPROG
   ```

3. Run it any time later: `BRMYPROG`
4. Make it the startup program: `BMMYPROG` — now `C100R` auto-runs it.

### Programs that load under `$0B00`

The bottom 2.75K of RAM holds OneDOS's working buffers (parse workspace, sector
buffer, T/S list, VTOC, encode/decode nibble buffers). Loading a file straight
into that range would overwrite the same buffers the disk read is using.

OneDOS handles this for you: if the load address is below `$0B00`, the file is
staged at `$1000` first, all disk traffic finishes, then the bytes are copied
down to where they belong before BRUN jumps in. Just BSAVE with the real target
address and it works.

The stage area is `$1000-$1FFF` (4K). Anything bigger must be split, relocated,
or loaded with an explicit `BLNAME,AAAA,LEN` to somewhere safe. *(Example: the
Apple-1 30th Anniversary demo wants `$0280-$0FFF`; OneDOS stages it at
`$1000-$1D7F`, copies `$0D80` bytes down to `$0280`, and BRUN lands at
`$0280`.)*

## File format

Binary files (type `B`) start with a 4-byte header:

| Offset | Field |
|---|---|
| 0–1 | load address (low, high) |
| 2–3 | length in bytes (low, high) |
| 4… | program bytes |

`BLNAME` reads the header to know where to load. `BLNAME,AAAA,LEN` overrides
both. `BSAVE` writes the header automatically from the `,AAAA,LEN` you supplied.

## Export jump table

These addresses don't move between builds. Each is a 3-byte `JMP` into the
resident image, so to call one you just `JSR` the entry. **C=0 on success,
C=1 on error.**

| Addr | Name | Description |
|---|---|---|
| `$2000` | `COLD` | Cold entry (boot1 lands here) |
| `$200D` | `WARM` | Return to the `<` prompt |
| `$2010` | `RWTSRD` | Read sector (TRACK, SECTOR, BUFPTR in ZP) |
| `$2013` | `RWTSWR` | Write sector (TRACK, SECTOR, BUFPTR in ZP) |
| `$2016` | `FOPEN` | Find file by name (PARSEWS, FNAMELEN) |
| `$2019` | `FSEEK` | A = data-sector index into open file |
| `$201C` | `FRDSEC` | Read next file data sector → SECBUF (advances TSL_IDX; C=1 on EOF/err) |
| `$201F` | `FWRSEC` | Write SECBUF as next file data sector (advances TSL_IDX; C=1 on EOF/err) |
| `$2022` | `RDVTOC` | Reload VTOCBUF from disk |
| `$2025` | `FLIST` | CAT-style file listing (paginates; prints BYTES FREE) |
| `$2028` | `FREE` | Count free sectors → A (lo) + X (hi); also ZP `$6B/$6C` |
| `$202B` | `FORMAT` | Zero-fill every sector + blank VTOC (`I` cmd) |
| `$202E` | `INITSYS` | FORMAT + write boot1 + OneDOS image + reserve T0/T1:S0 (`IS` cmd) |
| `$2031` | `BRUN` | BRUN file named in PARSEWS/FNAMELEN; returns when child RTSes |

### Zero-page convention for callers

| ZP | Field |
|---|---|
| `$27` | TRACK |
| `$28` | SECTOR (logical, DOS-3.3-style) |
| `$29` | BUFPTR_L |
| `$2A` | BUFPTR_H |
| `$50` | TS_TRK (set by FOPEN) |
| `$51` | TS_SEC (set by FOPEN) |
| `$5C` | TSL_IDX (set by FSEEK) |
| `$0280` | PARSEWS (set caller-side filename here) |
| `$0300` | SECBUF (RWTS / FRDSEC scratch) |
| `$0400` | TSLBUF (T/S list of open file) |
| `$0500` | VTOCBUF (loaded by RDVTOC) |

## Memory map

| Range | Use |
|---|---|
| `$0000-$01FF` | Stack, ZP |
| `$0200-$027F` | WozMon input buffer |
| `$0280-$029D` | PARSEWS (filename parse area) |
| `$02A0-$02BD` | PARSEWS2 (second filename for RENAME) |
| `$0300-$03FF` | SECBUF (sector buffer) |
| `$0400-$04FF` | TSLBUF (T/S list buffer) |
| `$0500-$05FF` | VTOCBUF (volume table of contents) |
| `$0800-$08FF` | boot1 (set up by P6 boot ROM) |
| `$0900-$0955` | WRNIBBUF (aux nibbles, write path) |
| `$0A00-$0AFF` | WRDATBUF (data sixes, write path) |
| `$0B00-$1DFF` | **Free for user programs** (sysdisk utilities load here) |
| `$1E00-$1FFF` | MENU parking area (when sysdisk MENU is the bootprog; free otherwise) |
| `$2000-$2EFF` | OneDOS resident, first 15 sectors |
| `$2F00-$2FFF` | OneDOS resident, 16th sector (loaded from T1 S0) |
| `$C000-$C00F` | Disk II soft switches |
| `$C100-$C1FF` | P6 boot ROM |
| `$C200-$C23F` | WRTAB (6-bit → disk byte encode) |
| `$C240-$C24F` | SKEW table — logical → physical |
| `$C250-$C25F` | INV_SKEW table — physical → logical |
| `$C260-$C2FF` | NIBTAB sentinel padding |
| `$D012-$D013` | PIA / display |
| `$E000-$EFFF` | BASIC (if loaded) |
| `$FF00-$FFFF` | WozMon |

## Compatibility

OneDOS disks are standard **DOS 3.3** — 35 tracks, 16 sectors, 143,360 bytes.
VTOC at T17 S0, catalog at T17 S15 chained back to S1. The `.dsk` file is
logical-order: byte `(T*16+L)*256` holds logical sector `L`.

Everything you call from outside takes **logical** track and sector; skew is the
controller's problem, not yours. Two skew tables live in the Disk-1 card ROM:

| Addr | Table | Use |
|---|---|---|
| `$C240` | `SKEW` | logical → physical (RWTS finds the right addr field) |
| `$C250` | `INV_SKEW` | physical → logical (boot1 places each sector in RAM) |

The upshot: OneDOS disks are interchangeable with real Apple II DOS 3.3 disks.
Drop one in a real //e and it boots; open a DOS 3.3 disk in CiderPress and
OneDOS can read it.

## Sysdisk

The shipped `ONEDOS.DSK` includes a bootmarked menu and a handful of BRUN-able
utilities. A fresh `C100R` lands you here:

```
1) DEMOS
2) EXAMPLES
3) FORMATTER       (real low-level format; Y/N confirm)
4) DISK CHECKER
5) DISK COPIER
6) FILE COPIER
0) EXIT TO ONEDOS
```

Pick a number and it BRUNs the corresponding utility via the `$2031` export;
each ends with `JMP $1E00` to return to the menu. `0` drops you at the `<`
prompt. MENU lives at `$1E00-$1FFF` so it survives children loading over the
lower half of stock RAM; utilities load at `$0B00`.

> DEMOS, EXAMPLES, DISKCHECK, DISKCOPY, and FILECOPY are stubs for now (they
> print TODO and bounce back). FORMATTER has a working low-level-format body,
> but its bit-level write timing hasn't been fully shaken out on real hardware.

## If things go sideways

If OneDOS wedges — hung, garbage on screen, weird state — hit **RESET** and type
`C100R`. The Disk-1 card's RAM is rebuilt from disk on every cold boot, so no
bad state survives a reboot. Whatever was on disk stays on disk; only RAM gets
clobbered.

---

*OneDOS and this guide © Phillip Allison ([@philtimmes](https://github.com/philtimmes)),
included with the Apple-1 Emulator. See [CONTRIBUTORS.md](CONTRIBUTORS.md).*
