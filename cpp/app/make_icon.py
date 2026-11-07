"""Gera ml_odonto.ico a partir de software/odontolesao/inst/app/www/logo.svg (sem depender de cairo/rsvg).

O logotipo tem só um retângulo arredondado, o contorno do dente (curvas cúbicas relativas) e um anel;
aqui as curvas são achatadas em polígonos e desenhadas com supersampling.
Uso: python cpp/app/make_icon.py
"""

from __future__ import annotations

import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SVG = ROOT / "software/odontolesao/inst/app/www/logo.svg"
SIZE = 1024  # desenha grande e reduz (antisserrilhado)
SCALE = SIZE / 64


def flatten(path: str, steps: int = 24) -> list[tuple[float, float]]:
    tokens = re.findall(r"[MmCcSsZzHhVvLl]|-?\d*\.?\d+", path)
    points: list[tuple[float, float]] = []
    x = y = 0.0
    last_control: tuple[float, float] | None = None
    command = ""
    index = 0

    def number() -> float:
        nonlocal index
        index += 1
        return float(tokens[index - 1])

    def cubic(p1, p2, p3):
        nonlocal x, y
        p0 = (x, y)
        for step in range(1, steps + 1):
            t = step / steps
            u = 1 - t
            points.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                           u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
        x, y = p3

    while index < len(tokens):
        if re.fullmatch(r"[A-Za-z]", tokens[index]):
            command = tokens[index]
            index += 1
            if command in "Zz":
                continue
        if command == "M":
            x, y = number(), number()
            points.append((x, y))
            command = "L"
        elif command in "cC":
            rel = command == "c"
            values = [number() for _ in range(6)]
            if rel:
                values = [values[i] + (x if i % 2 == 0 else y) for i in range(6)]
            p1, p2, p3 = (values[0], values[1]), (values[2], values[3]), (values[4], values[5])
            cubic(p1, p2, p3)
            last_control = p2
        elif command in "sS":
            rel = command == "s"
            values = [number() for _ in range(4)]
            if rel:
                values = [values[i] + (x if i % 2 == 0 else y) for i in range(4)]
            p1 = (2 * x - last_control[0], 2 * y - last_control[1]) if last_control else (x, y)
            p2, p3 = (values[0], values[1]), (values[2], values[3])
            cubic(p1, p2, p3)
            last_control = p2
        elif command in "hH":
            x = number() + (x if command == "h" else 0.0)
            points.append((x, y))
            last_control = None
        elif command in "vV":
            y = number() + (y if command == "v" else 0.0)
            points.append((x, y))
            last_control = None
        elif command in "lL":
            dx, dy = number(), number()
            x, y = (x + dx, y + dy) if command == "l" else (dx, dy)
            points.append((x, y))
            last_control = None
        else:
            raise ValueError(f"comando de caminho não suportado: {command}")
    return points


def main() -> None:
    svg = SVG.read_text(encoding="utf-8")
    rect = re.search(r'<rect[^>]*rx="([\d.]+)"[^>]*fill="(#[0-9A-Fa-f]{3,6})"', svg)
    tooth = re.search(r'<path d="([^"]+)" fill="(#[0-9A-Fa-f]{3,6})"', svg)
    ring = re.search(r'<circle cx="([\d.]+)" cy="([\d.]+)" r="([\d.]+)"[^>]*stroke="(#[0-9A-Fa-f]{3,6})" '
                     r'stroke-width="([\d.]+)"', svg)

    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, SIZE - 1, SIZE - 1), radius=float(rect[1]) * SCALE, fill=rect[2])
    draw.polygon([(px * SCALE, py * SCALE) for px, py in flatten(tooth[1])], fill=tooth[2])
    cx, cy, r, width = (float(ring[i]) * SCALE for i in (1, 2, 3, 5))
    draw.ellipse((cx - r - width / 2, cy - r - width / 2, cx + r + width / 2, cy + r + width / 2),
                 outline=ring[4], width=round(width))

    output = Path(__file__).with_name("ml_odonto.ico")
    image.resize((256, 256), Image.LANCZOS).save(
        output, sizes=[(256, 256), (64, 64), (48, 48), (32, 32), (16, 16)])
    print(output)


if __name__ == "__main__":
    main()
