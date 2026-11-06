"""Extract pinned historical shaders for independent A/B, regression and parity checks.

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
RETIREMENT_COMMIT='9f2cc97c8ea422ec7e68742cd321a6bde83a2502'


def extract(commit,dest,stems):
    dest.mkdir(parents=True,exist_ok=True)
    hashes={}
    for stem in stems:
        relative=f'OptiScaler/shaders/dlssnr/superior/precompile/{stem}_Shader.cso'
        result=subprocess.run(['git','show',f'{commit}:{relative}'],cwd=ROOT,capture_output=True)
        if result.returncode or not result.stdout.startswith(b'DXBC'):
            raise RuntimeError(f'Missing independent reference {commit}:{relative}; fetch the branch history first. '
                               'Current blobs are never used as a substitute.')
        (dest/f'{stem}_Shader.cso').write_bytes(result.stdout)
        hashes[stem]=hashlib.sha256(result.stdout).hexdigest()
    (dest/'reference.json').write_text(json.dumps(dict(commit=commit,sha256=hashes),indent=2)+'\n')
    print(f'Pinned compact reference {commit}: {len(hashes)} blobs extracted')


def main():
    extract('0301b61b1476fc0ded6f99c03c4a619b9ec2eda2',ROOT/'artifacts/ghost-study/pitch-baseline',
            ('private_ghost_history','private_ghost_compact_2','private_ghost_coarse_2'))
    extract('a1fd2b1bffa72ea7ec06e5c0cd6e6f5dbcf82b2e',ROOT/'artifacts/transport-modes/baseline',('private_carrier',))
    extract('ca52c0e35119dee1a13ec7b02438a020a7844bd8',ROOT/'artifacts/ghost-study/feather-full-baseline',
            ('private_ghost_feather_horizontal','private_ghost_feather_apply'))
    extract('c6a88c1d8f272b9a40e11b3e351488cf54883a47',ROOT/'artifacts/ghost-study/sky-baseline',
            ('private_ghost_coarse_1','private_ghost_coarse_2','private_carrier_coarse',
             'private_ghost_coarse_dilate_1'))
    extract('95dbc4810f9a716bc642ec48b1698bb974834089',ROOT/'artifacts/ghost-study/road-baseline',
            ('private_ghost_history','private_ghost_compact_2','private_ghost_coarse_2'))
    extract('b9924012a82022e4424931019d6ef6bccba54ace',ROOT/'artifacts/ghost-study/occluded-interior-baseline',
            ('private_ghost_history','private_ghost_compact_2','private_ghost_coarse_2'))
    extract('21b0ce92d3c54956b87b11e535f3244fe7213b21',ROOT/'artifacts/ghost-study/static-baseline',
            ('private_ghost_history','private_ghost_compact_2','private_ghost_coarse_2'))
    extract('a13e22743e6d7e6324aacc581c64edcc403b4258',ROOT/'artifacts/ghost-study/compact-math-baseline',
            ('private_carrier_compact_fast','private_carrier_compact_fast_answer',
             'private_ghost_compact_wave','private_ghost_compact_2'))
    extract(COMMIT,DEST,STEMS)
    extract('ac9540b3e09f88369fa7c5a7ab5c97e3b6984491',ROOT/'artifacts/ghost-study/fast-blend-baseline',
            ('private_carrier_compact_simple','private_carrier_compact_simple_answer',
             'private_ghost_compact_1','private_ghost_compact_2'))
    extract(RETIREMENT_COMMIT,ROOT/'artifacts/ghost-study/carrier-retirement-baseline',
            ('private_carrier_compact_simple','private_carrier_compact_simple_answer',
             'private_ghost_compact_1','private_ghost_compact_2'))


if __name__=='__main__':main()
