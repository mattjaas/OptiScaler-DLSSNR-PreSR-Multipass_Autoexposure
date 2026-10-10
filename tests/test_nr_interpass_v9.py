"""Independent v9 math checks: compact Linear, preweighted bilinear, shared source tiles.

No GPU-speed assertions: only actual end-user GPU timing can establish those.
"""
from __future__ import annotations
import math
import random

R = random.Random(9102026)


def weights(original: float):
    base = math.floor(original)
    rounded = math.floor(original + 0.5)
    fraction = original - base
    moved = rounded != base
    pre = (1.0 - fraction, fraction, 0.0) if moved else (0.0, 1.0 - fraction, fraction)
    for step in (-1, 0, 1):
        logical = rounded + step
        reference = ((1.0 - fraction) if logical == base else
                     (fraction if logical == base + 1 else 0.0))
        assert abs(reference - pre[step + 1]) < 1e-12


def verify_geometry(native_w, native_h, work_w, work_h, compact=False):
    pitch = 16 if compact else 20
    source_pitch = 24
    ratio_x = work_w / native_w
    ratio_y = work_h / native_h
    for gy in range(0, work_h, 8):
        for gx in range(0, work_w, 8):
            ex, ey = min(gx + 8, work_w), min(gy + 8, work_h)
            fx, fy = math.floor(gx * native_w / work_w), math.floor(gy * native_h / work_h)
            lx, ly = math.ceil(ex * native_w / work_w), math.ceil(ey * native_h / work_h)
            if lx - fx > pitch or ly - fy > pitch:
                assert not compact or not (native_w * 3 <= 5 * work_w and native_h * 3 <= 5 * work_h)
                continue
            cache0x = max(0, math.floor((fx + 0.5) * ratio_x) - 2)
            cache0y = max(0, math.floor((fy + 0.5) * ratio_y) - 2)
            cache1x = min(work_w - 1, math.floor((lx - 0.5) * ratio_x) + 2)
            cache1y = min(work_h - 1, math.floor((ly - 0.5) * ratio_y) + 2)
            assert 0 < cache1x - cache0x + 1 <= source_pitch
            assert 0 < cache1y - cache0y + 1 <= source_pitch
            for py in range(fy, ly):
                for px in range(fx, lx):
                    bx = math.floor((px + 0.5) * ratio_x)
                    by = math.floor((py + 0.5) * ratio_y)
                    for oy in (-1, 0, 1):
                        for ox in (-1, 0, 1):
                            sx = max(0, min(work_w - 1, bx + ox))
                            sy = max(0, min(work_h - 1, by + oy))
                            assert cache0x <= sx <= cache1x
                            assert cache0y <= sy <= cache1y


def test():
    for _ in range(20000):
        weights(R.uniform(-5, 500))
    for native_w, native_h in [
        (1920, 1080), (2560, 1440), (3840, 2160),
        (641, 359), (97, 71), (193, 109), (513, 289),
    ]:
        for ratio in (0.5, 0.65, 0.66, 0.75, 0.9):
            ww = max(1, round(native_w * ratio))
            hh = max(1, round(native_h * ratio))
            for compact in (False, True):
                verify_geometry(native_w, native_h, ww, hh, compact)
    # Classic: 8x8 native output produces <=12x12 source union, P50/P65.
    for native_w in (17, 641, 1920, 3840):
        for ratio in (0.5, 0.65):
            work_w = round(native_w * ratio)
            for first in range(0, native_w, 8):
                end = min(first + 8, native_w)
                start = max(0, math.floor((first + 0.5) * work_w / native_w) - 2)
                last = min(work_w - 1, math.floor((end - 0.5) * work_w / native_w) + 2)
                assert last - start + 1 <= 12
    print("v9 bilinear preweights and shared-cache bounds: OK")


if __name__ == "__main__":
    test()
