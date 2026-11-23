/* ==========================================================================
 *  graphics/graph3d/gra3dMirror.c
 *
 *  FF3-only (no FF2 counterpart; the FF2 names Ghidra had in this run were
 *  wrong matches): the planar mirror and a character light blend, FF3
 *  0x1de0d8..0x1dfb90, plus the linkonce helpers only this file calls
 *  (std::sort<LIGHTCOMPAREDATA>'s insertion steps 0x2bb3b0 / 0x2bb6a8 /
 *  0x2bb7b0 and the array clear 0x2bcab0).
 *
 *  The mirror (gra3dDrawModelCb, IngameDrawRoomMorph's draw):
 *   - SgCalcScreenRectPrim walks the mirror block to its first mesh and
 *     loads local->view / clip / screen into VU0 (Vu0LoadMatrices_4_25_8);
 *   - gra3dCalcMeshScreenRect clips each triangle of that preset mesh against
 *     the six clip planes, projects what is left and grows a 12.4 screen
 *     rectangle and the view-z range; a triangle facing away rejects the
 *     mirror. Its first five vertices are kept;
 *   - gra3dCalcMirrorMatrix builds the reflection through the plane of the
 *     first four (centre, normal, quaternion rotations around a z flip);
 *   - the room is drawn again through the reflected camera with the near
 *     plane at the mirror, VU1 VF2.x -1 and the scissor on the rectangle;
 *     then a Z-only sprite resets the rectangle's depth and the scissor and
 *     clip rectangle are restored.
 *
 *  PC: VU0 macro code. The matrices FF3 keeps in vf4..11 / vf25..28 between
 *  calls are statics here; the clip flag register is computed per vector.
 *  Checked instruction by instruction against FF3 (vf1 is (1, 1, 1, -1),
 *  Vu0InitConstRegisters, so vmaddz.x vf1 sums a dot product).
 * ======================================================================== */
#include "graphics/graph3d/gra3dMirror.h"

#include <math.h>                               // fabsf
#include <string.h>                             // memcpy
#include <algorithm>                            // std::fill, std::sort

#include "common/vu0math.h"                     // Vu0Sinf, Vu0Cosf, Vu0Acosf
#include "graphics/graph3d/g3dLightEx.h"        // LIGHTCOMPAREDATA, g3dCalcLightPower, g3dBlendLight
#include "graphics/graph3d/gra3d.h"             // camera, light bank, _SetClipValue, VU1 VF2
#include "graphics/graph3d/gra3dSGD.h"          // SgCalcScreenRectPrim, _GetVUVNPRIM, GetNextUnpackAddr
#include "graphics/graph3d/gra3dVu0.h"          // Vu0SetConstVector
#include "graphics/gs_reg.h"                    // SetGsRegCached
#include "graphics/motion/motion.h"             // motQuaternion2Matrix
#include "sdk/libgraph.h"                       // SCE_GIF_SET_TAG, SCE_GS_SET_*
#include "sdk/libvu0.h"                         // sceVu0MulMatrix
#include "system/os/system.h"                   // PACKET_BUF_MGR, pdrawenv1
#include "reipan/rendering/reipan_graph3d.h"
#include "reipan/rendering/reipan_renderer.h"
#include "reipan/reipan_memory.h"               // PC: ReiPan_GetHostPointer

// ──────────────────────────────────────────────────────────────────────
// Types
// ──────────────────────────────────────────────────────────────────────

// One polygon vertex in the clipper: the local position (w 1) and its
// local->clip transform.
struct MIRRORCLIPVERTEX                 /* 0x20 */
{
    /* 0x00 */ sceVu0FVECTOR vLocal;
    /* 0x10 */ sceVu0FVECTOR vClip;
};
static_assert(sizeof(MIRRORCLIPVERTEX) == 0x20, "MIRRORCLIPVERTEX is 0x20 bytes");
static_assert(offsetof(MIRRORCLIPVERTEX, vClip) == 0x10, "MIRRORCLIPVERTEX.vClip");

// A triangle being clipped (AddTriangleScreenRect's stack, sp+0x140): two
// vertex lists, each closed by a copy of its first vertex, swapped after
// every plane. A triangle cut by all six planes has nine vertices.
#define MIRROR_CLIP_VERTEX_MAX  10
// layout: host -- pSrc / pDst are pointers (EE 0x280 / 0x284, iNum 0x288)
struct MIRRORCLIPPOLYGON
{
    /* 0x000 */ MIRRORCLIPVERTEX  aBuffer[2][MIRROR_CLIP_VERTEX_MAX];
    /* 0x280 */ MIRRORCLIPVERTEX *pSrc;
    /* 0x284 */ MIRRORCLIPVERTEX *pDst;
    /* 0x288 */ int               iNum;
};
static_assert(offsetof(MIRRORCLIPPOLYGON, aBuffer) == 0x000, "MIRRORCLIPPOLYGON.aBuffer");
static_assert(sizeof(((MIRRORCLIPPOLYGON *)0)->aBuffer) == 0x280, "MIRRORCLIPPOLYGON.aBuffer is 0x280 bytes");

// One clip plane: the VU0 clip flag it tests, the axis and the side
// (axis == sign * w). Data 0x328ab0.
struct MIRRORCLIPPLANE                  /* 0xc */
{
    /* 0x0 */ u_int uiFlag;
    /* 0x4 */ int   iAxis;
    /* 0x8 */ float fSign;
};
static_assert(sizeof(MIRRORCLIPPLANE) == 0xc, "MIRRORCLIPPLANE is 0xc bytes");
static_assert(offsetof(MIRRORCLIPPLANE, fSign) == 0x8, "MIRRORCLIPPLANE.fSign");

