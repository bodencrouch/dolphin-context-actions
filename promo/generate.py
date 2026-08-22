#!/usr/bin/env python3
"""Render the store.kde.org preview images and demo GIF.

Everything is drawn as SVG in KDE Breeze-dark colors, then rasterized with
rsvg-convert; the GIF is assembled with ImageMagick. Rerun after visual
changes:  python3 promo/generate.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE / "src"

# Breeze dark
BG = "#1b1e20"
WINDOW = "#2a2e32"
PANEL = "#31363b"
BORDER = "#3f4447"
TEXT = "#eff0f1"
MUTED = "#95a1a6"
ACCENT = "#3daee9"
GREEN = "#27ae60"

W, H = 1280, 800


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y, s, size=22, fill=TEXT, weight="normal", anchor="start", family="sans-serif"):
    return (
        f'<text x="{x}" y="{y}" font-family="{family}" font-size="{size}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{esc(s)}</text>'
    )


def rrect(x, y, w, h, r=8, fill=PANEL, stroke=BORDER, sw=1, opacity=1.0):
    return (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}" opacity="{opacity}"/>'
    )


def file_icon(x, y, scale=1.0, color=ACCENT, label=""):
    w, h = 46 * scale, 58 * scale
    fold = 14 * scale
    parts = [
        f'<path d="M {x} {y} h {w - fold} l {fold} {fold} v {h - fold} h {-w} z" '
        f'fill="{WINDOW}" stroke="{color}" stroke-width="2.5"/>',
        f'<path d="M {x + w - fold} {y} v {fold} h {fold} z" fill="{color}"/>',
    ]
    if label:
        parts.append(text(x + w / 2, y + h * 0.68, label, size=13 * scale,
                          fill=color, weight="bold", anchor="middle"))
    return "".join(parts)


def menu(x, y, w, items, highlight=-1, title=None):
    """items: list of (icon_char, label, submenu_arrow)"""
    row_h = 44
    pad_top = 14 if title is None else 52
    h = pad_top + row_h * len(items) + 12
    parts = [rrect(x, y, w, h, r=10, fill=PANEL, stroke=BORDER, sw=1.5)]
    if title:
        parts.append(text(x + 20, y + 34, title, size=19, fill=MUTED))
        parts.append(f'<line x1="{x + 12}" y1="{y + 44}" x2="{x + w - 12}" '
                     f'y2="{y + 44}" stroke="{BORDER}" stroke-width="1"/>')
    for i, (icon, label, arrow) in enumerate(items):
        ry = y + pad_top + i * row_h
        if i == highlight:
            parts.append(rrect(x + 8, ry, w - 16, row_h - 4, r=6,
                               fill=ACCENT, stroke="none", sw=0))
        color = "#ffffff" if i == highlight else TEXT
        parts.append(text(x + 26, ry + 28, icon, size=20, fill=color))
        parts.append(text(x + 62, ry + 28, label, size=20, fill=color))
        if arrow:
            parts.append(text(x + w - 30, ry + 28, "❯", size=16,
                              fill="#ffffff" if i == highlight else MUTED))
    return "".join(parts), h


def svg(body, w=W, h=H):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}">'
        f'<rect width="{w}" height="{h}" fill="{BG}"/>' + body + "</svg>"
    )


MAIN_MENU = [
    ("✂", "Cut", False),
    ("⧉", "Copy", False),
    ("⌫", "Move to Trash", False),
    ("⇄", "Convert", True),
    ("♪", "Audio Converter", True),
    ("⤴", "Upload to Imgur", False),
    ("⚙", "Properties", False),
]

CONVERT_MENU = [
    ("▶", "To MP4", False),
    ("▶", "To WebM", False),
    ("▶", "To MKV", False),
    ("◉", "To GIF…", False),
    ("♪", "Extract Audio", False),
    ("⋯", "More…", False),
]


def frame(main_hl=-1, sub=False, sub_hl=-1, result=False, caption=""):
    parts = []
    # Mock Dolphin window
    parts.append(rrect(60, 60, 720, 640, r=12, fill=WINDOW, stroke=BORDER, sw=2))
    parts.append(rrect(60, 60, 720, 46, r=12, fill=PANEL))
    parts.append(text(420, 90, "Videos — Dolphin", size=19, fill=MUTED, anchor="middle"))
    for i, c in enumerate(("#f67400", "#27ae60", "#da4453")):
        parts.append(f'<circle cx="{745 - i * 26}" cy="83" r="7" fill="{c}"/>')

    # Files in the folder
    parts.append(file_icon(120, 170, 1.5, ACCENT, "MP4"))
    parts.append(text(154, 290, "talk.mp4", size=17, fill=TEXT, anchor="middle"))
    parts.append(file_icon(280, 170, 1.5, MUTED, "MOV"))
    parts.append(text(314, 290, "clip.mov", size=17, fill=MUTED, anchor="middle"))
    if result:
        parts.append(file_icon(440, 170, 1.5, GREEN, "GIF"))
        parts.append(text(474, 290, "talk.gif", size=17, fill=GREEN, anchor="middle"))
        parts.append(text(474, 316, "new", size=15, fill=GREEN, anchor="middle", weight="bold"))

    # Context menu
    if main_hl >= 0:
        m, _ = menu(200, 330, 300, MAIN_MENU, highlight=main_hl)
        parts.append(m)
    if sub:
        s, _ = menu(505, 380, 250, CONVERT_MENU, highlight=sub_hl)
        parts.append(s)

    # Caption panel on the right
    parts.append(text(1020, 200, "Convert files from", size=38, fill=TEXT,
                      weight="bold", anchor="middle"))
    parts.append(text(1020, 250, "the right-click menu", size=38, fill=TEXT,
                      weight="bold", anchor="middle"))
    parts.append(f'<line x1="880" y1="290" x2="1180" y2="290" stroke="{ACCENT}" stroke-width="3"/>')
    if caption:
        parts.append(text(1030, 350, caption, size=26, fill=ACCENT, anchor="middle"))
    parts.append(text(1030, 700, "Dolphin Context Actions", size=22, fill=MUTED, anchor="middle"))
    return svg("".join(parts))


def preview_hero():
    return frame(main_hl=3, sub=True, sub_hl=3, result=True,
                 caption="No apps. No upload sites.")


def preview_menu():
    parts = []
    parts.append(text(W / 2, 90, "One menu per file type,", size=40, fill=TEXT,
                      weight="bold", anchor="middle"))
    parts.append(text(W / 2, 140, "built from the tools you already have", size=40,
                      fill=TEXT, weight="bold", anchor="middle"))
    menus = [
        (90, "video.mp4", "MP4", [("▶", "To WebM", False), ("▶", "To MKV", False),
                           ("◉", "To GIF…", False), ("♪", "Extract Audio", False)]),
        (475, "song.flac", "FLAC", [("♪", "To MP3 (V0)", False), ("♪", "To OGG (Q6)", False),
                            ("♪", "To OPUS", False), ("♪", "To M4A (AAC)", False)]),
        (860, "paper.docx", "DOCX", [("⎙", "To PDF", False), ("⋯", "More…", False),
                             ("", "", False), ("", "", False)]),
    ]
    for x, fname, badge, items in menus:
        parts.append(file_icon(x + 120, 200, 1.4, ACCENT, badge))
        parts.append(text(x + 152, 320, fname, size=19, fill=TEXT, anchor="middle"))
        real = [i for i in items if i[1]]
        m, _ = menu(x, 350, 330, real, title="Convert")
        parts.append(m)
    parts.append(text(W / 2, 740, "ffmpeg for media · LibreOffice and pandoc for documents · "
                      "missing tools are named, not hidden", size=21, fill=MUTED, anchor="middle"))
    return svg("".join(parts))


def preview_formats():
    parts = []
    parts.append(text(W / 2, 100, "24 conversions built in", size=46, fill=TEXT,
                      weight="bold", anchor="middle"))
    parts.append(text(W / 2, 150, "Copies are made next to your file — the original is never touched",
                      size=24, fill=MUTED, anchor="middle"))
    groups = [
        ("Video", ["MP4", "WebM", "MKV", "GIF"], "#da4453"),
        ("Audio", ["MP3", "OGG", "FLAC", "WAV", "M4A", "OPUS", "ALAC"], "#f67400"),
        ("Images", ["PNG", "JPG", "WebP", "HEIC"], "#27ae60"),
        ("Documents", ["PDF", "DOCX", "ODT", "PPTX", "XLSX", "MD"], ACCENT),
        ("Data", ["JSON", "YAML", "CSV", "TOML"], "#9b59b6"),
    ]
    y = 210
    for label, formats, color in groups:
        parts.append(text(120, y + 34, label, size=26, fill=TEXT, weight="bold"))
        x = 320
        for fmt in formats:
            bw = 34 + len(fmt) * 15
            parts.append(rrect(x, y, bw, 52, r=26, fill=WINDOW, stroke=color, sw=2.5))
            parts.append(text(x + bw / 2, y + 34, fmt, size=21, fill=color,
                              weight="bold", anchor="middle"))
            x += bw + 18
        y += 92
    parts.append(text(W / 2, 730, "Add your own with one config entry — see docs/adding-conversions.md",
                      size=22, fill=MUTED, anchor="middle"))
    return svg("".join(parts))


def gif_frames():
    return [
        (frame(caption="Right-click a video…"), 120),
        (frame(main_hl=3, caption="Right-click a video…"), 90),
        (frame(main_hl=3, sub=True, caption="…pick Convert…"), 90),
        (frame(main_hl=3, sub=True, sub_hl=0, caption="…pick a format…"), 70),
        (frame(main_hl=3, sub=True, sub_hl=3, caption="…or make a GIF…"), 90),
        (frame(result=True, caption="Done. Original untouched."), 220),
    ]


def run(*cmd):
    subprocess.run(cmd, check=True)


def main() -> int:
    SRC.mkdir(exist_ok=True)
    previews = {
        "preview-1-hero": preview_hero(),
        "preview-2-menu": preview_menu(),
        "preview-3-formats": preview_formats(),
    }
    for name, content in previews.items():
        svg_path = SRC / f"{name}.svg"
        svg_path.write_text(content, encoding="utf-8")
        run("rsvg-convert", "-o", str(HERE / f"{name}.png"), str(svg_path))
        print(f"rendered promo/{name}.png")

    frame_args = []
    for i, (content, delay) in enumerate(gif_frames()):
        svg_path = SRC / f"gif-frame-{i}.svg"
        svg_path.write_text(content, encoding="utf-8")
        png = SRC / f"gif-frame-{i}.png"
        run("rsvg-convert", "-w", "800", "-o", str(png), str(svg_path))
        frame_args += ["-delay", str(delay), str(png)]
    run("magick", *frame_args, "-loop", "0", "-layers", "optimize",
        str(HERE / "demo.gif"))
    print("rendered promo/demo.gif")
    return 0


if __name__ == "__main__":
    sys.exit(main())
