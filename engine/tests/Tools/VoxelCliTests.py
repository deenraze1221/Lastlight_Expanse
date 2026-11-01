"""Exercise the real voxel CLI in an isolated temporary directory."""
import os
import pathlib
import subprocess
import sys
import tempfile

exe = sys.argv[1]
with tempfile.TemporaryDirectory(prefix="zodar-voxel-cli-") as directory:
    root = pathlib.Path(directory)
    region = root / "input.zregion"
    # One authored stone and an explicit runtime air override at the same cell.
    region.write_text('zodar.region 1\n0 0 0.5 0 0 "test"\n2\n""\n"stone"\n'
                      '1\n0 0 0 1 1 1\n0 1 0\n1\n0 0 0 1 0 1\n0 1 0\n')
    blocks = root / "test.blocks.json"
    blocks.write_text('{"schema":"zodar.blocks","version":1,"materials":'
                      '[{"name":"m","textures":"unused.png"}],"blocks":'
                      '[{"name":"stone","material":"m"}]}')

    def run(*args, success=True):
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout

    assert "runtime chunks 1" in run("info", region)
    structure = root / "captured.zstruct"
    assert "inherited cells omitted" in run("capture", region, structure, 0, 0, 0, 2, 1, 1)
    assert "cells 1" in run("info", structure)
    output = root / "stamped.zregion"
    run("stamp", region, structure, output, 32, 0, 0, 1, blocks)
    assert "authored chunks 2" in run("info", output)
    # Neither CLI writer may open a pre-existing predictable temporary symlink.
    if os.name != "nt":
        output.with_name(output.name + ".tmp").symlink_to(output)
        run("stamp", region, structure, output, 32, 0, 0, 1, blocks)
        assert not output.is_symlink()
        assert "authored chunks 2" in run("info", output)
        structure.with_name(structure.name + ".tmp").symlink_to(structure)
        run("capture", region, structure, 0, 0, 0, 2, 1, 1)
        assert not structure.is_symlink()
        assert "cells 1" in run("info", structure)
    before = output.read_bytes()
    run("stamp", region, structure, output, "bad", 0, 0, 1, blocks, success=False)
    assert output.read_bytes() == before
    region.write_text(region.read_text() + "trailing")
    run("info", region, success=False)
print("voxel CLI: info/capture/stamp, explicit air, invalid input and previous-file preservation passed")