// A projected vertex (AddTriangleScreenRect's stack, 0x30 stride): the 12.4
// screen position, the view position and the projected screen position.
struct MIRRORPROJVERTEX                 /* 0x30 */
{
    /* 0x00 */ int           aiScreen[4];   // vftoi4.xyz (w not written by FF3)
    /* 0x10 */ sceVu0FVECTOR vView;
    /* 0x20 */ sceVu0FVECTOR vScreen;
};
static_assert(sizeof(MIRRORPROJVERTEX) == 0x30, "MIRRORPROJVERTEX is 0x30 bytes");
static_assert(offsetof(MIRRORPROJVERTEX, vView) == 0x10, "MIRRORPROJVERTEX.vView");
static_assert(offsetof(MIRRORPROJVERTEX, vScreen) == 0x20, "MIRRORPROJVERTEX.vScreen");

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// VU0 clip flags (vclipw.xyz): +x, -x, +y, -y, +z, -z.
#define VU0_CLIP_PX     0x01
#define VU0_CLIP_NX     0x02
#define VU0_CLIP_PY     0x04
#define VU0_CLIP_NY     0x08
#define VU0_CLIP_PZ     0x10
#define VU0_CLIP_NZ     0x20
#define VU0_CLIP_MASK   0x3f

// Data 0x328ab0: the near / far planes first (clipped before the others).
static const MIRRORCLIPPLANE s_aClipPlane[6] =
{
    { VU0_CLIP_PZ, 2,  1.0f },
    { VU0_CLIP_NZ, 2, -1.0f },
    { VU0_CLIP_PX, 0,  1.0f },
    { VU0_CLIP_NX, 0, -1.0f },
    { VU0_CLIP_PY, 1,  1.0f },
    { VU0_CLIP_NY, 1, -1.0f },
};

// The 640x448 window at GS (1728, 1824), 12.4 fixed point.
#define MIRROR_SCREEN_X0        0x6c0
#define MIRROR_SCREEN_Y0        0x720
#define MIRROR_SCREEN_W         640
#define MIRROR_SCREEN_H         448

// PC: VU0 vf4..7 (local->view), vf25..28 (local->clip), vf8..11
// (local->screen) between Vu0LoadMatrices_4_25_8 and the clipper.
static sceVu0FMATRIX s_matVu0LocalView;
static sceVu0FMATRIX s_matVu0LocalClip;
static sceVu0FMATRIX s_matVu0LocalScreen;

// The mesh's screen rectangle (12.4) and view-z range.
static int   s_iScreenRectMaxX;                 // sdata gp-0x188
static int   s_iScreenRectMinX;                 // sdata gp-0x184
static int   s_iScreenRectMaxY;                 // sdata gp-0x180
static int   s_iScreenRectMinY;                 // sdata gp-0x17c
static float s_fMirrorMinZ;                     // sdata gp-0x178: the reflected camera's near z
static float s_fMirrorMaxZ;                     // sdata gp-0x174
static int   s_iNumMirrorPoint;                 // sdata gp-0x170

static sceVu0FMATRIX s_matMirror;               // bss 0x3ee7b0: the reflection, world space
static sceVu0FVECTOR s_vMirrorNormal;           // bss 0x3ee7f0
static sceVu0FVECTOR s_vMirrorPoint;            // bss 0x3ee800: the farthest vertex, then the centre
static sceVu0FVECTOR s_avMirrorPoint[5];        // bss 0x3ee810: local positions
static sceVu0FVECTOR s_avMirrorPointClip[5];    // bss 0x3ee860: their clip positions

// sdata gp-0xca0 caches GetPacketBufMgr (shared with g2d_draw.c / BpCtl.c).
static PACKET_BUF_MGR *s_pPacketBufMgr;

// ──────────────────────────────────────────────────────────────────────
// VU0 helpers
// ──────────────────────────────────────────────────────────────────────

// vmulax / vmadday / vmaddaz / vmaddw: m * (v.xyz, w).
static void _ApplyMatrixW(float *vOut, sceVu0FMATRIX m, const float *v, float w)
{
    float r[4];
    int   i;

    for (i = 0; i < 4; i++)
    {
        r[i] = m[0][i] * v[0] + m[1][i] * v[1] + m[2][i] * v[2] + m[3][i] * w;
    }
    memcpy(vOut, r, sizeof(r));
}

// vclipw.xyz v, v: each of x, y, z against +-|w|.
static u_int _ClipFlags(const float *v)
{
    float w = fabsf(v[3]);
    u_int f = 0;

    if (v[0] > w)  f |= VU0_CLIP_PX;
    if (v[0] < -w) f |= VU0_CLIP_NX;
    if (v[1] > w)  f |= VU0_CLIP_PY;
    if (v[1] < -w) f |= VU0_CLIP_NY;
    if (v[2] > w)  f |= VU0_CLIP_PZ;
    if (v[2] < -w) f |= VU0_CLIP_NZ;
    return f;
}

// vftoi4: float -> 12.4 fixed point, truncated.
static int _Ftoi4(float f)
{
    return (int)(f * 16.0f);
}

// FF3 0x001de0d8.
void Vu0LoadMatrices_4_25_8(float (*matLocalView)[4], float (*matLocalClip)[4], float (*matLocalScreen)[4])
{
    sceVu0CopyMatrix(s_matVu0LocalView, matLocalView);
    sceVu0CopyMatrix(s_matVu0LocalClip, matLocalClip);
    sceVu0CopyMatrix(s_matVu0LocalScreen, matLocalScreen);
}

// The clip flags of an edge: a's in bits 6..11, b's in 0..5 (the CLIP
// register after two vclipw). FF3 0x001de110.
static u_int Vu0ClipEdgeFlags(const float *vClipA, const float *vClipB)
{
    return (_ClipFlags(vClipA) << 6) | _ClipFlags(vClipB);
}

