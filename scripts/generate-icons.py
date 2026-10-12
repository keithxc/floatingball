#!/usr/bin/env python3
"""Stage portable app icons from the maintained SVG, outside the source tree.

Requires ImageMagick and Pillow. Copy reviewed results into assets/ after rendering.
Apple's layered .icon document is maintained separately, using the same geometry.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent / "assets" / "floatingball.svg"
    output = args.output.resolve()
    if output == source.parent.parent or source.parent.parent in output.parents:
        parser.error("stage generated icons outside the source checkout")
    output.mkdir(parents=True, exist_ok=True)
    master = output / "floatingball-1024.png"
    subprocess.run(["magick", "-background", "none", str(source),
                    "-depth", "8", str(master)], check=True)
    with Image.open(master) as original:
        icon = original.convert("RGBA")
        for size in (16, 32, 48, 64, 128, 256, 512):
            icon.resize((size, size), Image.Resampling.LANCZOS).save(
                output / f"floatingball-{size}.png")
        icon.save(output / "floatingball.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
        iconset = output / "FloatingBall.iconset"
        iconset.mkdir(exist_ok=True)
        for points in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                pixels = points * scale
                name = f"icon_{points}x{points}{'@2x' if scale == 2 else ''}.png"
                icon.resize((pixels, pixels), Image.Resampling.LANCZOS).save(iconset / name)
        if shutil.which("iconutil"):
            subprocess.run(["iconutil", "-c", "icns", "-o",
                            str(output / "AppIcon.icns"), str(iconset)], check=True)
        else:
            icon.save(output / "AppIcon.icns")


if __name__ == "__main__":
    main()
