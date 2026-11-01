#!/usr/bin/env python3
"""Generates the glTF tool fixtures (Phase D1). Python standard library only, deterministic.

The fixtures imitate the structure of real exporter output so the engine importer can be
tested without the tools installed:

  valid/blockbench_crate.glb            Blockbench-style: images in the binary buffer
  valid/blockbench_crate_embedded.gltf  same model, buffer and images as data: URIs
  valid/blender_wall.glb                Blender-style (Khronos glTF Blender I/O)
  rejected/draco.glb                    KHR_draco_mesh_compression required
  rejected/webp.glb                     EXT_texture_webp required
  rejected/quantization.glb             KHR_mesh_quantization required

Blockbench traits: generator "Blockbench ...", several textures per model (one primitive per
texture), node hierarchy with pivots (group nodes carry the pivot translation, cube vertices
are relative to it), per-face UVs (4 vertices per face), NEAREST samplers, an animation,
clashing node/mesh names ("cube" twice) and names with spaces.

Blender traits: generator "Khronos glTF Blender I/O ...", Principled BSDF metallic-roughness
materials, KHR_materials_emissive_strength, KHR_materials_specular/ior/transmission, vertex
colours (normalized unsigned short), a second UV set, tangents, smooth normals and a
non-uniform node scale.

Usage:
  python3 make_fixtures.py           write the fixtures next to this script
  python3 make_fixtures.py --check   exit 1 if a committed fixture differs from the output
  python3 make_fixtures.py --out DIR write into DIR instead

The registry next to the fixtures (assets.json) is written by the asset tool, not by this
script: after regenerating, run `python3 tools/zodar.py import engine/tests/data/gltf_tools/valid`.
"""

import argparse
import base64
import json
import math
import struct
import sys
import zlib
from pathlib import Path

# Muted test palette (also listed in assets.json "palette"). Index -> (r, g, b).
PALETTE = [
    (0x3b, 0x3a, 0x36),  # 0 dark grey
    (0x5c, 0x5a, 0x52),  # 1 grey
    (0x7d, 0x7a, 0x6e),  # 2 light grey
    (0x6b, 0x4f, 0x3a),  # 3 brown
    (0x8a, 0x6a, 0x4c),  # 4 light brown
    (0x4f, 0x5b, 0x4a),  # 5 olive
    (0xc8, 0xa0, 0x50),  # 6 lamp yellow
    (0x9e, 0x9a, 0x88),  # 7 pale
]

NEAREST = 9728
NEAREST_MIPMAP_LINEAR = 9986
REPEAT = 10497
FLOAT = 5126
USHORT = 5123
SHORT = 5122
UINT = 5125


# --- PNG (stored deflate blocks: identical bytes on every zlib version) --------------------

