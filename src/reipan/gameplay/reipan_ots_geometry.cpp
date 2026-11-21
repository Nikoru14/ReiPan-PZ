/* ==========================================================================
 *  reipan_ots_geometry.cpp  (the scenery the over-the-shoulder camera hits)
 *
 *  See reipan_ots_geometry.h.  Each mesh's triangles are decoded once, in its
 *  local space; a frame's draws keep the mesh, its world matrix and the
 *  inverse, and its world bounds.  A cast first tests the bounds, then only
 *  the triangles of the pieces it crosses, in their local space.
 * ======================================================================== */

#include "reipan_ots_geometry.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <vector>

namespace
{

struct Mesh
{
    const void        *top;
    std::vector<float> tris;   /* 9 floats a triangle, local space */
    float              lmin[3];
    float              lmax[3];
};

struct Piece
{
    const Mesh *mesh;
    float       m[4][4];       /* local to world (PS2 layout) */
    float       inv[3][3];     /* inverse of the 3x3 part */
    float       wmin[3];
    float       wmax[3];
};

/* Too many distinct meshes means the loaded data changed under us. */
constexpr size_t kMaxMeshes = 40000;
constexpr size_t kMaxPieces = 8000;

std::unordered_map<uintptr_t, Mesh> g_meshes;
std::vector<Piece>                  g_pieces[2];
int                                 g_read = 0;
int                                 g_enabled = 0;
int                                 g_scenery = 0;
int                                 g_suspend = 0;

void ToWorld(const float m[4][4], const float in[3], float out[3])
{
    for (int axis = 0; axis < 3; axis++)
    {
        out[axis] = m[0][axis] * in[0] + m[1][axis] * in[1] +
                    m[2][axis] * in[2] + m[3][axis];
    }
}

/* local = inv * (world - translation), inv the inverse of the 3x3 part
 * (whose columns are the matrix's rows 0-2).  False when it is singular. */
bool Invert(const float m[4][4], float inv[3][3])
{
    float a[3][3];
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            a[r][c] = m[c][r];
        }
    }
    const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                      a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                      a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (!(fabsf(det) >= 1e-12f))
    {
        return false;
    }
    const float s = 1.0f / det;
    inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * s;
    inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * s;
    inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * s;
    inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * s;
    inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * s;
    inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * s;
    inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * s;
    inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * s;
    inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * s;
    return true;
}

void ToLocal(const Piece &piece, const float in[3], float out[3])
{
    const float d[3] = {in[0] - piece.m[3][0], in[1] - piece.m[3][1],
                        in[2] - piece.m[3][2]};
    for (int r = 0; r < 3; r++)
    {
        out[r] = piece.inv[r][0] * d[0] + piece.inv[r][1] * d[1] +
                 piece.inv[r][2] * d[2];
    }
}

/* Does the segment from + dir * [0, length] pass the box grown by `grow`? */
bool SegmentCrossesBox(const float from[3], const float dir[3], float length,
                       const float bmin[3], const float bmax[3], float grow)
{
    float t0 = 0.0f;
    float t1 = length;
    for (int axis = 0; axis < 3; axis++)
    {
        const float lo = bmin[axis] - grow;
        const float hi = bmax[axis] + grow;
        if (fabsf(dir[axis]) < 1e-6f)
        {
            if (from[axis] < lo || from[axis] > hi)
            {
                return false;
            }
            continue;
        }
        float a = (lo - from[axis]) / dir[axis];
        float b = (hi - from[axis]) / dir[axis];
        if (a > b)
        {
            const float t = a;
            a = b;
            b = t;
        }
        t0 = a > t0 ? a : t0;
        t1 = b < t1 ? b : t1;
        if (t0 > t1)
        {
            return false;
        }
    }
    return true;
}

/* Smallest t in [0, best) where the segment p + d * t crosses a triangle of
 * the mesh (either face), or best. */
float CastMesh(const Mesh &mesh, const float p[3], const float d[3],
               float best, int *tested)
{
    const float *t = mesh.tris.data();
    const size_t n = mesh.tris.size() / 9;
    for (size_t i = 0; i < n; i++, t += 9)
    {
        const float e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const float e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const float h[3] = {d[1] * e2[2] - d[2] * e2[1],
                            d[2] * e2[0] - d[0] * e2[2],
                            d[0] * e2[1] - d[1] * e2[0]};
        const float a = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (fabsf(a) < 1e-12f)
        {
            continue;
        }
        const float f = 1.0f / a;
        const float s[3] = {p[0] - t[0], p[1] - t[1], p[2] - t[2]};
        const float u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }
        const float q[3] = {s[1] * e1[2] - s[2] * e1[1],
                            s[2] * e1[0] - s[0] * e1[2],
                            s[0] * e1[1] - s[1] * e1[0]};
        const float v = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
        if (v < 0.0f || u + v > 1.0f)
        {
            continue;
        }
        const float hit = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
        if (hit >= 0.0f && hit < best)
        {
            best = hit;
        }
    }
    *tested += (int)n;
    return best;
}

