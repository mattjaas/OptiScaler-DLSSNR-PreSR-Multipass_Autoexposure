"""v4 CPU arithmetic parity for paired P100 source stencils in Area inter-pass.

Compares independent v3 dynamic 3x3 bilinear/guided math with a single
logical source neighbourhood shared by adjacent P100 pixels. Includes:
- odd dimensions, P50-P97, clipped corners, interior, unusual scale ratios
- Gaussian range and spatial weights, bilinear clamping, Area weighted samples
- source-tap overlap count (not a simulated GPU timing).
"""
import math
import random
from test_nr_interpass_dynamic_geometry import shared_dynamic


def paired_sample(model, proxy, x, y, guide0, guide1, sigma_r, sigma_s):
    work_h, work_w = len(proxy), len(proxy[0])
    base0_x = math.floor(x + 0.5)
    base1_x = math.floor(x + 0.5 + 1.0 / RATIO_X)
    # This setup uses source coordinates with adjacent native P100 pixels.
    pos0 = (x, y)
    pos1 = (x + 1.0 / RATIO_X, y)
    center0 = (math.floor(pos0[0] + 0.5), math.floor(y + 0.5))
    center1 = (math.floor(pos1[0] + 0.5), math.floor(y + 0.5))
    bilbase0 = (math.floor(pos0[0]), math.floor(y))
    bilbase1 = (math.floor(pos1[0]), math.floor(y))
    frac0 = (pos0[0] - bilbase0[0], y - bilbase0[1])
    frac1 = (pos1[0] - bilbase1[0], y - bilbase1[1])
    bil = [[0.] * 3 for _ in range(2)]
    weighted = [[0.] * 3 for _ in range(2)]
    sums = [0., 0.]
    # For native->working ratio <=1, union has 3 or 4 x columns.
    count = 0
    for dy in range(-1, 2):
        logical_y = center0[1] + dy
        for logical_x in range(min(center0[0], center1[0]) - 1,
                               max(center0[0], center1[0]) + 2):
            count += 1
            sx = min(max(logical_x, 0), work_w - 1)
            sy = min(max(logical_y, 0), work_h - 1)
            p, n = proxy[sy][sx], model[sy][sx]
            residual = [n[k] - p[k] for k in range(3)]
            for index, (pos, center, base, frac, guide) in enumerate((
                    (pos0, center0, bilbase0, frac0, guide0),
                    (pos1, center1, bilbase1, frac1, guide1))):
                if abs(logical_x - center[0]) > 1:
                    continue
                bx = (1.0 - frac[0]) if logical_x == base[0] else (
                    frac[0] if logical_x == base[0] + 1 else 0.)
                by = (1.0 - frac[1]) if logical_y == base[1] else (
                    frac[1] if logical_y == base[1] + 1 else 0.)
                color = sum((p[k] - guide[k])**2 for k in range(3))
                spatial = (sx-pos[0])**2 + (sy-pos[1])**2
                weight = 2.0**(-(color * ((0.7213475204444817/3.)/(sigma_r*sigma_r)) +
                                 spatial * (0.7213475204444817/(sigma_s*sigma_s))))
                sums[index] += weight
                for k in range(3):
                    bil[index][k] += residual[k] * bx * by
                    weighted[index][k] += residual[k] * weight
    guided = [[weighted[q][c] / sums[q] if sums[q] > 1e-8 else bil[q][c]
               for c in range(3)] for q in range(2)]
    return bil, guided, count


def main():
    rng = random.Random(20261009)
    fetch_saved = 0
    tested = 0
    for resolution in (50, 53, 60, 63, 65, 66, 67, 70, 75, 77, 80, 91, 97):
        nw, nh = 49, 37
        ww = max(1, round(nw*resolution/100))
        wh = max(1, round(nh*resolution/100))
        global RATIO_X
        RATIO_X = nw / ww
        proxy = [[[rng.random() for _ in range(3)] for _ in range(ww)] for _ in range(wh)]
        model = [[[rng.random() for _ in range(3)] for _ in range(ww)] for _ in range(wh)]
        for py in (0, 1, nh//2, nh-2, nh-1):
            for px in (0, 1, nw//2, nw-3, nw-2):
                sy = ((py + .5) * wh / nh) - .5
                sx = ((px + .5) * ww / nw) - .5
                posx1 = ((px + 1.5) * ww / nw) - .5
                # Use one adjacent pair; skip overrun at end of row.
                if px + 1 >= nw:
                    continue
                nx0, ny0 = min(max(math.floor(sx+.5),0),ww-1), min(max(math.floor(sy+.5),0),wh-1)
                nx1 = min(max(math.floor(posx1+.5),0),ww-1)
                g0 = [proxy[ny0][nx0][c] + rng.uniform(-.002, .002) for c in range(3)]
                g1 = [proxy[ny0][nx1][c] + rng.uniform(-.002, .002) for c in range(3)]
                for sr in (.015, .1, 1.):
                    for ss in (.7, 1.2, 2.):
                        b, g, count = paired_sample(model, proxy, sx, sy, g0, g1, sr, ss)
                        for i, (p, guide) in enumerate(((sx, g0), (posx1, g1))):
                            rb, rg = shared_dynamic(model, proxy, p, sy, guide, sr, ss)
                            for c in range(3):
                                if abs(rb[c]-b[i][c]) > 2e-12 or abs(rg[c]-g[i][c]) > 2e-12:
                                    raise AssertionError((resolution, px, py, i, c,
                                                          rb[c], b[i][c], rg[c], g[i][c]))
                        fetch_saved += 18-count
                        tested += 1
    assert fetch_saved > 0, "paired mode did not reduce candidate source loads"
    print(f"Paired Area v4: PASS ({tested} adjacent pairs, "
          f"{fetch_saved} fewer logical sample locations than 2x3x3)")


if __name__ == "__main__":
    main()
