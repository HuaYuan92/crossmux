#!/usr/bin/env python3
"""Generate src/images/BootArt.h — a full-screen 1-bit boot splash image.

The firmware draws this bitmap through GfxRenderer::drawPixel(), which takes
*logical*, orientation-aware coordinates. So the bitmap is stored UPRIGHT at
the device's logical portrait size (default 480x800) with no rotation baked in.
This deliberately avoids GfxRenderer::drawImage(), which does NOT rotate bits
("// TODO: Rotate Bits") and therefore cannot fill a portrait screen correctly.

Bit convention: 1 = black ink, 0 = white. Row-major, MSB-first.

Usage:
    python3 scripts/gen_boot_art.py <input.png|jpg|...> [out_width] [out_height]

    out_width / out_height default to 480 / 800 (X3/X4/Waveshare 3.97 portrait).
"""
import sys
from pathlib import Path

from PIL import Image

DEFAULT_W = 480
DEFAULT_H = 800
OUT_HEADER = Path("src/images/BootArt.h")


def cover_crop(img: Image.Image, out_w: int, out_h: int) -> Image.Image:
    """Scale to fully cover out_w x out_h, then center-crop the overflow."""
    iw, ih = img.size
    scale = max(out_w / iw, out_h / ih)
    nw, nh = round(iw * scale), round(ih * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left = (nw - out_w) // 2
    top = (nh - out_h) // 2
    return img.crop((left, top, left + out_w, top + out_h))


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    src = sys.argv[1]
    out_w = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_W
    out_h = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_H

    img = Image.open(src).convert("RGB")
    img = cover_crop(img, out_w, out_h)
    # Mode "1" applies Floyd-Steinberg dithering, which preserves the tonal
    # gradients of ink-wash art far better than a hard threshold.
    bw = img.convert("L").convert("1")
    px = bw.load()

    row_bytes = (out_w + 7) // 8
    data = bytearray(row_bytes * out_h)
    for y in range(out_h):
        base = y * row_bytes
        for x in range(out_w):
            # In PIL mode "1": 0 == black, 255 == white.
            if px[x, y] == 0:
                data[base + (x >> 3)] |= 0x80 >> (x & 7)

    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        f"// Full-screen 1-bit boot art, {out_w}x{out_h}, stored UPRIGHT in logical",
        "// (orientation-aware) coordinates. bit=1 -> black, bit=0 -> white.",
        "// Row-major, MSB-first. Draw via GfxRenderer::drawPixel(), not drawImage().",
        f"// Source: {src}",
        "// Regenerate via scripts/gen_boot_art.py",
        f"#define BOOTART_WIDTH {out_w}",
        f"#define BOOTART_HEIGHT {out_h}",
        "static const uint8_t BootArt[] = {",
    ]
    hexes = [f"0x{b:02x}" for b in data]
    for i in range(0, len(hexes), 16):
        chunk = ", ".join(hexes[i:i + 16])
        trailing = "," if i + 16 < len(hexes) else ""
        lines.append(f"    {chunk}{trailing}")
    lines.append("};")

    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    OUT_HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {OUT_HEADER} ({out_w}x{out_h}, {len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
