"""Arithmetic regression: arbitrary-resolution guided/bilinear stencil reuse.

Checks every selected native-pixel contribution and the finished Area reduction,
including frame edges and odd dimensions. Does not claim GPU bit identity or speed.
"""
import math
import random

from test_nr_interpass_shared_bilinear import (
    GAUSSIAN_EXP2, reference_bilinear, reference_guided, sample2d
)


def shared_dynamic(model, proxy, px, py, guide, sigma_r, sigma_s):
    base_x, base_y = math.floor(px + 0.5), math.floor(py + 0.5)
    bx, by = math.floor(px), math.floor(py)
    fx, fy = px - bx, py - by
    bil, weighted = [0.0] * 3, [0.0] * 3
    weight_sum = 0.0
    for oy in range(-1, 2):
        for ox in range(-1, 2):
            lx, ly = base_x + ox, base_y + oy
            sx = max(0, min(len(proxy[0]) - 1, lx))
            sy = max(0, min(len(proxy) - 1, ly))
            p, n = proxy[sy][sx], model[sy][sx]
            bwx = (1.0 - fx) if lx == bx else (fx if lx == bx + 1 else 0.0)
            bwy = (1.0 - fy) if ly == by else (fy if ly == by + 1 else 0.0)
            dist_col = sum((p[c] - guide[c]) ** 2 for c in range(3))
            dist_spatial = (sx - px) ** 2 + (sy - py) ** 2
            # Same rearranged exp2 inputs as the v3 HLSL stencil.
            weight = 2.0 ** (-(
                dist_col * ((GAUSSIAN_EXP2 / 3.0) / (sigma_r * sigma_r)) +
                dist_spatial * (GAUSSIAN_EXP2 / (sigma_s * sigma_s))))
            weight_sum += weight
            for c in range(3):
                e = n[c] - p[c]
                bil[c] += e * (bwx * bwy)
                weighted[c] += e * weight
    ref_guided = [x / weight_sum for x in weighted]
    return bil, ref_guided


def area_pixel(native, model, proxy, px, py, gain_high, gain_low, sigma_r, sigma_s, share):
    """Manual area-integrate exactly the same per-native guided edit."""
    native_h, native_w = len(native), len(native[0])
    work_h, work_w = len(proxy), len(proxy[0])
    x0, x1 = px * native_w / work_w, (px + 1) * native_w / work_w
    y0, y1 = py * native_h / work_h, (py + 1) * native_h / work_h
    acc = [0.0, 0.0, 0.0]
    for j in range(math.floor(y0), math.ceil(y1)):
        wy = max(0.0, min(y1, j + 1) - max(y0, j))
        for i in range(math.floor(x0), math.ceil(x1)):
            wx = max(0.0, min(x1, i + 1) - max(x0, i))
            uv_posx = (i + 0.5) * work_w / native_w - 0.5
            uv_posy = (j + 0.5) * work_h / native_h - 0.5
            guide = native[j][i]
            if share:
                bil, guided = shared_dynamic(model, proxy, uv_posx, uv_posy, guide, sigma_r, sigma_s)
            else:
                bil = reference_bilinear(model, proxy, uv_posx, uv_posy)
                guided = reference_guided(model, proxy, uv_posx, uv_posy, guide, sigma_r, sigma_s)
            for c in range(3):
                edit = bil[c] * (1 - 0.75) + guided[c] * 0.75
                # Gain + low component is equivalent in both paths; apply
                # same fixed scalar to verify contribution integration.
                acc[c] += (guide[c] + edit * gain_high + gain_low * 0.0) * (wx * wy)
    return [a / ((x1 - x0) * (y1 - y0)) for a in acc]


def main():
    rng = random.Random(20261009)
    for percent in (25, 33, 40, 41, 42, 45, 49, 50, 53, 60, 63, 66, 67, 75, 77, 80, 91, 97):
        # Odd native dimensions exercise actual rounded model resolutions.
        nw, nh = 39, 27
        ww = max(1, round(nw * percent / 100))
        wh = max(1, round(nh * percent / 100))
        proxy = [[[rng.random() for _ in range(3)] for _ in range(ww)] for _ in range(wh)]
        model = [[[rng.random() for _ in range(3)] for _ in range(ww)] for _ in range(wh)]
        # Guide and reduced proxy belong to the SAME scene. This also
        # prevents all Gaussian weights underflowing to zero in Python
        # at capture-calibrated sigma_r=0.015.
        native = []
        for y in range(nh):
            row = []
            for x in range(nw):
                sx = max(0, min(ww - 1, math.floor((x + 0.5) * ww / nw)))
                sy = max(0, min(wh - 1, math.floor((y + 0.5) * wh / nh)))
                row.append([proxy[sy][sx][c] + rng.uniform(-0.002, 0.002) for c in range(3)])
            native.append(row)
        for sy in (0.015, 0.1, 1.0):
            for py in (0, wh // 2, wh - 1):
                for px in (0, ww // 2, ww - 1):
                    r = area_pixel(native, model, proxy, px, py, 0.75, 1.0, sy, 1.2, False)
                    d = area_pixel(native, model, proxy, px, py, 0.75, 1.0, sy, 1.2, True)
                    for c in range(3):
                        assert abs(r[c] - d[c]) < 2e-12, (percent, px, py, c, r[c], d[c])
    print("Dynamic all-scale guided/bilinear + Area arithmetic: PASS (P25..P97, odd dims, edges)")


if __name__ == "__main__":
    main()
