"""Reproduce FP16 cache/product/range experiments independently of DLL compilation."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
REFERENCE = "2a97b7b22c675ba69bf161ea05d0c59de910a629"
PRE = "OptiScaler/shaders/dlssnr/precompile/"


def blob(name):
    return subprocess.check_output(["git", "show", f"{REFERENCE}:{PRE}{name}"], cwd=ROOT)


def patch(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Prototype patch no longer unique: {old}")
    return source.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "artifacts/nr-fp16-audit")
    args = parser.parse_args()
    out = args.out.resolve()
    base = out / "baseline"
    base.mkdir(parents=True, exist_ok=True)
    for name in ("carrier", "low", "normal", "sdr", "hdr2", "hdr4"):
        filename = f"dlssnr_superior_{name}_Shader.cso"
        (base / filename).write_bytes(blob(filename))
    (out / "dlssnr_resize.hlsli").write_bytes(blob("dlssnr_resize.hlsli"))
    source = blob("dlssnr.hlsl").decode("utf-8")
    half4 = patch(source, "groupshared float3 gLow64Tile[36 * 20];",
                  "groupshared float16_t3 gLow64Tile[36 * 20];")
    half4 = patch(half4, "gLow64Tile[i] = SanitizeFinite3(gModel.SampleLevel(gLinear, uv, 0).rgb, 0.0);",
                  "gLow64Tile[i] = float16_t3(SanitizeFinite3(gModel.SampleLevel(gLinear, uv, 0).rgb, 0.0));")
    half8 = half4.replace("gLow64Tile[36 * 20]", "gLow64Tile[36 * 36]")
    half8 = half8.replace("groupId * uint2(8u, 4u)", "groupId * uint2(8u, 8u)")
    half8 = half8.replace("i < 36u * 20u; i += 32u", "i < 36u * 36u; i += 64u")
    half8 = half8.replace("[numthreads(8, 4, 1)]", "[numthreads(8, 8, 1)]")
    float8 = half8.replace("float16_t3", "float3")
    product = patch(half4, "sum += SanitizeFinite3(residualTap, 0.0) * (weights[x] * weights[y]);",
                    "sum += float3(float16_t3(SanitizeFinite3(residualTap, 0.0)) * float16_t(weights[x] * weights[y] * (1.0 / 10000.0)));")
    product = patch(product, "float4(sum * (1.0 / 10000.0), 1.0);", "float4(sum, 1.0);")
    old_range = "const float rangeDistance2 = dot(colourDelta, colourDelta) * (1.0 / 3.0);"
    # There are two guided implementations; change only the reference used by Superior resolve.
    range16 = source.replace(old_range,
                             "const float3 squared = float3(float16_t3(colourDelta) * float16_t3(colourDelta));\n"
                             "            const float rangeDistance2 = (squared.x + squared.y + squared.z) * (1.0 / 3.0);", 1)
    dxc = ROOT / "OptiScaler/shaders/shader_tools/dxc.exe"
    for variant, hlsl in (("half4", half4), ("half8", half8), ("float8", float8),
                           ("product16", product), ("range16", range16)):
        directory = out / variant
        directory.mkdir(exist_ok=True)
        path = directory / "test.hlsl"
        path.write_text(hlsl, encoding="utf-8")
        native = variant != "float8"
        for name in (("normal", "sdr", "hdr2", "hdr4") if variant == "range16" else ("low",)):
            defines = ["DLSSNR_SUPERIOR_SCALING=1"]
            defines += ["DLSSNR_SUPERIOR_LOW=1"] if name == "low" else ["DLSSNR_SUPERIOR_RESOLVE=1", "DLSSNR_SUPERIOR_NORMAL=1"]
            if name == "sdr":
                defines += ["DLSSNR_SUPERIOR_SDR=1"]
            if name.startswith("hdr"):
                defines += [f"DLSSNR_SUPERIOR_HDR={name[3]}"]
            command = [str(dxc), "-T", "cs_6_2" if native else "cs_6_0", "-E", "CSMain",
                       "-O3", "-Qstrip_debug", "-Qstrip_reflect", "-I", str(out)]
            if native:
                command += ["-enable-16bit-types"]
            for define in defines:
                command += ["-D", define]
            stem = directory / f"dlssnr_superior_{name}"
            subprocess.run(command + [str(path), "-Fo", f"{stem}_Shader.cso", "-Fc", f"{stem}.asm"], check=True)
    print(f"Prepared FP16 experiments in {out}; reference {REFERENCE}")


if __name__ == "__main__":
    main()
