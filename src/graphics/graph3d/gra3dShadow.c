// graphics/graph3d/gra3dShadow.c -- projected ("map") shadows, FF3 0x1e4760..0x1e6698
// plus the linkonce helpers 0x2bcbc0..0x2bcc90.
//
// From MioPan's gra3dShadow.c, checked function by function against FF3. FF3's
// changes: the debug switches, assign group, source model and DispShadowSprite
// are gone; the project models carry their coordinates (a parallel list) and
// are all projected, the caster included; the bounding-box clip test is the
// real VU0 one (FF2 port's pass-through replaced) with two edge groups; the
// fundament scale is 40 (FF2 1000), the depth push 400 (FF2 10000), the shadow
// camera's far / zmax 4095 / 65535, vOffsetData.z 25 (FF2 625); SetUpShadow
// no longer patches the GIF tags, and CLAMP_1 (not TEX1_1) gets 5.
//
// PC: the VU1 projection program (0x2e6ac0) has no interpreter. The caster
// draw in _RenderShadow and every receiver mesh in ShadowMeshDataVU are
// bracketed for the host renderer, which renders the casters into its shadow
// map from the light camera _ApplyCamera installs and replays the receivers
// through a projective shader (reipan_renderer.h "Projected shadows"). The
// DMA packets are still built so the emulated chain matches the ROM's.

#include "graphics/graph3d/gra3dShadow.h"

#include <algorithm>                                // std::min_element, std::fill
#include <math.h>                                   // fabsf, sqrtf
#include <string.h>                                 // memcpy

#include "common/vu0math.h"                         // IntLogBase, NormalizeAngle, g3dAtan2f
#include "graphics/graph3d/g3dCore.h"               // g3dSetGsRegister(s), g3dGetGsRegisterRef
#include "graphics/graph3d/g3dDma.h"                // g3dDmaCopyPacket, g3dDmaAddPacket
#include "graphics/graph3d/g3dGeom.h"               // g3dMatrixInverseTransform, g3dPlaneFromMatrixZ
#include "graphics/graph3d/g3dLight.h"              // G3DLIGHT, g3dCalcLightAttenuation
#include "graphics/graph3d/g3dRenderTarget.h"       // CRenderTarget
#include "graphics/graph3d/gra3d.h"                 // GRA3DCAMERA, gra3dcam*, gra3dGetFogRef
#include "graphics/graph3d/gra3dBoundingBox.h"      // gra3dbbApplyMatrix, gra3dbbCalcCenterBase
#include "graphics/graph3d/gra3dDma.h"              // gra3dDmaLoadVu1MicroProgram, gra3dCallMicroSubroutine3/4
#include "graphics/graph3d/gra3dSGD.h"              // _gra3dDrawSGD, _GetVUVNPRIM, _GetCoordCache
#include "graphics/graph3d/sgd_types.h"             // SGDPROCUNITHEADER, SGDCOORDINATE
#include "reipan/rendering/reipan_graph3d.h"        // PC: ReiPan_Graph3dDrawPresetMesh
#include "reipan/rendering/reipan_renderer.h"       // PC: ReiPan_RendererShadow*
#include "sdk/libgraph.h"                           // SCE_GS_*
#include "system/os/system.h"                       // GetSysWrk, GetPacketBufMgr

#define SHADOW_VU1MPG           0x2e6ac0    // the projection pass's VU1 program (EE address)
#define SHADOW_NUM_PROJECTMODEL 50

// layout: host -- host pointers (EE 0xcc bytes: 50 words and the sentinel).
// FF2 ctl/fixed_stack.h as FF3 instantiates it: 50 slots and a null sentinel
// after them, no count -- size() is the index of the first slot equal to the
// sentinel. Two of them: 0x3eeb20 (models) and 0x3eebf0 (their coordinates).
template <class T>
struct ShadowFixedStack
{
    T aData[SHADOW_NUM_PROJECTMODEL];
    T null;                                 // EE +0xc8

    // FF3 0x002bcc60 (fixed_stack<SGDFILEHEADER*,50>::size; inlined for the
    // coordinate list).
    u_int size() const
    {
        u_int i;

        for (i = 0; i < SHADOW_NUM_PROJECTMODEL; i++)
        {
            if (aData[i] == null)
            {
                return i;
            }
        }
        return SHADOW_NUM_PROJECTMODEL;
    }

    // FF3 0x002bcc00 (models) / 0x002bcc30 (coordinates): std::fill with the
    // sentinel.
    void clear()
    {
        std::fill(&aData[0], &aData[SHADOW_NUM_PROJECTMODEL], null);
    }
};

// ROM data 0x328b00: the VU1 image gra3dshadowInit copies into the live
// layout. Alpha 64 makes _IsDraw true before any _CalcColor.
static const GRA3DSCRATCHPADLAYOUT_MAPSHADOW s_gra3dScratchpadLayoutDefault = {
    .qwVif1Code = { 0, 0, 0x01000404, 0x6c190000 },
    .Vu1Mem = {
        ._vf01        = { 1.0f, 1.0f, 1.0f, -1.0f },
        ._vf02        = { 1.0f, 0.0f, 0.0f, 0.0f },
        .auiDBAddress = { 0x60, 0x230, 0, 0 },
        .vOffsetData  = { 0.0f, 0.0f, 25.0f, 0.0f },
        .gtTRISTRIP   = { 0x00008000, 0x302a4000, 0x00000412, 0 },
        .gtTRIFAN     = { 0x00008000, 0x302ac000, 0x00000412, 0 },
        .ivColor      = { 128, 128, 128, 64 },
    },
};
static GRA3DSCRATCHPADLAYOUT_MAPSHADOW *s_pScratchpadLayout;     // sdata 0x370cb8 (gp-0x2838)

