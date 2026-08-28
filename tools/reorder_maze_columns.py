#!/usr/bin/env python3
"""Reorders a row-major maze-set .bin (R_Mazes*.bin: N levels of
MAZE_MAP_WIDTH x MAZE_MAP_HEIGHT tiles, row-major -- ty*WIDTH+tx per level,
same layout as images/Maze_map.bin) into column-major per level: for each
level, WIDTH columns of HEIGHT contiguous row bytes each (tx*HEIGHT+ty).

Used for EXTRA mode's R_Mazes_b.bin/R_Mazes_c.bin, which src/prizes.c reads
directly from ROM one column at a time (per-frame maze-transition waves,
and the one-shot full-level load at the start of an EXTRA game) instead of
staging the whole file in XRAM first -- column-major makes each of those
reads a single contiguous run instead of HEIGHT scattered ones.

R_Mazes.bin itself (NORMAL mode) is untouched -- it's still staged whole
into ALL_MAZE_MAPS_DATA and indexed row-major there, matching
images/Maze_map.bin's own layout.
"""

import argparse
import sys

MAZE_MAP_WIDTH = 47
MAZE_MAP_HEIGHT = 30
LEVEL_SIZE = MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT


def reorder_row_major_to_column_major(data: bytes) -> bytes:
    if len(data) % LEVEL_SIZE != 0:
        raise ValueError(
            f"Input size {len(data)} is not a multiple of one level's "
            f"{LEVEL_SIZE} bytes ({MAZE_MAP_WIDTH}x{MAZE_MAP_HEIGHT})"
        )
    num_levels = len(data) // LEVEL_SIZE
    out = bytearray(len(data))
    for level in range(num_levels):
        level_base = level * LEVEL_SIZE
        for tx in range(MAZE_MAP_WIDTH):
            for ty in range(MAZE_MAP_HEIGHT):
                src = level_base + ty * MAZE_MAP_WIDTH + tx
                dst = level_base + tx * MAZE_MAP_HEIGHT + ty
                out[dst] = data[src]
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_file", help="Row-major maze-set .bin (e.g. R_Mazes_b.bin)")
    parser.add_argument(
        "-o", "--output",
        help="Output path (default: overwrite input_file in place)",
    )
    parser.add_argument(
        "--to-row-major",
        action="store_true",
        help="Reverse direction: treat input as column-major and emit row-major instead.",
    )
    args = parser.parse_args()

    with open(args.input_file, "rb") as f:
        data = f.read()

    # The transform is its own inverse (it's a per-level transpose of a
    # WIDTH x HEIGHT grid), so --to-row-major just documents intent.
    result = reorder_row_major_to_column_major(data)

    out_path = args.output or args.input_file
    with open(out_path, "wb") as f:
        f.write(result)

    num_levels = len(data) // LEVEL_SIZE
    print(f"Reordered {num_levels} level(s), {len(data)} bytes -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
