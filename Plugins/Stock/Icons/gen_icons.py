#!/usr/bin/env python3
"""Validate the versioned SVG sources used by Stock's icon renderer.

IconsData.h is the checked-in, hand-reviewed geometry. This script validates
that every source SVG stays inside the deliberately supported subset, so a new
source cannot silently introduce geometry the renderer cannot draw.

The renderer draws straight polylines only. Sources that use arc or Bezier
commands are allowed, but their curves must be approximated as polylines in
IconsData.h; the script lists them so that requirement stays visible.

Sources live in two directories:

* ``lucide/`` -- unmodified icons from lucide-static v1.43.0 (ISC licensed).
* ``custom/`` -- project-defined icons that Lucide does not provide. Each file
  must open with a comment naming its origin and why no library icon fits.

Run this from ``Plugins/Stock/Icons``:

    python gen_icons.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent
SOURCE_DIRS = (ROOT / "lucide", ROOT / "custom")
REQUIRED_DIRS = (ROOT / "lucide",)

CANVAS = 24.0
ALLOWED_ELEMENTS = {"path", "line", "polyline", "polygon", "circle", "rect"}
STRAIGHT_COMMANDS = set("MmLlHhVvZz")
CURVE_COMMANDS = set("CcSsQqTtAa")
NUMBER = r"-?(?:\d+\.?(?:\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


def tokenise(data: str) -> list[str]:
    return re.findall(rf"[MmLlHhVvZzCcSsQqTtAa]|{NUMBER}", data)


def validate_path(data: str, source: Path) -> bool:
    """Check the command set and report whether the path needs approximation."""
    commands = {token for token in tokenise(data) if token.isalpha()}
    unsupported = commands.difference(STRAIGHT_COMMANDS | CURVE_COMMANDS)
    if unsupported:
        raise ValueError(
            f"{source.name}: unrecognised SVG path command(s) {sorted(unsupported)}"
        )
    return bool(commands & CURVE_COMMANDS)


def validate_points(data: str, source: Path, element: str) -> None:
    numbers = [float(token) for token in re.findall(NUMBER, data)]
    if len(numbers) % 2 != 0:
        raise ValueError(f"{source.name}: <{element}> has an odd number of coordinates")
    for index in range(0, len(numbers), 2):
        x, y = numbers[index], numbers[index + 1]
        if not (0.0 <= x <= CANVAS and 0.0 <= y <= CANVAS):
            raise ValueError(
                f"{source.name}: <{element}> point ({x:g}, {y:g}) falls outside the "
                f"{CANVAS:g} x {CANVAS:g} canvas"
            )


def validate_attributes(child: ET.Element, source: Path, element: str) -> None:
    shapes = {
        "circle": (("cx", "cy"), ("r",)),
        "rect": (("x", "y"), ("width", "height")),
        "line": (("x1", "y1", "x2", "y2"), ()),
    }
    coordinates, extents = shapes.get(element, ((), ()))

    for name in coordinates:
        value = float(child.attrib.get(name, 0.0))
        if not 0.0 <= value <= CANVAS:
            raise ValueError(
                f"{source.name}: <{element}> attribute {name}={value:g} falls outside "
                f"the {CANVAS:g} x {CANVAS:g} canvas"
            )
    for name in extents:
        value = float(child.attrib.get(name, 0.0))
        if value <= 0.0 or value > CANVAS:
            raise ValueError(
                f"{source.name}: <{element}> attribute {name}={value:g} is not within "
                f"(0, {CANVAS:g}]"
            )


def validate_directory(directory: Path) -> tuple[int, list[str]]:
    """Validate every source in one directory, returning its approximated files."""
    files = sorted(directory.glob("*.svg"))
    approximated: list[str] = []
    for source in files:
        needs_approximation = False
        root = ET.parse(source).getroot()
        for child in root:
            if not isinstance(child.tag, str):
                continue  # XML comments and processing instructions
            element = child.tag.rsplit("}", 1)[-1]
            if element not in ALLOWED_ELEMENTS:
                raise ValueError(f"{source.name}: unsupported SVG element <{element}>")
            if element == "path":
                needs_approximation |= validate_path(child.attrib["d"], source)
            elif element in {"polyline", "polygon"}:
                validate_points(child.attrib["points"], source, element)
            else:
                validate_attributes(child, source, element)
        if needs_approximation:
            approximated.append(source.name)
    return len(files), approximated


def main() -> int:
    missing = [directory for directory in REQUIRED_DIRS if not directory.is_dir()]
    if missing:
        for directory in missing:
            print(f"error: required source directory is missing: {directory}", file=sys.stderr)
        return 1

    total = 0
    approximated: list[str] = []
    for directory in SOURCE_DIRS:
        if not directory.is_dir():
            continue
        try:
            count, curved = validate_directory(directory)
        except (ValueError, ET.ParseError) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        total += count
        approximated.extend(curved)
        print(f"Validated {count} SVG source(s) in {directory.name}/")

    if approximated:
        listed = ", ".join(approximated)
        print(
            f"note: {len(approximated)} source(s) use curve commands and rely on "
            f"hand-approximated polylines in IconsData.h: {listed}"
        )
    print(f"Total: {total} icon source(s) on a {CANVAS:g} x {CANVAS:g} canvas")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