// ROM data 0x328ca0: SetShadowCamera writes only the target, position and
// aspect; the rest comes from here (fov 60 degrees, orthographic).
static GRA3DCAMERA s_Camera = {
    .fFov     = 1.04719755f,
    .fNearZ   = 0.1f,
    .fFarZ    = 4095.0f,
    .fAspectX = 1.0f,
    .fAspectY = 1.0f,
    .fCenterX = 2048.0f,
    .fCenterY = 2048.0f,
    .fZmin    = 0.0f,
    .fZmax    = 65535.0f,
    .type     = PT_ORTHO,
    .aiPad    = { 0, 0 },
    .vTarget  = { 0.0f, 120.0f, 0.0f, 0.0f },
};

// ROM data 0x328ec0: the shadow texture's corners (unit square round the
// origin) for AppendShadowClipCheck.
static float shadowtex[4][4] = {
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
};
// ROM data 0x328f00: projected box -> clip space of the texture rectangle.
static sceVu0FMATRIX clipmtx = {
    {  1.0f,  0.0f,  0.0f, 0.0f },
    {  0.0f,  1.0f,  0.0f, 0.0f },
    {  0.0f,  0.0f,  1.0f, 0.0f },
    { -0.5f, -0.5f, -0.5f, 0.5f },
};
// An edge of the box as corner indices (FF2 BoundLine).
struct BoundLine
{
    int s;
    int e;
};
static_assert(sizeof(BoundLine) == 8, "BoundLine is 8 bytes");
// ROM data 0x328f40: two groups of four edges (FF2 had twelve); each group's
// first edge gives the separating axis, the four starts its points.
static BoundLine boundline[8] = {
    { 0, 1 }, { 4, 5 }, { 2, 3 }, { 6, 7 },
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
};
// ROM rdata 0x35a930: texture space from the projector's xy.
static const sceVu0FMATRIX s_matTransTexture = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.5f, 0.5f, 0.0f, 1.0f },
};
static const sceVu0FVECTOR s_v0001 = { 0.0f, 0.0f, 0.0f, 1.0f };   // rdata 0x329500 (g_v0001)

// FF3 0x001e6578 (__static_initialization_and_destruction_0, run by
// 0x1e6658 / 0x1e6678): constructs s_RenderTarget and fills both lists with
// their null sentinels -- the C++ definitions below do the same.
static CRenderTarget                     s_RenderTarget;          // bss 0x3ee950
static sceVu0FVECTOR                     s_vDirection;            // bss 0x3eea00, towards the light
static sceVu0FMATRIX                     s_matIP;                 // bss 0x3eea10, world -> shadow texture
static sceVu0FMATRIX                     s_matCull;               // bss 0x3eea50
static sceVu0FVECTOR                     s_avBB[9];               // bss 0x3eea90, [8] the target
static ShadowFixedStack<SGDFILEHEADER *> s_stackpProjectModel;    // bss 0x3eeb20
static ShadowFixedStack<SGDCOORDINATE *> s_stackpProjectCoord;    // bss 0x3eebf0
static G3DLIGHT                          s_Light;                 // bss 0x3eecc0
static float                             s_fFundamentScale;       // sbss gp-0x140
static float                             s_fTextureScale;         // sbss gp-0x13c
static int                               s_iMaxTextureWidth;      // sbss gp-0x138, a log2 (unread)
static int                               s_iMaxTextureHeight;     // sbss gp-0x134, a log2 (unread)

// PC: the receiver block's local->world, taken by AssignShadowPrim's box case
// so ShadowMeshDataVU can hand it to the host renderer (on the PS2 it only
// reaches VU1, folded into matLIP).
static float (*s_pHostReceiverLocalWorld)[4];

// ──────────────────────────────────────────────────────────────────────
// Small helpers
// ──────────────────────────────────────────────────────────────────────

// The frame buffer the shadow pass writes through: FBP 0 or 0x8c by frame
// parity. FF3 0x001e4760
static int _GetCurrentFBP(void)
{
    if ((GetSysWrk()->count & 1) == 0)
    {
        return 0;
    }
    return 0x8c;
}

// The projector colour's alpha is not 0. FF3 0x001e47a0
static int _IsDraw(void)
{
    return s_pScratchpadLayout->Vu1Mem.ivColor[3] != 0;
}

// The VU1 image as one packet (0x1a quadwords). FF3 0x001e47b0
static void SetVU1HeaderShadow(void)
{
    g3dDmaCopyPacket(s_pScratchpadLayout, 0x1a);
}

// ──────────────────────────────────────────────────────────────────────
// Receiver meshes
// ──────────────────────────────────────────────────────────────────────

