#!/usr/bin/env python3
"""Rasterize resources/mega_explorer_icon.svg into the app icon set.

Run by hand (needs Pillow and ImageMagick); the outputs are committed, so a
normal build never calls this. Re-run it whenever the SVG changes -- the SVG is
the source of truth, every PNG and the .ico are derived.

The size list is the Windows desktop-icon set: 16/24/32/48 are what the shell,
the taskbar and Alt-Tab ask for at 100-200% scaling, 64 covers 400%, and 256 is
the one Explorer's extra-large view uses (stored PNG-compressed inside the .ico,
which Pillow does on its own above 64px).

Pillow cannot rasterize SVG, so ImageMagick does that step -- its bundled
librsvg renders the gradients and the Segoe UI text of this file correctly,
where ImageMagick's own MSVG renderer does not. Everything below the master
render is Pillow.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

SIZES = [16, 24, 32, 48, 64, 256]
ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "resources" / "mega_explorer_icon.svg"
OUT_DIR = ROOT / "resources"

# 1536 is a whole multiple of every entry in SIZES, so each downsample lands on
# an integer ratio and no size picks up resampling artefacts the others avoid.
MASTER = 1536


def render_master(destination: Path) -> None:
    magick = shutil.which("magick") or shutil.which("convert")
    if magick is None:
        raise SystemExit("ImageMagick not found on PATH (need `magick`)")
    subprocess.run(
        [magick, "-background", "none", str(SVG), "-resize", f"{MASTER}x{MASTER}", str(destination)],
        check=True,
    )


def main() -> int:
    if not SVG.is_file():
        raise SystemExit(f"missing source artwork: {SVG}")

    with tempfile.TemporaryDirectory() as tmp:
        master_path = Path(tmp) / "master.png"
        render_master(master_path)
        master = Image.open(master_path).convert("RGBA")

    images = []
    for size in SIZES:
        image = master.resize((size, size), Image.LANCZOS)
        images.append(image)
        image.save(OUT_DIR / f"appicon-{size}.png")

    # append_images, not just sizes: without it Pillow fills every entry by
    # downscaling the 256 one, which loses the separately resampled 16/24 ones.
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
