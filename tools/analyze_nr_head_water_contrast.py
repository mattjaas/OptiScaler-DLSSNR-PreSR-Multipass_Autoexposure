"""Seeded depth segmentation and carrier contrast in the supplied Spider-Man scene.

Scene-specific CPU analysis: depth alone does not recognize a person or water.
No DLSS execution, source modification, generic detector or runtime calibration.
Requires NumPy/SciPy; Pillow is only needed for --preview.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import ndimage as ndi

from analyze_nr_edit_capture import digest, plane
from analyze_nr_edit_ghosting import load_edit

LUMA = np.array([.2126, .7152, .0722])
FLOOR_ROI = [.20, .62, .40, .88]
WATER_SEED_ROI = [.65, .36, .90, .42]
OBJECT_ROI = [.40, .33, .58, .93]
OBJECT_SEED = [.48, .55]
HEAD_FRACTION = .12


def fit_surface(depth, roi):
    h, w = depth.shape
    x0, y0, x1, y1 = roi
    ys, xs = np.arange(int(y0 * h), int(y1 * h), 3), np.arange(int(x0 * w), int(x1 * w), 3)
    yy, xx = np.meshgrid(ys / h, xs / w, indexing="ij")
    values = depth[np.ix_(ys, xs)].reshape(-1)
    design = np.stack([xx.ravel(), yy.ravel(), np.ones(xx.size)], axis=1)
    keep = np.ones(len(values), dtype=bool)
    for _ in range(6):
        coeff = np.linalg.lstsq(design[keep], values[keep], rcond=None)[0]
        error = np.abs(design @ coeff - values)
        keep = error <= max(np.quantile(error, .65), 1e-7)
    return coeff, dict(seed_roi=roi, coefficients_xy1=coeff.tolist(),
                       median_retained_depth_error=float(np.median(error[keep])), retained_fraction=float(keep.mean()))


def masks(depth):
    h, w = depth.shape
    floor, floor_info = fit_surface(depth, FLOOR_ROI)
    water_plane, water_info = fit_surface(depth, WATER_SEED_ROI)
    x, y = np.arange(w)[None, :] / w, np.arange(h)[:, None] / h
    floor_z = floor[0] * x + floor[1] * y + floor[2]
    water_z = water_plane[0] * x + water_plane[1] * y + water_plane[2]
    seed = (int(OBJECT_SEED[1] * h), int(OBJECT_SEED[0] * w))
    seed_depth = float(depth[seed])
    x0, y0, x1, y1 = OBJECT_ROI
    candidate = ((depth > seed_depth * .80) & (depth < seed_depth * 1.20) & (depth - floor_z > .00035)
                 & (x > x0) & (x < x1) & (y > y0) & (y < y1))
    labels, _ = ndi.label(candidate, structure=np.ones((3, 3)))
    if not labels[seed]:
        raise ValueError("Object seed is not in the depth foreground")
    character = labels == labels[seed]
    cy, cx = np.where(character)
    head_cutoff = cy.min() + HEAD_FRACTION * (cy.max() + 1 - cy.min())
    head = character & (np.arange(h)[:, None] < head_cutoff)
    water = ((np.abs(depth - water_z) < np.maximum(2e-5, np.abs(water_z) * .03))
             & (depth > 0) & (x > .25) & (x < .98) & (y > .325) & (y < .72))
    if np.any(character & water):
        raise ValueError("Depth masks overlap")
    description = dict(floor=floor_info, water=water_info, object_seed_uv=OBJECT_SEED, object_seed_depth=seed_depth,
        object_roi=OBJECT_ROI, character_pixels=int(character.sum()),
        character_bbox_xyxy=[int(cx.min()), int(cy.min()), int(cx.max() + 1), int(cy.max() + 1)],
        head_upper_fraction=HEAD_FRACTION, head_cutoff_y=float(head_cutoff),
        head_pixels=int(head.sum()), water_pixels=int(water.sum()),
        warning="ROIs and upper-head cutoff are scene-specific, visually checked priors, not semantic depth recognition.")
    return character, head, water, description


def carrier(raw, gain):
    # HDR values above 1 remain intact in all measurements.
    return np.clip(.5 + np.minimum(raw, 0) + gain * np.maximum(raw, 0), 0, 65504).astype(np.float16).astype(np.float64)


def region_statistics(values):
    y = values @ LUMA
    return dict(rgb_mean=values.mean(axis=0).tolist(), numerical_luma_mean=float(y.mean()))


def contrast(a, b):
    ya, yb = a @ LUMA, b @ LUMA
    absolute = np.abs(ya - yb)
    relative = np.divide(absolute, ya + yb, out=np.zeros_like(ya), where=ya + yb > 0)
    return dict(mean_absolute_luma_difference=float(absolute.mean()),
                mean_pair_michelson=float(relative.mean()),
                mean_absolute_rgb_difference=np.abs(a - b).mean(axis=0).tolist(),
                mean_absolute_rg_difference=float(np.abs(a[:, :2] - b[:, :2]).mean()),
                first_luma_mean=float(ya.mean()), second_luma_mean=float(yb.mean()))


def render(raw, character, head, water, nearwater, ring, output):
    from PIL import Image, ImageDraw, ImageFont, ImageOps
    cy, cx = np.where(head)
    crop = (int(cx.min()) - 90, int(cy.min()) - 45, int(cx.max()) + 90, int(cy.max()) + 55)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 23)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 18)
    views = []
    for gain in [6., 2.25, 6.]:
        rgb = np.uint8(np.floor(np.clip(carrier(raw, gain), 0, 1) * 255 + .5))
        if not views:
            rgb[head] = (rgb[head] * .35 + np.array([255, 65, 65]) * .65).astype(np.uint8)
            rgb[nearwater] = (rgb[nearwater] * .35 + np.array([40, 155, 255]) * .65).astype(np.uint8)
            rgb[ring] = [255, 235, 80]
        view = ImageOps.contain(Image.fromarray(rgb).crop(crop), (780, 520), Image.Resampling.NEAREST)
        panel = Image.new("RGB", (780, 520), (20, 24, 30))
        panel.paste(view, ((780 - view.width) // 2, (520 - view.height) // 2))
        views.append(panel)
    canvas = Image.new("RGB", (2400, 640), (20, 24, 30))
    draw = ImageDraw.Draw(canvas)
    for i, (title, view) in enumerate(zip(["Depth: head red, nearby water blue", "Carrier at 2.25x", "Carrier at 6x"], views)):
        x = 15 + i * 795
        draw.text((x, 12), title, font=font, fill="white")
        canvas.paste(view, (x, 55))
    draw.text((15, 590), "Yellow: measured inner contour. Identical numeric display scale; >1 clipped only in PNG, not in measurements.", font=small, fill=(201, 211, 222))
    draw.text((15, 614), "Same captured frame. Positive edit rescaled only; no DLSS reconstruction, exposure or ghosting simulation.", font=small, fill=(201, 211, 222))
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    m = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    if not m["depth_inverted"] or m["full_nr_answer"] or m["edit_saturation"] != 1 or m["depth_colour_strength"] != 0:
        parser.error("This scene-specific audit requires reversed depth and plain edit transport")
    path = args.metadata.with_suffix(".dds")
    source = load_edit(path)
    gain = float(m["positive_gain"])
    if not np.isfinite(gain) or gain <= 0:
        raise ValueError("Invalid saved multiplier")
    raw = np.where(source > 0, source.astype(np.float64) / gain, source)
    assert np.array_equal(raw[source <= 0], source[source <= 0])
    depth_path = path.with_suffix(".depth.dds")
    depth = plane(depth_path, 1, 41)[..., 0]
    if depth.shape != raw.shape[:2] or depth.shape != (m["height"], m["width"]) or not np.isfinite(depth).all():
        raise ValueError("Invalid depth or grid")
    character, head, water, mask_info = masks(depth)
    head_inner = ndi.binary_erosion(character, iterations=2) & head
    water_inner = ndi.binary_erosion(water, iterations=2)
    nearwater = water_inner & (ndi.distance_transform_edt(~head_inner) <= 64)
    transported = {g: carrier(raw, g) for g in [2.25, 6.]}
    if gain == 6:
        independent_saved_carrier = np.clip(.5 + source.astype(np.float64), 0, 65504).astype(np.float16)
        assert np.array_equal(transported[6], independent_saved_carrier)
    report = dict(warning="One static scene, seeded depth masks. Numerical carrier contrast is not a ghosting forecast.",
        sha256={path.name: digest(path), depth_path.name: digest(depth_path), args.metadata.name: digest(args.metadata)},
        user_observation="Ghosting on water during camera rotation; this capture only overlaps water with the head.",
        mask_description=mask_info, numerical_luma_weights=LUMA.tolist(),
        measurement_stage="Private DLSS FP16 carrier before NGX, no clipping at 1 and no exposure/normalization.",
        head_interior_pixels=int(head_inner.sum()), nearby_water_pixels=int(nearwater.sum()), region_statistics=[],
        local_pairs=[])
    for g, colour in transported.items():
        hs, ws = region_statistics(colour[head_inner]), region_statistics(colour[nearwater])
        yh, yw = hs["numerical_luma_mean"], ws["numerical_luma_mean"]
        report["region_statistics"].append(dict(gain=g, head=hs, nearby_water=ws,
            difference_of_luma_means=abs(yh - yw), michelson_of_luma_means=abs(yh - yw) / (yh + yw)))
    report["head_cutoff_sensitivity"] = []
    cy, _ = np.where(character)
    ci = ndi.binary_erosion(character, iterations=2)
    cr = ci & ~ndi.binary_erosion(ci)
    wd, wn = ndi.distance_transform_edt(~water_inner, return_indices=True)
    for fraction in [.12, .15]:
        limit = cy.min() + fraction * (cy.max() + 1 - cy.min())
        subset = cr & (np.arange(depth.shape[0])[:, None] < limit) & (wd <= 32)
        sy, sx = np.where(subset)
        wy, wx = wn[:, sy, sx]
        vals = [dict(gain=g, **contrast(c[sy, sx], c[wy, wx])) for g, c in transported.items()]
        report["head_cutoff_sensitivity"].append(dict(upper_fraction=fraction, cutoff_y=float(limit), pairs=len(sy),
            statistics=vals, absolute_luma_difference_ratio=vals[1]["mean_absolute_luma_difference"] / vals[0]["mean_absolute_luma_difference"]))
    report["within_water_contrast"] = []
    for offset in [1, 4]:
        pairs = []
        for a, b in (((slice(None), slice(None, -offset)), (slice(None), slice(offset, None))),
                     ((slice(None, -offset), slice(None)), (slice(offset, None), slice(None)))):
            pairs.append((a, b, nearwater[a] & nearwater[b]))
        stats = []
        for g, c in transported.items():
            a = np.concatenate([c[left][mask] for left, _, mask in pairs])
            b = np.concatenate([c[right][mask] for _, right, mask in pairs])
            stats.append(dict(gain=g, **contrast(a, b)))
        report["within_water_contrast"].append(dict(offset_pixels=offset,
            pairs=sum(int(mask.sum()) for _, _, mask in pairs), statistics=stats,
            ratios_6_over_2_25={key: stats[1][key] / stats[0][key] for key in
                ["mean_absolute_luma_difference", "mean_pair_michelson", "mean_absolute_rg_difference"]}))
    display_ring = None
    for erosion in [2, 4, 8]:
        inside = ndi.binary_erosion(character, iterations=erosion)
        ring = inside & ~ndi.binary_erosion(inside) & head
        clean_water = ndi.binary_erosion(water, iterations=erosion)
        distance, nearest = ndi.distance_transform_edt(~clean_water, return_indices=True)
        for radius in [16, 32, 64]:
            selected = ring & (distance <= radius)
            yy, xx = np.where(selected)
            if not len(xx):
                raise ValueError("No local head/water pairs in the selected band")
            wy, wx = nearest[:, yy, xx]
            assert np.all(character[yy, xx]) and np.all(water[wy, wx])
            stats = [dict(gain=g, **contrast(c[yy, xx], c[wy, wx])) for g, c in transported.items()]
            ratios = {key: stats[1][key] / stats[0][key] for key in
                ["mean_absolute_luma_difference", "mean_pair_michelson", "mean_absolute_rg_difference"]}
            report["local_pairs"].append(dict(erosion_pixels=erosion, maximum_pair_distance=radius,
                pairs=len(xx), distinct_water_pixels=len(np.unique(wy * depth.shape[1] + wx)),
                statistics=stats, ratios_6_over_2_25=ratios))
            if erosion == 2 and radius == 32:
                display_ring = selected
                print(f"Local reference (erosion=2, radius=32): {len(xx)} pairs, Y contrast {stats[0]['mean_absolute_luma_difference']:.9f} -> {stats[1]['mean_absolute_luma_difference']:.9f}, ratio {ratios['mean_absolute_luma_difference']:.6f}")
    if args.preview:
        render(raw, character, head, water, nearwater, display_ring, args.preview)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
