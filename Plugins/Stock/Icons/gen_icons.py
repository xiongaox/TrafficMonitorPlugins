#!/usr/bin/env python3
"""Validate the versioned Lucide SVG sources used by Stock's icon renderer.

IconsData.h is the checked-in generated artifact. This script validates that
new source SVG files stay inside the deliberately supported line-art subset.
Add a reviewed tessellator before accepting curves or arcs into the renderer.
"""

from __future__ import annotations

import re
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "lucide"


def tokenise_path(data: str) -> list[str]:
    return re.findall(r"[MmLlHhVvZz]|-?(?:\d+\.?(?:\d*)?|\.\d+)(?:[eE][-+]?\d+)?", data)


def validate_path(data: str) -> None:
    commands = {token for token in tokenise_path(data) if token.isalpha()}
    unsupported = commands.difference("MmLlHhVvZz")
    if unsupported:
        raise ValueError(f"Unsupported SVG path command(s): {sorted(unsupported)}")


def main() -> None:
    files = sorted(SOURCE.glob("*.svg"))
    if not files:
        raise FileNotFoundError(f"No Lucide SVG sources found in {SOURCE}")

    allowed_elements = {"path", "line", "polyline", "polygon", "circle", "rect"}
    for source in files:
        root = ET.parse(source).getroot()
        for child in root:
            element = child.tag.rsplit("}", 1)[-1]
            if element not in allowed_elements:
                raise ValueError(f"Unsupported SVG element in {source.name}: {element}")
            if element == "path":
                validate_path(child.attrib["d"])

    print(f"Validated {len(files)} Lucide SVG sources in {SOURCE}")


if __name__ == "__main__":
    main()
