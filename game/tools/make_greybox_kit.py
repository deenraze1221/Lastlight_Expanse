#!/usr/bin/env python3
"""Generates the Lastlight Expanse bunker greybox (Phase D2). Python standard library only,
deterministic bytes.

Writes into game/assets/:
  palette.png, palettes/glow.png        master palette (GDD 3: muted, faded post-war colours)
                                         and the "glow" sub-palette (lamps, screens, lenses)
  models/<module>.glb                    20 Blockbench-style modules + 1 Blender-style arch
  textures/signs/<role>.png              merchant area signs (32x32)
  materials/sign_<role>.material.json    sign materials (pixel-art sampling)
  prefabs/ceiling_lamp.prefab.json       lamp model + point light
  prefabs/merchant_stall.prefab.json     counter + shelf + sign
  levels/bunker.scene.json               the static bunker level (GDD 10)

The modules are placeholders: real Blockbench/Blender exports replace them later under the
same file names (models/<module>.glb), with their sources in sources/blockbench/ or
sources/blender/. This script is the source of every generated file. "Blockbench-style" and
"Blender-style" describe the glTF structure (generator string, per-face UVs, NEAREST
samplers, node layout; smooth normals and tangents for Blender); they are not real exports.

The registry (assets.json: palette, defaults, budgets, entries) is not written here; after
regenerating run the import commands in game/assets/README.md.

Usage:
  python3 game/tools/make_greybox_kit.py           write the files
  python3 game/tools/make_greybox_kit.py --check   exit 1 if a committed file differs
"""

import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

ASSETS = Path(__file__).resolve().parent.parent / "assets"

# --- Palette (GDD 3: muted, faded colours; Eastern European post-war atmosphere) -----------

MASTER = {
    "black": 0x1e1d1b,
    "c0": 0x2e2e2b, "c1": 0x45443f, "c2": 0x5d5b54, "c3": 0x77746a, "c4": 0x918d80,  # concrete
    "g0": 0x3f4a3c, "g1": 0x56634f, "g2": 0x6e7b64,                                  # faded green paint
    "r0": 0x4a3528, "r1": 0x6b4a33, "r2": 0x8a6748,                                  # rust, brown
    "b0": 0x4b5560, "b1": 0x6a7680,                                                  # blue-grey steel
    "wood": 0x7a6a52, "pale": 0xa39a82, "paint": 0xb8b2a0,                           # worn wood, fabric, white paint
    "ochre": 0x8a7442, "red": 0x7a3a32,                                              # hazard ochre, faded red
}
GLOW = {
    "lamp0": 0xc9a95c, "lamp1": 0xe0c98a,  # warm lamp light
    "em0": 0x8c2f26, "em1": 0xb2463a,      # emergency lens
    "scr0": 0x5f8f6e, "scr1": 0x8fbf94,    # terminal phosphor
}
COLORS = {**MASTER, **GLOW}


