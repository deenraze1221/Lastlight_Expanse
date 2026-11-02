#!/usr/bin/env python3
"""Generates the Lastlight Expanse bunker greybox (Phase D2, revised: modern shelter). Python
standard library only, deterministic bytes.

Design: a maintained, modern underground military / civil-defence shelter built for long-term
survival (GDD 10: one permanent shared bunker with merchants, NPC space, private lockers and
common areas). 4 m planning grid, 4 m wide main corridor, 4.0 m clear ceiling height,
reinforced 0.5 m walls with 0.5 m pilasters, ceiling beams, organised cable trays and vents,
neutral linear overhead lights, warm wall lights, lit exit signs, room signage. Muted, faded
GDD 3 palette with subtle wear only.

Writes into game/assets/:
  palette.png, palettes/glow.png        master palette (GDD 3) and the "glow" sub-palette
  models/<module>.glb                    27 Blockbench-style modules + 1 Blender-style arch
  textures/signs/<name>.png              merchant and room signs (64x64)
  materials/sign_<name>.material.json    sign materials (pixel-art sampling)
  prefabs/*.prefab.json                  ceiling_lamp, wall_light, room_sign, merchant_stall
  levels/bunker.scene.json               the static bunker level (GDD 10)

The modules are placeholders: real Blockbench/Blender exports replace them later under the
same file names (models/<module>.glb), with their sources in sources/blockbench/ or
sources/blender/. This script is the source of every generated file. "Blockbench-style" and
"Blender-style" describe the glTF structure (generator string, per-face UVs, NEAREST
samplers, node layout; smooth normals and tangents for Blender); they are not real exports.

The registry (assets.json: palette, defaults, budgets, entries) is not written here; after
regenerating run the commands printed by --import-commands (game/assets/README.md).

Usage:
  python3 game/tools/make_greybox_kit.py                    write the files
  python3 game/tools/make_greybox_kit.py --check            exit 1 if a committed file differs
  python3 game/tools/make_greybox_kit.py --import-commands  print the registry commands
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
    "g0": 0x3f4a3c, "g1": 0x56634f, "g2": 0x6e7b64,                                  # muted green-grey paint
    "r0": 0x4a3528, "r1": 0x6b4a33, "r2": 0x8a6748,                                  # brown (wood, leather)
    "b0": 0x4b5560, "b1": 0x6a7680,                                                  # blue-grey steel
    "wood": 0x7a6a52, "pale": 0xa39a82, "paint": 0xb8b2a0,                           # laminate, fabric, white paint
    "ochre": 0x8a7442, "red": 0x7a3a32,                                              # safety ochre, faded red
}
GLOW = {
    "lamp0": 0xc9a95c, "lamp1": 0xe0c98a,    # warm wall lights
    "white0": 0xcfcdc4, "white1": 0xe6e4dc,  # neutral overhead lights
    "em0": 0x8c2f26, "em1": 0xb2463a,        # emergency lens (lit from D7)
    "scr0": 0x5f8f6e, "scr1": 0x8fbf94,      # terminal phosphor, exit signs
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


# --- Textures (32x32, one texel per art pixel, 32 texels per metre; maintained, subtle wear) ---

def t_concrete(x, y):
    """Smooth cast concrete panel (1 m): faint seam, four form-tie holes."""
    if (x, y) in ((7, 7), (24, 7), (7, 24), (24, 24)):
        return "c1"
    if x == 0:
        return "c2"
    n = noise(x, y, 1)
    return "c2" if n < 14 else ("c4" if n > 246 else "c3")


def t_paint(x, y):
    """Painted lower wall band, kept clean; darker trim line on top."""
    if y == 0:
        return "g0"
    return "g2" if noise(x, y, 3) > 250 else "g1"


def t_floor(x, y):
    """Sealed concrete floor, 1 m slabs."""
    if x == 0 or y == 0:
        return "c1"
    return "c1" if noise(x, y, 4) < 20 else "c2"


def t_ceiling(x, y):
    return "c4" if noise(x, y, 5) < 12 else "c3"


def t_metal(x, y):
    """Brushed steel sheet with a folded edge."""
    if x in (0, 31) or y in (0, 31):
        return "b0"
    return "b0" if noise(x, y // 4, 7) < 30 else "b1"


def t_locker(x, y):
    if x in (0, 31) or y in (0, 31) or x == 15:
        return "b0"  # frame and centre seam
    if y in (4, 6, 8) and (3 <= x <= 12 or 18 <= x <= 28):
        return "black"  # vents
    if 11 <= x <= 12 and 14 <= y <= 18:
        return "c4"  # handle
    return "b1"


def t_grille(x, y):
    if x in (0, 31) or y in (0, 31):
        return "b0"
    return "black" if y % 3 == 1 else "b1"


def t_cable(x, y):
    return "c0" if y % 4 == 0 else ("black" if (x + y) % 9 else "g0")


def t_wood(x, y):
    """Laminate top."""
    if y % 16 == 0:
        return "r2"
    return "r2" if noise(x // 6, y, 10) < 30 else "wood"


def t_fabric(x, y):
    return "b0" if (x // 4 + y // 4) % 2 and (x + y) % 2 else "b1"


def t_olive(x, y):
    """Painted equipment housing (green-grey) with a service panel line."""
    if x in (0, 31) or y in (0, 31):
        return "g0"
    return "g2" if noise(x, y, 11) > 248 else "g1"


def t_hazard(x, y):
    return "ochre" if ((x + y) // 4) % 2 else "black"


def t_marking(x, y):
    return "pale" if noise(x, y, 13) > 245 else "ochre"


def t_lamp(x, y):
    """Neutral LED diffuser."""
    if x in (0, 31) or y in (0, 31):
        return "white0"
    return "white1"


def t_warm(x, y):
    d = max(abs(x - 15.5), abs(y - 15.5))
    return "lamp1" if d < 11 else "lamp0"


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


def t_exit(x, y):
    """Lit exit sign: running figure and arrow, simplified."""
    if x in (0, 31) or y in (0, 31):
        return "black"
    arrow = (14 <= y <= 17 and 18 <= x <= 28) or (abs(y - 15.5) <= 28 - x - 18 + 3 and 22 <= x <= 28 and
                                                  abs(y - 15.5) < 7)
    figure = (6 <= x <= 9 and 8 <= y <= 11) or (7 <= x <= 8 and 12 <= y <= 19) or \
             (5 <= x <= 10 and y == 14) or (x + 12 == y and 5 <= x <= 7) or (y - x == 11 and 8 <= x <= 11)
    return "scr1" if arrow or figure else "scr0"


TEXTURES = {
    "concrete": t_concrete, "paint": t_paint, "floor": t_floor, "ceiling": t_ceiling, "metal": t_metal,
    "locker": t_locker, "grille": t_grille, "cable": t_cable, "wood": t_wood, "fabric": t_fabric,
    "olive": t_olive, "hazard": t_hazard, "marking": t_marking, "lamp": t_lamp, "warm": t_warm,
    "lens": t_lens, "screen": t_screen, "exit": t_exit,
}
# PBR values per texture (metallic, roughness, emissive strength or None).
MATERIAL = {
    "concrete": (0.0, 0.9, None), "paint": (0.0, 0.7, None), "floor": (0.0, 0.6, None),
    "ceiling": (0.0, 0.95, None), "metal": (0.7, 0.45, None), "locker": (0.6, 0.5, None),
    "grille": (0.6, 0.6, None), "cable": (0.0, 0.8, None), "wood": (0.0, 0.6, None),
    "fabric": (0.0, 1.0, None), "olive": (0.3, 0.55, None), "hazard": (0.0, 0.7, None),
    "marking": (0.0, 0.6, None), "lamp": (0.0, 0.4, 5.0), "warm": (0.0, 0.4, 3.0),
    "lens": (0.0, 0.4, None), "screen": (0.0, 0.3, 1.5), "exit": (0.0, 0.3, 2.0),
}


# --- Signs (GDD 9: five merchant roles; GDD 10: bunker rooms) ------------------------------

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


def icon_dormitory(x, y):  # bed with pillow
    return (5 <= x <= 26 and 17 <= y <= 20) or (5 <= x <= 7 and 10 <= y <= 25) or \
           (24 <= x <= 26 and 15 <= y <= 25) or (9 <= x <= 13 and 13 <= y <= 16)


def icon_lockers(x, y):  # two locker doors
    door = (6 <= x <= 14 or 17 <= x <= 25) and 5 <= y <= 26
    inner = (8 <= x <= 12 or 19 <= x <= 23) and (8 <= y <= 9 or 11 <= y <= 12)
    handle = (x in (13, 18)) and 15 <= y <= 17
    return door and not inner and not handle


def icon_common(x, y):  # table with two seats
    return (6 <= x <= 25 and 13 <= y <= 15) or (9 <= x <= 10 and 16 <= y <= 24) or \
           (21 <= x <= 22 and 16 <= y <= 24) or (3 <= x <= 5 and 17 <= y <= 24) or (26 <= x <= 28 and 17 <= y <= 24)


def icon_generator(x, y):  # lightning bolt
    return (abs(x - (22 - y * 0.55)) < 2.2 and 4 <= y <= 15) or (12 <= x <= 21 and 15 <= y <= 16) or \
           (abs(x - (24 - y * 0.55)) < 2.2 and 16 <= y <= 28)


def icon_airlock(x, y):  # door with outward arrow
    frame = (4 <= x <= 15 and 4 <= y <= 27) and not (6 <= x <= 13 and 6 <= y <= 27)
    arrow = (15 <= y <= 16 and 16 <= x <= 26) or (abs(y - 15.5) + (x - 20) <= 6 and 20 <= x <= 26 and
                                                  abs(y - 15.5) <= 6 and x - 20 >= abs(y - 15.5))
    return frame or arrow


SIGNS = {"weapons": icon_weapons, "medic": icon_medic, "resources": icon_resources,
         "technician": icon_technician, "scout": icon_scout, "dormitory": icon_dormitory,
         "lockers": icon_lockers, "common": icon_common, "generator": icon_generator, "airlock": icon_airlock}

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
        images.append({"name": t, "mimeType": "image/png", "bufferView": b.view(png(64, 64, lambda x, y: TEXTURES[t](x // 2, y // 2)))})
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



# --- Modules -----------------------------------------------------------------------------
# Planning grid 4 m; clear height 3.6 m; walls 0.3 m (centred on grid lines); pilasters 0.5 m.
CELL = 4.0
H = 4.0
WALL = 0.25  # half wall thickness
BAND = 1.0   # painted lower band


def wall_column(x0, x1, z0=-WALL, z1=WALL, top=H, faces_low=("+x", "-x", "+z", "-z", "-y"),
                faces_high=("+x", "-x", "+z", "-z", "+y")):
    """Painted lower band + clean concrete above (the shelter's wall finish)."""
    return [b((x0, 0, z0), (x1, BAND, z1), "paint", faces_low),
            b((x0, BAND, z0), (x1, top, z1), "concrete", faces_high)]


def opening(width, height, trim=0.08):
    """4 m wall with an opening (width x height), concrete header and steel frame trims."""
    half = width / 2
    out = wall_column(-2, -half) + wall_column(half, 2)
    out.append(b((-half, height, -WALL), (half, H, WALL), "concrete", ("+z", "-z", "-y")))
    depth = WALL + 0.03
    out += [b((-half - trim, 0, -depth), (-half, height + trim, depth), "metal", ("+x", "-x", "+z", "-z", "+y")),
            b((half, 0, -depth), (half + trim, height + trim, depth), "metal", ("+x", "-x", "+z", "-z", "+y")),
            b((-half, height, -depth), (half, height + trim, depth), "metal", ("+z", "-z", "-y"))]
    return out


def legs(x, z, height, size=0.05):
    return [b((sx * x - size / 2, 0, sz * z - size / 2), (sx * x + size / 2, height, sz * z + size / 2), "metal")
            for sx in (-1, 1) for sz in (-1, 1)]


def stairs(steps=6, rise=1.2):
    depth = CELL / steps
    out = []
    for k in range(steps):
        z0 = -2 + depth * k
        top = rise / steps * (k + 1)
        out.append(b((-2, 0, z0), (2, top, z0 + depth), "floor"))
        out.append(b((-2, top, z0), (2, top + 0.004, z0 + 0.08), "marking", ("+y", "-z")))  # step nosing
    return out


MODULES = {
    # Architecture
    "floor": [b((-2, -0.1, -2), (2, 0, 2), "floor", ("+y",))],
    "floor_marked": [b((-2, -0.1, -2), (2, 0, 2), "floor", ("+y",))] +
                    [b((-2, 0, z - 0.05), (2, 0.004, z + 0.05), "marking", ("+y",)) for z in (-1.55, 1.55)],
    "ceiling": [b((-2, H, -2), (2, H + 0.1, 2), "ceiling", ("-y",))],
    "wall": wall_column(-2, 2),
    "doorway": opening(2.0, 2.7),
    "opening_wide": opening(2.8, 2.9),
    "corner": wall_column(-0.25, 0.25, -0.25, 0.25),
    "pillar": [b((-0.3, 0, -0.3), (0.3, 0.1, 0.3), "hazard", ("+x", "-x", "+z", "-z")),
               b((-0.3, 0.1, -0.3), (0.3, BAND, 0.3), "paint", ("+x", "-x", "+z", "-z")),
               b((-0.3, BAND, -0.3), (0.3, H, 0.3), "concrete", ("+x", "-x", "+z", "-z"))],
    "beam": [b((-0.2, H - 0.4, -2), (0.2, H, 2), "concrete", ("+x", "-x", "-y"))],
    "stairs": stairs(),
    # Doors and services (wall-mounted modules: origin on the wall surface at floor level, facing +z)
    "door_leaf": [b((0, 0, -0.03), (2.1, 2.75, 0.03), "metal"),
                  b((0.15, 1.0, 0.03), (0.2, 1.4, 0.06), "metal", ("+x", "-x", "+z", "+y", "-y"))],
    "lamp": [b((-0.7, -0.08, -0.12), (0.7, 0, 0.12), "metal", ("+x", "-x", "+z", "-z")),
             b((-0.65, -0.09, -0.09), (0.65, -0.08, 0.09), "lamp", ("-y", "+x", "-x", "+z", "-z"))],
    "wall_light": [b((-0.15, 1.95, 0), (0.15, 2.3, 0.04), "metal"),
                   b((-0.12, 1.98, 0.04), (0.12, 2.27, 0.1), "warm", ("+z", "+x", "-x", "+y", "-y"))],
    "exit_sign": [b((-0.3, 0, 0), (0.3, 0.2, 0.06), "metal", ("+x", "-x", "+y", "-y")),
                  b((-0.28, 0.01, 0.06), (0.28, 0.19, 0.07), "exit", ("+z",))],
    "emergency_light": [b((-0.15, 2.95, 0), (0.15, 3.07, 0.1), "metal"),
                        b((-0.11, 2.97, 0.1), (0.11, 3.05, 0.13), "lens")],
    "vent": [b((-0.45, 2.85, 0), (0.45, 3.25, 0.05), "metal", ("+x", "-x", "+y", "-y")),
             b((-0.42, 2.88, 0.05), (0.42, 3.22, 0.06), "grille", ("+z",))],
    # Suspended cable tray (4 m run along x, below the beams): tray, side lips, cables, hangers.
    "cable_tray": [b((-2, 3.0, -0.15), (2, 3.03, 0.15), "metal", ("+y", "-y", "+z", "-z")),
                   b((-2, 3.03, 0.12), (2, 3.1, 0.15), "metal", ("+z", "-z", "+y")),
                   b((-2, 3.03, -0.15), (2, 3.1, -0.12), "metal", ("+z", "-z", "+y")),
                   b((-2, 3.03, -0.11), (2, 3.08, 0.11), "cable", ("+y",))] +
                  [b((x - 0.01, 3.1, -0.01), (x + 0.01, H, 0.01), "metal", ("+x", "-x", "+z", "-z")) for x in (-1, 1)],
    "electrical_panel": [b((-0.35, 0.9, 0), (0.35, 1.9, 0.22), "metal"),
                         b((-0.12, 1.7, 0.22), (0.12, 1.78, 0.23), "marking", ("+z",)),
                         b((-0.05, 1.9, 0.06), (0.05, H, 0.16), "metal", ("+x", "-x", "+z"))],
    # Furniture and equipment (origin on the floor, centred, front facing +z)
    "crate": [b((-0.45, 0, -0.35), (0.45, 0.55, 0.35), "olive"),
              b((-0.47, 0.55, -0.37), (0.47, 0.6, 0.37), "metal")],
    "bunk": [b((sx * 0.97 - 0.03, 0, sz * 0.42 - 0.03), (sx * 0.97 + 0.03, 1.9, sz * 0.42 + 0.03), "metal")
             for sx in (-1, 1) for sz in (-1, 1)] +
            [b((-1, 0.35, -0.45), (1, 0.42, 0.45), "metal"), b((-0.95, 0.42, -0.42), (0.95, 0.58, 0.42), "fabric"),
             b((-1, 1.35, -0.45), (1, 1.42, 0.45), "metal"), b((-0.95, 1.42, -0.42), (0.95, 1.58, 0.42), "fabric")],
    "locker": [b((-0.25, 0, -0.25), (0.25, 1.95, 0.25), "locker")],
    "terminal": [b((-0.45, 0, -0.3), (0.45, 0.85, 0.3), "metal"),
                 b((-0.35, 0.85, -0.08), (0.35, 1.35, 0.08), "metal"),
                 b((-0.3, 0.9, 0.08), (0.3, 1.3, 0.09), "screen", ("+z",))],
    "generator": [b((-1.25, 0, -0.85), (1.25, 0.08, 0.85), "hazard"),
                  b((-1.2, 0.08, -0.8), (1.2, 1.6, 0.8), "olive"),
                  b((-0.9, 1.6, -0.5), (0.4, 2.0, 0.5), "metal"),
                  b((0.6, 1.6, -0.15), (0.9, H, 0.15), "metal", ("+x", "-x", "+z", "-z")),
                  b((-0.5, 0.9, 0.8), (0.1, 1.3, 0.81), "screen", ("+z",))],
    "merchant_counter": [b((-1.4, 0, -0.3), (1.4, 1.0, 0.3), "olive"),
                         b((-1.45, 1.0, -0.35), (1.45, 1.05, 0.35), "wood")],
    "shelf": [b((sx * 0.6 - 0.02, 0, sz * 0.25 - 0.02), (sx * 0.6 + 0.02, 2.1, sz * 0.25 + 0.02), "metal")
              for sx in (-1, 1) for sz in (-1, 1)] +
             [b((-0.6, y, -0.25), (0.6, y + 0.03, 0.25), "metal") for y in (0.1, 0.6, 1.1, 1.6, 2.07)],
    "table": [b((-0.9, 0.72, -0.45), (0.9, 0.76, 0.45), "wood")] + legs(0.85, 0.4, 0.72),
    "bench": [b((-0.9, 0.43, -0.18), (0.9, 0.47, 0.18), "wood"),
              b((-0.85, 0, -0.15), (-0.8, 0.43, 0.15), "metal"), b((0.8, 0, -0.15), (0.85, 0.43, 0.15), "metal")],
}
# Imported trims remain models; structural jambs/headers become voxels.
MODULES["door_frame"] = [box for box in MODULES["doorway"] if box[2] == "metal"]
MODULES["wide_frame"] = [box for box in MODULES["opening_wide"] if box[2] == "metal"]

ARCHITECTURE = ("floor", "floor_marked", "ceiling", "wall", "doorway", "opening_wide", "corner", "pillar", "beam",
                "stairs", "airlock_arch")
GLOWING = ("lamp", "wall_light", "exit_sign", "emergency_light", "terminal", "generator")  # "glow" sub-palette


def airlock_arch():
    """Blender-style curved blast-door frame: 4 m wide, 3.6 m tall, 0.8 m deep; opening 2.6 m
    wide with a semicircular top (radius 1.3 m, crown at 3.2 m); smooth normals on the curve."""
    segments = 16
    r, spring, half_w, depth = 1.3, 1.9, 2.0, 0.4
    inner = [(-r, 0.0), (-r, spring)]
    inner += [(-r * math.cos(math.pi * k / segments), spring + r * math.sin(math.pi * k / segments))
              for k in range(1, segments)]
    inner += [(r, spring), (r, 0.0)]

    def outer(p):  # project onto the frame's outer rectangle [-2, 2] x [0, H]
        x, y = p
        if y <= spring:
            return (-half_w if x < 0 else half_w, y)
        dx, dy = x, y - spring
        t = min((half_w / abs(dx)) if abs(dx) > 1e-9 else 1e9, (H - spring) / dy)
        return (max(-half_w, min(half_w, dx * t)), spring + dy * t)

    def inward(i):
        x, y = inner[i]
        if i in (0, 1, len(inner) - 2, len(inner) - 1):
            return (1.0 if x < 0 else -1.0, 0.0, 0.0)
        dx, dy = x, y - spring
        length = math.hypot(dx, dy)
        return (-dx / length, -dy / length, 0.0)

    frame = []
    for z, sign_ in ((depth, 1), (-depth, -1)):
        for i in range(len(inner) - 1):
            a, c = inner[i], inner[i + 1]
            oa, oc = outer(a), outer(c)
            quad = [(a[0], a[1], z), (oa[0], oa[1], z), (oc[0], oc[1], z), (c[0], c[1], z)]
            if sign_ < 0:
                quad = [quad[0], quad[3], quad[2], quad[1]]
            frame.append(([r6(p) for p in quad], [[0.0, 0.0, float(sign_)]] * 4,
                          [r6((p[0] + half_w, H - p[1])) for p in quad]))
    length = 0.0
    for i in range(len(inner) - 1):
        a, c = inner[i], inner[i + 1]
        step = math.hypot(c[0] - a[0], c[1] - a[1])
        quad = [(a[0], a[1], depth), (a[0], a[1], -depth), (c[0], c[1], -depth), (c[0], c[1], depth)]
        normals = [r6(inward(i)), r6(inward(i)), r6(inward(i + 1)), r6(inward(i + 1))]
        uvs = [r6(t) for t in ((length, 0.0), (length, 2 * depth), (length + step, 2 * depth), (length + step, 0.0))]
        frame.append(([r6(p) for p in quad], normals, uvs))
        length += step
    sides = box_faces((-half_w, 0, -depth), (half_w, H, depth), ("+x", "-x", "+y"))
    return model_gltf("airlock_arch", BLENDER, {"metal": frame, "concrete": sides}, BL_SAMPLER, tangents=True)


# --- Level (GDD 10: one permanent shared bunker; static greybox, no NPC/merchant/locker logic) ---

# Grid: cell (i, j) covers x in [4i, 4i+4], z in [4j-2, 4j+2]; floor at y = 0, ceiling at 3.6 m.
ROOMS = [
    # id, name, i range, j range
    ("corridor", "Main corridor", (0, 14), (0, 0)),             # 60 x 4 m
    ("dormitory", "Dormitory", (0, 2), (1, 2)),                 # 12 x 8 m
    ("locker_room", "Locker room", (3, 4), (1, 2)),             # 8 x 8 m
    ("common_area", "Common area", (5, 10), (1, 3)),            # 24 x 12 m
    ("generator_room", "Generator room", (11, 14), (1, 2)),     # 16 x 8 m
    ("weapon_dealer", "Weapon dealer area", (0, 2), (-2, -1)),  # merchant zones 12 x 8 m
    ("medic", "Medic area", (3, 5), (-2, -1)),
    ("resource_trader", "Resource trader area", (6, 8), (-2, -1)),
    ("technician", "Technician area", (9, 11), (-2, -1)),
    ("scout", "Scout area", (12, 14), (-2, -1)),
    ("airlock", "Airlock", (-2, -1), (-1, 1)),                  # 8 x 12 m
    ("exit_passage", "Exit passage", (-4, -3), (0, 0)),         # 8 x 4 m, stairs up to the surface door
]
# Openings between two cells: ((i, j), (i, j), module).
OPENINGS = [((1, 1), (1, 0), "doorway"), ((3, 1), (3, 0), "doorway"), ((6, 1), (6, 0), "opening_wide"),
            ((9, 1), (9, 0), "opening_wide"), ((12, 1), (12, 0), "doorway"),
            ((1, -1), (1, 0), "opening_wide"), ((4, -1), (4, 0), "opening_wide"), ((7, -1), (7, 0), "opening_wide"),
            ((10, -1), (10, 0), "opening_wide"), ((13, -1), (13, 0), "opening_wide"),
            ((-1, 0), (0, 0), "doorway"), ((-3, 0), (-2, 0), "airlock_arch")]
MERCHANTS = [("weapon_dealer", "weapons"), ("medic", "medic"), ("resource_trader", "resources"),
             ("technician", "technician"), ("scout", "scout")]
NEUTRAL = [0.92, 0.93, 1.0]  # overhead LED light
WARM = [1.0, 0.78, 0.5]      # wall lights


def rnd(v):
    return [round(c, 4) + 0.0 for c in v]


def level(authoring=False):
    cells = {}
    for rid, _, (i0, i1), (j0, j1) in ROOMS:
        for i in range(i0, i1 + 1):
            for j in range(j0, j1 + 1):
                cells[(i, j)] = rid
    openings = {frozenset((a, c)): m for a, c, m in OPENINGS}
    entities = []
    counters = {}

    def next_id(room, kind):
        n = counters.get((room, kind), 0) + 1
        counters[(room, kind)] = n
        return n

    def add(room, model, pos, yaw=0.0, name=None):
        if model in VOXEL_ARCHITECTURE and not authoring:
            if model in ("doorway", "opening_wide"):
                model = "door_frame" if model == "doorway" else "wide_frame"
            else:
                return
        n = next_id(room, model)
        e = {"id": "%s/%s_%d" % (room, model, n), "name": name or "%s %d" % (model.replace("_", " "), n),
             "parent": room, "transform": {"position": rnd(pos)}}
        if yaw:
            e["transform"]["rotationDegrees"] = [0.0, float(yaw), 0.0]
        e["modelRenderer"] = {"model": model}
        entities.append(e)

    def prefab(room, kind, path, pos, yaw=0.0, overrides=None, name=None):
        n = next_id(room, kind)
        e = {"id": "%s/%s_%d" % (room, kind, n), "name": name or "%s %d" % (kind.replace("_", " "), n),
             "parent": room, "prefab": path, "transform": {"position": rnd(pos)}}
        if yaw:
            e["transform"]["rotationDegrees"] = [0.0, float(yaw), 0.0]
        if overrides:
            e["overrides"] = overrides
        entities.append(e)

    def lamp(room, x, z, intensity=12.0, light_range=7.0):
        prefab(room, "lamp", "../prefabs/ceiling_lamp.prefab.json", (x, 0, z),
               overrides={"light": {"pointLight": {"intensity": intensity, "range": light_range}}})

    def wall_light(room, x, z, yaw):
        prefab(room, "wall_light", "../prefabs/wall_light_unit.prefab.json", (x, 0, z), yaw)

    def room_sign(room, icon, x, z, yaw, y=3.15):
        prefab(room, "sign", "../prefabs/room_sign.prefab.json", (x, y, z), yaw,
               {"sign": {"meshRenderer": {"material": "sign_" + icon}}})

    entities.append({"id": "bunker", "name": "Bunker"})
    for rid, name, _, _ in ROOMS:
        entities.append({"id": rid, "name": name, "parent": "bunker"})

    # Floors (corridor: guidance lines), ceilings; stairs in the far exit-passage cell.
    for (i, j), rid in sorted(cells.items()):
        centre = (CELL * i + 2, 0, CELL * j)
        if (i, j) == (-4, 0):
            add(rid, "stairs", centre, -90)
        else:
            add(rid, "floor_marked" if rid in ("corridor", "exit_passage") else "floor", centre)
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
            if di:
                add(owner, module, (CELL * max(i, i + di), 0, CELL * j), 90)
            else:
                add(owner, module, (CELL * i + 2, 0, CELL * max(j, j + dj) - 2), 0)
    # Reinforced pilasters at every wall vertex.
    vertices = set()
    for key in edges:
        (a, c) = sorted(key)
        if a[0] != c[0]:
            x = CELL * max(a[0], c[0])
            vertices.update({(x, CELL * a[1] - 2), (x, CELL * a[1] + 2)})
        else:
            z = CELL * max(a[1], c[1]) - 2
            vertices.update({(CELL * a[0], z), (CELL * a[0] + 4, z)})
    for (x, z) in sorted(vertices):
        i, j = int(x // CELL), int((z + 2) // CELL)
        room = next(cells[c] for c in ((i, j), (i - 1, j), (i, j - 1), (i - 1, j - 1)) if c in cells)
        add(room, "corner", (x, 0, z))

    # Main corridor (60 x 4 m): beams every 4 m, cable tray on the north wall, neutral overhead
    # lights every 8 m, warm wall lights between them, exit signs towards the airlock.
    for i in range(1, 15):
        add("corridor", "beam", (CELL * i, 0, 0))
    for i in range(15):
        add("corridor", "cable_tray", (CELL * i + 2, 0, 1.1))
    for i in range(0, 15, 2):
        lamp("corridor", CELL * i + 2, 0, 10.0, 6.0)
    for i in (2, 5, 8, 11, 14):  # south wall cells without merchant openings
        wall_light("corridor", CELL * i + 2, -2 + WALL, 0)
    for x in (13.0, 25.0, 37.0, 49.0):
        add("corridor", "exit_sign", (x, 2.75, -2 + WALL), name="Exit sign (to airlock)")
    add("corridor", "exit_sign", (WALL, 2.8, 0.0), 90, name="Exit sign (airlock door)")
    for x in (10.0, 30.0, 46.0):
        add("corridor", "emergency_light", (x, 0, 2 - WALL), 180)
    add("corridor", "electrical_panel", (22.0, 0, 2 - WALL), 180)
    add("corridor", "electrical_panel", (45.0, 0, -2 + WALL))
    # Room signs on the corridor side above each opening.
    room_sign("corridor", "dormitory", 6.0, 2 - WALL - 0.01, 180)
    room_sign("corridor", "lockers", 14.0, 2 - WALL - 0.01, 180)
    room_sign("corridor", "common", 26.0, 2 - WALL - 0.01, 180, 3.28)
    room_sign("corridor", "common", 38.0, 2 - WALL - 0.01, 180, 3.28)
    room_sign("corridor", "generator", 50.0, 2 - WALL - 0.01, 180)
    for (rid, role), i in zip(MERCHANTS, (1, 4, 7, 10, 13)):
        room_sign("corridor", role, CELL * i + 2, -2 + WALL + 0.01, 0, 3.28)
    room_sign("corridor", "airlock", WALL + 0.01, 0.0, 90, 3.3)

    # Dormitory (12 x 8 m): eight bunks (16 beds), personal lockers, a table.
    for x in (1.6, 4.0, 9.0):
        add("dormitory", "bunk", (x, 0, 9.3))
    for x in (1.6, 10.4):
        add("dormitory", "bunk", (x, 0, 4.6))
    add("dormitory", "bunk", (0.75, 0, 7.0), 90)
    add("dormitory", "bunk", (11.25, 0, 7.0), 90)
    for k in range(4):
        add("dormitory", "locker", (5.4 + k * 0.6, 0, 9.5), 180)
    add("dormitory", "table", (6.0, 0, 6.2))
    add("dormitory", "bench", (6.0, 0, 5.5))
    add("dormitory", "bench", (6.0, 0, 6.9))
    add("dormitory", "door_leaf", (7.1, 0, 2 + WALL + 0.04), name="Sliding door")
    add("dormitory", "vent", (0.0 + WALL, 0, 6.0), 90)
    lamp("dormitory", 3.0, 6.0, 10.0, 6.0)
    lamp("dormitory", 9.0, 6.0, 10.0, 6.0)

    # Locker room (8 x 8 m): private player lockers (props only), benches.
    for k in range(11):
        add("locker_room", "locker", (12.75 + k * 0.6, 0, 9.5), 180)
    for k in range(9):
        add("locker_room", "locker", (19.5, 0, 3.3 + k * 0.6), -90)
    add("locker_room", "bench", (15.0, 0, 7.6))
    add("locker_room", "bench", (15.0, 0, 5.0))
    add("locker_room", "door_leaf", (15.1, 0, 2 + WALL + 0.04), name="Sliding door")
    lamp("locker_room", 16.0, 6.0, 10.0, 6.0)

    # Common area (24 x 12 m): dining tables, structural columns, supply shelves, notice terminal.
    for x in (25.0, 32.0, 39.0):
        for z in (6.0, 10.5):
            add("common_area", "table", (x, 0, z))
            add("common_area", "bench", (x, 0, z - 0.7))
            add("common_area", "bench", (x, 0, z + 0.7))
    for x in (28.5, 35.5):
        add("common_area", "pillar", (x, 0, 8.25))
    for x in (21.5, 23.0, 41.0, 42.5):
        add("common_area", "shelf", (x, 0, 13.5))
    add("common_area", "crate", (43.0, 0, 3.0))
    add("common_area", "crate", (43.0, 0, 3.9))
    add("common_area", "terminal", (20.6, 0, 8.25), 90, name="Notice terminal")
    add("common_area", "vent", (32.0, 0, 14 - WALL), 180)
    for x in (25.0, 32.0, 39.0):
        lamp("common_area", x, 4.5, 12.0, 7.0)
        lamp("common_area", x, 11.0, 12.0, 7.0)

    # Generator room (16 x 8 m): two generator sets, distribution panels (power for D7).
    add("generator_room", "generator", (51.0, 0, 7.5))
    add("generator_room", "generator", (56.0, 0, 7.5))
    for z in (3.4, 4.4, 7.6):
        add("generator_room", "electrical_panel", (60 - WALL, 0, z), -90, name="Distribution panel")
    add("generator_room", "terminal", (45.0, 0, 8.0), 90, name="Power monitor")
    add("generator_room", "crate", (45.0, 0, 3.2))
    add("generator_room", "door_leaf", (51.1, 0, 2 + WALL + 0.04), name="Sliding door")
    add("generator_room", "vent", (52.0, 0, 10 - WALL), 180)
    lamp("generator_room", 48.0, 6.0, 10.0, 6.0)
    lamp("generator_room", 56.0, 4.5, 10.0, 6.0)

    # Merchant zones (12 x 8 m each): open front, stall with sign, role-specific props.
    for rid, role in MERCHANTS:
        x0 = CELL * next(r[2][0] for r in ROOMS if r[0] == rid)
        prefab(rid, "stall", "../prefabs/merchant_stall.prefab.json", (x0 + 6, 0, -10 + WALL),
               overrides={"sign": {"meshRenderer": {"material": "sign_" + role}}},
               name="Merchant stall (%s)" % role)
        lamp(rid, x0 + 3, -6.0, 10.0, 6.0)
        lamp(rid, x0 + 9, -6.0, 10.0, 6.0)
    for z in (-4.0, -5.0):
        add("weapon_dealer", "crate", (1.0, 0, z))
    add("weapon_dealer", "locker", (11.6, 0, -4.5), -90)
    add("weapon_dealer", "locker", (11.6, 0, -5.1), -90)
    add("medic", "bunk", (21.0, 0, -4.5), 90, name="Treatment cot")
    add("medic", "shelf", (12.6, 0, -4.5), 90)
    for x, z in ((25.0, -4.0), (25.0, -4.9), (34.6, -4.0), (34.6, -4.9)):
        add("resource_trader", "crate", (x, 0, z))
    add("technician", "terminal", (36.6, 0, -4.5), 90)
    add("technician", "electrical_panel", (48 - WALL, 0, -4.5), -90)
    add("technician", "table", (46.0, 0, -6.0), 90)
    add("scout", "table", (51.0, 0, -5.0), name="Map table")
    add("scout", "terminal", (59.2, 0, -4.5), -90, name="Radio terminal")

    # Airlock (8 x 12 m): decontamination benches, gear lockers, blast-door arch to the exit.
    for k in range(5):
        add("airlock", "locker", (-7.2 + k * 0.6, 0, 5.5), 180)
    add("airlock", "bench", (-4.0, 0, 3.5))
    add("airlock", "bench", (-4.0, 0, -3.5))
    add("airlock", "crate", (-1.2, 0, -5.2))
    add("airlock", "emergency_light", (-4.0, 0, -6 + WALL))
    add("airlock", "exit_sign", (-8 + 0.4, 3.3, 0.0), 90, name="Exit sign (blast door)")
    add("airlock", "door_leaf", (WALL + 0.04, 0, 1.1), -90, name="Sliding door")
    lamp("airlock", -4.0, -3.0, 10.0, 6.0)
    lamp("airlock", -4.0, 3.0, 10.0, 6.0)
    lamp("exit_passage", -10.0, 0.0, 6.0, 4.5)

    # Player start at the bunker entrance (exit passage, at the foot of the stairs from the
    # surface, facing the blast door); used by the fly camera and the exploration walker.
    entities.append({"id": "player_start", "name": "Player start", "parent": "bunker",
                     "transform": {"position": [-11.0, 1.65, 0.0], "rotationDegrees": [0.0, -90.0, 0.0]},
                     "camera": {"verticalFovDegrees": 70.0, "nearPlane": 0.05}})
    entities.append({"id": "environment", "name": "Bunker air", "parent": "bunker",
                     "environment": {"clearColor": [0.01, 0.01, 0.012, 1.0], "ambientColor": [0.8, 0.8, 0.82],
                                     "ambientIntensity": 0.1, "drawBackground": False},
                     "fog": {"color": [0.08, 0.08, 0.085], "density": 0.012, "startDistance": 6.0,
                             "maxOpacity": 0.4}})
    materials = {"sign_" + name: "../materials/sign_%s.material.json" % name for name in SIGNS}
    return {"version": 1, "materials": materials, "entities": entities}


# Modules without collision (fixtures above head height or flat on walls).
NON_SOLID = ("lamp", "wall_light", "exit_sign", "emergency_light", "vent", "cable_tray")


def colliders():
    """colliders.json: model-local collision boxes per module (temporary, used by the game's
    exploration mode until scene files carry Collider components). Thin decorative boxes
    (markings, screens, trims under 2 cm) are left out."""
    models = {}
    for name, boxes in MODULES.items():
        if name in VOXEL_ARCHITECTURE:
            continue
        out = []
        if name not in NON_SOLID:
            for lo, hi, _, _ in boxes:
                if min(hi[i] - lo[i] for i in range(3)) >= 0.02:
                    out.append(rnd(list(lo) + list(hi)))
        models[name] = out
    # Arch: posts, the band above the curve's springing and the crown header (approximates the
    # semicircular opening; the walkable opening stays 2.6 m wide and over 2.7 m high).
    models["airlock_arch"] = [rnd([-2.0, 0.0, -0.4, -1.3, H, 0.4]), rnd([1.3, 0.0, -0.4, 2.0, H, 0.4]),
                              rnd([-1.3, 2.7, -0.4, -0.9, H, 0.4]), rnd([0.9, 2.7, -0.4, 1.3, H, 0.4]),
                              rnd([-0.9, 3.2, -0.4, 0.9, H, 0.4])]
    return {"version": 1, "models": {k: models[k] for k in sorted(models)}}


def ceiling_lamp_prefab():
    return {"version": 1, "entities": [
        {"id": "lamp", "name": "Overhead light", "transform": {"position": [0.0, H, 0.0]},
         "modelRenderer": {"model": "lamp"}},
        {"id": "light", "name": "Overhead light source", "parent": "lamp", "transform": {"position": [0.0, -0.3, 0.0]},
         "pointLight": {"color": NEUTRAL, "intensity": 12.0, "range": 7.0}}]}


def wall_light_prefab():
    return {"version": 1, "entities": [
        {"id": "fixture", "name": "Wall light", "modelRenderer": {"model": "wall_light"}},
        {"id": "light", "name": "Wall light source", "parent": "fixture", "transform": {"position": [0.0, 2.15, 0.3]},
         "pointLight": {"color": WARM, "intensity": 2.0, "range": 3.5}}]}


def room_sign_prefab():
    # Origin: sign centre on the wall surface, facing +z.
    return {"version": 1, "materials": {"sign": "../materials/sign_dormitory.material.json"}, "entities": [
        {"id": "sign", "name": "Sign", "transform": {"rotationDegrees": [90.0, 0.0, 0.0]},
         "meshRenderer": {"mesh": "sign", "material": "sign"}}]}


def merchant_stall_prefab():
    # Origin: inner face of the back wall, facing +z (towards the corridor).
    return {"version": 1, "materials": {"sign": "../materials/sign_weapons.material.json"}, "entities": [
        {"id": "counter", "name": "Counter", "transform": {"position": [0.0, 0.0, 3.6]},
         "modelRenderer": {"model": "merchant_counter"}},
        {"id": "shelf_1", "name": "Shelf", "transform": {"position": [-2.2, 0.0, 0.3]}, "modelRenderer": {"model": "shelf"}},
        {"id": "shelf_2", "name": "Shelf", "transform": {"position": [2.2, 0.0, 0.3]}, "modelRenderer": {"model": "shelf"}},
        {"id": "sign", "name": "Sign", "transform": {"position": [0.0, 2.4, 0.02], "rotationDegrees": [90.0, 0.0, 0.0],
                                                      "scale": 2.0},
         "meshRenderer": {"mesh": "sign", "material": "sign"}}]}


def sign_material(name):
    return {"name": "sign_" + name, "shader": "zodar/pbr", "roughness": 0.8, "doubleSided": True,
            "baseColorTexture": {"path": "../textures/signs/%s.png" % name, "filter": "pixelArt"}}


def text(obj):
    return (json.dumps(obj, indent=2) + "\n").encode()


# V5: named, canonical x-fastest RLE; format owned by the engine's V4 codec.
VOXEL_ARCHITECTURE = tuple(n for n in ARCHITECTURE if n != "airlock_arch")


def bunker_structure():
    import math
    cells = {}

    def put(x, y, z, name):
        cells[(x, y, z)] = name

    for e in level(authoring=True)["entities"]:
        model = e.get("modelRenderer", {}).get("model")
        if model not in VOXEL_ARCHITECTURE:
            continue
        p = e["transform"]["position"]
        yaw = round(e["transform"].get("rotationDegrees", [0, 0, 0])[1] / 90) % 4
        if model == "stairs":
            # Four half-metre rises, each split into the built-in 0.25 m risers.
            for i in range(8):
                x = -25 - i
                h = i // 2
                for z in range(-4, 4):
                    for y in range(-1, h):
                        put(x, y, z, "floor")
                    put(x, h, z, "stairs@3" if i % 2 == 0 else "floor")
            continue
        for lo, hi, texture, _ in MODULES[model]:
            if model in ("doorway", "opening_wide") and texture == "metal":
                continue
            if model == "floor_marked" and texture == "marking":
                continue
            if texture not in ("concrete", "paint", "floor", "ceiling", "metal"):
                continue
            lo, hi = list(lo), list(hi)
            if model in ("floor", "floor_marked"):
                lo[1], hi[1] = -0.5, 0
            if model == "ceiling":
                lo[1], hi[1] = 4, 4.5
            if model == "beam":
                lo[1], hi[1] = 3.5, 4
            # Round the former thin modules onto the 0.5 m lattice, never zero-width.
            a = [math.floor(v * 2 + 0.5) for v in lo]
            b = [max(a[k] + 1, math.floor(hi[k] * 2 + 0.5)) for k in range(3)]
            for y in range(a[1], b[1]):
                for z in range(a[2], b[2]):
                    for x in range(a[0], b[0]):
                        wx, wz = x, z
                        for _ in range(yaw):
                            wx, wz = wz, -wx - 1
                        wx += round(p[0] * 2)
                        wz += round(p[2] * 2)
                        # Keep the main corridor's full four metres between wall faces.
                        if model in ("wall", "doorway", "opening_wide") and yaw == 0 and p[2] == -2:
                            wz -= 1
                        put(wx, y + round(p[1] * 2), wz, texture)
    lower = tuple(min(p[k] for p in cells) for k in range(3))
    size = tuple(max(p[k] for p in cells) - lower[k] + 1 for k in range(3))
    names = sorted({""} | set(cells.values()))
    linear = sorted((x - lower[0] + size[0] * (z - lower[2] + size[2] * (y - lower[1])), names.index(n))
                    for (x, y, z), n in cells.items())
    runs = []
    for i, n in linear:
        if runs and runs[-1][0] + runs[-1][1] == i and runs[-1][2] == n:
            runs[-1][1] += 1
        else:
            runs.append([i, 1, n])
    out = ["zodar.structure 1", " ".join(map(str, size + tuple(-v for v in lower))), str(len(names))]
    out += [json.dumps(n) for n in names]
    out += [str(len(runs))] + ["%d %d %d" % tuple(r) for r in runs]
    out += ["1", '"player_start" %d %d %d' % (-22-lower[0], -lower[1], -lower[2]), "0"]
    return ("\n".join(out) + "\n").encode()


def outputs():
    files = {"palette.png": swatch(list(MASTER)), "palettes/glow.png": swatch(list(GLOW))}
    files["structures/bunker.zstruct"] = bunker_structure()
    block_names = ("concrete", "paint", "floor", "ceiling", "metal", "marking")
    for name in block_names:
        files["textures/blocks/%s.png" % name] = png(64, 64, TEXTURES[name])
    files["blocks/bunker_blocks.blocks.json"] = text({
        "schema": "zodar.blocks", "version": 1, "uvPerMetre": 1,
        "materials": [{"name": n, "textures": "../textures/blocks/%s.png" % n,
                       "roughness": MATERIAL[n][1], "metallic": MATERIAL[n][0]} for n in block_names],
        "blocks": [{"name": n, "material": n, "structural": True} for n in block_names] +
                  [{"name": "stairs", "material": "floor", "shape": "stair", "rotations": 4,
                    "structural": True}]})
    for name, boxes in MODULES.items():
        if name in VOXEL_ARCHITECTURE:
            continue
        files["models/%s.glb" % name] = blockbench(name, boxes)
    files["models/airlock_arch.glb"] = airlock_arch()
    for name, icon in SIGNS.items():
        files["textures/signs/%s.png" % name] = png(64, 64, lambda x, y: sign(icon)(x // 2, y // 2))
        files["materials/sign_%s.material.json" % name] = text(sign_material(name))
    files["prefabs/ceiling_lamp.prefab.json"] = text(ceiling_lamp_prefab())
    files["prefabs/wall_light_unit.prefab.json"] = text(wall_light_prefab())
    files["prefabs/room_sign.prefab.json"] = text(room_sign_prefab())
    files["prefabs/merchant_stall.prefab.json"] = text(merchant_stall_prefab())
    files["levels/bunker.scene.json"] = text(level())
    files["colliders.json"] = (json.dumps(colliders(), separators=(", ", ": ")) + "\n").encode()
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
            "game/assets/materials/sign_%s.material.json" % name for name in SIGNS) + " " +
            " ".join("game/assets/prefabs/%s.prefab.json" % p
                     for p in ("ceiling_lamp", "wall_light_unit", "room_sign", "merchant_stall")))
        print("python3 tools/zodar.py import " + " ".join(models + m + ".glb" for m in ("airlock_arch",)) +
              " --class architecture")
        print("python3 tools/zodar.py import " + " ".join(models + m + ".glb" for m in GLOWING) +
              " --sub-palette glow")
        return 0
    stale = ["models/%s.glb (obsolete architecture)" % n for n in VOXEL_ARCHITECTURE
             if (args.out / ("models/%s.glb" % n)).exists()] if args.check else []
    files = outputs()
    for name, data in sorted(files.items()):
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
    print("greybox kit up to date" if args.check else "wrote %d files to %s" % (len(files), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