Mesh *FindMesh(const void *key, const void *top)
{
    auto found = g_meshes.find((uintptr_t)key);
    if (found != g_meshes.end() && found->second.top == top)
    {
        return &found->second;
    }
    return nullptr;
}

} // namespace

extern "C" {

int ReiPan_OtsGeomCollecting(void)
{
    return g_enabled && g_scenery > 0 && g_suspend == 0;
}

void ReiPan_OtsGeomSetEnabled(int on)
{
    g_enabled = on ? 1 : 0;
}

void ReiPan_OtsGeomBeginScenery(void)
{
    g_scenery++;
}

void ReiPan_OtsGeomEndScenery(void)
{
    if (g_scenery > 0)
    {
        g_scenery--;
    }
}

void ReiPan_OtsGeomSuspend(void)
{
    g_suspend++;
}

void ReiPan_OtsGeomResume(void)
{
    if (g_suspend > 0)
    {
        g_suspend--;
    }
}

int ReiPan_OtsGeomHasMesh(const void *key, const void *top)
{
    return FindMesh(key, top) != nullptr;
}

void ReiPan_OtsGeomAddMesh(const void *key, const void *top,
                           const float *positions, int vertex_count,
                           const unsigned int *indices, int index_count)
{
    if (key == nullptr)
    {
        return;
    }
    /* No usable triangles: kept as an empty mesh, so it is not decoded again
     * every frame. */
    if (positions == nullptr || indices == nullptr || vertex_count <= 0)
    {
        index_count = 0;
    }

    if (g_meshes.size() >= kMaxMeshes)
    {
        /* The pieces still point into the old meshes. */
        g_meshes.clear();
        g_pieces[0].clear();
        g_pieces[1].clear();
    }

    Mesh &mesh = g_meshes[(uintptr_t)key];
    mesh.top = top;
    mesh.tris.clear();
    mesh.tris.reserve((size_t)(index_count / 3) * 9);

    bool first = true;
    for (int i = 0; i + 2 < index_count; i += 3)
    {
        const unsigned int idx[3] = {indices[i], indices[i + 1], indices[i + 2]};
        if (idx[0] >= (unsigned int)vertex_count ||
            idx[1] >= (unsigned int)vertex_count ||
            idx[2] >= (unsigned int)vertex_count)
        {
            continue;
        }
        float v[9];
        bool  finite = true;
        for (int k = 0; k < 3; k++)
        {
            const float *p = positions + (size_t)idx[k] * 3;
            for (int axis = 0; axis < 3; axis++)
            {
                v[k * 3 + axis] = p[axis];
                finite = finite && isfinite(p[axis]);
            }
        }
        if (!finite)
        {
            continue;
        }
        for (int k = 0; k < 9; k++)
        {
            const int axis = k % 3;
            mesh.tris.push_back(v[k]);
            if (first)
            {
                mesh.lmin[axis] = mesh.lmax[axis] = v[k];
            }
            else
            {
                mesh.lmin[axis] = v[k] < mesh.lmin[axis] ? v[k] : mesh.lmin[axis];
                mesh.lmax[axis] = v[k] > mesh.lmax[axis] ? v[k] : mesh.lmax[axis];
            }
            if (axis == 2)
            {
                first = false;
            }
        }
    }
    if (first)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            mesh.lmin[axis] = mesh.lmax[axis] = 0.0f;
        }
    }
}

