"""End-to-end bounded region streaming regression; no game assets required."""
import json
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="zodar-region-bench-") as directory:
    output = pathlib.Path(directory) / "report.json"
    subprocess.run([sys.argv[1], "--scene", "region", "--headless", "--warmup", "0",
                    "--frames", "544", "--width", "160", "--height", "90",
                    "--output", str(output)], check=True)
    report = json.loads(output.read_text())
    region = report["region"]
    assert region["extentMeters"] == [4000, 4000]
    assert region["reloadVerified"] and region["reloads"] > 0
    assert region["edits"] == 17 and region["saves"] >= 16
    assert region["meshed"] > 100 and region["generated"] > 100
    assert 0 < region["peakCpuBytes"] < 512 * 1024**2
    assert 0 < region["peakGpuAndStagingBytes"] < 128 * 1024**2
    assert region["peakChunks"] <= 100 and region["peakRegions"] <= 40
    assert region["finalSettled"] and region["finalBacklog"] == 0
    assert region["generationMs"] > 0 and region["meshingMs"] > 0
