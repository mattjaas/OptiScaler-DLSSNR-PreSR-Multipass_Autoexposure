"""Reconstruct post-v58 probes in ignored artifacts, without changing production."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = ROOT / "artifacts/nr-post-v58"
REFERENCE = "c241e38477578ec38af230ce11a441539f975d41"
PRE = "OptiScaler/shaders/dlssnr/precompile/"
FIXTURES = {
    "low_quality": "superior_precision_quality.cpp",
    "tall_quality": "superior_precision_quality.cpp",
    "low_timing": "superior_precision_timing.cpp",
    "tall_timing": "superior_precision_timing.cpp",
    "area_quality": "superior_area_quality.cpp",
    "area_timing": "superior_area_timing.cpp",
    "wave_quality": "superior_exposure_quality.cpp",
    "wave_timing": "superior_exposure_timing.cpp",
    "poly_quality": "superior_numeric_quality.cpp",
    "poly_timing": "superior_softknee_timing.cpp",
}
VARIANTS = {"row": "low", "tall16": "low", "area_unroll": "area", "area_guard": "area",
            "wave64": "meter", "wave32": "meter", "poly7": "hdr0", "poly9": "hdr0",
            "shift11": "hdr0", "shift13": "hdr0"}


def blob(path):
    return subprocess.check_output(["git", "show", f"{REFERENCE}:{path}"], cwd=ROOT)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def apply(name, directory):
    subprocess.run(["git", "apply", "--unidiff-zero", f"--directory={directory.relative_to(ROOT).as_posix()}",
                    str(HERE / "patches" / f"{name}.patch")], cwd=ROOT, check=True)


def main():
    tracked = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", REFERENCE],
                                      cwd=ROOT, text=True).splitlines()
    for path in tracked:
        if path.startswith(PRE) and path.endswith(".cso"):
            write(OUT / "baseline" / Path(path).name, blob(path))
        if path.startswith("tests/nr_gpu/") and path.endswith(".h"):
            write(OUT / "include" / path, blob(path))
    for path in ("OptiScaler/shaders/dlssnr/DlssNr_Common.h",
                 "OptiScaler/dlssnr/DlssNr_SuperiorScaling.h",
                 "OptiScaler/dlssnr/DlssNr_BenchmarkCases.h",
                 "OptiScaler/dlssnr/DlssNr_InterPassPolicy.h"):
        write(OUT / "include" / path, blob(path))
    write(OUT / "dlssnr_resize.hlsli", blob(PRE + "dlssnr_resize.hlsli"))
    for name, source in FIXTURES.items():
        path = OUT / "fixtures" / f"{name}.cpp"
        write(path, blob("tests/nr_gpu/" + source))
        apply(name, path.parent)
        # Keep production headers pinned even if the current checkout advances.
        code = path.read_text(encoding="utf-8").replace('"../../OptiScaler/',
                   '"' + (OUT / "include/OptiScaler").as_posix() + '/')
        write(path, code.encode("utf-8"))
    for name, stage in VARIANTS.items():
        directory = OUT / name
        path = directory / "test.hlsl"
        write(path, blob(PRE + "dlssnr.hlsl"))
        apply(name, directory)
        defines = ["DLSSNR_SUPERIOR_SCALING=1", f"DLSSNR_SUPERIOR_{stage.upper()}=1"]
        if stage == "hdr0":
            defines = ["DLSSNR_SUPERIOR_SCALING=1", "DLSSNR_SUPERIOR_RESOLVE=1",
                       "DLSSNR_SUPERIOR_NORMAL=1", "DLSSNR_SUPERIOR_HDR=0"]
        command = [str(ROOT / "OptiScaler/shaders/shader_tools/dxc.exe"), "-T", "cs_6_0",
                   "-E", "CSMain", "-O3", "-Qstrip_debug", "-Qstrip_reflect", "-I", str(OUT)]
        for define in defines:
            command += ["-D", define]
        subprocess.run(command + [str(path), "-Fo", str(directory / f"dlssnr_superior_{stage}_Shader.cso")],
                       check=True)
        if stage == "meter":
            write(directory / "dlssnr_superior_exposure_Shader.cso",
                  blob(PRE + "dlssnr_superior_exposure_Shader.cso"))
    print(f"Reconstructed probes against {REFERENCE} in {OUT}")


if __name__ == "__main__":
    main()
