#define NOMINMAX
#include "PolygonTriangulate.h"

#include <algorithm>
#include <cmath>

namespace CatalystImport {

// Returns true when all four corners turn the same way. Only a convex quad can
// be split along either diagonal; a concave one has exactly one diagonal that
// stays inside the outline.
bool QuadIsConvex(const float* p0, const float* p1, const float* p2, const float* p3) {
    const float* points[4] = {p0, p1, p2, p3};

    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
    for (int index = 0; index < 4; ++index) {
        const float* current = points[index];
        const float* next = points[(index + 1) & 3];
        normalX += (current[1] - next[1]) * (current[2] + next[2]);
        normalY += (current[2] - next[2]) * (current[0] + next[0]);
        normalZ += (current[0] - next[0]) * (current[1] + next[1]);
    }

    const float absX = fabsf(normalX);
    const float absY = fabsf(normalY);
    const float absZ = fabsf(normalZ);
    int axisU = 0;
    int axisV = 1;
    if (absX >= absY && absX >= absZ) {
        axisU = 1;
        axisV = 2;
    } else if (absY >= absX && absY >= absZ) {
        axisU = 2;
        axisV = 0;
    }

    int positiveTurns = 0;
    int negativeTurns = 0;
    for (int index = 0; index < 4; ++index) {
        const float* a = points[index];
        const float* b = points[(index + 1) & 3];
        const float* c = points[(index + 2) & 3];
        const float turn = (b[axisU] - a[axisU]) * (c[axisV] - b[axisV]) -
                           (b[axisV] - a[axisV]) * (c[axisU] - b[axisU]);
        if (turn > 0.0f) {
            ++positiveTurns;
        } else if (turn < 0.0f) {
            ++negativeTurns;
        }
    }

    return positiveTurns == 0 || negativeTurns == 0;
}

// Triangulates a polygon of four or more corners by ear clipping, writing
// exactly (count - 2) triangles. Fanning from corner zero is only valid for
// convex polygons - on a concave one it emits triangles that stick out beyond
// the polygon's own outline.
//
// Returns false when the polygon cannot be projected or clipped, leaving the
// caller to fall back to a fan.
bool TriangulatePolygon(const std::vector<ObjCorner>& polygon,
                        const std::vector<float>& positions,
                        size_t positionCount,
                        ObjCorner* target) {
    const size_t cornerCount = polygon.size();
    if (cornerCount < 4) {
        return false;
    }

    for (const ObjCorner& corner : polygon) {
        if (corner.position < 0 || static_cast<size_t>(corner.position) >= positionCount) {
            return false;
        }
    }

    auto positionAt = [&](size_t index) {
        return positions.data() + static_cast<size_t>(polygon[index].position) * 3;
    };

    // Newell's method: works on non-planar polygons too, which exporters emit
    // more often than they admit.
    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
    for (size_t index = 0; index < cornerCount; ++index) {
        const float* current = positionAt(index);
        const float* next = positionAt((index + 1) % cornerCount);
        normalX += (current[1] - next[1]) * (current[2] + next[2]);
        normalY += (current[2] - next[2]) * (current[0] + next[0]);
        normalZ += (current[0] - next[0]) * (current[1] + next[1]);
    }

    // Drop the axis the polygon faces most directly; the other two keep the
    // most area and so the most numerical headroom.
    const float absX = fabsf(normalX);
    const float absY = fabsf(normalY);
    const float absZ = fabsf(normalZ);
    int axisU = 0;
    int axisV = 1;
    float normalOnAxis = normalZ;
    if (absX >= absY && absX >= absZ) {
        axisU = 1;
        axisV = 2;
        normalOnAxis = normalX;
    } else if (absY >= absX && absY >= absZ) {
        axisU = 2;
        axisV = 0;
        normalOnAxis = normalY;
    }

    if (absX + absY + absZ <= 1e-20f) {
        return false;
    }

    std::vector<float> pointU(cornerCount);
    std::vector<float> pointV(cornerCount);
    for (size_t index = 0; index < cornerCount; ++index) {
        const float* position = positionAt(index);
        pointU[index] = position[axisU];
        pointV[index] = position[axisV];
    }

    // Ear clipping needs a counter-clockwise loop. If the projection came out
    // clockwise, walk it backwards and flip each emitted triangle so the
    // original winding survives.
    const bool reversed = (normalOnAxis < 0.0f);

    std::vector<size_t> remaining(cornerCount);
    for (size_t index = 0; index < cornerCount; ++index) {
        remaining[index] = reversed ? (cornerCount - 1 - index) : index;
    }

    auto crossAt = [&](size_t a, size_t b, size_t c) {
        return (pointU[b] - pointU[a]) * (pointV[c] - pointV[a]) -
               (pointV[b] - pointV[a]) * (pointU[c] - pointU[a]);
    };

    auto insideTriangle = [&](size_t a, size_t b, size_t c, size_t probe) {
        const float d1 = (pointU[probe] - pointU[b]) * (pointV[a] - pointV[b]) -
                         (pointU[a] - pointU[b]) * (pointV[probe] - pointV[b]);
        const float d2 = (pointU[probe] - pointU[c]) * (pointV[b] - pointV[c]) -
                         (pointU[b] - pointU[c]) * (pointV[probe] - pointV[c]);
        const float d3 = (pointU[probe] - pointU[a]) * (pointV[c] - pointV[a]) -
                         (pointU[c] - pointU[a]) * (pointV[probe] - pointV[a]);
        const bool anyNegative = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
        const bool anyPositive = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
        return !(anyNegative && anyPositive);
    };

    size_t emitted = 0;
    size_t cursor = 0;
    size_t attemptsSinceClip = 0;

    // The counting pass reserved room for exactly this many triangles. Clamping
    // here means a mistake in the clipping logic can only produce a worse
    // triangulation, never a write past the end of the corner array.
    const size_t triangleBudget = cornerCount - 2;

    auto emit = [&](size_t a, size_t b, size_t c) {
        if (emitted >= triangleBudget) {
            return;
        }
        if (reversed) {
            *target++ = polygon[c];
            *target++ = polygon[b];
            *target++ = polygon[a];
        } else {
            *target++ = polygon[a];
            *target++ = polygon[b];
            *target++ = polygon[c];
        }
        ++emitted;
    };

    while (remaining.size() > 3) {
        const size_t count = remaining.size();
        const size_t previousSlot = (cursor + count - 1) % count;
        const size_t currentSlot = cursor % count;
        const size_t nextSlot = (cursor + 1) % count;

        const size_t a = remaining[previousSlot];
        const size_t b = remaining[currentSlot];
        const size_t c = remaining[nextSlot];

        bool isEar = crossAt(a, b, c) > 0.0f;
        if (isEar) {
            for (size_t slot = 0; slot < count && isEar; ++slot) {
                const size_t probe = remaining[slot];
                if (probe == a || probe == b || probe == c) {
                    continue;
                }
                if (insideTriangle(a, b, c, probe)) {
                    isEar = false;
                }
            }
        }

        if (isEar) {
            emit(a, b, c);
            remaining.erase(remaining.begin() + static_cast<long long>(currentSlot));
            // The loop only clips while more than three corners remain, so
            // the list is never empty here.
            cursor = currentSlot % remaining.size();
            attemptsSinceClip = 0;
            continue;
        }

        cursor = nextSlot;
        ++attemptsSinceClip;
        if (attemptsSinceClip > count) {
            // Self-intersecting or fully degenerate: fan whatever is left so the
            // triangle count still matches what the counting pass reserved.
            // Fanning m corners emits exactly m - 2 triangles, which completes
            // the polygon - so clear the list rather than leaving three corners
            // for the tail below to emit a fourth time and run past the buffer.
            for (size_t index = 1; index + 1 < remaining.size(); ++index) {
                emit(remaining[0], remaining[index], remaining[index + 1]);
            }
            remaining.clear();
            break;
        }
    }

    if (remaining.size() == 3) {
        emit(remaining[0], remaining[1], remaining[2]);
    }

    return emitted == cornerCount - 2;
}

}
