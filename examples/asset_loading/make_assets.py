#!/usr/bin/env python3
"""Regenerates the asset_loading example data (deterministic, stdlib only).

Outputs: palette.png (master palette swatch, 6x1), textures/crate.png and textures/rune.png
(32x32, the default texture size, drawn only with palette colours; the rune's glow colour is
the "glow" sub-palette in assets.json) and
crate.gltf (textured cube, embedded buffer, nearest glTF sampler). Run from any directory:
    python3 examples/asset_loading/make_assets.py
"""
import base64, json, pathlib, struct, zlib

HERE = pathlib.Path(__file__).resolve().parent


def write_png(path, width, height, pixels):
    """pixels: list of rows of (r, g, b, a) tuples."""
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


# Small master palette (the game keeps the real one in game/).
DARK = (58, 38, 24, 255)
WOOD = (150, 98, 52, 255)
LIGHT = (186, 132, 74, 255)
NAIL = (200, 200, 190, 255)
STONE = (70, 74, 86, 255)
STONE_HI = (98, 104, 120, 255)
GLOW = (90, 220, 255, 255)


def crate():
    rows = []
    for y in range(32):
        row = []
        for x in range(32):
            border = x < 2 or y < 2 or x >= 30 or y >= 30
            diagonal = abs(x - y) < 2 or abs(x - (31 - y)) < 2
            if border:
                c = DARK if (x in (0, 31) or y in (0, 31)) else WOOD
                if x in (1, 30) and y in (1, 30):
                    c = NAIL
            elif diagonal:
                c = LIGHT
            else:
                c = WOOD if (y // 4) % 2 == 0 else LIGHT
                if y % 4 == 0:
                    c = DARK
            row.append(c)
        rows.append(row)
    return rows


def rune():
    rows = []
    for y in range(32):
        row = []
        for x in range(32):
            c = STONE_HI if (x // 4 + y // 4) % 2 == 0 else STONE
            if x in (0, 31) or y in (0, 31):
                c = DARK
            if (x in range(14, 18) and 6 <= y <= 25) or (y in range(14, 18) and 6 <= x <= 25):
                c = GLOW
            row.append(c)
        rows.append(row)
    return rows


def cube_gltf():
    # 24 vertices (4 per face): position, normal, uv; 36 uint16 indices.
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in faces:
        base = len(pos)
        for su, sv, tu, tv in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
            pos.append(tuple(0.5 * (n[i] + su * u[i] + sv * v[i]) for i in range(3)))
            nrm.append(n)
            uv.append((tu, tv))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    blob = b"".join(struct.pack("<3f", *p) for p in pos)
    blob += b"".join(struct.pack("<3f", *n) for n in nrm)
    blob += b"".join(struct.pack("<2f", *t) for t in uv)
    blob += struct.pack(f"<{len(idx)}H", *idx)
    n = len(pos)
    views = [(0, n * 12, 34962), (n * 12, n * 12, 34962), (n * 24, n * 8, 34962), (n * 32, len(idx) * 2, 34963)]
    gltf = {
        "asset": {"version": "2.0", "generator": "examples/asset_loading/make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "crate", "mesh": 0}],
        "meshes": [{"name": "crate", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "crate", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.85}}],
        "textures": [{"source": 0, "sampler": 0}],
        # Blockbench-style export: nearest magnification, nearest-texel / linear-mip minification.
        "samplers": [{"magFilter": 9728, "minFilter": 9986, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": "textures/crate.png"}],
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
        "bufferViews": [dict(buffer=0, byteOffset=o, byteLength=l, target=t) for o, l, t in views],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3",
             "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5]},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ],
    }
    return json.dumps(gltf, indent=2) + "\n"


write_png(HERE / "palette.png", 6, 1, [[DARK, WOOD, LIGHT, NAIL, STONE, STONE_HI]])
write_png(HERE / "textures" / "crate.png", 32, 32, crate())
write_png(HERE / "textures" / "rune.png", 32, 32, rune())
(HERE / "crate.gltf").write_text(cube_gltf())
