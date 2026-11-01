#!/usr/bin/env python3
"""Source-file policy check (Phase D2; all files in regular Git, no Git LFS).

Fails (exit 1) if, anywhere below the given directory:
  - a Blender source (*.blend) is larger than 20 MiB (20 * 1024 * 1024 bytes), or
  - a Blender backup file (*.blend1, *.blend2) exists.

No other file type has a size limit here: .glb, PNG and other assets are limited by the asset
checks where applicable (texture size standards, triangle budgets, bounds; zodar.py assets check).

Usage:
  python3 check_source_sizes.py DIR      check DIR (e.g. game/assets)
  python3 check_source_sizes.py --self-test
"""

import argparse
import sys
import tempfile
from pathlib import Path

BLEND_LIMIT = 20 * 1024 * 1024
BACKUP_SUFFIXES = (".blend1", ".blend2")


def problems(root):
    found = []
    for path in sorted(p for p in Path(root).rglob("*") if p.is_file()):
        rel = path.relative_to(root).as_posix()
        suffix = path.suffix.lower()
        if suffix in BACKUP_SUFFIXES:
            found.append("%s: Blender backup file; delete it (ignored by .gitignore, never committed)" % rel)
        elif suffix == ".blend":
            size = path.stat().st_size
            if size > BLEND_LIMIT:
                found.append("%s: %.1f MiB exceeds the 20 MiB limit for .blend files (save with File > Save > "
                             "Compress, unpack textures to sources/blender/textures/)" % (rel, size / 1048576.0))
    return found


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "sources" / "blender").mkdir(parents=True)
        (root / "models").mkdir()
        (root / "sources" / "blender" / "ok.blend").write_bytes(b"BLENDER" + b"\0" * 1024)
        with open(root / "sources" / "blender" / "limit.blend", "wb") as f:
            f.truncate(BLEND_LIMIT)  # exactly at the limit: allowed
        with open(root / "models" / "big.glb", "wb") as f:
            f.truncate(BLEND_LIMIT * 2)  # other assets: no limit here
        with open(root / "models" / "big.png", "wb") as f:
            f.truncate(BLEND_LIMIT + 1)
        assert problems(root) == [], problems(root)
        with open(root / "sources" / "blender" / "huge.blend", "wb") as f:
            f.truncate(BLEND_LIMIT + 1)
        (root / "sources" / "blender" / "ok.blend1").write_bytes(b"x")
        (root / "sources" / "blender" / "ok.blend2").write_bytes(b"x")
        found = problems(root)
        assert len(found) == 3, found
        assert found[0].startswith("sources/blender/huge.blend: "), found
        assert found[1].startswith("sources/blender/ok.blend1: "), found
        assert found[2].startswith("sources/blender/ok.blend2: "), found
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("directory", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.directory or not Path(args.directory).is_dir():
        parser.error("a directory is required")
    found = problems(Path(args.directory))
    for p in found:
        print("error: " + p)
    blends = sum(1 for p in Path(args.directory).rglob("*.blend") if p.is_file())
    print("checked %d .blend files: %d problems" % (blends, len(found)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