def png(width, height, texel):
    """texel(x, y) -> colour name. RGBA8 opaque; stored deflate blocks (zlib-version independent)."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            c = COLORS[texel(x, y)]
            rows += bytes(((c >> 16) & 255, (c >> 8) & 255, c & 255, 255))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 0)) +
            chunk(b"IEND", b""))


def swatch(names):
    return png(len(names), 1, lambda x, y: names[x])


def noise(x, y, seed):
    h = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFF


# --- Textures (32x32, one texel per art pixel, 32 texels per metre) ----------------------

def t_concrete(x, y):
    n = noise(x, y, 1)
    if n < 10:
        return "c1"
    if noise(x // 3, y // 5, 2) < 40 and y > 8:
        return "c2"  # water stains
    return "c2" if n < 60 else ("c3" if n < 245 else "c4")


def t_paint(x, y):
    n = noise(x, y, 3)
    if y >= 30:
        return "g0"  # grime at the bottom edge
    if n < 18:
        return "c3"  # chipped paint shows concrete
    return "g2" if n > 230 else "g1"


def t_floor(x, y):
    if x == 0 or y == 0:
        return "c0"  # 1 m tile seams
    n = noise(x, y, 4)
    return "c1" if n < 150 else "c2"


def t_ceiling(x, y):
    n = noise(x, y, 5)
    if noise(x // 4, y // 4, 6) < 30:
        return "c1"
    return "c2" if n < 170 else "c3"


def t_metal(x, y):
    if x in (0, 31) or y in (0, 31):
        return "b0"
    if (x, y) in ((3, 3), (28, 3), (3, 28), (28, 28)):
        return "c4"  # rivets
    return "b0" if noise(x, y, 7) < 40 else "b1"


def t_rust(x, y):
    n = noise(x, y, 8)
    if noise(x // 4, y // 4, 9) < 70:
        return "b0"
    return "r0" if n < 90 else ("r1" if n < 200 else "r2")


def t_wood(x, y):
    if y % 8 == 0:
        return "r0"  # plank gaps
    return "r1" if noise(x // 4, y, 10) < 60 else "wood"


def t_fabric(x, y):
    return "b0" if (x // 2 + y // 2) % 2 else "b1"


def t_olive(x, y):
    if y in (4, 6, 8):
        return "black" if 10 <= x <= 21 else "g0"  # vent slits
    return "g0" if noise(x, y, 11) < 50 else "g1"


def t_hazard(x, y):
    return "ochre" if ((x + y) // 4) % 2 else "black"


def t_lamp(x, y):
    if x in (0, 31) or y in (0, 31):
        return "c1"
    d = max(abs(x - 15.5), abs(y - 15.5))
    return "lamp1" if d < 10 else "lamp0"


def t_lens(x, y):
    return "em1" if 6 <= x <= 25 and 6 <= y <= 25 else "em0"


def t_screen(x, y):
    if x < 2 or x > 29 or y < 2 or y > 29:
        return "black"
    if y % 4 == 1 and 3 <= x <= 3 + (noise(0, y, 12) % 24):
        return "scr0"  # text lines
    if y == 25 and 3 <= x <= 5:
        return "scr1"  # cursor
    return "black"


TEXTURES = {
    "concrete": t_concrete, "paint": t_paint, "floor": t_floor, "ceiling": t_ceiling, "metal": t_metal,
    "rust": t_rust, "wood": t_wood, "fabric": t_fabric, "olive": t_olive, "hazard": t_hazard,
    "lamp": t_lamp, "lens": t_lens, "screen": t_screen,
}
# PBR values per texture (metallic, roughness, emissive strength or None).
MATERIAL = {
    "concrete": (0.0, 0.95, None), "paint": (0.0, 0.85, None), "floor": (0.0, 0.9, None),
    "ceiling": (0.0, 0.95, None), "metal": (0.6, 0.55, None), "rust": (0.3, 0.8, None),
    "wood": (0.0, 0.8, None), "fabric": (0.0, 1.0, None), "olive": (0.4, 0.6, None),
    "hazard": (0.0, 0.8, None), "lamp": (0.0, 0.5, 4.0), "lens": (0.0, 0.4, None),
    "screen": (0.0, 0.3, 1.5),
}


# --- Merchant signs (GDD 9: five merchant roles) --------------------------------------------

def sign(icon):
    def texel(x, y):
        if x in (0, 31) or y in (0, 31):
            return "c1"
        return "black" if icon(x, y) else "paint"
    return texel


def icon_weapons(x, y):  # rifle silhouette
    return (12 <= y <= 15 and 4 <= x <= 27) or (16 <= y <= 22 and 8 <= x <= 11) or \
           (16 <= y <= 19 and 18 <= x <= 20) or (10 <= y <= 11 and 22 <= x <= 25)


def icon_medic(x, y):  # cross
    return (13 <= x <= 18 and 6 <= y <= 25) or (6 <= x <= 25 and 13 <= y <= 18)


def icon_resources(x, y):  # canister
    return (9 <= x <= 22 and 10 <= y <= 26) and not (12 <= x <= 19 and 16 <= y <= 18) or \
           (13 <= x <= 18 and 6 <= y <= 9)


def icon_technician(x, y):  # gear
    d = math.hypot(x - 15.5, y - 15.5)
    teeth = (abs(x - 15.5) < 2.5 or abs(y - 15.5) < 2.5) and d < 12
    return (7 <= d <= 10) or teeth and d > 9


def icon_scout(x, y):  # antenna with signal arcs
    d = math.hypot(x - 15.5, y - 8)
    mast = 14 <= x <= 17 and 8 <= y <= 26
    base = 10 <= x <= 21 and 24 <= y <= 26
    arcs = y < 14 and (5 <= d <= 6.5 or 9.5 <= d <= 11)
    return mast or base or arcs


SIGNS = {"weapons": icon_weapons, "medic": icon_medic, "resources": icon_resources,
         "technician": icon_technician, "scout": icon_scout}

# --- glTF writer -----------------------------------------------------------------------------

FLOAT, USHORT, NEAREST, NEAREST_MIPMAP_LINEAR, REPEAT = 5126, 5123, 9728, 9986, 10497

FACES = {
    "+x": ((1, 0, 0), (0, 0, -1), (0, 1, 0)), "-x": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    "+y": ((0, 1, 0), (1, 0, 0), (0, 0, -1)), "-y": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    "+z": ((0, 0, 1), (1, 0, 0), (0, 1, 0)), "-z": ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
}


def r6(v):
    return [round(c, 6) + 0.0 for c in v]


class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, values, kind, component=FLOAT, minmax=False, target=34962):
        flat = [c for v in values for c in (v if isinstance(v, (list, tuple)) else (v,))]
        data = struct.pack("<%d%s" % (len(flat), "f" if component == FLOAT else "H"), *flat)
        a = {"bufferView": self.view(data, target), "componentType": component, "count": len(values), "type": kind}
        if minmax:
            width = len(values[0])
            a["min"] = [min(v[i] for v in values) for i in range(width)]
            a["max"] = [max(v[i] for v in values) for i in range(width)]
        self.accessors.append(a)
        return len(self.accessors) - 1


def box_faces(lo, hi, faces):
    """Quads of an axis-aligned box with per-face UVs in metres (32 texels per metre)."""
    out = []
    for face in faces:
        n, u, v = FACES[face]
        c = [(lo[i] + hi[i]) / 2 + n[i] * (hi[i] - lo[i]) / 2 for i in range(3)]
        hu = [u[i] * (hi[i] - lo[i]) / 2 for i in range(3)]
        hv = [v[i] * (hi[i] - lo[i]) / 2 for i in range(3)]
        du = 2 * math.sqrt(sum(h * h for h in hu))
        dv = 2 * math.sqrt(sum(h * h for h in hv))
        corners = [r6([c[i] + su * hu[i] + sv * hv[i] for i in range(3)])
                   for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        uvs = [r6(t) for t in ((0, dv), (du, dv), (du, 0), (0, 0))]
        out.append((corners, [list(map(float, n))] * 4, uvs))
    return out


def primitive(b, quads, material, tangents=False):
    pos, nor, uv, idx = [], [], [], []
    for corners, normals, uvs in quads:
        base = len(pos)
        pos += corners
        nor += normals
        uv += uvs
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    attrs = {"POSITION": b.accessor(pos, "VEC3", minmax=True), "NORMAL": b.accessor(nor, "VEC3"),
             "TEXCOORD_0": b.accessor(uv, "VEC2")}
    if tangents:
        tan = []
        for n in nor:
            t = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 0.0, 1.0)
            d = sum(t[i] * n[i] for i in range(3))
            t = [t[i] - d * n[i] for i in range(3)]
            length = math.sqrt(sum(c * c for c in t))
            tan.append(r6([c / length for c in t] + [1.0]))
        attrs["TANGENT"] = b.accessor(tan, "VEC4")
    return {"attributes": attrs, "indices": b.accessor(idx, "SCALAR", USHORT, target=34963), "material": material}


def glb(gltf, binary):
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = bytes(binary) + b"\0" * (-len(binary) % 4)
    return (struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(text) + 8 + len(binary)) +
            struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def model_gltf(name, generator, prims_by_texture, sampler, tangents=False):
    """prims_by_texture: ordered {texture: quads}. One mesh, one primitive per texture."""
    b = Builder()
    textures = list(prims_by_texture)
    prims = [primitive(b, prims_by_texture[t], i, tangents) for i, t in enumerate(textures)]
    images, materials = [], []
    for i, t in enumerate(textures):
        images.append({"name": t, "mimeType": "image/png", "bufferView": b.view(png(32, 32, TEXTURES[t]))})
        metallic, roughness, emissive = MATERIAL[t]
        m = {"name": t, "pbrMetallicRoughness": {"baseColorTexture": {"index": i}, "metallicFactor": metallic,
                                                  "roughnessFactor": roughness}}
        if emissive:
            m["emissiveFactor"] = [1.0, 1.0, 1.0]
            m["emissiveTexture"] = {"index": i}
            if emissive != 1.0:
                m["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": emissive}}
        materials.append(m)
    gltf = {"asset": {"version": "2.0", "generator": generator}}
    if any("extensions" in m for m in materials):
        gltf["extensionsUsed"] = ["KHR_materials_emissive_strength"]
    gltf.update({
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "children": [1]}, {"name": name + " mesh", "mesh": 0}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
        "textures": [{"sampler": 0, "source": i} for i in range(len(textures))],
        "samplers": [sampler],
        "images": images,
        "accessors": b.accessors, "bufferViews": b.views, "buffers": [{"byteLength": len(b.bin)}],
    })
    return glb(gltf, b.bin)


BLOCKBENCH = "Blockbench 4.11.2 glTF exporter (Lastlight greybox kit, generated)"
BLENDER = "Khronos glTF Blender I/O v4.2.83 (Lastlight greybox kit, generated)"
BB_SAMPLER = {"magFilter": NEAREST, "minFilter": NEAREST, "wrapS": REPEAT, "wrapT": REPEAT}
BL_SAMPLER = {"magFilter": NEAREST, "minFilter": NEAREST_MIPMAP_LINEAR}

ALL = ("+x", "-x", "+y", "-y", "+z", "-z")


def blockbench(name, boxes):
    """boxes: [(lo, hi, texture, faces)]; textures grouped into one primitive each."""
    by_texture = {}
    for lo, hi, texture, faces in boxes:
        by_texture.setdefault(texture, []).extend(box_faces(lo, hi, faces))
    return model_gltf(name, BLOCKBENCH, by_texture, BB_SAMPLER)


def b(lo, hi, texture, faces=ALL):
    return (lo, hi, texture, faces)


def wall_column(x0, x1, z0=-0.125, z1=0.125, top=3.0):
    """Painted lower 1.2 m + concrete above (the bunker's wall finish)."""
    return [b((x0, 0, z0), (x1, 1.2, z1), "paint", ("+x", "-x", "+z", "-z", "-y")),
            b((x0, 1.2, z0), (x1, top, z1), "concrete", ("+x", "-x", "+z", "-z", "+y"))]


def legs(x, z, height, size=0.05):
    return [b((sx * x - size / 2, 0, sz * z - size / 2), (sx * x + size / 2, height, sz * z + size / 2), "metal")
            for sx in (-1, 1) for sz in (-1, 1)]


MODULES = {
    # Architecture (2 m grid, 3 m ceiling height; walls 0.25 m thick, centred on grid lines)
    "floor": [b((-1, -0.1, -1), (1, 0, 1), "floor", ("+y",))],
    "ceiling": [b((-1, 3.0, -1), (1, 3.1, 1), "ceiling", ("-y",))],
    "wall": wall_column(-1, 1),
    "doorway": wall_column(-1, -0.6) + wall_column(0.6, 1) +
               [b((-0.6, 2.2, -0.125), (0.6, 3.0, 0.125), "concrete", ("+z", "-z", "-y"))],
    "corner": wall_column(-0.125, 0.125),
    "pillar": [b((-0.2, 0, -0.2), (0.2, 0.3, 0.2), "hazard", ("+x", "-x", "+z", "-z")),
               b((-0.2, 0.3, -0.2), (0.2, 3.0, 0.2), "concrete", ("+x", "-x", "+z", "-z"))],
    "stairs": [b((-1, 0, -1 + 0.5 * k), (1, 0.25 * (k + 1), -0.5 + 0.5 * k), "floor") for k in range(4)],
    # Props (origin on the floor, centred; wall-mounted ones on the wall surface, facing +z)
    "door_leaf": [b((0, 0, -0.03), (1.1, 2.15, 0.03), "metal"), b((0.95, 1.0, 0.03), (1.02, 1.08, 0.07), "rust")],
    "lamp": [b((-0.2, -0.12, -0.2), (0.2, 0, 0.2), "metal", ("+x", "-x", "+z", "-z")),
             b((-0.15, -0.14, -0.15), (0.15, -0.12, 0.15), "lamp", ("-y", "+x", "-x", "+z", "-z"))],
    "crate": [b((-0.4, 0, -0.4), (0.4, 0.8, 0.4), "wood"),
              b((-0.42, 0.1, -0.42), (0.42, 0.18, 0.42), "metal", ("+x", "-x", "+z", "-z")),
              b((-0.42, 0.62, -0.42), (0.42, 0.7, 0.42), "metal", ("+x", "-x", "+z", "-z"))],
    "bunk": [b((sx * 0.95 - 0.025, 0, sz * 0.4 - 0.025), (sx * 0.95 + 0.025, 1.8, sz * 0.4 + 0.025), "metal")
             for sx in (-1, 1) for sz in (-1, 1)] +
            [b((-1, 0.3, -0.45), (1, 0.4, 0.45), "metal"), b((-0.95, 0.4, -0.42), (0.95, 0.55, 0.42), "fabric"),
             b((-1, 1.3, -0.45), (1, 1.4, 0.45), "metal"), b((-0.95, 1.4, -0.42), (0.95, 1.55, 0.42), "fabric")],
    "locker": [b((-0.25, 0, -0.25), (0.25, 1.9, 0.25), "olive"), b((0.15, 0.9, 0.25), (0.2, 1.1, 0.29), "metal")],
    "terminal": [b((-0.4, 0, -0.3), (0.4, 0.8, 0.3), "metal"), b((-0.3, 0.8, -0.15), (0.3, 1.25, 0.15), "metal"),
                 b((-0.25, 0.85, 0.15), (0.25, 1.2, 0.16), "screen", ("+z",))],
    "generator": [b((-0.95, 0, -0.65), (0.95, 0.1, 0.65), "hazard"), b((-0.9, 0.1, -0.6), (0.9, 1.2, 0.6), "olive"),
                  b((-0.7, 1.2, -0.4), (0.2, 1.6, 0.4), "rust"), b((0.4, 1.2, -0.1), (0.6, 2.4, 0.1), "metal")],
    "electrical_panel": [b((-0.3, 1.0, 0), (0.3, 1.8, 0.15), "metal"),
                         b((-0.1, 1.55, 0.15), (0.1, 1.7, 0.16), "hazard", ("+z",)),
                         b((-0.03, 1.8, 0.02), (0.03, 3.0, 0.08), "metal", ("+x", "-x", "+z"))],
    "emergency_light": [b((-0.12, 2.3, 0), (0.12, 2.42, 0.1), "metal"),
                        b((-0.08, 2.32, 0.1), (0.08, 2.4, 0.14), "lens")],
    "merchant_counter": [b((-1, 0, -0.3), (1, 1.0, 0.3), "wood"), b((-1.05, 1.0, -0.35), (1.05, 1.05, 0.35), "metal")],
    "shelf": [b((sx * 0.6 - 0.02, 0, sz * 0.2 - 0.02), (sx * 0.6 + 0.02, 2.0, sz * 0.2 + 0.02), "metal")
              for sx in (-1, 1) for sz in (-1, 1)] +
             [b((-0.6, y, -0.2), (0.6, y + 0.04, 0.2), "wood") for y in (0.1, 0.6, 1.1, 1.6)],
    "table": [b((-0.8, 0.72, -0.45), (0.8, 0.78, 0.45), "wood")] + legs(0.75, 0.4, 0.72),
    "bench": [b((-0.8, 0.42, -0.18), (0.8, 0.47, 0.18), "wood"),
              b((-0.75, 0, -0.15), (-0.7, 0.42, 0.15), "metal"), b((0.7, 0, -0.15), (0.75, 0.42, 0.15), "metal")],
}
ARCHITECTURE = ("floor", "ceiling", "wall", "doorway", "corner", "pillar", "stairs", "airlock_arch")
GLOWING = ("lamp", "emergency_light", "terminal")  # use the "glow" sub-palette


def airlock_arch():
    """Blender-style curved airlock frame: 2 m wide, 3 m tall, 0.5 m deep; opening 1.4 m wide
    with a semicircular top (radius 0.7 m, crown at 2.7 m); smooth normals on the curve."""
    segments = 12
    inner = [(-0.7, 0.0), (-0.7, 2.0)]
    inner += [(-0.7 * math.cos(math.pi * k / segments), 2.0 + 0.7 * math.sin(math.pi * k / segments))
              for k in range(1, segments)]
    inner += [(0.7, 2.0), (0.7, 0.0)]

    def outer(p):  # project onto the frame's outer rectangle [-1, 1] x [0, 3]
        x, y = p
        if y <= 2.0:
            return (-1.0 if x < 0 else 1.0, y)
        dx, dy = x, y - 2.0
        t = min((1.0 / abs(dx)) if abs(dx) > 1e-9 else 1e9, 1.0 / dy)
        return (max(-1.0, min(1.0, dx * t)), 2.0 + dy * t)

    def inward(i):  # smooth normal of the opening surface (pointing into the opening)
        x, y = inner[i]
        if y <= 2.0 + 1e-9 and i in (0, 1, len(inner) - 2, len(inner) - 1):
            return (1.0 if x < 0 else -1.0, 0.0, 0.0)
        dx, dy = x, y - 2.0
        length = math.hypot(dx, dy)
        return (-dx / length, -dy / length, 0.0)

    frame = []
    for z, sign in ((0.25, 1), (-0.25, -1)):
        for i in range(len(inner) - 1):
            a, c = inner[i], inner[i + 1]
            oa, oc = outer(a), outer(c)
            quad = [(a[0], a[1], z), (oa[0], oa[1], z), (oc[0], oc[1], z), (c[0], c[1], z)]
            if sign < 0:
                quad = [quad[0], quad[3], quad[2], quad[1]]
            frame.append(([r6(p) for p in quad], [[0.0, 0.0, float(sign)]] * 4,
                          [r6((p[0] + 1.0, 3.0 - p[1])) for p in quad]))
    # Opening surface: one strip with shared (smooth) normals along the curve.
    length = 0.0
    for i in range(len(inner) - 1):
        a, c = inner[i], inner[i + 1]
        step = math.hypot(c[0] - a[0], c[1] - a[1])
        quad = [(a[0], a[1], 0.25), (a[0], a[1], -0.25), (c[0], c[1], -0.25), (c[0], c[1], 0.25)]
        normals = [r6(inward(i)), r6(inward(i)), r6(inward(i + 1)), r6(inward(i + 1))]
        uvs = [r6(t) for t in ((length, 0.0), (length, 0.5), (length + step, 0.5), (length + step, 0.0))]
        frame.append(([r6(p) for p in quad], normals, uvs))
        length += step
    sides = box_faces((-1, 0, -0.25), (1, 3, 0.25), ("+x", "-x", "+y"))
    return model_gltf("airlock_arch", BLENDER, {"metal": frame, "concrete": sides}, BL_SAMPLER, tangents=True)


# --- Level (GDD 10: one permanent shared bunker; static greybox, no NPC/merchant/locker logic) ---

# Grid: cell (i, j) covers x in [2i, 2i+2], z in [2j-1, 2j+1]; floor at y = 0, ceiling at 3 m.
ROOMS = [
    # id, name, i range, j range
    ("corridor", "Main corridor", (0, 14), (0, 0)),
    ("dormitory", "Dormitory", (0, 2), (1, 3)),
    ("locker_room", "Locker room", (3, 5), (1, 3)),
    ("common_area", "Common area", (6, 11), (1, 3)),
    ("generator_room", "Generator room", (12, 14), (1, 3)),
    ("weapon_dealer", "Weapon dealer area", (0, 2), (-3, -1)),
    ("medic", "Medic area", (3, 5), (-3, -1)),
    ("resource_trader", "Resource trader area", (6, 8), (-3, -1)),
    ("technician", "Technician area", (9, 11), (-3, -1)),
    ("scout", "Scout area", (12, 14), (-3, -1)),
    ("airlock", "Airlock", (-2, -1), (-1, 1)),
    ("exit_passage", "Exit passage", (-4, -3), (0, 0)),
]
# Openings between two cells: ((i, j), (i, j), module).
OPENINGS = [((1, 1), (1, 0), "doorway"), ((4, 1), (4, 0), "doorway"), ((7, 1), (7, 0), "doorway"),
            ((10, 1), (10, 0), "doorway"), ((13, 1), (13, 0), "doorway"),
            ((1, -1), (1, 0), "doorway"), ((4, -1), (4, 0), "doorway"), ((7, -1), (7, 0), "doorway"),
            ((10, -1), (10, 0), "doorway"), ((13, -1), (13, 0), "doorway"),
            ((-1, 0), (0, 0), "doorway"), ((-3, 0), (-2, 0), "airlock_arch")]
MERCHANTS = [("weapon_dealer", "weapons"), ("medic", "medic"), ("resource_trader", "resources"),
             ("technician", "technician"), ("scout", "scout")]


def rnd(v):
    return [round(c, 4) + 0.0 for c in v]


def level():
    cells = {}
    for rid, _, (i0, i1), (j0, j1) in ROOMS:
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                cells[(i, j)] = rid
    openings = {frozenset((a, c)): m for a, c, m in OPENINGS}
    entities = []
    counters = {}

    def add(room, model, pos, yaw=0.0, name=None, extra=None):
        n = counters.get((room, model), 0) + 1
        counters[(room, model)] = n
        e = {"id": "%s/%s_%d" % (room, model, n), "name": name or "%s %d" % (model.replace("_", " "), n),
             "parent": room, "transform": {"position": rnd(pos)}}
        if yaw:
            e["transform"]["rotationDegrees"] = [0.0, float(yaw), 0.0]
        if model:
            e["modelRenderer"] = {"model": model}
        if extra:
            e.update(extra)
        entities.append(e)
        return e

    entities.append({"id": "bunker", "name": "Bunker"})
    for rid, name, _, _ in ROOMS:
        entities.append({"id": rid, "name": name, "parent": "bunker"})

    # Floors, ceilings (stairs replace the floor of the exit passage's far cell).
    for (i, j), rid in sorted(cells.items()):
        centre = (2 * i + 1, 0, 2 * j)
        add(rid, "stairs" if (i, j) == (-4, 0) else "floor", centre, -90 if (i, j) == (-4, 0) else 0)
        add(rid, "ceiling", centre)

    # Walls on every cell edge between different rooms (or the outside); openings by table.
    edges = set()
    for (i, j) in sorted(cells):
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            other = (i + di, j + dj)
            if cells.get(other) == cells[(i, j)]:
                continue
            key = frozenset(((i, j), other))
            if key in edges:
                continue
            edges.add(key)
            module = openings.get(key, "wall")
            owner = cells[(i, j)] if (i, j) < other or other not in cells else cells[other]
            if di:  # edge along z at x = 2 * max(i, i+di)
                x = 2 * max(i, i + di)
                add(owner, module, (x, 0, 2 * j), 90)
            else:   # edge along x at z = 2 * max(j, j+dj) - 1
                z = 2 * max(j, j + dj) - 1
                add(owner, module, (2 * i + 1, 0, z), 0)
    # Corner pieces where walls meet at an angle or end.
    vertices = {}
    for key in edges:
        (a, c) = sorted(key)
        if a[0] != c[0]:  # vertical edge at x, spanning z cell j
            x, j = 2 * max(a[0], c[0]), a[1]
            for z in (2 * j - 1, 2 * j + 1):
                vertices.setdefault((x, z), set()).add("z")
        else:
            z, i = 2 * max(a[1], c[1]) - 1, a[0]
            for x in (2 * i, 2 * i + 2):
                vertices.setdefault((x, z), set()).add("x")
    # Every wall vertex gets a corner column: it fills the junction and hides module seams.
    for (x, z) in sorted(vertices):
        room = next(cells[c] for c in ((x // 2, (z + 1) // 2), (x // 2 - 1, (z + 1) // 2),
                                       (x // 2, (z - 1) // 2), (x // 2 - 1, (z - 1) // 2)) if c in cells)
        add(room, "corner", (x, 0, z))

    # Lights: a ceiling lamp prefab per room (two in the common area), corridor every 6 m.
    def lamp(room, x, z, intensity=6.0, light_range=5.0):
        n = counters.get((room, "lamp"), 0) + 1
        counters[(room, "lamp")] = n
        entities.append({"id": "%s/lamp_%d" % (room, n), "name": "Ceiling lamp %d" % n, "parent": room,
                         "prefab": "../prefabs/ceiling_lamp.prefab.json",
                         "transform": {"position": rnd((x, 0, z))},
                         "overrides": {"light": {"pointLight": {"intensity": intensity, "range": light_range}}}})

    for rid, _, (i0, i1), (j0, j1) in ROOMS:
        cx, cz = (2 * i0 + 2 * i1 + 2) / 2, (2 * j0 - 1 + 2 * j1 + 1) / 2
        if rid == "corridor":
            for x in (3, 9, 15, 21, 27):
                lamp(rid, x, 0, 4.0, 4.0)
        elif rid == "common_area":
            lamp(rid, cx - 3, cz)
            lamp(rid, cx + 3, cz)
        elif rid == "exit_passage":
            lamp(rid, -5, 0, 2.5, 3.0)
        else:
            lamp(rid, cx, cz)

    # Props per room (GDD 10: merchants, private lockers, common areas; static only).
    for x in (1.2, 4.8):  # dormitory: bunks along the north wall and the west wall
        add("dormitory", "bunk", (x, 0, 6.3))
    add("dormitory", "bunk", (0.6, 0, 3.5), 90)
    add("dormitory", "locker", (5.6, 0, 4.0), -90)
    add("dormitory", "door_leaf", (2.4, 0, 1.13), -90, name="Door")
    for k in range(5):  # locker room: private player lockers (props only)
        add("locker_room", "locker", (6.6 + k * 0.55 + 0.6, 0, 6.6), 180)
        add("locker_room", "locker", (6.6 + k * 0.55 + 0.6, 0, 3.8))
    add("locker_room", "bench", (9.0, 0, 5.2))
    for x in (15.0, 21.0):  # common area: tables, benches, shelf, pillars
        add("common_area", "table", (x, 0, 4.0))
        add("common_area", "bench", (x, 0, 3.3))
        add("common_area", "bench", (x, 0, 4.7))
    add("common_area", "pillar", (18.0, 0, 3.0))
    add("common_area", "pillar", (18.0, 0, 5.0))
    add("common_area", "shelf", (13.0, 0, 6.5))
    add("common_area", "crate", (23.0, 0, 6.3))
    add("common_area", "terminal", (23.2, 0, 2.0), -90)
    add("generator_room", "generator", (27.0, 0, 4.5))  # generator room: power for D7
    add("generator_room", "electrical_panel", (29.875, 0, 3.0), -90, name="Electrical panel 1")
    add("generator_room", "electrical_panel", (29.875, 0, 5.5), -90, name="Electrical panel 2")
    add("generator_room", "crate", (24.8, 0, 6.2))
    add("generator_room", "door_leaf", (26.4, 0, 1.13), -90, name="Door")
    for rid, role in MERCHANTS:  # merchant areas: stall facing the corridor + role props
        i0 = next(r[2][0] for r in ROOMS if r[0] == rid)
        x0 = 2 * i0
        entities.append({"id": rid + "/stall", "name": "Merchant stall (%s)" % role, "parent": rid,
                         "prefab": "../prefabs/merchant_stall.prefab.json",
                         "transform": {"position": rnd((x0 + 3, 0, -6.875))},
                         "overrides": {"sign": {"meshRenderer": {"material": "sign_" + role}}}})
    add("weapon_dealer", "crate", (1.0, 0, -3.0))
    add("weapon_dealer", "crate", (1.0, 0, -4.0))
    add("medic", "shelf", (6.8, 0, -3.5), 90)
    add("medic", "bunk", (10.8, 0, -3.5), 90)
    add("resource_trader", "crate", (13.0, 0, -3.0))
    add("resource_trader", "crate", (13.0, 0, -4.0))
    add("resource_trader", "crate", (16.9, 0, -3.0))
    add("technician", "terminal", (18.8, 0, -3.5), 90)
    add("technician", "electrical_panel", (23.875, 0, -4.0), -90)
    add("scout", "table", (27.0, 0, -3.0))
    add("scout", "terminal", (29.2, 0, -3.5), -90)
    for x in (6.0, 18.0, 30.0 - 0.6):  # corridor: emergency lights (props until D7), panel
        add("corridor", "emergency_light", (x, 0, -0.875))
    add("corridor", "electrical_panel", (25.0, 0, 0.875), 180)
    add("airlock", "crate", (-3.2, 0, 2.2))
    add("airlock", "emergency_light", (-3.0, 0, -2.875))

    # Player start (fly camera until D7), environment and dim fog.
    entities.append({"id": "player_start", "name": "Player start", "parent": "bunker",
                     "transform": {"position": [0.9, 1.65, -0.35], "rotationDegrees": [0.0, -84.0, 0.0]},
                     "camera": {"verticalFovDegrees": 70.0, "nearPlane": 0.05}})
    entities.append({"id": "environment", "name": "Bunker air", "parent": "bunker",
                     "environment": {"clearColor": [0.01, 0.01, 0.012, 1.0], "ambientColor": [0.55, 0.5, 0.42],
                                     "ambientIntensity": 0.12, "drawBackground": False},
                     "fog": {"color": [0.05, 0.045, 0.04], "density": 0.045, "startDistance": 2.0,
                             "maxOpacity": 0.55}})
    materials = {"sign_" + role: "../materials/sign_%s.material.json" % role for _, role in MERCHANTS}
    return {"version": 1, "materials": materials, "entities": entities}


def ceiling_lamp_prefab():
    return {"version": 1, "entities": [
        {"id": "lamp", "name": "Lamp", "transform": {"position": [0.0, 3.0, 0.0]}, "modelRenderer": {"model": "lamp"}},
        {"id": "light", "name": "Lamp light", "parent": "lamp", "transform": {"position": [0.0, -0.35, 0.0]},
         "pointLight": {"color": [1.0, 0.82, 0.55], "intensity": 6.0, "range": 5.0}}]}


def merchant_stall_prefab():
    # Origin: inner face of the back wall, facing +z (towards the corridor).
    return {"version": 1, "materials": {"sign": "../materials/sign_weapons.material.json"}, "entities": [
        {"id": "counter", "name": "Counter", "transform": {"position": [0.0, 0.0, 2.6]},
         "modelRenderer": {"model": "merchant_counter"}},
        {"id": "shelf", "name": "Shelf", "transform": {"position": [-1.4, 0.0, 0.35]}, "modelRenderer": {"model": "shelf"}},
        {"id": "sign", "name": "Sign", "transform": {"position": [0.8, 2.0, 0.02], "rotationDegrees": [90.0, 0.0, 0.0]},
         "meshRenderer": {"mesh": "sign", "material": "sign"}}]}


def sign_material(role):
    return {"name": "sign_" + role, "shader": "zodar/pbr", "roughness": 0.9, "doubleSided": True,
            "baseColorTexture": {"path": "../textures/signs/%s.png" % role, "filter": "pixelArt"}}


def text(obj):
    return (json.dumps(obj, indent=2) + "\n").encode()


def outputs():
    files = {"palette.png": swatch(list(MASTER)), "palettes/glow.png": swatch(list(GLOW))}
    for name, boxes in MODULES.items():
        files["models/%s.glb" % name] = blockbench(name, boxes)
    files["models/airlock_arch.glb"] = airlock_arch()
    for role, icon in SIGNS.items():
        files["textures/signs/%s.png" % role] = png(32, 32, sign(icon))
        files["materials/sign_%s.material.json" % role] = text(sign_material(role))
    files["prefabs/ceiling_lamp.prefab.json"] = text(ceiling_lamp_prefab())
    files["prefabs/merchant_stall.prefab.json"] = text(merchant_stall_prefab())
    files["levels/bunker.scene.json"] = text(level())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="compare with the files on disk, write nothing")
    parser.add_argument("--out", type=Path, default=ASSETS)
    parser.add_argument("--import-commands", action="store_true",
                        help="print the zodar.py commands that (re)build assets.json entries")
    args = parser.parse_args()
    if args.import_commands:
        models = "game/assets/models/"
        print("python3 tools/zodar.py import game/assets")
        # Material files and prefabs are dependencies of the level (skipped by the directory
        # import); registered explicitly so they have ids of their own.
        print("python3 tools/zodar.py import " + " ".join(
            "game/assets/materials/sign_%s.material.json" % role for role in SIGNS) +
            " game/assets/prefabs/ceiling_lamp.prefab.json game/assets/prefabs/merchant_stall.prefab.json")
        print("python3 tools/zodar.py import " + " ".join(models + m + ".glb" for m in ARCHITECTURE) +
              " --class architecture")
        print("python3 tools/zodar.py import " + " ".join(models + m + ".glb" for m in GLOWING) +
              " --sub-palette glow")
        return 0
    stale = []
    for name, data in sorted(outputs().items()):
        path = args.out / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                stale.append(name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    if stale:
        print("stale greybox files (run game/tools/make_greybox_kit.py): " + ", ".join(stale))
        return 1
    print("greybox kit up to date" if args.check else "wrote %d files to %s" % (len(outputs()), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
