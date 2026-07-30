#!/usr/bin/env python3
"""Build minimal widescreen menu overrides with an overscan backplate."""

from __future__ import annotations

import argparse
from pathlib import Path

from lxml import etree

MENU_FILES = ("ui_mm_main_16.xml", "ui_mm_main_c_16.xml")
FUNCTIONAL_ATTRIBUTES = ("name", "id", "action", "handler")


def parse_fragment(path: Path) -> tuple[etree._Element, list[str]]:
    parser = etree.XMLParser(resolve_entities=False, no_network=True)
    try:
        content = path.read_text(encoding="utf-8")
        wrapped = f"<ui_fragment>{content}</ui_fragment>"
        return etree.fromstring(wrapped.encode("utf-8"), parser), []
    except etree.XMLSyntaxError as error:
        return etree.Element("invalid"), [str(error)]


def functional_identifiers(root: etree._Element) -> tuple[tuple[str, str, str], ...]:
    identifiers = []
    for element in root.iter():
        for attribute in FUNCTIONAL_ATTRIBUTES:
            value = element.get(attribute)
            if value is not None:
                identifiers.append((element.tag, attribute, value))
    return tuple(identifiers)


def serialize_fragment(root: etree._Element) -> str:
    return "".join(
        etree.tostring(child, encoding="unicode", pretty_print=True)
        for child in root
    )


def add_backplate(root: etree._Element) -> None:
    background = root.find("w/background")
    if background is None:
        raise RuntimeError("menu XML has no w/background element")

    backplate = etree.Element(
        "auto_static",
        x="-4096",
        y="0",
        width="9216",
        height="768",
        stretch="1",
    )
    texture = etree.SubElement(backplate, "texture")
    texture.text = r"ui_menu2_backgraund"
    background.insert(0, backplate)


def build(source: Path, output: Path) -> None:
    source_root, messages = parse_fragment(source)
    if messages:
        raise RuntimeError(f"{source.name}: {'; '.join(messages)}")

    expected_identifiers = functional_identifiers(source_root)
    add_backplate(source_root)
    if functional_identifiers(source_root) != expected_identifiers:
        raise RuntimeError(f"{source.name}: functional identifiers changed")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(serialize_fragment(source_root), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("stock_ui", type=Path)
    parser.add_argument("output_ui", type=Path)
    args = parser.parse_args()

    for name in MENU_FILES:
        build(args.stock_ui / name, args.output_ui / name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
