"""Parse the real capture serializer output without a shell/native-program pipeline."""
import json
from pathlib import Path
import subprocess
import sys

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "artifacts/validation/nr_edit_dump_smoke.exe").resolve()
for argument, paired in (("--metadata", False), ("--paired-metadata", True)):
    result = subprocess.run([str(binary), argument], check=True, capture_output=True, text=True)
    if not result.stdout.strip():
        raise RuntimeError(f"{binary.name} {argument} returned no JSON; stderr={result.stderr!r}")
    metadata = json.loads(result.stdout)
    assert metadata["paired_before_after_capture"] is paired
    assert metadata["low_motion_reset_units"] == "pixels_at_1080p_height_per_frame"
    if paired:
        assert metadata["paired_same_evaluation"] is True
        assert metadata["raw_before_edit_suffix"] == ".raw.dds"
        assert metadata["decoded_after_edit_suffix"] == ".after.dds"
    print(f"Capture JSON parse PASS: {argument}")