// FF3: the caller's vclipw.xyz runs in the jal delay slot and this reads the
// CLIP register; the caller keeps the low six bits. PC: the test is made here.
// FF3 0x001de140.
static u_int Vu0GetClipFlags(const float *vClip)
{
    return _ClipFlags(vClip);
}

// pDst = where the edge pIn (inside) -> pOut (outside) crosses pPlane.
// FF3 0x001de160.
static void ClipEdgeIntersect(MIRRORCLIPVERTEX *pDst, const MIRRORCLIPVERTEX *pIn, const MIRRORCLIPVERTEX *pOut,
                              const MIRRORCLIPPLANE *pPlane)
{
    float fIn  = pIn->vClip[pPlane->iAxis] - pPlane->fSign * pIn->vClip[3];
    float fOut = pOut->vClip[pPlane->iAxis] - pPlane->fSign * pOut->vClip[3];
    float t    = fabsf(fIn / (fOut - fIn));
    int   i;

    for (i = 0; i < 4; i++)
    {
        pDst->vLocal[i] = pIn->vLocal[i] * (1.0f - t) + pOut->vLocal[i] * t;   // sdata 0x370c24
    }
    pDst->vLocal[3] = 1.0f;
    _ApplyMatrixW(pDst->vClip, s_matVu0LocalClip, pDst->vLocal, 1.0f);
}

// Clip the polygon against one plane (Sutherland-Hodgman) into the other
// list, close it and swap the lists. FF3 0x001de250.
static void ClipEdgeListByFlags(MIRRORCLIPPOLYGON *pPoly, const MIRRORCLIPPLANE *pPlane)
{
    u_int m = pPlane->uiFlag;
    int   n = 0;
    int   i;
    MIRRORCLIPVERTEX *pSwap;

    for (i = 0; i < pPoly->iNum; i++)
    {
        MIRRORCLIPVERTEX *pA = &pPoly->pSrc[i];
        MIRRORCLIPVERTEX *pB = &pPoly->pSrc[i + 1];
        u_int             f  = Vu0ClipEdgeFlags(pA->vClip, pB->vClip);

        if ((f & (m | (m << 6))) == 0)
        {
            pPoly->pDst[n++] = *pA;                     // both inside
        }
        else if ((f & (m << 6)) == 0)
        {
            if ((f & m) != 0)                           // a inside, b outside
            {
                pPoly->pDst[n] = *pA;
                ClipEdgeIntersect(&pPoly->pDst[n + 1], pA, pB, pPlane);
                n += 2;
            }
        }
        else if ((f & m) == 0)                          // a outside, b inside
        {
            ClipEdgeIntersect(&pPoly->pDst[n++], pB, pA, pPlane);
        }
    }

    pPoly->pDst[n] = pPoly->pDst[0];
    pPoly->iNum    = n;
    pSwap          = pPoly->pDst;
    pPoly->pDst    = pPoly->pSrc;
    pPoly->pSrc    = pSwap;
}

// vOut = cross(v0 - v1, v1 - v2) of the first three 12.4 screen positions
// (z 0), times vOut.z: only z is non-zero, the facing times the strip sign.
// FF3 0x001de400.
static void CalcNormalFromIVectors(float *vOut, const MIRRORPROJVERTEX *aV)
{
    float a[4];
    float b[4];
    float v[3][4];
    float s = vOut[2];
    int   i;

    for (i = 0; i < 3; i++)
    {
        v[i][0] = (float)aV[i].aiScreen[0] * (1.0f / 16.0f);   // vitof4.xy
        v[i][1] = (float)aV[i].aiScreen[1] * (1.0f / 16.0f);
        v[i][2] = 0.0f;                                         // vmove.zw vf0
        v[i][3] = 1.0f;
    }
    for (i = 0; i < 4; i++)
    {
        a[i] = v[0][i] - v[1][i];
        b[i] = v[1][i] - v[2][i];
    }
    vOut[0] = (a[1] * b[2] - b[1] * a[2]) * s;
    vOut[1] = (a[2] * b[0] - b[2] * a[0]) * s;
    vOut[2] = (a[0] * b[1] - b[0] * a[1]) * s;
    vOut[3] = a[3];
}

// Project one clipped vertex: local->screen with w 1, divided by w and
// converted to 12.4; local->view with the vertex's own w.
static void _ProjectVertex(MIRRORPROJVERTEX *pOut, const float *vLocal)
{
    float q;

    _ApplyMatrixW(pOut->vScreen, s_matVu0LocalScreen, vLocal, 1.0f);
    q = 1.0f / pOut->vScreen[3];                                // vdiv vf0w / w
    _ApplyMatrixW(pOut->vView, s_matVu0LocalView, vLocal, vLocal[3]);
    pOut->vScreen[0] *= q;
    pOut->vScreen[1] *= q;
    pOut->vScreen[2] *= q;
    pOut->aiScreen[0] = _Ftoi4(pOut->vScreen[0]);
    pOut->aiScreen[1] = _Ftoi4(pOut->vScreen[1]);
    pOut->aiScreen[2] = _Ftoi4(pOut->vScreen[2]);
    pOut->aiScreen[3] = 0;                                      // PC: not written by FF3
}