// A preset mesh of type iMT_2 / iMT_2F (& 0x53 == 0x12; FF3's second test,
// 0x32, can never match after the mask): its vertices and strips, then the
// projection routine (the clipping one when the block crosses the guard
// band). FF3 0x001e47d0
static void ShadowMeshDataVU(SGDPROCUNITHEADER *pPUHead)
{
    PACKET_BUF_MGR *pPBM  = GetPacketBufMgr();     // FF3 caches it in g3dDma.c's sdata 0x372850
    int             mtype = pPUHead->VUMeshDesc.ucMeshType & 0x53;

    if (mtype != 0x12 && mtype != 0x32)
    {
        return;
    }
    pPBM->AddRefTag((u_int)ReiPan_GetPs2Address(_GetVUVNPRIM() + 1), _GetVUVNPRIM()->VUVNDesc.ucSize, 0, 0);
    g3dDmaAddPacket(pPUHead + 2, pPUHead->VUMeshDesc.iTagSize);

    // PC: the same block as a host shadow receiver (the preset bridge: the
    // receivers are room geometry, preset SGDs).
    if (s_pHostReceiverLocalWorld != nullptr)
    {
        ReiPan_RendererShadowBeginReceiver();
        ReiPan_Graph3dDrawPresetMesh(gra3dsgdGetData(), _GetVUVNPRIM(), pPUHead,
                                     &s_pHostReceiverLocalWorld[0][0]);
        ReiPan_RendererShadowEndReceiver();
    }

    if (_GetEdgeCheck() == 0)
    {
        gra3dCallMicroSubroutine4(0x1a68);
        return;
    }
    gra3dCallMicroSubroutine3(0xb20);
}

// ──────────────────────────────────────────────────────────────────────
// Bounding box vs the shadow volume
// ──────────────────────────────────────────────────────────────────────

// vDest = mat * (vSrc.xyz, 1) and VCLIPw.xyz of it: bit 0/1 x > |w| / x < -|w|,
// 2/3 y, 4/5 z (the newest judgement of the CLIP flags; every caller masks
// to it). PC: mat is the matrix FF3 leaves in vf4..vf7. FF3 0x001e48c0
static u_int ShadowBoundClip(float *vDest, const float *vSrc, float (*mat)[4])
{
    float fW;
    u_int clip;
    int   c;

    for (c = 0; c < 4; c++)
    {
        vDest[c] = mat[0][c] * vSrc[0] + mat[1][c] * vSrc[1] + mat[2][c] * vSrc[2] + mat[3][c];
    }
    fW   = fabsf(vDest[3]);
    clip = 0;
    for (c = 0; c < 3; c++)
    {
        if (vDest[c] > fW)
        {
            clip |= 1u << (c * 2);
        }
        if (vDest[c] < -fW)
        {
            clip |= 2u << (c * 2);
        }
    }
    return clip;
}

// Separating-axis test in the texture plane: project the box points
// sts[bl[0..3].s] and the texture corners on the normal of edge bl[0]
// (slope kei: (kei, -1)) and test whether the ranges overlap. A vertical or
// flat edge counts as overlapping. FF3 0x001e48f8
static int AppendShadowClipCheck(float (*sts)[4], BoundLine *bl)
{
    float kei;
    float bmin;
    float bmax;
    float smin;
    float smax;
    float fB;
    float fS;
    int   i;

    if (sts[bl[0].e][0] == sts[bl[0].s][0])
    {
        kei = 0.0f;
    }
    else
    {
        kei = (sts[bl[0].e][1] - sts[bl[0].s][1]) / (sts[bl[0].e][0] - sts[bl[0].s][0]);
    }
    if (kei == 0.0f)
    {
        return 1;
    }

    bmin = bmax = sts[bl[0].s][0] * kei - sts[bl[0].s][1];
    smin = smax = shadowtex[0][0] * kei - shadowtex[0][1];
    for (i = 1; i < 4; i++)
    {
        fB = sts[bl[i].s][0] * kei - sts[bl[i].s][1];
        fS = shadowtex[i][0] * kei - shadowtex[i][1];
        if (fB > bmax)
        {
            bmax = fB;
        }
        if (fB < bmin)
        {
            bmin = fB;
        }
        if (fS > smax)
        {
            smax = fS;
        }
        if (fS < smin)
        {
            smin = fS;
        }
    }

    if (bmin <= smax)
    {
        return (bmax < smin) ? 0 : 1;
    }
    return 0;
}

// Does the block's box (local corners avBB, placed by lwmtx) reach the
// shadow? First against the culling half-spaces (all corners behind one of
// -x / -y / -z: no), then projected into the texture rectangle: a corner
// inside, or corners on both sides of every edge plus both axis tests.
// FF3 0x001e4a80 (dir, FF2's third argument, is passed and unused).
static int CheckBoundingBoxShadowTrace(float (*lwmtx)[4], float (*avBB)[4], float *dir)
{
    sceVu0FMATRIX tmpmat;
    sceVu0FMATRIX setmat;
    sceVu0FVECTOR avWork[8];                // FF3: scratchpad 0x70003900
    u_int         clipAnd;
    u_int         clipOr;
    u_int         clip;
    int           i;

    (void)dir;
    sceVu0MulMatrix(setmat, s_matCull, lwmtx);
    clipAnd = 0x2a;
    for (i = 0; i < 8; i++)
    {
        clipAnd &= ShadowBoundClip(avWork[i], avBB[i], setmat);
    }
    if (clipAnd != 0)
    {
        return 0;
    }

    sceVu0MulMatrix(tmpmat, s_matIP, lwmtx);
    sceVu0MulMatrix(setmat, clipmtx, tmpmat);
    clipAnd = 0x2f;
    clipOr  = 0;
    for (i = 0; i < 8; i++)
    {
        clip     = ShadowBoundClip(avWork[i], avBB[i], setmat) & 0xf;
        clipOr  |= clip;
        clipAnd &= clip;
        if (clip == 0)
        {
            return 1;
        }
    }
    if (clipAnd != 0)
    {
        return 0;
    }
    if (clipOr != 0xf)
    {
        return 1;
    }
    if (AppendShadowClipCheck(avWork, &boundline[0]) != 0 &&
        AppendShadowClipCheck(avWork, &boundline[4]) != 0)
    {
        return 1;
    }
    return 0;
}