void ReiPan_OtsGeomForgetOwner(const void *top)
{
    if (top == nullptr || g_meshes.empty())
    {
        return;
    }
    for (std::vector<Piece> &pieces : g_pieces)
    {
        size_t keep = 0;
        for (size_t i = 0; i < pieces.size(); i++)
        {
            if (pieces[i].mesh->top != top)
            {
                pieces[keep++] = pieces[i];
            }
        }
        pieces.resize(keep);
    }
    for (auto it = g_meshes.begin(); it != g_meshes.end();)
    {
        if (it->second.top == top)
        {
            it = g_meshes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ReiPan_OtsGeomRecordDraw(const void *key, const void *top,
                              const float model[4][4])
{
    std::vector<Piece> &out = g_pieces[g_read ^ 1];
    if (!ReiPan_OtsGeomCollecting() || model == nullptr ||
        out.size() >= kMaxPieces)
    {
        return;
    }

    const Mesh *mesh = FindMesh(key, top);
    if (mesh == nullptr || mesh->tris.empty())
    {
        return;
    }

    Piece piece;
    piece.mesh = mesh;
    memcpy(piece.m, model, sizeof(piece.m));
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            if (!isfinite(piece.m[r][c]))
            {
                return;
            }
        }
    }
    if (!Invert(piece.m, piece.inv))
    {
        return;
    }
    for (int c = 0; c < 8; c++)
    {
        const float corner[3] = {(c & 1) ? mesh->lmax[0] : mesh->lmin[0],
                                 (c & 2) ? mesh->lmax[1] : mesh->lmin[1],
                                 (c & 4) ? mesh->lmax[2] : mesh->lmin[2]};
        float w[3];
        ToWorld(piece.m, corner, w);
        for (int axis = 0; axis < 3; axis++)
        {
            if (c == 0)
            {
                piece.wmin[axis] = piece.wmax[axis] = w[axis];
            }
            else
            {
                piece.wmin[axis] = w[axis] < piece.wmin[axis] ? w[axis] : piece.wmin[axis];
                piece.wmax[axis] = w[axis] > piece.wmax[axis] ? w[axis] : piece.wmax[axis];
            }
        }
    }
    out.push_back(piece);
}

void ReiPan_OtsGeomNextFrame(void)
{
    g_read ^= 1;
    g_pieces[g_read ^ 1].clear();
    /* A frame that ended mid-scope (a phase change between the begin and
     * the end) must not leave the next one collecting everything. */
    g_scenery = 0;
    g_suspend = 0;
}

int ReiPan_OtsGeomPieceCount(void)
{
    return (int)g_pieces[g_read].size();
}

void ReiPan_OtsGeomCast(const float from[3], const float dir[3], float length,
                        float radius, float thin, ReiPan_OtsGeomHit *out)
{
    out->distance = length;
    out->hit = 0;
    out->pieces_tested = 0;
    out->triangles_tested = 0;
    out->hit_size[0] = out->hit_size[1] = out->hit_size[2] = 0.0f;
    if (!(length > 0.0f))
    {
        return;
    }

    /* Two directions across the segment, for the sphere's outer rays. */
    float side[3];
    float up[3];
    {
        const float ref[3] = {fabsf(dir[1]) < 0.9f ? 0.0f : 1.0f,
                              fabsf(dir[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
        side[0] = dir[1] * ref[2] - dir[2] * ref[1];
        side[1] = dir[2] * ref[0] - dir[0] * ref[2];
        side[2] = dir[0] * ref[1] - dir[1] * ref[0];
        const float len =
            sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (!(len > 0.0f))
        {
            return;
        }
        for (int axis = 0; axis < 3; axis++)
        {
            side[axis] /= len;
        }
        up[0] = dir[1] * side[2] - dir[2] * side[1];
        up[1] = dir[2] * side[0] - dir[0] * side[2];
        up[2] = dir[0] * side[1] - dir[1] * side[0];
    }
    const float offsets[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    float best = 1.0f; /* fraction of the length */
    for (const Piece &piece : g_pieces[g_read])
    {
        if (piece.wmax[0] - piece.wmin[0] < thin &&
            piece.wmax[2] - piece.wmin[2] < thin)
        {
            continue;
        }
        if (!SegmentCrossesBox(from, dir, length, piece.wmin, piece.wmax,
                               radius))
        {
            continue;
        }
        out->pieces_tested++;
        const float before = best;
        for (const auto &o : offsets)
        {
            float start[3];
            float end[3];
            for (int axis = 0; axis < 3; axis++)
            {
                start[axis] = from[axis] + (side[axis] * o[0] + up[axis] * o[1]) * radius;
                end[axis] = start[axis] + dir[axis] * length;
            }
            float p[3];
            float q[3];
            ToLocal(piece, start, p);
            ToLocal(piece, end, q);
            const float d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
            best = CastMesh(*piece.mesh, p, d, best, &out->triangles_tested);
        }
        if (best < before)
        {
            out->hit = 1;
            for (int axis = 0; axis < 3; axis++)
            {
                out->hit_size[axis] = piece.wmax[axis] - piece.wmin[axis];
            }
        }
    }
    out->distance = best * length;
}

}
