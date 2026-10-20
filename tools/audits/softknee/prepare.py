"""Extract the v57 resident-GPU timing reference without changing the checkout."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
REFERENCE = "0d246bea8777c0674a26b2da65754a2cb4e9159a"
PRE = "OptiScaler/shaders/dlssnr/precompile/"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "artifacts/nr-softknee/baseline")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for name in ("carrier", "low", "normal"):
        filename = f"dlssnr_superior_{name}_Shader.cso"
        code = subprocess.check_output(["git", "show", f"{REFERENCE}:{PRE}{filename}"], cwd=ROOT)
        (args.out / filename).write_bytes(code)
    print(f"Prepared v57 reference {REFERENCE} in {args.out}")


if __name__ == "__main__":
    main()
