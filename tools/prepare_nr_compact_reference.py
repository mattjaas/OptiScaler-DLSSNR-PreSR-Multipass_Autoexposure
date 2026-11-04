"""Extract the immutable d419c5e7 compact shaders for independent A/B and parity checks.

No fallback to current blobs. Requires that commit in the local Git object database.
"""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
COMMIT='d419c5e7032f0c8b37aa274fc831f3d1471849bb'
DEST=ROOT/'artifacts/ghost-study/compact-baseline'
STEMS=('private_carrier_compact_native16','private_ghost_compact_1','private_ghost_compact_2')


def main():
    DEST.mkdir(parents=True,exist_ok=True)
    hashes={}
    for stem in STEMS:
        relative=f'OptiScaler/shaders/dlssnr/superior/precompile/{stem}_Shader.cso'
        result=subprocess.run(['git','show',f'{COMMIT}:{relative}'],cwd=ROOT,capture_output=True)
        if result.returncode or not result.stdout.startswith(b'DXBC'):
            raise RuntimeError(f'Missing independent reference {COMMIT}:{relative}; fetch the branch history first. '
                               'Current blobs are never used as a substitute.')
        (DEST/f'{stem}_Shader.cso').write_bytes(result.stdout)
        hashes[stem]=hashlib.sha256(result.stdout).hexdigest()
    (DEST/'reference.json').write_text(json.dumps(dict(commit=COMMIT,sha256=hashes),indent=2)+'\n')
    print(f'Pinned compact reference {COMMIT}: {len(hashes)} blobs extracted')


if __name__=='__main__':main()
