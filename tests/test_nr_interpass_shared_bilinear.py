"""CPU regression for the P50 Area guided stencil's bilinear reuse.

No GPU timings or image equivalence are implied by this arithmetic test:
the Windows DXC/MSBuild workflow checks shader compilation separately.
"""
import math
import random

GAUSSIAN_EXP2 = 0.7213475204444817


def sample2d(img, x, y):
    return img[max(0, min(len(img) - 1, y))][max(0, min(len(img[0]) - 1, x))]


def reference_bilinear(model, proxy, xpos, ypos):
    x0, y0 = math.floor(xpos), math.floor(ypos)
    fx, fy = xpos - x0, ypos - y0
    out = [0.0, 0.0, 0.0]
    for dy in (0, 1):
        for dx in (0, 1):
            wx = (1.0 - fx) if dx == 0 else fx
            wy = (1.0 - fy) if dy == 0 else fy
            n = sample2d(model, x0 + dx, y0 + dy)
            p = sample2d(proxy, x0 + dx, y0 + dy)
            for c in range(3):
                out[c] += (n[c] - p[c]) * wx * wy
    return out


def reference_guided(model, proxy, px, py, guide, sigma_range, sigma_spatial):
    bx, by = math.floor(px + 0.5), math.floor(py + 0.5)
    sums = [0.0, 0.0, 0.0]
    total = 0.0
    for oy in range(-1, 2):
        for ox in range(-1, 2):
            sx = max(0, min(len(proxy[0]) - 1, bx + ox))
            sy = max(0, min(len(proxy) - 1, by + oy))
            p, n = proxy[sy][sx], model[sy][sx]
            dist_color = sum((p[c] - guide[c]) ** 2 for c in range(3)) / 3.0
            dist_spatial = (sx - px) ** 2 + (sy - py) ** 2
            w = 2.0 ** (-GAUSSIAN_EXP2 * (
                dist_color / sigma_range ** 2 + dist_spatial / sigma_spatial ** 2))
            total += w
            for c in range(3):
                sums[c] += (n[c] - p[c]) * w
    return [v / total for v in sums]


def shared_stencil(model, proxy, out_x, out_y, guides, sigma_range, sigma_spatial):
    bilinear = [[0.0] * 3 for _ in range(4)]
    guided = [[0.0] * 3 for _ in range(4)]
    totals = [0.0] * 4
    pos = [(out_x - 0.25, out_y - 0.25),
           (out_x + 0.25, out_y - 0.25),
           (out_x - 0.25, out_y + 0.25),
           (out_x + 0.25, out_y + 0.25)]
    for oy in range(-1, 2):
        wy0 = 0.25 if oy == -1 else 0.75 if oy == 0 else 0.0
        wy1 = 0.25 if oy == 1 else 0.75 if oy == 0 else 0.0
        for ox in range(-1, 2):
            wx0 = 0.25 if ox == -1 else 0.75 if ox == 0 else 0.0
            wx1 = 0.25 if ox == 1 else 0.75 if ox == 0 else 0.0
            x = max(0, min(len(proxy[0]) - 1, out_x + ox))
            y = max(0, min(len(proxy) - 1, out_y + oy))
            p, n = proxy[y][x], model[y][x]
            residual = [n[c] - p[c] for c in range(3)]
            bilinear_weights = (wx0 * wy0, wx1 * wy0, wx0 * wy1, wx1 * wy1)
            for k in range(4):
                px, py = pos[k]
                dist_color = sum((p[c] - guides[k][c]) ** 2 for c in range(3))
                dist_spatial = (x - px) ** 2 + (y - py) ** 2
                w = 2.0 ** (-
                    ((GAUSSIAN_EXP2 / 3.0) / sigma_range ** 2) * dist_color
                    - (GAUSSIAN_EXP2 / sigma_spatial ** 2) * dist_spatial)
                totals[k] += w
                for c in range(3):
                    bilinear[k][c] += residual[c] * bilinear_weights[k]
                    guided[k][c] += residual[c] * w
    return bilinear, [[v / totals[k] for v in guided[k]] for k in range(4)]


def main():
    rng = random.Random(20261009)
    w, h = 19, 13
    proxy = [[[rng.random() for _ in range(3)] for _ in range(w)] for _ in range(h)]
    model = [[[rng.random() for _ in range(3)] for _ in range(w)] for _ in range(h)]
    # Include corners, edges and interior; include very narrow range-sigma.
    for sigma_r in (0.015, 0.1, 1.0):
        for sigma_s in (0.5, 1.2, 3.0):
            for out_x, out_y in ((0, 0), (w - 1, 0), (0, h - 1),
                                 (w - 1, h - 1), (1, 6), (9, 6), (17, 11)):
                guides = [[rng.random() for _ in range(3)] for _ in range(4)]
                b, g = shared_stencil(model, proxy, out_x, out_y, guides, sigma_r, sigma_s)
                positions = ((out_x - 0.25, out_y - 0.25),
                             (out_x + 0.25, out_y - 0.25),
                             (out_x - 0.25, out_y + 0.25),
                             (out_x + 0.25, out_y + 0.25))
                for k, (px, py) in enumerate(positions):
                    b_ref = reference_bilinear(model, proxy, px, py)
                    g_ref = reference_guided(model, proxy, px, py, guides[k], sigma_r, sigma_s)
                    for c in range(3):
                        assert abs(b_ref[c] - b[k][c]) < 1e-12, ("bilinear", out_x, out_y, k, c)
                        assert abs(g_ref[c] - g[k][c]) < 1e-12, ("guided", out_x, out_y, k, c)
    print("P50 3x3 shared bilateral/bilinear math: PASS (corners, edges, interiors)")


if __name__ == "__main__":
    main()
