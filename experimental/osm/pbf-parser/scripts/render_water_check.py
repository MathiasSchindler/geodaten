#!/usr/bin/env python3
"""Analyze rendered map PNG water coverage and water/land diffs.

This is a developer-side regression helper. It intentionally lives outside the
freestanding C toolchain and uses Pillow when available.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional, Tuple

from PIL import Image


Rgb = Tuple[int, int, int]


@dataclass
class Box:
    x0: int
    y0: int
    x1: int
    y1: int


def is_water(pixel: Rgb) -> bool:
    red, green, blue = pixel
    return blue >= 175 and green >= 130 and red <= 190 and blue >= red + 20


def is_light_land(pixel: Rgb) -> bool:
    red, green, blue = pixel
    return red >= 225 and green >= 225 and blue >= 220 and max(pixel) - min(pixel) <= 35


def parse_box(text: str, width: int, height: int) -> Box:
    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 4:
        raise ValueError("box must be x0,y0,x1,y1")
    values = [float(part) for part in parts]
    if all(0.0 <= value <= 1.0 for value in values):
        x0 = int(values[0] * width)
        y0 = int(values[1] * height)
        x1 = int(values[2] * width)
        y1 = int(values[3] * height)
    else:
        x0, y0, x1, y1 = [int(value) for value in values]
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    x0 = max(0, min(width, x0))
    x1 = max(0, min(width, x1))
    y0 = max(0, min(height, y0))
    y1 = max(0, min(height, y1))
    if x0 == x1 or y0 == y1:
        raise ValueError("box has no area")
    return Box(x0, y0, x1, y1)


def parse_box_expectation(text: str, width: int, height: int) -> Tuple[str, Box, float]:
    name_and_box, threshold_text = text.rsplit(",", 1)
    name, box_text = name_and_box.split("=", 1)
    name = name.strip()
    if not name:
        raise ValueError("expectation name must not be empty")
    return name, parse_box(box_text, width, height), float(threshold_text)


def iter_box(box: Box) -> Iterable[Tuple[int, int]]:
    for y in range(box.y0, box.y1):
        for x in range(box.x0, box.x1):
            yield x, y


def water_ratio(image: Image.Image, box: Optional[Box] = None) -> Tuple[int, int, float]:
    if box is None:
        box = Box(0, 0, image.width, image.height)
    water = 0
    total = 0
    pixels = image.load()
    for x, y in iter_box(box):
        total += 1
        if is_water(pixels[x, y]):
            water += 1
    return water, total, water / total if total else 0.0


def bbox_from_points(points: Iterable[Tuple[int, int]]) -> Optional[Box]:
    iterator = iter(points)
    try:
        first_x, first_y = next(iterator)
    except StopIteration:
        return None
    min_x = max_x = first_x
    min_y = max_y = first_y
    for x, y in iterator:
        min_x = min(min_x, x)
        max_x = max(max_x, x)
        min_y = min(min_y, y)
        max_y = max(max_y, y)
    return Box(min_x, min_y, max_x + 1, max_y + 1)


def compare_images(base: Image.Image, other: Image.Image) -> Tuple[int, int, int, Optional[Box], Optional[Box]]:
    if base.size != other.size:
        raise ValueError("images must have the same dimensions")
    base_pixels = base.load()
    other_pixels = other.load()
    both_water = 0
    other_water_base_light = []
    base_water_other_light = []
    for y in range(base.height):
        for x in range(base.width):
            base_pixel = base_pixels[x, y]
            other_pixel = other_pixels[x, y]
            base_water = is_water(base_pixel)
            other_water = is_water(other_pixel)
            if base_water and other_water:
                both_water += 1
            if other_water and is_light_land(base_pixel):
                other_water_base_light.append((x, y))
            if base_water and is_light_land(other_pixel):
                base_water_other_light.append((x, y))
    return (
        both_water,
        len(other_water_base_light),
        len(base_water_other_light),
        bbox_from_points(other_water_base_light),
        bbox_from_points(base_water_other_light),
    )


def write_diff_mask(base: Image.Image, other: Image.Image, path: Path) -> None:
    if base.size != other.size:
        raise ValueError("images must have the same dimensions")
    base_pixels = base.load()
    other_pixels = other.load()
    out = Image.new("RGB", base.size, (25, 25, 25))
    out_pixels = out.load()
    for y in range(base.height):
        for x in range(base.width):
            base_pixel = base_pixels[x, y]
            other_pixel = other_pixels[x, y]
            if is_water(other_pixel) and is_light_land(base_pixel):
                out_pixels[x, y] = (0, 96, 255)
            elif is_water(base_pixel) and is_light_land(other_pixel):
                out_pixels[x, y] = (255, 96, 0)
            elif is_water(base_pixel) and is_water(other_pixel):
                out_pixels[x, y] = (0, 180, 180)
            elif is_light_land(base_pixel) and is_light_land(other_pixel):
                out_pixels[x, y] = (235, 235, 225)
    out.save(path)


def print_grid(image: Image.Image, columns: int, rows: int) -> None:
    for grid_y in range(rows):
        row = []
        for grid_x in range(columns):
            box = Box(
                image.width * grid_x // columns,
                image.height * grid_y // rows,
                image.width * (grid_x + 1) // columns,
                image.height * (grid_y + 1) // rows,
            )
            _, _, ratio = water_ratio(image, box)
            row.append(f"{ratio:0.2f}")
        print(" ".join(row))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="rendered PNG to analyze")
    parser.add_argument("--compare", help="second PNG to compare against IMAGE")
    parser.add_argument("--box", action="append", default=[], help="region as x0,y0,x1,y1, either pixels or 0..1 fractions")
    parser.add_argument("--expect-water-min", action="append", default=[], help="NAME=x0,y0,x1,y1,RATIO; fail if water ratio is below RATIO")
    parser.add_argument("--expect-water-max", action="append", default=[], help="NAME=x0,y0,x1,y1,RATIO; fail if water ratio is above RATIO")
    parser.add_argument("--grid", default="", help="print water-ratio grid as COLSxROWS, e.g. 12x12")
    parser.add_argument("--write-diff", help="write visual diff mask for --compare")
    args = parser.parse_args()

    image = Image.open(args.image).convert("RGB")
    failed = 0
    water, total, ratio = water_ratio(image)
    print(f"image: {args.image}")
    print(f"size: {image.width}x{image.height}")
    print(f"water_pixels: {water}")
    print(f"total_pixels: {total}")
    print(f"water_ratio: {ratio:.6f}")

    for index, text in enumerate(args.box):
        box = parse_box(text, image.width, image.height)
        box_water, box_total, box_ratio = water_ratio(image, box)
        print(f"box_{index}: {box.x0},{box.y0},{box.x1},{box.y1} water_pixels={box_water} total={box_total} ratio={box_ratio:.6f}")

    for text in args.expect_water_min:
        name, box, threshold = parse_box_expectation(text, image.width, image.height)
        box_water, box_total, box_ratio = water_ratio(image, box)
        passed = box_ratio >= threshold
        print(f"expect_water_min {name}: ratio={box_ratio:.6f} threshold={threshold:.6f} status={'PASS' if passed else 'FAIL'} water_pixels={box_water} total={box_total}")
        if not passed:
            failed = 1

    for text in args.expect_water_max:
        name, box, threshold = parse_box_expectation(text, image.width, image.height)
        box_water, box_total, box_ratio = water_ratio(image, box)
        passed = box_ratio <= threshold
        print(f"expect_water_max {name}: ratio={box_ratio:.6f} threshold={threshold:.6f} status={'PASS' if passed else 'FAIL'} water_pixels={box_water} total={box_total}")
        if not passed:
            failed = 1

    if args.grid:
        columns_text, rows_text = args.grid.lower().split("x", 1)
        print_grid(image, int(columns_text), int(rows_text))

    if args.compare:
        other = Image.open(args.compare).convert("RGB")
        both, other_water_base_light, base_water_other_light, other_bbox, base_bbox = compare_images(image, other)
        print(f"compare: {args.compare}")
        print(f"both_water_pixels: {both}")
        print(f"compare_water_where_image_light: {other_water_base_light} bbox={other_bbox}")
        print(f"image_water_where_compare_light: {base_water_other_light} bbox={base_bbox}")
        if args.write_diff:
            write_diff_mask(image, other, Path(args.write_diff))
            print(f"diff_mask: {args.write_diff}")

    return failed


if __name__ == "__main__":
    raise SystemExit(main())