// The box unit's test; a hit forces the clipping VU1 routine (edge check 1).
// FF3 0x001e4d78
static int CheckBoundingBoxShadow(SGDPROCUNITHEADER *pPUHead)
{
    SGDCOORDINATE *pCoord = gra3dsgdGetCoordinate(pPUHead->BoundingBoxDesc.iCoordId);
    int            clip;

    clip = CheckBoundingBoxShadowTrace(pCoord->matLocalWorld, (float (*)[4])(pPUHead + 1), s_vDirection);
    if (clip != 0)
    {
        _SetEdgeCheck(1);
    }
    return clip != 0;
}

// FF3 0x001e4dd8
static void _CalcTarget(float *vTarget, float (*avBB)[4])
{
    gra3dbbCalcCenterBase(vTarget, avBB);
}

// Within 0.02 of the target in XZ (FF3 sums only x and z: vaddz.x; FF2 0.5
// in 3D). FF3 0x001e4df0
static int _IsSamePositionAsTarget(float *vPosition)
{
    float dx = vPosition[0] - s_avBB[8][0];
    float dz = vPosition[2] - s_avBB[8][2];

    return sqrtf(dx * dx + dz * dz) < 0.02f;
}

// ──────────────────────────────────────────────────────────────────────
// Receiver blocks
// ──────────────────────────────────────────────────────────────────────

// callcheck: memcpy
//   (the CoordCache copies are struct assignments)
// Walk one block's units for the projection pass: VUVN units are remembered,
// meshes queued (ShadowMeshDataVU), and each bounding-box unit tests its box
// against the shadow (a miss, or the caster's own box, ends the block) and,
// when its coordinate's matrix changed, loads the receiver matrices and the
// VU1 image. FF3 0x001e4e48
void AssignShadowPrim(SGDPROCUNITHEADER *pPUHead)
{
    sceVu0FMATRIX                    matInv;     // FF3: scratchpad 0x70003900
    sceVu0FVECTOR                    avBBWorld[8];
    sceVu0FVECTOR                    vCenter;    // FF3: scratchpad 0x70003980
    sceVu0FVECTOR                    tmpvec;
    CoordCache                       CC;
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pSL;
    SGDCOORDINATE                   *pCoord;
    GRA3DCAMERA                     *pCamera;
    int                              cn;
    int                              i;
    int                              j;
    int                              c;

    s_pHostReceiverLocalWorld = nullptr;        // PC
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;

        case SGDPU_MESH:
            ShadowMeshDataVU(pPUHead);
            break;

        case SGDPU_BOUNDING_BOX:
            if (CheckBoundingBoxShadow(pPUHead) == 0)
            {
                return;
            }
            cn     = pPUHead->BoundingBoxDesc.iCoordId;
            pCoord = gra3dsgdGetCoordinate(cn);
            gra3dbbApplyMatrix(avBBWorld, (float (*)[4])(pPUHead + 1), pCoord->matLocalWorld);
            gra3dbbCalcCenterBase(vCenter, avBBWorld);
            if (_IsSamePositionAsTarget(vCenter) != 0)
            {
                return;
            }
            s_pHostReceiverLocalWorld = pCoord->matLocalWorld;     // PC

            // the cached coordinate is the same matrix: keep what VU1 has
            if (_GetCoordCache()->cache_on == 1 && _GetCoordCache()->edge_check == _GetEdgeCheck())
            {
                for (i = 0; i < 4; i++)
                {
                    for (j = 0; j < 4; j++)
                    {
                        if (gra3dsgdGetCoordinate(cn)->matLocalWorld[i][j] !=
                            gra3dsgdGetCoordinate(_GetCoordCache()->cn0)->matLocalWorld[i][j])
                        {
                            break;
                        }
                    }
                    if (j < 4)
                    {
                        CC          = *_GetCoordCache();
                        CC.cache_on = -1;
                        _SetCoordCache(&CC);
                        break;
                    }
                }
            }
            if (_GetCoordCache()->cache_on == 1)
            {
                break;
            }

            pSL           = s_pScratchpadLayout;
            pCoord        = gra3dsgdGetCoordinate(cn);
            CC            = *_GetCoordCache();
            CC.cache_on   = 1;
            CC.edge_check = _GetEdgeCheck();
            CC.cn0        = cn;
            _SetCoordCache(&CC);

            // the light direction in the block's own space
            g3dMatrixInverseTransform(matInv, pCoord->matLocalWorld);
            for (c = 0; c < 4; c++)
            {
                tmpvec[c] = matInv[0][c] * s_vDirection[0] + matInv[1][c] * s_vDirection[1] +
                            matInv[2][c] * s_vDirection[2];
            }
            sceVu0MulMatrix(pSL->Vu1Mem.matLIP, s_matIP, pCoord->matLocalWorld);
            pCamera = gra3dGetCamera();
            sceVu0MulMatrix(pSL->Vu1Mem.matLocalScreen, pCamera->matWorldScreen, pCoord->matLocalWorld);
            pCamera = gra3dGetCamera();
            sceVu0MulMatrix(pSL->Vu1Mem.matLocalClip, pCamera->matWorldClipPolygon, pCoord->matLocalWorld);
            pSL->Vu1Mem.matLIP[0][3] = -tmpvec[0];
            pSL->Vu1Mem.matLIP[1][3] = -tmpvec[1];
            pSL->Vu1Mem.matLIP[3][3] = 0.0f;
            pSL->Vu1Mem.matLIP[2][3] = -tmpvec[2];
            pSL->Vu1Mem.matLocalScreen[3][2] += 400.0f;
            SetVU1HeaderShadow();
            break;

        default:                                // SGDPU_COORDINATE and the rest
            break;
        }
        pPUHead = SgdPtr<SGDPROCUNITHEADER>(pPUHead->pNext);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Projection pass setup
