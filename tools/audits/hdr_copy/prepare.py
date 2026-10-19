"""Build isolated HDR-copy prototypes; never edit production shaders or headers."""
import argparse
from pathlib import Path
import re
import subprocess

REFERENCE = "1ce30f76cd6c3193dc6d32e1ca89cd5851885225"
ROOT = Path(__file__).resolve().parents[3]
PRECOMPILE = "OptiScaler/shaders/dlssnr/precompile/"


def blob(name):
    return subprocess.check_output(["git", "show", f"{REFERENCE}:{PRECOMPILE}{name}"], cwd=ROOT)


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Unexpected source at prototype patch: {old}")
    return source.replace(old, new)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "artifacts/nr-hdr-copy-audit")
    args = parser.parse_args()
    out = args.out.resolve()
    base, candidate = out / "baseline", out / "candidate"
    base.mkdir(parents=True, exist_ok=True)
    candidate.mkdir(parents=True, exist_ok=True)
    for name in ("encode", "area", "low", "normal", "hdr2", "hdr4"):
        stem = f"dlssnr_superior_{name}_Shader.cso"
        (base / stem).write_bytes(blob(stem))
    (out / "dlssnr_resize.hlsli").write_bytes(blob("dlssnr_resize.hlsli"))
    source = blob("dlssnr.hlsl").decode("utf-8")
    source = replace_once(source, "#if defined(DLSSNR_SUPERIOR_ENCODE)",
                          "#if defined(DLSSNR_SUPERIOR_INPLACE)\n"
                          "#define gReplaceDetailStrength 0.0\n#endif\n"
                          "#if defined(DLSSNR_SUPERIOR_ENCODE)")
    store = "        gKeep[id.xy] = float4(originalFrame, source.a);"
    source = replace_once(source, store,
                          "#if !defined(DLSSNR_SUPERIOR_NO_HDR_COPY)\n" + store + "\n#endif")
    load = ("    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)\n"
            "                                              : gOriginal.Load(int3(id.xy, 0));")
    source = replace_once(source, load,
                          "#if defined(DLSSNR_SUPERIOR_INPLACE)\n"
                          "    float4 originalSample = gTarget[id.xy];\n#else\n" + load + "\n#endif")
    hlsl = out / "no_copy.hlsl"
    hlsl.write_text(source, encoding="utf-8")
    dxc = ROOT / "OptiScaler/shaders/shader_tools/dxc.exe"
    for name in ("encode_nocopy", "normal_inplace", "hdr2_inplace", "hdr4_inplace"):
        defines = ["DLSSNR_SUPERIOR_SCALING=1"]
        if name == "encode_nocopy":
            defines += ["DLSSNR_SUPERIOR_ENCODE=1", "DLSSNR_SUPERIOR_NO_HDR_COPY=1"]
        else:
            defines += ["DLSSNR_SUPERIOR_RESOLVE=1", "DLSSNR_SUPERIOR_NORMAL=1",
                        "DLSSNR_SUPERIOR_INPLACE=1"]
            if name.startswith("hdr"):
                defines += [f"DLSSNR_SUPERIOR_HDR={name[3]}"]
        stem = f"dlssnr_superior_{name}"
        asm = candidate / f"{stem}.asm"
        command = [str(dxc), "-T", "cs_6_0", "-E", "CSMain", "-O3",
                   "-Qstrip_debug", "-Qstrip_reflect", "-I", str(out)]
        for define in defines:
            command += ["-D", define]
        subprocess.run(command + [str(hlsl), "-Fo", str(candidate / f"{stem}_Shader.cso"),
                                  "-Fc", str(asm)], check=True)
        if name != "encode_nocopy":
            listing = asm.read_text(encoding="utf-8")
            bindings = listing.split("; Resource Bindings:", 1)[1].split("target datalayout", 1)[0]
            if re.search(r"\bt2\b", bindings):
                raise RuntimeError("In-place resolve still binds original SRV t2")
            if "Typed UAV Load Additional Formats" not in listing:
                raise RuntimeError("Expected typed UAV load requirement")
    print(f"Isolated prototypes ready: {out}; reference {REFERENCE}")


if __name__ == "__main__":
    main()
