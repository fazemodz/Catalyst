#pragma once
#include <cstdint>
#include <vector>

#include "FastObjParser.h"

namespace CatalystImport {

// Returns true when all four corners of a quad turn the same way. Only a convex
// quad can be split along either diagonal; a concave one has exactly one
// diagonal that stays inside the outline.
bool QuadIsConvex(const float* p0, const float* p1, const float* p2, const float* p3);

// Triangulates a polygon of four or more corners by ear clipping, writing
// exactly (corners - 2) triangles to target. Fanning from corner zero is only
// valid for a convex polygon - on a concave one it emits triangles that stick
// out past the outline.
//
// Returns false when the polygon cannot be projected or clipped, leaving the
// caller to fall back to a fan. Shared by the OBJ and FBX readers, because both
// formats can hand over arbitrary polygons.
bool TriangulatePolygon(const std::vector<ObjCorner>& polygon,
                        const std::vector<float>& positions,
                        size_t positionCount,
                        ObjCorner* target);

}
