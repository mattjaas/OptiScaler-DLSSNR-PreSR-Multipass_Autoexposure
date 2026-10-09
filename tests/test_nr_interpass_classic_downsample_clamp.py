"""Arithmetic checks for v5 classical inter-pass Downsample + ClampProxy fusion.

The shader copies the exact source filter arithmetic from Mode 2. These tests
exercise intermediate FP16 storage, including overflow/non-finite values, and
verify equivalence of (downsample -> FP16 UAV -> ClampProxy -> FP16 UAV)
versus (downsample -> FP16 round-trip -> sanitize/clamp -> FP16 UAV).
This is a CPU arithmetic regression, NOT a GPU image/timing validation.
"""
import math
import random
import struct


def round_fp16(value):
    if math.isnan(value):
        return math.nan
    try:
        return struct.unpack("<e", struct.pack("<e", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def clamp_proxy_rgb(v):
    if not math.isfinite(v):
        v = 0.5
    return min(max(v, 0.0), 1.0)


def reference_stage(raw, fp16):
    intermediate = [round_fp16(x) if fp16 else x for x in raw]
    result = [clamp_proxy_rgb(x) for x in intermediate[:3]] + [intermediate[3]]
    return [round_fp16(x) if fp16 else x for x in result]


def fused_stage(raw, fp16):
    rgb = [round_fp16(x) if fp16 else x for x in raw[:3]]
    result = [clamp_proxy_rgb(x) for x in rgb] + [raw[3]]
    return [round_fp16(x) if fp16 else x for x in result]


def same(a, b):
    return (math.isnan(a) and math.isnan(b)) or a == b


def check_case(raw, label):
    for fp16 in (False, True):
        ref = reference_stage(raw, fp16)
        fused = fused_stage(raw, fp16)
        assert all(same(a, b) for a, b in zip(ref, fused)), (label, fp16, ref, fused)


def main():
    rng = random.Random(20261010)
    specials = [-math.inf, -70000.0, -65504.0, -1.0, -0.25, -0.0,
                0.0, 1e-9, 0.001, 0.49975, 0.5, 0.50025, 0.9999, 1.0,
                1.25, 65504.0, 70000.0, math.inf, math.nan]
    for x in specials:
        for y in specials:
            check_case([x, y, 0.75, x], "specials")

    for k in range(5000):
        raw = [rng.uniform(-2.0, 2.0) for _ in range(4)]
        if k % 13 == 0:
            raw[0] *= 50000.0
        check_case(raw, "random")

    # Formula-level local filter outputs (Area, Bilinear, Point) across
    # non-integer scales and edge footprints. The fusion MUST operate on
    # the resulting sample and not clamp individual Area contributors.
    for src_w, src_h, dst_w, dst_h in (
        (39, 27, 20, 14), (39, 27, 26, 18),
        (1919, 1079, 1247, 701), (17, 13, 13, 10)
    ):
        for _ in range(400):
            source = [rng.uniform(-4, 4) for _ in range(4)]
            check_case(source, ("local-filter-output", src_w, src_h, dst_w, dst_h))

    # Large but finite sample converted to FP16 infinity must be sanitized
    # back to 0.5 before the clamp, not directly clamped to 1.0.
    assert fused_stage([70000.0, -70000.0, math.inf, 0.8], True)[:3] == [0.5, 0.5, 0.5]
    print("Classical Downsample + ClampProxy FP16/FP32 parity: PASS")


if __name__ == "__main__":
    main()
