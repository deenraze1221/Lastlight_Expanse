// Built-in voxel shapes (docs/guide/VOXELS.md §3). Geometry in cell-local space [0, 1]^3, at
// rotation 0: sloped and stepped shapes rise toward +Z, panel_thin lies against the -Z face and
// curved pieces bend about the X axis. Engine-generic: games register more shapes.
#include "Scene/Voxel/BlockRegistry.h"

#include <utility>

namespace zodar::voxel {
namespace {

using V = glm::vec3;

// Axis-aligned rectangle on plane `axis` = c, spanning [u0, u1] x [v0, v1] along the mesher's
// face axes u = (axis + 1) % 3, v = (axis + 2) % 3, facing +axis or -axis.
ShapePolygon rect(int axis, bool positive, float c, float u0, float u1, float v0, float v1) {
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    const float cu[4] = {u0, u1, u1, u0};
    const float cv[4] = {v0, v0, v1, v1};
    ShapePolygon p;
    for (int i = 0; i < 4; ++i) {
        V q;
        q[axis] = c;
        q[u] = cu[i];
        q[v] = cv[i];
        p.corners.push_back(q);
    }
    if (!positive) std::swap(p.corners[1], p.corners[3]);
    return p;
}

void addBox(ShapeTemplate& t, const V& lo, const V& hi) {
    for (int a = 0; a < 3; ++a) {
        const int u = (a + 1) % 3;
        const int v = (a + 2) % 3;
        t.polygons.push_back(rect(a, false, lo[a], lo[u], hi[u], lo[v], hi[v]));
        t.polygons.push_back(rect(a, true, hi[a], lo[u], hi[u], lo[v], hi[v]));
    }
    t.collision.push_back({lo, hi});
}

ShapeTemplate boxShape(const char* name, const V& lo, const V& hi, bool aoOccluder) {
    ShapeTemplate t;
    t.name = name;
    t.aoOccluder = aoOccluder;
    addBox(t, lo, hi);
    return t;
}

// Axis 0 = X (u = Y, v = Z), axis 1 = Y (u = Z, v = X), axis 2 = Z (u = X, v = Y).
ShapeTemplate stair() {
    ShapeTemplate t;
    t.name = "stair"; // two risers of half a cell (0.25 m at 0.5 m cells)
    t.aoOccluder = true;
    t.polygons = {
        rect(1, false, 0.0f, 0, 1, 0, 1),      // bottom
        rect(2, true, 1.0f, 0, 1, 0, 1),       // back
        rect(2, false, 0.0f, 0, 1, 0, 0.5f),   // front (lower riser)
        rect(1, true, 0.5f, 0, 0.5f, 0, 1),    // lower tread
        rect(2, false, 0.5f, 0, 1, 0.5f, 1),   // upper riser
        rect(1, true, 1.0f, 0.5f, 1, 0, 1),    // upper tread
        rect(0, false, 0.0f, 0, 0.5f, 0, 1),   // -X side, lower part
        rect(0, false, 0.0f, 0.5f, 1, 0.5f, 1), // -X side, upper part
        rect(0, true, 1.0f, 0, 0.5f, 0, 1),    // +X side, lower part
        rect(0, true, 1.0f, 0.5f, 1, 0.5f, 1), // +X side, upper part
    };
    t.collision = {{V(0, 0, 0), V(1, 0.5f, 1)}, {V(0, 0.5f, 0.5f), V(1, 1, 1)}};
    return t;
}

// Slope from the bottom of the -Z face to the top of the +Z face.
ShapeTemplate wedge() {
    ShapeTemplate t;
    t.name = "wedge";
    t.polygons = {
        rect(1, false, 0.0f, 0, 1, 0, 1),
        rect(2, true, 1.0f, 0, 1, 0, 1),
        {{V(0, 0, 0), V(0, 1, 1), V(1, 1, 1), V(1, 0, 0)}}, // slope
        {{V(0, 0, 0), V(0, 0, 1), V(0, 1, 1)}},             // -X
        {{V(1, 0, 0), V(1, 1, 1), V(1, 0, 1)}},             // +X
    };
    t.collision = {{V(0, 0, 0.25f), V(1, 0.5f, 1)}, {V(0, 0.5f, 0.75f), V(1, 1, 1)}};
    return t;
}

// Concave corner of two slopes: height = max(x, z).
ShapeTemplate cornerInner() {
    ShapeTemplate t;
    t.name = "corner_inner";
    t.polygons = {
        rect(1, false, 0.0f, 0, 1, 0, 1),
        rect(0, true, 1.0f, 0, 1, 0, 1),
        rect(2, true, 1.0f, 0, 1, 0, 1),
        {{V(0, 0, 0), V(0, 0, 1), V(0, 1, 1)}}, // -X
        {{V(0, 0, 0), V(1, 1, 0), V(1, 0, 0)}}, // -Z
        {{V(0, 0, 0), V(1, 1, 1), V(1, 1, 0)}}, // slope facing -X
        {{V(0, 0, 0), V(0, 1, 1), V(1, 1, 1)}}, // slope facing -Z
    };
    t.collision = {{V(0.75f, 0, 0), V(1, 1, 1)},
                   {V(0, 0, 0.75f), V(0.75f, 1, 1)},
                   {V(0.25f, 0, 0), V(0.75f, 0.5f, 0.75f)},
                   {V(0, 0, 0.25f), V(0.25f, 0.5f, 0.75f)}};
    return t;
}

// Convex corner of two slopes: height = min(x, z).
ShapeTemplate cornerOuter() {
    ShapeTemplate t;
    t.name = "corner_outer";
    t.polygons = {
        rect(1, false, 0.0f, 0, 1, 0, 1),
        {{V(1, 0, 0), V(1, 1, 1), V(1, 0, 1)}}, // +X
        {{V(0, 0, 1), V(1, 0, 1), V(1, 1, 1)}}, // +Z
        {{V(0, 0, 0), V(0, 0, 1), V(1, 1, 1)}}, // slope facing -X
        {{V(0, 0, 0), V(1, 1, 1), V(1, 0, 0)}}, // slope facing -Z
    };
    t.collision = {{V(0.25f, 0, 0.25f), V(1, 0.5f, 1)}, {V(0.75f, 0.5f, 0.75f), V(1, 1, 1)}};
    return t;
}

// Quarter circle of radius 1 about the cell edge y = 0, z = 0, in 4 segments. Point k at angle
// k * 22.5 degrees from +Z toward +Y: (y, z) = (sin, cos). Exact literals keep output libm-free.
constexpr float kArcY[5] = {0.0f, 0.38268343f, 0.70710678f, 0.92387953f, 1.0f};
constexpr float kArcZ[5] = {1.0f, 0.92387953f, 0.70710678f, 0.38268343f, 0.0f};

// Convex rounded edge: solid inside the quarter circle (bulges toward +Y +Z).
ShapeTemplate quarterRound() {
    ShapeTemplate t;
    t.name = "quarter_round";
    t.aoOccluder = true; // fill 6/8
    t.polygons = {rect(1, false, 0.0f, 0, 1, 0, 1), rect(2, false, 0.0f, 0, 1, 0, 1)};
    ShapePolygon posX{{V(1, 0, 0)}};
    ShapePolygon negX{{V(0, 0, 0)}};
    for (int k = 4; k >= 0; --k) posX.corners.push_back(V(1, kArcY[k], kArcZ[k]));
    for (int k = 0; k <= 4; ++k) negX.corners.push_back(V(0, kArcY[k], kArcZ[k]));
    t.polygons.push_back(posX);
    t.polygons.push_back(negX);
    for (int k = 0; k < 4; ++k) {
        t.polygons.push_back({{V(0, kArcY[k], kArcZ[k]), V(1, kArcY[k], kArcZ[k]), V(1, kArcY[k + 1], kArcZ[k + 1]),
                               V(0, kArcY[k + 1], kArcZ[k + 1])}});
    }
    t.collision = {{V(0, 0, 0), V(1, 0.5f, 0.875f)}, {V(0, 0.5f, 0), V(1, 0.875f, 0.5f)}};
    return t;
}

// Concave arch piece: the cell minus the quarter circle (solid toward +Y +Z).
ShapeTemplate quarterRoundInner() {
    ShapeTemplate t;
    t.name = "quarter_round_inner";
    t.polygons = {rect(1, true, 1.0f, 0, 1, 0, 1), rect(2, true, 1.0f, 0, 1, 0, 1)};
    ShapePolygon posX{{V(1, 1, 1)}};
    ShapePolygon negX{{V(0, 1, 1)}};
    for (int k = 0; k <= 4; ++k) posX.corners.push_back(V(1, kArcY[k], kArcZ[k]));
    for (int k = 4; k >= 0; --k) negX.corners.push_back(V(0, kArcY[k], kArcZ[k]));
    t.polygons.push_back(posX);
    t.polygons.push_back(negX);
    for (int k = 0; k < 4; ++k) {
        t.polygons.push_back({{V(0, kArcY[k + 1], kArcZ[k + 1]), V(1, kArcY[k + 1], kArcZ[k + 1]),
                               V(1, kArcY[k], kArcZ[k]), V(0, kArcY[k], kArcZ[k])}});
    }
    t.collision = {{V(0, 0.875f, 0), V(1, 1, 1)}, {V(0, 0, 0.875f), V(1, 1, 1)}, {V(0, 0.75f, 0.75f), V(1, 1, 1)}};
    return t;
}

} // namespace

ShapeLibrary ShapeLibrary::builtins() {
    ShapeLibrary lib;
    lib.add(boxShape("slab_low", V(0, 0, 0), V(1, 0.5f, 1), false));
    lib.add(boxShape("slab_high", V(0, 0.5f, 0), V(1, 1, 1), false));
    lib.add(stair());
    lib.add(wedge());
    lib.add(cornerInner());
    lib.add(cornerOuter());
    lib.add(quarterRound());
    lib.add(quarterRoundInner());
    lib.add(boxShape("panel_thin", V(0, 0, 0), V(1, 1, 0.125f), false)); // 1/8-cell plate on -Z
    return lib;
}

} // namespace zodar::voxel
