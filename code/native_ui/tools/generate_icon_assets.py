#!/usr/bin/env python3
"""Generate platform icon artifacts from the single canonical SVG icon."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image
from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer


ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)


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
    base.save(ico_path, format="ICO", sizes=[(size, size) for size in ICON_SIZES])
    base.save(icns_path, format="ICNS")

    print(f"generated {png_path}")
    print(f"generated {ico_path}")
    print(f"generated {icns_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