// Clip one triangle (v0 v1 v2, local) and add it to the screen rectangle.
// fSign is the strip's alternating winding: a triangle whose screen normal
// times fSign is >= 0 faces away and sets the min view z past 65535, which
// rejects the mirror. FF3 0x001de448.
static void AddTriangleScreenRect(float fSign, float *v0, float *v1, float *v2)
{
    MIRRORCLIPPOLYGON poly;
    MIRRORPROJVERTEX  aProj[MIRROR_CLIP_VERTEX_MAX];   // PC: FF3's 0x140 bytes overlap the first list
    sceVu0FVECTOR     vNormal;
    float            *av[3] = { v0, v1, v2 };
    u_int             flags;
    int               i;

    poly.pSrc = poly.aBuffer[0];
    poly.pDst = poly.aBuffer[1];
    for (i = 0; i < 4; i++)
    {
        float *v = av[i % 3];

        poly.pSrc[i].vLocal[0] = v[0];
        poly.pSrc[i].vLocal[1] = v[1];
        poly.pSrc[i].vLocal[2] = v[2];
        poly.pSrc[i].vLocal[3] = 1.0f;                          // sdata 0x370c28
        _ApplyMatrixW(poly.pSrc[i].vClip, s_matVu0LocalClip, poly.pSrc[i].vLocal, 1.0f);
    }
    poly.iNum = 3;

    flags = (Vu0GetClipFlags(poly.pSrc[0].vClip) & VU0_CLIP_MASK)
          | (Vu0GetClipFlags(poly.pSrc[1].vClip) & VU0_CLIP_MASK)
          | (Vu0GetClipFlags(poly.pSrc[2].vClip) & VU0_CLIP_MASK);

    if ((flags & (VU0_CLIP_PZ | VU0_CLIP_NZ)) != 0)
    {
        if ((flags & s_aClipPlane[0].uiFlag) != 0)
        {
            ClipEdgeListByFlags(&poly, &s_aClipPlane[0]);
        }
        if ((flags & s_aClipPlane[1].uiFlag) != 0)
        {
            ClipEdgeListByFlags(&poly, &s_aClipPlane[1]);
        }
        for (i = 0; i < poly.iNum; i++)
        {
            flags |= Vu0GetClipFlags(poly.pSrc[i].vClip) & VU0_CLIP_MASK;
        }
    }

    for (i = 2; i < 6; i++)
    {
        if ((flags & s_aClipPlane[i].uiFlag) != 0)
        {
            ClipEdgeListByFlags(&poly, &s_aClipPlane[i]);
        }
    }

    if (poly.iNum < 3)
    {
        return;
    }

    for (i = 0; i < 3; i++)
    {
        _ProjectVertex(&aProj[i], poly.pSrc[i].vLocal);
    }
    vNormal[2] = fSign;
    CalcNormalFromIVectors(vNormal, aProj);
    if (0.0f <= vNormal[2])
    {
        s_fMirrorMinZ = 2000000.0f;                             // sdata 0x370c2c
        return;
    }

    for (i = 0; i < poly.iNum; i++)
    {
        MIRRORPROJVERTEX *p = &aProj[i];
        float             z;

        _ProjectVertex(p, poly.pSrc[i].vLocal);

        if (s_iScreenRectMaxX < p->aiScreen[0])
        {
            s_iScreenRectMaxX = p->aiScreen[0];
        }
        if (p->aiScreen[0] < s_iScreenRectMinX)
        {
            s_iScreenRectMinX = p->aiScreen[0];
        }
        if (s_iScreenRectMaxY < p->aiScreen[1])
        {
            s_iScreenRectMaxY = p->aiScreen[1];
        }
        if (p->aiScreen[1] < s_iScreenRectMinY)
        {
            s_iScreenRectMinY = p->aiScreen[1];
        }

        z = p->vView[2];
        if (z < s_fMirrorMinZ)
        {
            s_fMirrorMinZ = z;
        }
        if (s_fMirrorMaxZ < z)
        {
            s_fMirrorMaxZ = z;
            memcpy(s_vMirrorPoint, poly.pSrc[i].vLocal, sizeof(sceVu0FVECTOR));
        }
    }
}

