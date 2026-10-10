"""v8: 20x20 linear vs 2D-strided group fill and 16x16 compact proof.

Reconstructed P100 float4 and each working pixel's Area reduction are unchanged.
This test verifies every tile P100 position is produced once in either
work distribution and the 16x16 capacity guarantee based on actual dimensions.
It does not imply GPU image parity or a performance win.
"""
import math
import random


def area_tile_range(native, work, group):
    lo = group * 8
    hi = min(lo + 8, work)
    # Mirrors output-tile math: same output coordinates and extrema.
    start = math.floor((float(lo) * float(native)) / float(work))
    stop = math.ceil((float(hi) * float(native)) / float(work))
    return start, stop


def legacy_linear_fill(w, h):
    destinations = []
    for lane in range(64):
        for index in range(lane, w * h, 64):
            destinations.append((index % w, index // w))
    return destinations


def new_2d_fill(w, h):
    destinations = []
    for ly in range(8):
        for lx in range(8):
            for ty in range(ly, h, 8):
                for tx in range(lx, w, 8):
                    destinations.append((tx, ty))
    return destinations


def verify_group(native_w, native_h, work_w, work_h, gx, gy):
    start_x, end_x = area_tile_range(native_w, work_w, gx)
    start_y, end_y = area_tile_range(native_h, work_h, gy)
    w, h = end_x - start_x, end_y - start_y
    assert 0 < w <= 20 and 0 < h <= 20, (native_w, native_h, work_w, work_h, gx, gy, w, h)
    original = legacy_linear_fill(w, h)
    modified = new_2d_fill(w, h)
    unique = set(original)
    assert unique == set(modified) and len(original) == len(modified) == len(unique) == w * h
    # Physical groupshared index must be in bounds and injective at both pitches.
    assert len({(y * 20 + x) for x, y in modified}) == w * h
    if 3 * native_w <= 5 * work_w and 3 * native_h <= 5 * work_h:
        assert w <= 16 and h <= 16, (native_w, native_h, work_w, work_h, gx, gy, w, h)
        assert all(0 <= y * 16 + x < 256 for x, y in modified)
        assert len({(y * 16 + x) for x, y in modified}) == w * h


def main():
    rng = random.Random(20261010)
    count = 0
    for native_w, native_h in [
        (17, 9), (39, 27), (51, 37), (128, 72), (153, 87),
        (191, 107), (192, 108), (257, 143), (384, 216),
        (3840, 2160), (7680, 4320),
    ]:
        for scale in (51, 53, 55, 57, 59, 60, 61, 63, 65, 66, 70, 75, 80, 85, 90):
            w = max(1, round(native_w * scale / 100))
            h = max(1, round(native_h * scale / 100))
            ncols = (w + 7) // 8
            nrows = (h + 7) // 8
            groups = {(0, 0), (ncols - 1, 0), (0, nrows - 1),
                      (ncols - 1, nrows - 1), (ncols // 2, nrows // 2)}
            for _ in range(24):
                groups.add((rng.randrange(ncols), rng.randrange(nrows)))
            for gx, gy in groups:
                verify_group(native_w, native_h, w, h, gx, gy)
                count += 1
    print(f"v8 tiled 2D mapping and compact tile bounds: PASS ({count} groups)")


if __name__ == "__main__":
    main()
