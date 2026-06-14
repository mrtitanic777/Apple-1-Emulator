# Author: Phillip Allison (github.com/philtimmes)
# Written for the Apple-1 Emulator. See CONTRIBUTORS.md for details.

# make_icon.ps1
#
# Generates apple1.ico (32x32, 32-bit RGBA) next to this script.
# Designed to be run once and the resulting .ico checked in, but
# safe to re-run any time.
#
# Glyph: a stylised disk - dark grey square, a lighter inner
# rectangle representing the magnetic medium, and a centred hub
# hole.  Looks like a 5.25" floppy at small sizes.

param([string]$Out = (Join-Path $PSScriptRoot 'apple1.ico'))

$W = 32
$H = 32

# 32x32 BGRA pixel buffer, bottom-up (BMP convention)
$px = New-Object byte[] ($W * $H * 4)

function PutPx([int]$x, [int]$y, [byte]$r, [byte]$g, [byte]$b, [byte]$a) {
    if ($x -lt 0 -or $x -ge $W -or $y -lt 0 -or $y -ge $H) { return }
    # BMP is bottom-up: flip Y
    $row = $H - 1 - $y
    $off = ($row * $W + $x) * 4
    $px[$off + 0] = $b
    $px[$off + 1] = $g
    $px[$off + 2] = $r
    $px[$off + 3] = $a
}

# Background: floppy jacket (dark grey)
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        # 1-pixel transparent border so the icon doesn't touch the edge
        if ($x -eq 0 -or $x -eq ($W - 1) -or $y -eq 0 -or $y -eq ($H - 1)) {
            PutPx $x $y 0 0 0 0      # transparent
        } else {
            PutPx $x $y 32 32 36 255 # jacket
        }
    }
}

# Media window (lighter rectangle near the top)
for ($y = 4; $y -lt 14; $y++) {
    for ($x = 6; $x -lt 26; $x++) {
        PutPx $x $y 180 180 184 255
    }
}

# Hub hole (white disc in centre-bottom)
$cx = 16; $cy = 21; $r = 5
for ($y = ($cy - $r); $y -le ($cy + $r); $y++) {
    for ($x = ($cx - $r); $x -le ($cx + $r); $x++) {
        $dx = $x - $cx; $dy = $y - $cy
        if (($dx*$dx + $dy*$dy) -le ($r*$r)) {
            PutPx $x $y 230 230 232 255
        }
    }
}

# Drive label slot (small dark notch at the top edge of the media window)
for ($y = 3; $y -lt 5; $y++) {
    for ($x = 14; $x -lt 18; $x++) {
        PutPx $x $y 10 10 12 255
    }
}

# Build the .ico container in memory.
# Layout: ICONDIR (6) + ICONDIRENTRY (16) + BITMAPINFOHEADER (40)
#         + pixel data (W*H*4) + AND mask (W*H/8, ignored for 32-bit but
#         required by spec).

$bmpInfoHeaderSize  = 40
$pixelBytes         = $W * $H * 4
$andMaskBytes       = $W * $H / 8                # 1 bit per pixel
$imageSize          = $bmpInfoHeaderSize + $pixelBytes + $andMaskBytes
$imageOffset        = 6 + 16                     # ICONDIR + 1 entry

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)

# ICONDIR
$bw.Write([uint16]0)   # reserved
$bw.Write([uint16]1)   # type = icon
$bw.Write([uint16]1)   # count

# ICONDIRENTRY
$bw.Write([byte]$W)            # width (0 == 256)
$bw.Write([byte]$H)            # height
$bw.Write([byte]0)             # color count (0 for >=8bpp)
$bw.Write([byte]0)             # reserved
$bw.Write([uint16]1)           # planes
$bw.Write([uint16]32)          # bit count
$bw.Write([uint32]$imageSize)  # bytes in resource
$bw.Write([uint32]$imageOffset)

# BITMAPINFOHEADER  -- biHeight is doubled (XOR + AND masks).
$bw.Write([uint32]$bmpInfoHeaderSize)
$bw.Write([int32]$W)
$bw.Write([int32]($H * 2))
$bw.Write([uint16]1)        # planes
$bw.Write([uint16]32)       # bit count
$bw.Write([uint32]0)        # compression = BI_RGB
$bw.Write([uint32]0)        # size image (can be 0 for BI_RGB)
$bw.Write([int32]0)         # x ppm
$bw.Write([int32]0)         # y ppm
$bw.Write([uint32]0)        # clr used
$bw.Write([uint32]0)        # clr important

# XOR mask (the pixel data)
$bw.Write($px)

# AND mask: all-zero (transparency comes from the alpha channel).
$bw.Write((New-Object byte[] $andMaskBytes))

[IO.File]::WriteAllBytes($Out, $ms.ToArray())
$bw.Close()

Write-Host "Wrote $Out ($($imageSize + $imageOffset) bytes)"