def png(width, height, texel):
    """texel(x, y) -> palette index. RGBA8, fully opaque."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter: none
        for x in range(width):
            r, g, b = PALETTE[texel(x, y)]
            rows += bytes((r, g, b, 255))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 0)) +
            chunk(b"IEND", b""))


def planks(x, y):
    # Horizontal planks with dark seams every 8 rows and a frame.
    if x in (0, 31) or y in (0, 31):
        return 3
    if y % 8 == 0:
        return 0
    return 4 if (x // 8 + y // 8) % 2 else 3


def metal(x, y):
    return 1 if (x + y) % 4 else 2


def concrete(x, y):
    if y % 16 == 0 or (x + (y // 16) * 8) % 16 == 0:
        return 0
    return 7 if (x * 7 + y * 3) % 11 == 0 else 2


def lamp(x, y):
    edge = x in (0, 15) or y in (0, 15)
    return 1 if edge else 6


# --- Buffer builder --------------------------------------------------------------------

class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def _align(self):
        while len(self.bin) % 4:
            self.bin.append(0)

    def view(self, data, target=None):
        self._align()
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, values, kind, component=FLOAT, normalized=False, minmax=False, target=34962):
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        fmt = {FLOAT: "f", USHORT: "H", SHORT: "h", UINT: "I"}[component]
        flat = [c for v in values for c in (v if isinstance(v, (tuple, list)) else (v,))]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        a = {"bufferView": self.view(data, target), "componentType": component, "count": len(values), "type": kind}
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"] = [min(v[i] for v in values) for i in range(width)]
            a["max"] = [max(v[i] for v in values) for i in range(width)]
        self.accessors.append(a)
        return len(self.accessors) - 1

    def image(self, data):
        return self.view(data)


def r6(v):
    """Round to 6 decimals so the JSON is stable across platforms."""
    return tuple(round(c, 6) + 0.0 for c in v)


# Box faces: (normal, u axis, v axis). Each face gets 4 own vertices (per-face UVs).
FACES = {
    "+x": ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    "-x": ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    "+y": ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    "-y": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    "+z": ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    "-z": ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
}


def box_face(lo, hi, face, uv_rect):
    """Quad of an axis-aligned box: positions, normals, uvs, 6 indices (counter-clockwise)."""
    n, u, v = FACES[face]
    centre = [(lo[i] + hi[i]) / 2 for i in range(3)]
    half = [(hi[i] - lo[i]) / 2 for i in range(3)]
    c = [centre[i] + n[i] * half[i] for i in range(3)]
    hu = [u[i] * half[i] for i in range(3)]
    hv = [v[i] * half[i] for i in range(3)]
    corners = []
    for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        corners.append(r6([c[i] + su * hu[i] + sv * hv[i] for i in range(3)]))
    u0, v0, u1, v1 = uv_rect
    uvs = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]  # glTF: v down
    return corners, [n] * 4, uvs


def primitive(builder, faces, material, extra=None):
    """faces: list of (lo, hi, face, uv_rect). Returns a glTF primitive."""
    pos, nor, uv, idx = [], [], [], []
    for lo, hi, face, rect in faces:
        p, n, t = box_face(lo, hi, face, rect)
        base = len(pos)
        pos += p
        nor += [tuple(float(c) for c in x) for x in n]
        uv += [tuple(float(c) for c in x) for x in t]
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    attributes = {
        "POSITION": builder.accessor(pos, "VEC3", minmax=True),
        "NORMAL": builder.accessor(nor, "VEC3"),
        "TEXCOORD_0": builder.accessor(uv, "VEC2"),
    }
    if extra:
        attributes.update(extra(builder, pos, nor, uv))
    indices = builder.accessor(idx, "SCALAR", component=USHORT, target=34963)
    return {"attributes": attributes, "indices": indices, "material": material}


# --- Blockbench-style crate ----------------------------------------------------------------

def blockbench_crate(embedded):
    b = Builder()
    full = (0.0, 0.0, 1.0, 1.0)
    body_lo, body_hi = (-0.5, -0.5, -0.5), (0.5, 0.5, 0.5)
    lid_lo, lid_hi = (-0.5, 0.0, 0.0), (0.5, 0.125, 1.0)
    # Body: sides use texture "crate" (32x32 on 1 m faces), top/bottom "metal band" (16x16).
    body_sides = primitive(b, [(body_lo, body_hi, f, full) for f in ("+x", "-x", "+z", "-z")], 0)
    body_caps = primitive(b, [(body_lo, body_hi, f, full) for f in ("+y", "-y")], 1)
    # Lid: thin plate; top uses the whole metal texture, edges thin strips of it.
    strip = (0.0, 0.0, 1.0, 0.125)
    lid = primitive(b, [(lid_lo, lid_hi, "+y", full), (lid_lo, lid_hi, "-y", full)] +
                    [(lid_lo, lid_hi, f, strip) for f in ("+x", "-x", "+z", "-z")], 1)

    # Lid animation (Blockbench exports animations; ignored by the importer).
    times = b.accessor([0.0, 1.0], "SCALAR", minmax=False, target=None)
    b.accessors[times]["min"], b.accessors[times]["max"] = [0.0], [1.0]
    half = math.sin(math.radians(-30) / 2), math.cos(math.radians(-30) / 2)
    rotations = b.accessor([(0.0, 0.0, 0.0, 1.0), r6((half[0], 0.0, 0.0, half[1]))], "VEC4", target=None)

    images = []
    for name, data in (("crate", png(32, 32, planks)), ("metal band", png(16, 16, metal))):
        if embedded:
            images.append({"name": name, "uri": "data:image/png;base64," + base64.b64encode(data).decode()})
        else:
            images.append({"name": name, "mimeType": "image/png", "bufferView": b.image(data)})

    def material(name, texture):
        return {"name": name, "pbrMetallicRoughness": {"baseColorTexture": {"index": texture},
                                                       "metallicFactor": 0, "roughnessFactor": 1},
                "alphaMode": "MASK", "alphaCutoff": 0.05}

    gltf = {
        "asset": {"version": "2.0", "generator": "Blockbench 4.11.2 glTF exporter"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "crate", "children": [1, 3]},
            {"name": "body", "translation": [0, 0.5, 0], "children": [2]},
            {"name": "cube", "mesh": 0},
            {"name": "lid group", "translation": [0, 1.0, -0.5], "children": [4]},
            {"name": "cube", "mesh": 1},
        ],
        "meshes": [
            {"name": "cube", "primitives": [body_sides, body_caps]},
            {"name": "cube", "primitives": [lid]},
        ],
        "materials": [material("crate", 0), material("metal band", 1)],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}],
        "samplers": [{"magFilter": NEAREST, "minFilter": NEAREST, "wrapS": REPEAT, "wrapT": REPEAT}],
        "images": images,
        "animations": [{"name": "open lid",
                        "samplers": [{"input": times, "output": rotations, "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0, "target": {"node": 3, "path": "rotation"}}]}],
        "accessors": b.accessors,
        "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.bin)}],
    }
    if embedded:
        gltf["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(bytes(b.bin)).decode()
        return (json.dumps(gltf, indent=1) + "\n").encode()
    return glb(gltf, b.bin)


# --- Blender-style wall --------------------------------------------------------------------

def blender_extras(builder, pos, nor, uv):
    """Tangents, second UV set and vertex colours, as the Blender exporter writes them."""
    tangents = []
    for n in nor:
        # Any unit vector perpendicular to n, handedness +1.
        t = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 0.0, 1.0)
        d = sum(t[i] * n[i] for i in range(3))
        t = [t[i] - d * n[i] for i in range(3)]
        length = math.sqrt(sum(c * c for c in t))
        tangents.append(r6([c / length for c in t] + [1.0]))
    lightmap = [r6((u * 0.5, v * 0.5)) for u, v in uv]
    colours = [(65535, 65535, 65535, 65535)] * len(pos)  # white: shading unchanged
    return {
        "TANGENT": builder.accessor(tangents, "VEC4"),
        "TEXCOORD_1": builder.accessor(lightmap, "VEC2"),
        "COLOR_0": builder.accessor(colours, "VEC4", component=USHORT, normalized=True),
    }


def wall_front(builder):
    """Front face as a 3x3 vertex grid with smooth (averaged, slightly bulging) normals."""
    pos, nor, uv = [], [], []
    for j in range(3):
        for i in range(3):
            x, y = -1.0 + i, j * 1.0
            pos.append(r6((x, y, 0.125)))
            n = (x * 0.2, (y - 1.0) * 0.2, 1.0)
            length = math.sqrt(sum(c * c for c in n))
            nor.append(r6([c / length for c in n]))
            uv.append(r6((i * 1.0, 1.5 - j * 0.75)))  # 2 x 1.5 texture repeats over 4 x 3 m
    idx = []
    for j in range(2):
        for i in range(2):
            a = j * 3 + i
            idx += [a, a + 1, a + 4, a, a + 4, a + 3]
    return pos, nor, uv, idx


def blender_wall():
    b = Builder()
    pos, nor, uv, idx = wall_front(b)
    # Back face: 4 vertices, flat normal.
    base = len(pos)
    for x, y in ((1.0, 0.0), (-1.0, 0.0), (-1.0, 2.0), (1.0, 2.0)):
        pos.append((x, y, -0.125))
        nor.append((0.0, 0.0, -1.0))
        uv.append(r6(((1.0 - x), 1.5 - y * 0.75)))
    idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    attributes = {
        "POSITION": b.accessor(pos, "VEC3", minmax=True),
        "NORMAL": b.accessor(nor, "VEC3"),
        "TEXCOORD_0": b.accessor(uv, "VEC2"),
    }
    attributes.update(blender_extras(b, pos, nor, uv))
    wall = {"attributes": attributes, "indices": b.accessor(idx, "SCALAR", component=USHORT, target=34963),
            "material": 0}

    lo, hi = (-0.125, -0.125, -0.125), (0.125, 0.125, 0.125)
    lamp_prim = primitive(b, [(lo, hi, f, (0.0, 0.0, 1.0, 1.0)) for f in FACES], 1, extra=blender_extras)

    images = [
        {"name": "concrete_wall", "mimeType": "image/png", "bufferView": b.image(png(64, 64, concrete))},
        {"name": "lamp", "mimeType": "image/png", "bufferView": b.image(png(16, 16, lamp))},
    ]
    gltf = {
        "asset": {"generator": "Khronos glTF Blender I/O v4.2.83", "version": "2.0"},
        "extensionsUsed": ["KHR_materials_specular", "KHR_materials_ior", "KHR_materials_transmission",
                           "KHR_materials_emissive_strength"],
        "scene": 0,
        "scenes": [{"name": "Scene", "nodes": [0]}],
        "nodes": [
            {"name": "Wall", "mesh": 0, "scale": [2.0, 1.5, 1.0], "children": [1]},
            {"name": "Lamp", "mesh": 1, "translation": [0.0, 1.5, 0.25]},
        ],
        "materials": [
            {"name": "Concrete",
             "extensions": {"KHR_materials_specular": {"specularFactor": 0.5},
                            "KHR_materials_ior": {"ior": 1.45}},
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                      "roughnessFactor": 0.9}},
            {"name": "Lamp Glass",
             "emissiveFactor": [1.0, 0.8, 0.4],
             "emissiveTexture": {"index": 1},
             "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0},
                            "KHR_materials_transmission": {"transmissionFactor": 0.5}},
             "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "metallicFactor": 0.0,
                                      "roughnessFactor": 0.5}},
        ],
        "meshes": [{"name": "Wall", "primitives": [wall]}, {"name": "Cube.001", "primitives": [lamp_prim]}],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}],
        "samplers": [{"magFilter": NEAREST, "minFilter": NEAREST_MIPMAP_LINEAR}],
        "images": images,
        "accessors": b.accessors,
        "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.bin)}],
    }
    return glb(gltf, b.bin)


# --- Rejected exports ------------------------------------------------------------------

def triangle(b):
    return {"POSITION": b.accessor([(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)], "VEC3", minmax=True)}


def rejected(extension):
    b = Builder()
    gltf = {"asset": {"generator": "Khronos glTF Blender I/O v4.2.83", "version": "2.0"},
            "extensionsUsed": [extension], "extensionsRequired": [extension],
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "Cube", "mesh": 0}]}
    prim = {"attributes": {}}
    if extension == "KHR_draco_mesh_compression":
        prim["attributes"] = triangle(b)
        prim["extensions"] = {extension: {"bufferView": b.view(b"DRACO\0\0\0"), "attributes": {"POSITION": 0}}}
    elif extension == "KHR_mesh_quantization":
        quantized = b.accessor([(0, 0, 0), (16383, 0, 0), (0, 16383, 0)], "VEC3", component=SHORT)
        b.accessors[quantized]["min"], b.accessors[quantized]["max"] = [0, 0, 0], [16383, 16383, 0]
        prim["attributes"] = {"POSITION": quantized}
    elif extension == "EXT_texture_webp":
        prim["attributes"] = triangle(b)
        prim["material"] = 0
        gltf["materials"] = [{"name": "Material", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
        gltf["textures"] = [{"extensions": {extension: {"source": 0}}}]
        gltf["images"] = [{"name": "Image", "mimeType": "image/webp", "bufferView": b.image(b"RIFF\0\0\0\0WEBP")}]
    gltf["meshes"] = [{"name": "Cube", "primitives": [prim]}]
    gltf["accessors"] = b.accessors
    gltf["bufferViews"] = b.views
    gltf["buffers"] = [{"byteLength": len(b.bin)}]
    return glb(gltf, b.bin)


def glb(gltf, binary):
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = bytes(binary) + b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<I4s", len(text), b"JSON") + text +
            struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def fixtures():
    return {
        "valid/blockbench_crate.glb": blockbench_crate(embedded=False),
        "valid/blockbench_crate_embedded.gltf": blockbench_crate(embedded=True),
        "valid/blender_wall.glb": blender_wall(),
        "rejected/draco.glb": rejected("KHR_draco_mesh_compression"),
        "rejected/webp.glb": rejected("EXT_texture_webp"),
        "rejected/quantization.glb": rejected("KHR_mesh_quantization"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="compare with the files on disk, write nothing")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    stale = []
    for name, data in fixtures().items():
        path = args.out / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                stale.append(name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print("wrote %s (%d bytes)" % (path, len(data)))
    if stale:
        print("stale fixtures (run make_fixtures.py): " + ", ".join(stale))
        return 1
    if args.check:
        print("fixtures up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