// Keep one of the first five mesh vertices with its clip position.
// FF3 0x001de860.
static void AddMirrorPoint(float *v)
{
    if (s_iNumMirrorPoint < 5)
    {
        memcpy(s_avMirrorPoint[s_iNumMirrorPoint], v, sizeof(sceVu0FVECTOR));
        _ApplyMatrixW(s_avMirrorPointClip[s_iNumMirrorPoint], s_matVu0LocalClip, v, 1.0f);
        s_iNumMirrorPoint++;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The reflection
// ──────────────────────────────────────────────────────────────────────

// q = (axis * sin(a / 2), cos(a / 2)) as a matrix.
static void _AxisAngleMatrix(sceVu0FMATRIX m, const float *vAxis, float fHalfAngle)
{
    sceVu0FVECTOR q;
    float         s = Vu0Sinf(fHalfAngle);
    int           i;

    for (i = 0; i < 4; i++)
    {
        q[i] = vAxis[i] * s;
    }
    q[3] = Vu0Cosf(fHalfAngle);
    motQuaternion2Matrix(m, q);
}

// The reflection through the plane of the first four saved points (world
// space, by SgCalcScreenRectPrim's local->world): rotate the normal onto
// -up, flip z, rotate back, around the centre. FF3 0x001de8c8 (its callers
// pass the camera in a0; it is not read).
// callcheck: Vu0Sinf, Vu0Cosf, motQuaternion2Matrix (in _AxisAngleMatrix;
//   FF3 writes the sin / cos / quaternion sequence out three times)
static void gra3dCalcMirrorMatrix(void)
{
    static const sceVu0FVECTOR vUp       = { 0.0f, 0.0f, -1.0f, 1.0f };    // sdata 0x370c30 / 0x370c34
    static const sceVu0FVECTOR vAxisDflt = { 0.0f, 1.0f, 0.0f, 1.0f };     // data 0x329480
    sceVu0FMATRIX matRot;
    sceVu0FMATRIX matMirror;
    sceVu0FMATRIX matTrans;
    sceVu0FVECTOR vCenter;
    sceVu0FVECTOR vNormal;
    sceVu0FVECTOR vAxis;
    sceVu0FVECTOR vDown;
    sceVu0FVECTOR vWork;
    float         a[4];
    float         b[4];
    float         fLen;
    float         fAngle;
    int           i;

    // Centre, to world.
    for (i = 0; i < 4; i++)
    {
        vCenter[i] = (s_avMirrorPoint[0][i] + s_avMirrorPoint[1][i] + s_avMirrorPoint[2][i]
                      + s_avMirrorPoint[3][i]) * 0.25f;
    }
    _ApplyMatrixW(vCenter, s_matScreenRectLocalWorld, vCenter, vCenter[3]);

    // Normal (w keeps the first point's), rotated to world and normalised.
    for (i = 0; i < 3; i++)
    {
        a[i] = s_avMirrorPoint[1][i] - s_avMirrorPoint[0][i];
        b[i] = s_avMirrorPoint[2][i] - s_avMirrorPoint[1][i];
    }
    vNormal[0] = a[1] * b[2] - b[1] * a[2];
    vNormal[1] = a[2] * b[0] - b[2] * a[0];
    vNormal[2] = a[0] * b[1] - b[0] * a[1];
    vNormal[3] = s_avMirrorPoint[0][3];
    _ApplyMatrixW(vNormal, s_matScreenRectLocalWorld, vNormal, 0.0f);
    fLen = 1.0f / sqrtf(vNormal[0] * vNormal[0] + vNormal[1] * vNormal[1] + vNormal[2] * vNormal[2]);
    vNormal[0] *= fLen;
    vNormal[1] *= fLen;
    vNormal[2] *= fLen;

    // Either parallel normal needs a fallback axis: its cross product is zero.
    if (vNormal[0] == vUp[0] && vNormal[1] == vUp[1])
    {
        memcpy(vAxis, vAxisDflt, sizeof(vAxis));
    }
    else
    {
        vAxis[0] = vUp[1] * vNormal[2] - vNormal[1] * vUp[2];
        vAxis[1] = vUp[2] * vNormal[0] - vNormal[2] * vUp[0];
        vAxis[2] = vUp[0] * vNormal[1] - vNormal[0] * vUp[1];
        vAxis[3] = vUp[3];
    }
    fLen = 1.0f / sqrtf(vAxis[0] * vAxis[0] + vAxis[1] * vAxis[1] + vAxis[2] * vAxis[2]);
    vAxis[0] *= fLen;
    vAxis[1] *= fLen;
    vAxis[2] *= fLen;

    // The angle between -up and the normal; its sign is the one that turns
    // the normal onto -up.
    vDown[0] = -vUp[0];
    vDown[1] = -vUp[1];
    vDown[2] = -vUp[2];
    vDown[3] = vUp[3];
    fAngle = Vu0Acosf(vDown[0] * vNormal[0] + vDown[1] * vNormal[1] + vDown[2] * vNormal[2]);
    _AxisAngleMatrix(matRot, vAxis, fAngle * 0.5f);             // sdata 0x370c38
    _ApplyMatrixW(vWork, matRot, vNormal, vNormal[3]);
    if (0.0f < vWork[2] * vUp[2])
    {
        fAngle = -fAngle;
    }

    // Flip z between the two rotations.
    sceVu0UnitMatrix(matMirror);                                // data 0x3293c0
    matMirror[2][2] = -1.0f;
    _AxisAngleMatrix(matRot, vAxis, fAngle * 0.5f);
    sceVu0MulMatrix(matMirror, matMirror, matRot);
    _AxisAngleMatrix(matRot, vAxis, -fAngle * 0.5f);
    sceVu0MulMatrix(matMirror, matRot, matMirror);

    // Around the centre.
    sceVu0UnitMatrix(matTrans);
    matTrans[3][0] = -vCenter[0];
    matTrans[3][1] = -vCenter[1];
    matTrans[3][2] = -vCenter[2];
    matTrans[3][3] = vCenter[3];
    sceVu0MulMatrix(matMirror, matMirror, matTrans);
    matTrans[3][0] = vCenter[0];
    matTrans[3][1] = vCenter[1];
    matTrans[3][2] = vCenter[2];
    sceVu0MulMatrix(matMirror, matTrans, matMirror);

    memcpy(s_vMirrorPoint, vCenter, sizeof(sceVu0FVECTOR));
    memcpy(s_vMirrorNormal, vNormal, sizeof(sceVu0FVECTOR));
    sceVu0CopyMatrix(s_matMirror, matMirror);
}

// Draw pfnDraw through the reflected camera: a copy of pCam with the near
// plane at the mirror is applied, its world->screen / clip matrices times the
// reflection go into the live camera, and VU1 VF2.x is -1 while it draws
// (the mirror reverses the winding). FF3 0x001dee98.
static void gra3dDrawMirrorView(GRA3DCAMERA *pCam, int (*pfnDraw)(void))
{
    GRA3DCAMERA   cam;
    GRA3DCAMERA  *pLive;
    sceVu0FVECTOR vVF2;

    cam        = *pCam;
    cam.fNearZ = s_fMirrorMinZ;
    gra3dApplyCamera(&cam, 1, 1);
    gra3dCalcMirrorMatrix();

    sceVu0MulMatrix(cam.matWorldScreen, cam.matWorldScreen, s_matMirror);
    sceVu0MulMatrix(cam.matWorldClipPolygon, cam.matWorldClipPolygon, s_matMirror);
    sceVu0MulMatrix(cam.matWorldClipObject, cam.matWorldClipObject, s_matMirror);
    pLive = gra3dGetCamera();
    sceVu0CopyMatrix(pLive->matWorldScreen, cam.matWorldScreen);
    pLive = gra3dGetCamera();
    sceVu0CopyMatrix(pLive->matWorldClipPolygon, cam.matWorldClipPolygon);
    pLive = gra3dGetCamera();
    sceVu0CopyMatrix(pLive->matWorldClipObject, cam.matWorldClipObject);

    // The host renderer needs the reflected view as well as the PS2 matrices.
    sceVu0FMATRIX reflectedView;
    sceVu0MulMatrix(reflectedView, cam.matWorldView, s_matMirror);
    ReiPan_Graph3dApplyCamera(&cam, reflectedView);

    vVF2[0] = -1.0f;                                            // sdata 0x370c3c
    vVF2[1] = 0.0f;
    vVF2[2] = 0.0f;
    vVF2[3] = 0.0f;
    Vu0SetConstVector(2, vVF2);
    gra3dSetVu1ConstVF2(vVF2);
    ReiPan_RendererSetMirrorWinding(1);
    pfnDraw();
    ReiPan_RendererSetMirrorWinding(0);

    vVF2[0] = 1.0f;                                             // sdata 0x370c40
    vVF2[1] = 0.0f;
    vVF2[2] = 0.0f;
    vVF2[3] = 0.0f;
    Vu0SetConstVector(2, vVF2);
    gra3dSetVu1ConstVF2(vVF2);
    gra3dApplyCamera(nullptr, 1, 1);
}

// Grow the 12.4 rectangle by a pixel each side, clamped to the window.
// FF3 0x001df1c8.
static void PadScreenRect(void)
{
    s_iScreenRectMaxX += 16;
    if (s_iScreenRectMaxX / 16 - MIRROR_SCREEN_X0 >= MIRROR_SCREEN_W)
    {
        s_iScreenRectMaxX = 0x93f0;
    }
    s_iScreenRectMinX -= 16;
    if (s_iScreenRectMinX / 16 - MIRROR_SCREEN_X0 < 0)
    {
        s_iScreenRectMinX = MIRROR_SCREEN_X0 << 4;
    }
    s_iScreenRectMaxY += 16;
    if (s_iScreenRectMaxY / 16 - MIRROR_SCREEN_Y0 >= MIRROR_SCREEN_H)
    {
        s_iScreenRectMaxY = 0x8df0;
    }
    s_iScreenRectMinY -= 16;
    if (s_iScreenRectMinY / 16 - MIRROR_SCREEN_Y0 < 0)
    {
        s_iScreenRectMinY = MIRROR_SCREEN_Y0 << 4;
    }
}

// The block named by block 0's SGDPU_MIRROR_BLOCK unit, 0 when it has none.
// FF3 0x001df298.
static int gra3dsgdGetMirrorBlock(SGDFILEHEADER *pSgdTop)
{
    SGDPROCUNITHEADER *pPU = SgdPtr<SGDPROCUNITHEADER>(pSgdTop->ProcUnitHeads()[0]);

    for (; pPU != nullptr; pPU = SgdPtr<SGDPROCUNITHEADER>(pPU->pNext))
    {
        if (pPU->iCategory == SGDPU_MIRROR_BLOCK)
        {
            return pPU->MirrorBlockDesc.iBlock;
        }
    }
    return 0;
}

// FF3 0x001df2d8.
int gra3dCalcMeshScreenRect(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDATA_PRESET *pMeshData = (SGDVUMESHDATA_PRESET *)&pPUHead[1];
    SGDPROCUNITHEADER    *pVUVN;
    SGDVUVNDATA_PRESET   *pVUVNData;
    int                   iMeshType;
    int                   iNumStrip;
    int                   iVertexBase;
    int                   bResult = 1;
    sceVu0FVECTOR         avTri[3];                 // [0] the newest vertex
    u_int                *prim;

    s_fMirrorMaxZ     = 0.0f;
    s_iScreenRectMinY = 0xa800;
    s_iScreenRectMinX = 0xa800;
    s_fMirrorMinZ     = 65535.0f;                               // sdata 0x370c44
    s_iScreenRectMaxX = 0;
    s_iScreenRectMaxY = 0;

    pVUVN     = _GetVUVNPRIM();
    pVUVNData = (SGDVUVNDATA_PRESET *)&_GetVUVNPRIM()[1];
    iMeshType = pPUHead->VUMeshDesc.ucMeshType;

    if (pMeshData->sOffsetToPrim == 0)
    {
        return 0;
    }

    iVertexBase       = 0;
    s_iNumMirrorPoint = 0;
    memset(avTri, 0, sizeof(avTri));
    avTri[0][3] = 1.0f;                                         // sdata 0x370c48
    avTri[1][3] = 1.0f;
    avTri[2][3] = 1.0f;

    prim = (u_int *)pPUHead + pMeshData->sOffsetToPrim;
    for (iNumStrip = pPUHead->VUMeshDesc.ucNumMesh; iNumStrip != 0; iNumStrip--)
    {
        _SGDVUMESHCOLORDATA *pStrip = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr(prim);
        int                  loops  = pStrip->aucVifUnpack[2];
        u_short             *pFlag  = (u_short *)pStrip->Color();
        float                fSign  = 1.0f;
        int                  k;

        for (k = 0; k < loops; k++, pFlag += 6)
        {
            float *pVertex;

            // A marker colour past the strip's first two vertices restarts it.
            if (*pFlag == 1 && k >= 2)
            {
                fSign = -fSign;
                continue;
            }

            if (iMeshType == iMT_0 || iMeshType == iMT_2)
            {
                pVertex = pVUVNData->Type2()[iVertexBase + k].vVertex;
            }
            else
            {
                pVertex = pVUVNData->Type2F()[iVertexBase + k + pVUVN->VUVNDesc.sNumNormal];
            }

            memcpy(avTri[2], avTri[1], sizeof(VECTOR3));
            memcpy(avTri[1], avTri[0], sizeof(VECTOR3));
            memcpy(avTri[0], pVertex, sizeof(VECTOR3));
            AddMirrorPoint(avTri[0]);

            if (k >= 2)
            {
                AddTriangleScreenRect(fSign, avTri[2], avTri[1], avTri[0]);
                if (65535.0f < s_fMirrorMinZ)
                {
                    bResult = 0;
                }
            }
            fSign = -fSign;
        }

        iVertexBase += loops;
        prim = (u_int *)&pStrip->Color()[loops];
    }

    if (bResult != 0)
    {
        PadScreenRect();
    }
    return bResult;
}

// FF3 0x001df568.
void gra3dDrawModelCb(GRA3DCAMERA *pCam, SGDFILEHEADER *pSgdTop, u_int uiCoord, int (*pfnDraw)(void))
{
    PACKET_BUF_MGR *mgr;
    u_long         *pkt;
    u_int           adrs;
    sceGsDrawEnv1  *pDrawEnv;
    u_long          scissor;
    float           fMinX;
    float           fMaxX;
    float           fMinY;
    float           fMaxY;
    int             iBlock;
    int             i;

    if (uiCoord == 0)
    {
        if (pSgdTop == nullptr)
        {
            return;
        }
        uiCoord = pSgdTop->pCoord;
    }

    gra3dsgdSetCoordinate(SgdPtr<SGDCOORDINATE>(uiCoord), -1);
    _SetClipValue(-1.0f, 1.0f, -1.0f, 1.0f);                    // sdata 0x370c4c / 0x370c50
    gra3dsgdSetData(pSgdTop);

    iBlock = gra3dsgdGetMirrorBlock(pSgdTop);
    if (iBlock == 0
        || SgCalcScreenRectPrim(pCam, SgdPtr<SGDPROCUNITHEADER>(pSgdTop->ProcUnitHeads()[iBlock])) == 0)
    {
        gra3dCalcMirrorMatrix();
        return;
    }

    // The clip rectangle: the saved points' clip x / y over |w|.
    fMinX = 1.0f;
    fMaxX = -1.0f;
    fMinY = 1.0f;
    fMaxY = -1.0f;
    for (i = 0; i < s_iNumMirrorPoint; i++)
    {
        float s = 1.0f / fabsf(s_avMirrorPointClip[i][3]);
        float x = s_avMirrorPointClip[i][0] * s;
        float y = s_avMirrorPointClip[i][1] * s;

        if (x < fMinX)
        {
            fMinX = x;
        }
        if (fMaxX < x)
        {
            fMaxX = x;
        }
        if (y < fMinY)
        {
            fMinY = y;
        }
        if (fMaxY < y)
        {
            fMaxY = y;
        }
    }
    if (fMinX < -1.0f)
    {
        fMinX = -1.0f;
    }
    if (1.0f < fMaxX)
    {
        fMaxX = 1.0f;
    }
    if (fMinY < -1.0f)
    {
        fMinY = -1.0f;
    }
    if (1.0f < fMaxY)
    {
        fMaxY = 1.0f;
    }
    _SetClipValue(fMinX, fMaxX, fMinY, fMaxY);

    // PC: the box is fitted to this frame's camera; frame smoothing's
    // in-between cameras move it with the mirror's centre, the mean of its
    // first four points as gra3dCalcMirrorMatrix() takes it (that runs only
    // once the camera is replaced), as a point (the points carry no w).
    {
        sceVu0FVECTOR vCentre;

        for (i = 0; i < 3; i++)
        {
            vCentre[i] = (s_avMirrorPoint[0][i] + s_avMirrorPoint[1][i] + s_avMirrorPoint[2][i]
                          + s_avMirrorPoint[3][i]) * 0.25f;
        }
        _ApplyMatrixW(vCentre, s_matScreenRectLocalWorld, vCentre, 1.0f);
        ReiPan_Graph3dBeginScissorAnchor(vCentre);
    }

    // Scissor to the screen rectangle and draw the reflection.
    gra3dSetGsRegisterDefault();
    SetGsRegCached((u_long)(s_iScreenRectMinX / 16 - MIRROR_SCREEN_X0)
                   | (u_long)(s_iScreenRectMaxY / 16 - MIRROR_SCREEN_Y0) << 48
                   | (u_long)(s_iScreenRectMinY / 16 - MIRROR_SCREEN_Y0) << 32
                   | (u_long)(s_iScreenRectMaxX / 16 - MIRROR_SCREEN_X0) << 16,
                   SCE_GS_SCISSOR_1, 1);
    gra3dDrawMirrorView(pCam, pfnDraw);

    // Z-only sprite over the window (alpha test NEVER, fail keeps Z, Z test
    // ALWAYS, Z 0): the scissored rectangle's depth is cleared.
    if (s_pPacketBufMgr == nullptr)
    {
        s_pPacketBufMgr = GetPacketBufMgr();
    }
    mgr  = s_pPacketBufMgr;
    adrs = mgr->GetPacket();
    pkt  = (u_long *)ReiPan_GetHostPointer(adrs);

    pkt[0] = SCE_GIF_SET_TAG(1, 1, 0, 0, SCE_GIF_PACKED, 1);
    pkt[1] = SCE_GIF_PACKED_AD;
    pkt[2] = SCE_GS_SET_TEST(1, 0, 0, 2, 0, 0, 1, 1);           // 0x32001
    pkt[3] = SCE_GS_TEST_1;
    pkt[4] = SCE_GIF_SET_TAG(2, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 1, 0, 0, 1, 0, 0, 0, 0),
                             SCE_GIF_PACKED, 2);
    pkt[5] = SCE_GS_RGBAQ | (SCE_GS_XYZ2 << 4);

    s_iScreenRectMaxX = (MIRROR_SCREEN_X0 + MIRROR_SCREEN_W) << 4;
    s_iScreenRectMinX = MIRROR_SCREEN_X0 << 4;
    s_iScreenRectMaxY = (MIRROR_SCREEN_Y0 + MIRROR_SCREEN_H) << 4;
    s_iScreenRectMinY = MIRROR_SCREEN_Y0 << 4;

    {
        u_int *pw = (u_int *)&pkt[6];

        pw[0]  = 0x80;                                          // RGBAQ
        pw[1]  = 0x80;
        pw[2]  = 0x80;
        pw[3]  = 0x80;
        pw[4]  = (u_int)s_iScreenRectMinX;                      // XYZ2
        pw[5]  = (u_int)s_iScreenRectMinY;
        pw[6]  = 0;
        pw[7]  = 0;
        pw[8]  = 0x80;                                          // RGBAQ
        pw[9]  = 0x80;
        pw[10] = 0x80;
        pw[11] = 0x80;
        pw[12] = (u_int)s_iScreenRectMaxX;                      // XYZ2
        pw[13] = (u_int)s_iScreenRectMaxY;
        pw[14] = 0;
        pw[15] = 0;
    }
    mgr->SetPacket(adrs + 0x70, 1, 1);
    ReiPan_RendererClearMirrorDepth();
    ReiPan_RendererEndScissorAnchor();  // PC

    pDrawEnv = pdrawenv1;
    gra3dSetGsRegisterDefault();
    memcpy(&scissor, &pDrawEnv->scissor1, sizeof(scissor));
    SetGsRegCached(scissor & 0x07ff07ff07ff07ffull, SCE_GS_SCISSOR_1, 1);
    _SetClipValue(-1.0f, 1.0f, -1.0f, 1.0f);
}

// ──────────────────────────────────────────────────────────────────────
// Character light blend
// ──────────────────────────────────────────────────────────────────────

// Clear n LIGHTCOMPAREDATA (memset 0). FF3 0x002bcab0 (linkonce).
static void ZeroLightCompareData(LIGHTCOMPAREDATA *aCD, int n)
{
    std::fill(aCD, aCD + n, LIGHTCOMPAREDATA());
}

// The std::sort below is FF3's instances 0x2bb210 (__introsort_loop),
// 0x2bb3b0 (__final_insertion_sort), 0x2bb460 (__partial_sort), 0x2bb5e0
// (__unguarded_partition), 0x2bb6a8 (__insertion_sort), 0x2bb7b0
// (__unguarded_linear_insert), 0x2bb838 (sort_heap), 0x2bb908 (__adjust_heap),
// 0x2bb9f8 (__push_heap), shared with g3dLightEx.c's
// g3dSortLightForBoundingBoxByPowerOrder. Equal powers (unlit slots, 0) may
// come out in another order than SGI STL's; only their null lights differ.
// callcheck: __introsort_loop, __final_insertion_sort
// FF3 0x001df968.
void gra3dGenerateBlendedLight(G3DLIGHT *pDest, float *vTarget, float fScale)
{
    // Scratchpad 0x70003900 on the EE.
    static LIGHTCOMPAREDATA aCD[NUM_GRA3DLIGHTID];
    G3DLIGHT                LightFixed;
    G3DLIGHT               *apSrc[5];
    int                     i;

    ZeroLightCompareData(aCD, NUM_GRA3DLIGHTID);
    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        GRA3DLIGHTSTATUS *pStatus = gra3dGetLightStatusRef(i);

        if (pStatus->bEnable == 0 || pStatus->bEnableToShadow == 0)
        {
            aCD[i].iIndex = 0x7fffffff;
        }
        else
        {
            aCD[i].pLight = gra3dGetLightRef(i);
            aCD[i].fPower = g3dCalcLightPower(aCD[i].pLight, vTarget);
            aCD[i].iIndex = i;
        }
    }
    std::sort(aCD, aCD + NUM_GRA3DLIGHTID, LIGHTCOMPAREDATA::greater());

    // The default light (rdata 0x35a4d0, gra3dMisc.c's s_NullLight) as a
    // white directional light from (0, -1, 1e-4).
    memset(&LightFixed, 0, sizeof(LightFixed));
    LightFixed.vPosition[3]  = 1.0f;
    LightFixed.fFalloff      = 1.0f;
    LightFixed.vDiffuse[0]   = 1.0f;                            // sdata 0x370c58
    LightFixed.vDiffuse[1]   = 1.0f;
    LightFixed.vDiffuse[2]   = 1.0f;
    LightFixed.vDiffuse[3]   = 1.0f;
    LightFixed.vDirection[0] = 0.0f;
    LightFixed.vDirection[1] = -1.0f;                           // sdata 0x370c5c
    LightFixed.vDirection[2] = 0.0001f;                         // sdata 0x370c60
    LightFixed.vDirection[3] = 0.0f;
    LightFixed.Type          = G3DLIGHT_DIRECTIONAL;

    apSrc[0] = aCD[0].pLight;
    apSrc[1] = aCD[1].pLight;
    apSrc[2] = aCD[2].pLight;
    apSrc[3] = aCD[3].pLight;
    apSrc[4] = &LightFixed;
    g3dBlendLight(pDest, apSrc, 5, vTarget);

    pDest->vDiffuse[0] *= fScale;
    pDest->vDiffuse[1] *= fScale;
    pDest->vDiffuse[2] *= fScale;
}
