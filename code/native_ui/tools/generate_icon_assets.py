#!/usr/bin/env python3
"""Generate platform icon artifacts from the single canonical SVG icon."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image
from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer


ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
MACOS_ICONSET_SIZES = (
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
)


def render_svg(svg_path: Path, size: int) -> Image.Image:
    """Render the SVG with Qt so the build does not depend on browser tools."""
    if os.name != "nt" and "QT_QPA_PLATFORM" not in os.environ:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"

    app = QGuiApplication.instance()
    if app is None:
        app = QGuiApplication([])

    renderer = QSvgRenderer(str(svg_path))
    if not renderer.isValid():
        raise RuntimeError(f"Invalid SVG icon: {svg_path}")

    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)

    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
    renderer.render(painter)
    painter.end()

    with TemporaryDirectory() as tmp_dir:
        tmp_png = Path(tmp_dir) / "icon.png"
        if not image.save(str(tmp_png), "PNG"):
            raise RuntimeError(f"Failed to render PNG: {tmp_png}")
        return Image.open(tmp_png).convert("RGBA")


def save_macos_icns(base: Image.Image, icns_path: Path) -> None:
    """Prefer Apple's iconutil so Finder receives a complete .icns set."""
    resampling = getattr(Image, "Resampling", Image)
    resample = resampling.LANCZOS
    with TemporaryDirectory() as tmp_dir:
        iconset_dir = Path(tmp_dir) / f"{icns_path.stem}.iconset"
        iconset_dir.mkdir()
        for filename, size in MACOS_ICONSET_SIZES:
            base.resize((size, size), resample).save(iconset_dir / filename)
        try:
            subprocess.run(
                ["iconutil", "-c", "icns", "-o", str(icns_path), str(iconset_dir)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            return
        except (FileNotFoundError, subprocess.CalledProcessError):
            pass

    base.save(icns_path, format="ICNS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--base", default="aseapp_surface_builder_icon")
    args = parser.parse_args()

    source = args.source.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    base = render_svg(source, 1024)
    png_path = out_dir / f"{args.base}.png"
    ico_path = out_dir / f"{args.base}.ico"
    icns_path = out_dir / f"{args.base}.icns"

    base.save(png_path)
    base.save(ico_path, format="ICO", sizes=[(size, size) for size in ICO_SIZES])
    save_macos_icns(base, icns_path)

    print(f"generated {png_path}")
    print(f"generated {ico_path}")
    print(f"generated {icns_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