// ──────────────────────────────────────────────────────────────────────

// Fog, the VU1 program, the GS defaults, then the shadow texture as TEX0 with
// clamped wrapping. FF3 0x001e52e8
static void SetUpShadow(void)
{
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pSL = s_pScratchpadLayout;
    GS_REG_SET                       aGPA[3];

    g3dCalcVu1Fog(&pSL->Vu1Mem.Fog, gra3dGetFogRef());
    gra3dDmaLoadVu1MicroProgram(SHADOW_VU1MPG, 0);
    gra3dSetGsRegisterDefault();
    aGPA[0].data = s_RenderTarget.GetGsTex0Ref();
    aGPA[0].reg  = SCE_GS_TEX0_1;
    aGPA[1].data = 5;                           // CLAMP_1: WMS = WMT = CLAMP
    aGPA[1].reg  = SCE_GS_CLAMP_1;
    aGPA[2].data = 0x5801b;
    aGPA[2].reg  = SCE_GS_TEST_1;
    g3dSetGsRegisters(aGPA, 3, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Projection matrix
// ──────────────────────────────────────────────────────────────────────

// A rotation that turns vSrc onto +z: about y (from vSrc's x / z), then
// about x (from the turned y / z). FF3 0x001e5398
static void GetRotMatrixZAxis(float (*matDest)[4], float *vSrc)
{
    static sceVu0FMATRIX s_matUnit = {              // rdata 0x3293c0 (g_matUnit)
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    sceVu0FVECTOR vWork;
    int           c;

    memcpy(matDest, s_matUnit, sizeof(s_matUnit));
    for (c = 0; c < 4; c++)
    {
        vWork[c] = vSrc[c];
    }
    if (vWork[0] != 0.0f || vWork[2] != 0.0f)
    {
        sceVu0RotMatrixY(matDest, s_matUnit, NormalizeAngle(-g3dAtan2f(vSrc[0], vSrc[2])));
        sceVu0ApplyMatrix(vWork, matDest, vSrc);
    }
    if (vWork[1] != 0.0f || vWork[2] != 0.0f)
    {
        sceVu0RotMatrixX(matDest, matDest, NormalizeAngle(g3dAtan2f(vWork[1], vWork[2])));
    }
}

// World -> shadow texture: the texture matrix with its rows scaled by the
// aspect / texture size (and z flipped), turned towards the light, moved to
// the target; z offset so the nearest box corner sits at 0. FF3 0x001e54a8
static void _CalcProjectionShadowMatrix(float (*matProjection)[4], float ax, float ay,
                                        float fTextureScale, int iTexWidth, int iTexHeight,
                                        float *vLightDir, float (*avBBWorld)[4])
{
    sceVu0FVECTOR vScale;
    sceVu0FMATRIX matRot;
    sceVu0FVECTOR vTransInverse;
    sceVu0FVECTOR planeCull;
    int           aiBBIndex[8];
    float         culval[8];
    int           i;
    int           c;

    vScale[0] = (fTextureScale / (float)iTexWidth) * ax;
    vScale[1] = (-fTextureScale / (float)iTexHeight) * ay;
    vScale[2] = -1.0f;
    vScale[3] = 0.0f;
    for (i = 0; i < 3; i++)
    {
        for (c = 0; c < 4; c++)
        {
            matProjection[i][c] = s_matTransTexture[i][c] * vScale[i];
        }
    }
    for (c = 0; c < 4; c++)
    {
        matProjection[3][c] = s_matTransTexture[3][c];
    }

    GetRotMatrixZAxis(matRot, vLightDir);
    sceVu0MulMatrix(matProjection, matProjection, matRot);

    for (c = 0; c < 3; c++)
    {
        vTransInverse[c] = s_v0001[c] - s_avBB[8][c];
    }
    vTransInverse[3] = s_v0001[3];
    sceVu0ApplyMatrix(matProjection[3], matProjection, vTransInverse);

    g3dPlaneFromMatrixZ(planeCull, matProjection);
    for (i = 0; i < 8; i++)
    {
        aiBBIndex[i] = i;
    }
    for (i = 0; i < 8; i++)
    {
        float *v = avBBWorld[aiBBIndex[i]];

        culval[i] = planeCull[0] * v[0] + planeCull[1] * v[1] + planeCull[2] * v[2];
    }
    // FF3 0x002bcbc0: min_element<float*>
    matProjection[3][2] = -*std::min_element(&culval[0], &culval[8]);
}

// ──────────────────────────────────────────────────────────────────────
// Shadow texture size, culling matrix, shadow camera
// ──────────────────────────────────────────────────────────────────────

// callcheck: sceVu0MulVector
//   (the squared diagonal is written out)
// The size the box's diagonal asks for (1 / 2 / 4 / 8 tiles of 64), then
// 256x256 whatever it said. FF3 0x001e5690
static void CalcShadowHeight(float (*bbox)[4])
{
    sceVu0FVECTOR tmpvec;
    int           tmp;
    int           c;

    for (c = 0; c < 4; c++)
    {
        tmpvec[c] = (bbox[0][c] - bbox[7][c]) * (bbox[0][c] - bbox[7][c]);
    }
    tmp = (int)(sqrtf(tmpvec[0] + tmpvec[1] + tmpvec[2]) / s_fFundamentScale + 0.5f);
    if (tmp < 1)
    {
        tmp = 1;
    }
    else if (tmp == 3)
    {
        tmp = 4;
    }
    else if ((u_int)(tmp - 5) < 3)
    {
        tmp = 8;
    }
    s_RenderTarget.SetWidth(tmp << 6);
    s_RenderTarget.SetHeight(tmp << 6);
    s_RenderTarget.SetWidth(0x100);
    s_RenderTarget.SetHeight(0x100);
}

// s_matCull: mirror the axes the light comes from, then move the box's
// lowest corner to the origin, so a point in the shadow volume has x, y,
// z >= 0. FF3 0x001e57a8
static void _CalcCullingMatrix(void)
{
    sceVu0FMATRIX tmpmat;
    sceVu0FVECTOR avWork[8];                // FF3: scratchpad 0x70003900
    sceVu0FVECTOR tmpvec;
    int           i;
    int           c;

    sceVu0UnitMatrix(tmpmat);
    if (0.0f < s_vDirection[0])
    {
        tmpmat[0][0] = -1.0f;
    }
    if (0.0f < s_vDirection[1])
    {
        tmpmat[1][1] = -1.0f;
    }
    if (0.0f < s_vDirection[2])
    {
        tmpmat[2][2] = -1.0f;
    }
    gra3dbbApplyMatrix(avWork, s_avBB, tmpmat);

    tmpvec[0] = avWork[0][0];
    tmpvec[1] = avWork[0][1];
    tmpvec[2] = avWork[0][2];
    tmpvec[3] = 0.0f;
    for (i = 1; i < 8; i++)
    {
        for (c = 0; c < 3; c++)
        {
            if (avWork[i][c] < tmpvec[c])
            {
                tmpvec[c] = avWork[i][c];
            }
        }
    }
    for (i = 0; i < 3; i++)
    {
        for (c = 0; c < 4; c++)
        {
            s_matCull[i][c] = tmpmat[i][c];
        }
    }
    for (c = 0; c < 4; c++)
    {
        s_matCull[3][c] = tmpvec[c] * -1.0f;
    }
}

// The shadow camera looks at the target from 1000 along the light direction.
// FF3 0x001e58f0
static void _SetCameraCoord(void)
{
    sceVu0FVECTOR tmpvec;
    int           c;

    for (c = 0; c < 4; c++)
    {
        s_Camera.vTarget[c] = s_avBB[8][c];
    }
    sceVu0ScaleVector(tmpvec, s_vDirection, 1000.0f);
    sceVu0AddVector(s_Camera.matCoord[3], s_Camera.vTarget, tmpvec);
}

// The aspect that fits the box's screen extent into the texture (1, 1 when the
// camera stands on the target). FF3 0x001e5950
static void _CalcAspectRatioForRenderShadow(float *pAX, float *pAY)
{
    sceVu0FMATRIX matWorldScreen;
    sceVu0FVECTOR avWork[8];                // FF3: scratchpad 0x70003900
    float         xmax;
    float         ymax;
    int           i;

    if (_IsSamePositionAsTarget(s_Camera.matCoord[3]) != 0)
    {
        *pAX = 1.0f;
        *pAY = 1.0f;
        return;
    }

    s_Camera.fAspectX = 1.0f;
    s_Camera.fAspectY = 1.0f;
    gra3dCalcWorldScreenMatrix(matWorldScreen, &s_Camera, 1);
    gra3dbbApplyMatrix(avWork, s_avBB, matWorldScreen);
    xmax = 0.0f;
    ymax = 0.0f;
    for (i = 0; i < 8; i++)
    {
        if (xmax < fabsf(avWork[i][0] - s_Camera.fCenterX))
        {
            xmax = fabsf(avWork[i][0] - s_Camera.fCenterX);
        }
        if (ymax < fabsf(avWork[i][1] - s_Camera.fCenterY))
        {
            ymax = fabsf(avWork[i][1] - s_Camera.fCenterY);
        }
    }
    CalcShadowHeight(s_avBB);
    *pAX = ((float)s_RenderTarget.GetWidth() * 0.5f) / xmax;
    *pAY = ((float)s_RenderTarget.GetHeight() * 0.5f) / ymax;
}

// FF3 0x001e5ae8
static void SetShadowCamera(void)
{
    _SetCameraCoord();
    _CalcAspectRatioForRenderShadow(&s_Camera.fAspectX, &s_Camera.fAspectY);
    _CalcCullingMatrix();
}

// FF3 0x001e5b18
static void _ApplyCamera(GRA3DCAMERA *pCamera, int bFixUp)
{
    gra3dcamSetPosition(pCamera->matCoord[3]);
    gra3dcamSetTarget(pCamera->vTarget, bFixUp);
    gra3dcamSetAspect(pCamera->fAspectX, pCamera->fAspectY);
    gra3dcamSetFov(pCamera->fFov);
    gra3dcamSetDepth(pCamera->fZmin, pCamera->fZmax);
    gra3dcamSetType(pCamera->type);
    gra3dApplyCamera(nullptr, bFixUp, 1);
}

// ──────────────────────────────────────────────────────────────────────
// The two passes
// ──────────────────────────────────────────────────────────────────────

// Render the caster from the light into the shadow texture: frame buffer,
// shadow camera, blend / test / Z-write-off state, the target, a cleared
// texture with a one-pixel border, then the model. FF3 0x001e5b90
static void _RenderShadow(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, int iObjectIndex)
{
    GS_REG_SET aGPA0[5];
    GS_REG_SET aGPA1[2];
    u_long    *pZbuf;
    int        iWidth;
    int        iHeight;

    g3dSetGsRegister((u_long)_GetCurrentFBP() | 0x10a0000, SCE_GS_FRAME_1, 1);
    SetShadowCamera();
    _ApplyCamera(&s_Camera, 1);

    pZbuf = g3dGetGsRegisterRef(SCE_GS_ZBUF_1);
    aGPA0[0].data = 0x44;
    aGPA0[0].reg  = SCE_GS_ALPHA_1;
    aGPA0[1].data = 0x60;
    aGPA0[1].reg  = SCE_GS_TEX1_1;
    aGPA0[2].data = 0;
    aGPA0[2].reg  = SCE_GS_CLAMP_1;
    aGPA0[3].data = 0x30803;
    aGPA0[3].reg  = SCE_GS_TEST_1;
    aGPA0[4].data = (*pZbuf & 0xf000000) | 0x100000000ull;  // keep PSM, ZMSK on
    aGPA0[4].reg  = SCE_GS_ZBUF_1;
    g3dSetGsRegisters(aGPA0, 5, 1);

    iWidth  = s_RenderTarget.GetWidth();
    iHeight = s_RenderTarget.GetHeight();
    s_RenderTarget.Begin();
    s_RenderTarget.Clear();
    aGPA1[0].data = 0x3001b;
    aGPA1[0].reg  = SCE_GS_TEST_1;
    aGPA1[1].data = (u_long)(iWidth - 2) << 16 | (u_long)(iHeight - 2) << 48 | 0x100000001ull;
    aGPA1[1].reg  = SCE_GS_SCISSOR_1;
    g3dSetGsRegisters(aGPA1, 2, 1);

    // PC: the caster draws go to the host shadow map; EndCaster also takes the
    // light camera _ApplyCamera installed, before _DrawShadow restores the
    // game's.
    ReiPan_RendererShadowBeginCaster();
    _gra3dDrawSGD(pSGDTop, SRT_REALTIME, pCoord, iObjectIndex);
    ReiPan_RendererShadowEndCaster();
    s_RenderTarget.End();
}

// Back to the game camera, build the texture projection and run every
// project model through the SRT_MAPSHADOW pass with its coordinates.
// FF3 0x001e5d60
static void _DrawShadow(float ax, float ay, GRA3DCAMERA *pCamera)
{
    int i;

    _gra3dSetCameraForce(pCamera);
    // PC: _gra3dSetCameraForce only assigns the struct; put back the host
    // view / projection gra3dshadowDrawSGD saved.
    ReiPan_RendererShadowRestoreCamera();

    _CalcProjectionShadowMatrix(s_matIP, ax, ay, s_fTextureScale, s_RenderTarget.GetWidth(),
                                s_RenderTarget.GetHeight(), s_vDirection, s_avBB);
    SetUpShadow();

    // PC: the projector's darkness (_CalcColor's alpha, 0..255 on VU1).
    ReiPan_RendererShadowSetStrength((float)s_pScratchpadLayout->Vu1Mem.ivColor[3] / 255.0f);
    // PC: what the VU1 program culls receiver triangles by -- the projector z
    // (s_matIP's z row; below 0 between the caster and the light), the light
    // direction (matLIP's w column) and the camera's side of each face.
    {
        float plane[4] = { s_matIP[0][2], s_matIP[1][2], s_matIP[2][2], s_matIP[3][2] };

        ReiPan_RendererShadowSetProjector(plane, s_vDirection, pCamera->matCoord[3]);
    }

    for (i = 0; i < (int)s_stackpProjectModel.size(); i++)
    {
        _gra3dDrawSGD(s_stackpProjectModel.aData[i], SRT_MAPSHADOW, s_stackpProjectCoord.aData[i], -1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────

// callcheck: fill, memcpy, memset
//   (ShadowFixedStack::clear is the fill; the layout copy is a struct
//   assignment; memset clears the TEX0 word)
// FF3 0x001e5e88
void gra3dshadowInit(GRA3DSHADOWCREATIONDATA *pCD)
{
    RENDERTARGETCREATIONDATA rtCD;
    u_long                   gsTex0;
    int                      iBlockW;

    s_pScratchpadLayout  = pCD->pSL;
    *s_pScratchpadLayout = s_gra3dScratchpadLayoutDefault;

    // TEX0: TBP0 0x3000, TW / TH log2(256), TBW from the width, TCC 1,
    // CBP 0x3000, CLD 1 (bits written as the ROM does)
    gsTex0  = 0;
    gsTex0  = (gsTex0 & 0xfffffffffc0fc000ull) | 0x3000;
    gsTex0  = (gsTex0 & 0xffffffffc3ffffffull) | ((u_long)IntLogBase(2, 0x100) & 0xf) << 26;
    iBlockW = 1 << (int)((gsTex0 & 0x3c000000) >> 26);
    if (iBlockW < 0)
    {
        iBlockW += 0x3f;
    }
    gsTex0 = (gsTex0 & 0x43ff03fffull) | ((u_long)IntLogBase(2, 0x100) & 0xf) << 30 |
             ((u_long)(iBlockW >> 6) & 0x3f) << 14 | 0x2006000400000000ull;

    s_iMaxTextureWidth  = IntLogBase(2, 0x100);
    s_iMaxTextureHeight = IntLogBase(2, 0x100);
    s_fTextureScale     = 1.0176f;              // sdata 0x370ce0
    s_fFundamentScale   = 40.0f;                // sdata 0x370ce4
    s_stackpProjectModel.clear();
    s_stackpProjectCoord.clear();

    rtCD.gsTex0     = gsTex0;
    rtCD.ClearColor = 0;
    rtCD.fZMax      = 0.771114647f;             // sdata 0x370ce8
    s_RenderTarget.Create(&rtCD);
}

// FF3 0x001e60a0
void gra3dshadowAddProjectModel(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord)
{
    u_int n;
    u_int i;

    if (pSGDTop == nullptr || s_stackpProjectModel.size() == SHADOW_NUM_PROJECTMODEL)
    {
        return;
    }
    n = s_stackpProjectModel.size();
    for (i = 0; i < n; i++)
    {
        if (s_stackpProjectModel.aData[i] == pSGDTop)
        {
            return;
        }
    }
    s_stackpProjectModel.aData[s_stackpProjectModel.size()] = pSGDTop;
    // ROM: a full coordinate list would write the sentinel slot; the two lists
    // grow together, so it never is.
    s_stackpProjectCoord.aData[s_stackpProjectCoord.size()] = pCoord;
}

// callcheck: fill
//   (ShadowFixedStack::clear)
// FF3 0x001e6198
void gra3dshadowClearProjectModel(void)
{
    s_stackpProjectModel.clear();
    s_stackpProjectCoord.clear();

    // PC: once a frame before the rooms draw (MapDrawAll) -- where the host's
    // shadow capture state is reset too.
    ReiPan_RendererShadowReset();
}

// callcheck: memcpy
//   (the save / restore of s_Light is LightOrigin)
// The projector colour: the light's diffuse at the target (a spot widened:
// outer angle doubled, inner halved, for this only), alpha = its length /
// sqrt(3) / 2, all * 255. FF3 0x001e61d0
static void _CalcColor(void)
{
    G3DLIGHT      LightOrigin = s_Light;
    sceVu0FVECTOR vWork;
    float         fAtten;
    int           c;

    if (s_Light.Type == G3DLIGHT_SPOT)
    {
        s_Light.fAngleOutside += s_Light.fAngleOutside;
        s_Light.fAngleInside *= 0.5f;
    }
    fAtten = g3dCalcLightAttenuation(&s_Light, s_avBB[8]);
    for (c = 0; c < 4; c++)
    {
        vWork[c] = s_Light.vDiffuse[c] * fAtten;
    }
    vWork[3] = (sqrtf(vWork[0] * vWork[0] + vWork[1] * vWork[1] + vWork[2] * vWork[2]) / sqrtf(3.0f)) * 0.5f;
    for (c = 0; c < 4; c++)
    {
        s_pScratchpadLayout->Vu1Mem.ivColor[c] = (int)(vWork[c] * 255.0f);
    }
    s_Light = LightOrigin;
}

// The direction towards the light: a directional light's own, a spot's from
// the target to its position (normalised xyz), 0 for the invalid type; a point
// light leaves the last one (FF2 asserted). FF3 0x001e6310
static void _CalcDirection(void)
{
    float fLen;
    int   c;

    switch (s_Light.Type)
    {
    case G3DLIGHT_DIRECTIONAL:
        for (c = 0; c < 4; c++)
        {
            s_vDirection[c] = s_Light.vDirection[c];
        }
        break;

    case G3DLIGHT_SPOT:
        for (c = 0; c < 4; c++)
        {
            s_vDirection[c] = s_Light.vPosition[c] - s_avBB[8][c];
        }
        fLen = Vu0Rsqrt(s_vDirection[0] * s_vDirection[0] + s_vDirection[1] * s_vDirection[1] +
                        s_vDirection[2] * s_vDirection[2]);     // PC: PS2 rsqrt
        for (c = 0; c < 3; c++)
        {
            s_vDirection[c] *= fLen;
        }
        break;

    case 0x7fffffff:                            // G3DLIGHTTYPE_FORCE_DWORD: g_v0000
        for (c = 0; c < 4; c++)
        {
            s_vDirection[c] = 0.0f;
        }
        break;

    default:                                    // G3DLIGHT_POINT and others
        break;
    }
}

// FF3 0x001e63d0
void gra3dshadowSetBoundingBox(float (*avBB)[4], float (*mat)[4])
{
    gra3dbbApplyMatrix(s_avBB, avBB, mat);
    _CalcTarget(s_avBB[8], s_avBB);
    _CalcDirection();
    _CalcColor();
}

// FF3 0x001e6418
void gra3dshadowSetLight(G3DLIGHT *pLight)
{
    s_Light = *pLight;
    _CalcDirection();
    _CalcColor();
}

// FF3 0x001e6480
void gra3dshadowDrawSGD(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, int iBlock)
{
    GRA3DCAMERA camOrigin = *gra3dGetCamera();

    // PC: the host keeps view / projection in the renderer, not in the struct.
    ReiPan_RendererShadowSaveCamera();

    if (_IsDraw() != 0)
    {
        _RenderShadow(pSGDTop, pCoord, iBlock);
        _DrawShadow(-s_Camera.fAspectX, -s_Camera.fAspectY, &camOrigin);
        g3dSetGsRegister(0, SCE_GS_CLAMP_1, 1);
    }
}
