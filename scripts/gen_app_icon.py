#!/usr/bin/env python3
"""Regenerate the placeholder application icon in resources/.

Run by hand (needs Pillow); the outputs are committed, so a normal build never
calls this. It exists so the stand-in art stays reproducible until a real icon
replaces it -- swapping that in means replacing every file listed in SIZES plus
appicon.ico, keeping the same names.

The size list is the Windows desktop-icon set: 16/24/32/48 are what the shell,
the taskbar and Alt-Tab ask for at 100-200% scaling, 64 covers 400%, and 256 is
the one Explorer's extra-large view uses (stored PNG-compressed inside the .ico,
which Pillow does on its own above 64px).
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIZES = [16, 24, 32, 48, 64, 256]
OUT_DIR = Path(__file__).resolve().parent.parent / "resources"

# Fluent's accent blue, top to bottom. Deliberately unlike MEGA's red: this is a
# placeholder and must not read as the service's own mark.
TOP = (58, 150, 221)
BOTTOM = (12, 90, 160)
SUPERSAMPLE = 8


def draw_master(size: int) -> Image.Image:
    """One icon at `size`, drawn `SUPERSAMPLE` times larger and downsampled."""
    big = size * SUPERSAMPLE
    canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    gradient = Image.new("RGBA", (1, big))
    for y in range(big):
        t = y / max(1, big - 1)
        gradient.putpixel(
            (0, y),
            (
                round(TOP[0] + (BOTTOM[0] - TOP[0]) * t),
                round(TOP[1] + (BOTTOM[1] - TOP[1]) * t),
                round(TOP[2] + (BOTTOM[2] - TOP[2]) * t),
                255,
            ),
        )
    gradient = gradient.resize((big, big))

    mask = Image.new("L", (big, big), 0)
    inset = big * 0.02
    ImageDraw.Draw(mask).ellipse((inset, inset, big - inset - 1, big - inset - 1), fill=255)
    canvas.paste(gradient, (0, 0), mask)

    glyph = load_font(round(big * 0.62))
    draw = ImageDraw.Draw(canvas)
    if glyph is not None:
        draw.text((big / 2, big / 2), "M", font=glyph, fill=(255, 255, 255, 255), anchor="mm")
    else:
        # No usable font: a white ring still reads as a deliberate placeholder,
        # where a bare disc reads as a rendering failure.
        ring = big * 0.28
        draw.ellipse(
            (ring, ring, big - ring - 1, big - ring - 1),
            outline=(255, 255, 255, 255),
            width=max(1, round(big * 0.06)),
        )

    return canvas.resize((size, size), Image.LANCZOS)


def load_font(px: int):
    for name in ("segoeuib.ttf", "seguisb.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return None


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    images = []
    for size in SIZES:
        image = draw_master(size)
        images.append(image)
        image.save(OUT_DIR / f"appicon-{size}.png")

    # append_images, not just sizes: without it Pillow fills every entry by
    # downscaling the 256 one, which loses the separately drawn 16/24 masters.
    images[-1].save(
        OUT_DIR / "appicon.ico",
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=images[:-1],
    )
    print(f"wrote {len(SIZES)} PNGs and appicon.ico into {OUT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
