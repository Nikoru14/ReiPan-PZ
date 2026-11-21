/* ==========================================================================
 *  graphics/graph3d/gra3dSGD.c
 *
 *  The SGD model walker. A model's blocks are chains of process units
 *  (sgd_types.h); _gra3dDrawSGD walks them per render type:
 *   - realtime: VUVN / mesh / material units become VU1 packets, a bounding
 *     box unit culls its block and sends the coordinate's light block; the
 *     last block (skinned meshes) is drawn after the others, with the weighted
 *     vertices blended on the EE;
 *   - preset (pre-lit models): the same with the VU1 routines for baked
 *     colours, TRI2 textures sent ahead of the first visible block;
 *   - prelighting / clear: the EE bakes (or clears) the preset colours with
 *     gra3dCalcVertexColor, lights fed into the g3d core three per type;
 *   - map shadow: blocks in the view volume go to gra3dShadow.c.
 *  Also: ST scrolling, preset vertex colours, material alpha, and the walker
 *  gra3dDrawModelCb uses for a model's screen rectangle.
 *
 *  Ported from MioPan's gra3dSGD.c (FF2 0x1b7c60..0x1bba08) and checked
 *  function by function against FF3 0x1e0330..0x1e3850 (+ linkonce 0x2bcad0..
 *  0x2bcb80). FF3 changes: 27 point / 9 spot lights, a prelight switch per
 *  light, the spot ranking threshold (gra3dGetDrawParamI / F), material
 *  packets cached in EE memory again (iTagAddressOld, no toggle reset), colour
 *  markers (a colour word of 1) left alone by the prelight / colour editors,
 *  separate VU1 programs for preset iMT_0 and iMT_2 meshes, and the out-of-line
 *  material-index test _IsSameMaterialIndexCache.
 *
 *  PC: the VU1 never runs. The packets are still built as on the EE, and the
 *  host renderer gets each mesh through reipan_graph3d.h at the points MioPan
 *  calls its bridge. SGD pointers are EE addresses (SgdPtr / SgdAddr).
 * ======================================================================== */

#include "graphics/graph3d/gra3dSGD.h"

#include <math.h>                               // sqrtf, fabsf
#include <string.h>                             // memcpy, memcmp
#include <algorithm>                            // std::fill

#include "graphics/graph3d/g3dCore.h"           // g3dLightEnable, g3dSetLight, g3dApplyLight, g3dSetGsRegister
#include "graphics/graph3d/g3dDma.h"            // g3dDmaOpenPacket, g3dDmaClosePacket, g3dDmaAddPacket
#include "graphics/graph3d/g3dLightEx.h"        // g3dIsBBLightingup
#include "graphics/graph3d/gra3d.h"             // camera, light bank, transforms, VU1 light / material blocks
#include "graphics/graph3d/gra3dBoundingBox.h"  // gra3dbbApplyMatrix
#include "graphics/graph3d/gra3dDma.h"          // gra3dSetVif1Code_Unpack, micro-program / subroutine kicks
#include "graphics/graph3d/gra3dMirror.h"       // Vu0LoadMatrices_4_25_8, gra3dCalcMeshScreenRect
#include "graphics/graph3d/gra3dSGDData.h"      // sgdResetMaterialCache
#include "graphics/graph3d/gra3dShadow.h"       // AssignShadowPrim
#include "graphics/graph3d/gra3dTRI2.h"         // gra3dLoadTRI2FileToVRAM
#include "sdk/libvu0.h"                         // sceVu0MulMatrix

#include "reipan/rendering/reipan_renderer.h"
#include "reipan/rendering/reipan_graph3d.h"    // PC: the host renderer's mesh hooks

// ──────────────────────────────────────────────────────────────────────
// Packets and constants
// ──────────────────────────────────────────────────────────────────────

// A VU1 light block with its UNPACK code (_SetCoordData, SgSortUnitPrimPost).
struct GRA3DVU1LIGHTPACKET              /* 0x160 */
{
    /* 0x000 */ u_int             qwVif1Code[4];
    /* 0x010 */ GRA3DVU1LIGHTDATA Data;
};
static_assert(sizeof(GRA3DVU1LIGHTPACKET) == 0x160, "GRA3DVU1LIGHTPACKET is 0x160 bytes");
static_assert(offsetof(GRA3DVU1LIGHTPACKET, Data) == 0x10, "GRA3DVU1LIGHTPACKET.Data");

// A VU1 material block with its UNPACK code (SetMaterialDataVU: spot, point,
// directional, 8 quadwords each).
struct GRA3DVU1MATERIALPACKET           /* 0x80 */
{
    /* 0x00 */ u_int                      qwVif1Code[4];
    /* 0x10 */ GRA3DVU1MATERIALDATA_POINT Data;    // point / spot; directional has the same size
};
static_assert(sizeof(GRA3DVU1MATERIALPACKET) == 0x80, "GRA3DVU1MATERIALPACKET is 0x80 bytes");
static_assert(sizeof(GRA3DVU1MATERIALDATA_DIRECTIONAL) == sizeof(GRA3DVU1MATERIALDATA_POINT),
              "directional and point material blocks share one packet size");

// UNPACK targets in VU1 memory (quadword addresses) and the V4-32 command.
#define VU1ADDR_LIGHT                   0x19
#define VU1ADDR_MATERIAL_DIRECTIONAL    0x2e
#define VU1ADDR_MATERIAL_SPOT           0x35
#define VU1ADDR_MATERIAL_POINT          0x3c
#define VIF1_UNPACK_V4_32               0x6c

// VU1 microprograms (EE addresses of their DMA chains in .data).
#define VU1MPG_REALTIME                 0x2df690
#define VU1MPG_PRESET_0                 0x2e11b0    // iMT_0
#define VU1MPG_PRESET_2                 0x2e3780    // iMT_2 / iMT_2F

// Ranked light, sorted by the power reaching a box (FF2 _LIGHTCOMPAREDATA).
struct _LIGHTCOMPAREDATA                /* 0x8 */
{
    /* 0x0 */ float fMaxPower;
    /* 0x4 */ int   iIndex;                     // index within its type, -1: empty
};
static_assert(sizeof(_LIGHTCOMPAREDATA) == 0x8, "_LIGHTCOMPAREDATA is 8 bytes");

// PC: the weighted-blend kernels read both bone matrices from VU0 vf4..vf7 /
// vf8..vf11 on the EE; here they come as arguments.
typedef void (*LPFUNC_CALCWEIGHTEDVECTORBUFFER)(float *dp, const float *v, float (*mat0)[4], float (*mat1)[4]);

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

static float (*s_pGlobalVertexBuffer)[4];       // sdata 0x370c80: blended skin vertices
static float (*s_pGlobalNormalBuffer)[4];       // sdata 0x370c84: blended skin normals
static int    s_iGlobalBufferSize;              // sdata 0x370c88: quadwords in each
static SGDMATERIAL *s_pMatOld;                  // sdata 0x370c8c: material sent last
static int    iCount_GRA3DDL;                   // sdata 0x370c98: prelight progress per light type
static int    iCount_GRA3DPL;                   // sdata 0x370c9c
static int    iCount_GRA3DSL;                   // sdata 0x370ca0
// sdata 0x370ca8 = { 0.0, -1 }: the -1 marks a rank no light filled.
static _LIGHTCOMPAREDATA s_NullLightCompareData = { 0.0f, -1 };

static SGDPROCUNITHEADER *save_tri2_pointer;    // sbss 0x37338c: TRI2 unit held for the first visible block
static SGDPROCUNITHEADER *save_bw_pointer;      // sbss 0x373390: its monotone twin
static SGDPROCUNITHEADER *s_ppuhVUVN;           // sbss 0x373394: VUVN unit of the meshes that follow
static SGDCOORDINATE     *s_pCoordBase;         // sbss 0x373398
static SGDFILEHEADER     *s_pSGDTop;            // sbss 0x37339c
static int                edge_check;           // sbss 0x3733a0
static SGDPROCUNITHEADER *previous_tri2_prim;   // sbss 0x3733a4

static CoordCache ccahe;                        // bss 0x3ee920

// SgCalcScreenRectPrim's copy of the block's local->world (data 0x328a70;
// gra3dMirror.c's gra3dCalcMirrorMatrix reads it).
sceVu0FMATRIX s_matScreenRectLocalWorld;

// "Not sent yet" for save_tri2_pointer / save_bw_pointer (FF3 stores -1).
#define SGD_SAVE_TRI2_PENDING   ((SGDPROCUNITHEADER *)(intptr_t)-1)

// PC: the pairs SetVUVNDataPost last wrote, for ReiPan_Graph3dDrawRuntimeMeshPost
// (the VU1 reads them from the packet; the host renderer needs the pointer).
static float (*s_pPostVUVNVerts)[2][4];

template <int NUM> static void _SelectLightByType(int type, float (*avBB)[4]);
static void _CalcWeightedVertexBuffer(float *dp, const float *v, float (*mat0)[4], float (*mat1)[4]);
static void _CalcWeightedNormalBuffer(float *dp, const float *v, float (*mat0)[4], float (*mat1)[4]);
static int  _IsSameMaterialIndexCache(GRA3DMATERIALINDEXCACHE *pPoint, GRA3DMATERIALINDEXCACHE *pSpot);

static SGDPROCUNITHEADER *_Next(SGDPROCUNITHEADER *pPUHead)
{
    return SgdPtr<SGDPROCUNITHEADER>(pPUHead->pNext);
}

// The block heads as host pointers (block i of the current model).
static SGDPROCUNITHEADER *_BlockHead(SGDFILEHEADER *pSGDTop, int i)
{
    return SgdPtr<SGDPROCUNITHEADER>(pSGDTop->ProcUnitHeads()[i]);
}

// ──────────────────────────────────────────────────────────────────────
// PC: host renderer helpers (MioPan gra3dSGD.c's GetHostRuntimeMeshTransform /
// IsPostSkinVertexBufferActive; no ROM counterpart)
// ──────────────────────────────────────────────────────────────────────

static const float s_matHostIdentity[4][4] =
{
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

static PHEAD *_GetLPHEAD(void);

// Whether the meshes being drawn read the blended skin buffer (their vertices
// are already in world space).
static int _HostIsPostSkinVertexBufferActive(void)
{
    if (s_ppuhVUVN == nullptr || s_ppuhVUVN->VUVNDesc.ucVectorType < 2)
    {
        return 0;
    }

    PHEAD *pVI = _GetLPHEAD();
    return pVI != nullptr && pVI->pWeightedList != 0;
}

// The realtime path sends local->world in the coordinate's light block, not
// through GRA3DTS_WORLD: use the coordinate the last bounding box selected.
static const float *_HostRuntimeMeshTransform(void)
{
    if (_HostIsPostSkinVertexBufferActive())
    {
        return &s_matHostIdentity[0][0];
    }
    if (s_pCoordBase != nullptr && ccahe.cache_on != -1 && ccahe.cn0 >= 0 &&
        ccahe.cn0 < gra3dsgdGetNumBlock() - 1)
    {
        return &s_pCoordBase[ccahe.cn0].matLocalWorld[0][0];
    }
    if (s_pCoordBase != nullptr)
    {
        return &s_pCoordBase[0].matLocalWorld[0][0];
    }
    return &s_matHostIdentity[0][0];
}

// ──────────────────────────────────────────────────────────────────────
// Vertex / normal packets
// ──────────────────────────────────────────────────────────────────────

// The current model's vertex pools.
// FF3 0x001e0330; from MioPan gra3dSGD.c (inlined in FF2).
static PHEAD *_GetLPHEAD(void)
{
    return SgdPtr<PHEAD>(s_pSGDTop->pVectorInfo);
}

// Skip to the next VIF UNPACK code (bits 0x60000000 set).
// FF3 0x001e0340; from MioPan gra3dSGD.c.
u_int *GetNextUnpackAddr(u_int *prim)
{
    for (; (*prim & 0x60000000) != 0x60000000; prim++)
    {
    }
    return prim;
}

// The VUVN unit's two header quadwords, then its vertex / normal pairs.
// FF3 0x001e0368; from MioPan gra3dSGD.c.
static void SetVUVNData(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUVNDESC    &rVUVNDesc = pPUHead->VUVNDesc;
    _VECTORADDRESS *raVA      = (_VECTORADDRESS *)&pPUHead[3];
    float         (*ravDest)[2][4] = (float (*)[2][4])g3dDmaOpenPacket();
    int             i;

    memcpy(&ravDest[0], &pPUHead[1], sizeof(SGDVUVNDATA));

    for (i = 0; i < rVUVNDesc.sNumVertex; i++)
    {
        memcpy(ravDest[1 + i][0], SgdPtr<float>(raVA[i].pVertex), 0x10);
        memcpy(ravDest[1 + i][1], SgdPtr<float>(raVA[i].pNormal), 0x10);
    }

    g3dDmaClosePacket(&ravDest[1 + rVUVNDesc.sNumVertex]);
}

// Skin one vertex + normal across two bone matrices (FF2 gra3dVu0.h's
// inline): pVertex / pNormal are each two quadwords, one per bone, bone 0's
// weight in the vertex's w. The normal's first weight is the vertex's w
// (vf13.w carries over) and the blended normal is scaled by mat0[3][3].
// From MioPan gra3dVu0.h gra3dVu0BlendVectorWeighted (FF3 0x001e05f4..0x001e0660, same instructions).
static void _BlendVectorWeighted(float (*pDest)[4], const float *pVertex, const float *pNormal,
                                 float (*mat0)[4], float (*mat1)[4])
{
    const float *vtx0  = pVertex + 0;
    const float *vtx1  = pVertex + 4;
    const float *nrm0  = pNormal + 0;
    const float *nrm1  = pNormal + 4;
    const float  wVtx  = vtx0[3];
    const float  wVtxI = 1.0f - vtx0[3];
    const float  wNrmI = 1.0f - nrm0[3];
    int          c;

    for (c = 0; c < 3; c++)
    {
        pDest[0][c] = (vtx0[0] * mat0[0][c] + vtx0[1] * mat0[1][c] + vtx0[2] * mat0[2][c] + mat0[3][c]) * wVtx
                    + (vtx1[0] * mat1[0][c] + vtx1[1] * mat1[1][c] + vtx1[2] * mat1[2][c] + mat1[3][c]) * wVtxI;
    }
    pDest[0][3] = 1.0f;

    for (c = 0; c < 3; c++)
    {
        pDest[1][c] = ((nrm0[0] * mat0[0][c] + nrm0[1] * mat0[1][c] + nrm0[2] * mat0[2][c]) * wVtx
                    +  (nrm1[0] * mat1[0][c] + nrm1[1] * mat1[1][c] + nrm1[2] * mat1[2][c]) * wNrmI) * mat0[3][3];
    }
    pDest[1][3] = 1.0f;
}

// As SetVUVNData for the last block: weighted vertices are blended on the EE
// unless the model has the skin buffer (pWeightedList), whose entries
// already point at blended data.
// FF3 0x001e0428; from MioPan gra3dSGD.c.
static void SetVUVNDataPost(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUVNDESC    &rVUVNDesc = pPUHead->VUVNDesc;
    _VECTORADDRESS *raVA      = (_VECTORADDRESS *)&pPUHead[3];
    float         (*ravDest)[2][4] = (float (*)[2][4])g3dDmaOpenPacket();
    PHEAD          *pVI       = _GetLPHEAD();
    SGDCOORDINATE  *pCoord0;
    SGDCOORDINATE  *pCoord1;
    int             i;

    memcpy(&ravDest[0], &pPUHead[1], sizeof(SGDVUVNDATA));
    s_pPostVUVNVerts = &ravDest[1];                     // PC

    if (rVUVNDesc.ucVectorType == 2 && pVI->pWeightedList == 0)
    {
        // one bone pair for the whole unit
        SGDVUVNDATA_WEIGHTED *pW = (SGDVUVNDATA_WEIGHTED *)&pPUHead[1];

        pCoord0 = gra3dsgdGetCoordinate(pW->ucBoneId0);
        pCoord1 = gra3dsgdGetCoordinate(pW->ucBoneId1);
        for (i = 0; i < rVUVNDesc.sNumVertex; i++)
        {
            _BlendVectorWeighted(ravDest[1 + i], SgdPtr<float>(raVA[i].pVertex), SgdPtr<float>(raVA[i].pNormal),
                                 pCoord0->matLocalWorld, pCoord1->matLocalWorld);
        }
    }
    else if (rVUVNDesc.ucVectorType == 3 && pVI->pWeightedList == 0)
    {
        // a bone pair per vertex
        for (i = 0; i < rVUVNDesc.sNumVertex; i++)
        {
            SGDVUVNDATA_WEIGHTED *pW = SgdPtr<SGDVUVNDATA_WEIGHTED>(raVA[i].pVertex);

            pCoord0 = gra3dsgdGetCoordinate(pW->ucBoneId0);
            pCoord1 = gra3dsgdGetCoordinate(pW->ucBoneId1);
            _BlendVectorWeighted(ravDest[1 + i], SgdPtr<float>(raVA[i].pVertex), SgdPtr<float>(raVA[i].pNormal),
                                 pCoord0->matLocalWorld, pCoord1->matLocalWorld);
        }
    }
    else
    {
        for (i = 0; i < rVUVNDesc.sNumVertex; i++)
        {
            memcpy(ravDest[1 + i][0], SgdPtr<float>(raVA[i].pVertex), 0x10);
            memcpy(ravDest[1 + i][1], SgdPtr<float>(raVA[i].pNormal), 0x10);
        }
    }

    g3dDmaClosePacket(&ravDest[1 + rVUVNDesc.sNumVertex]);
}

// ──────────────────────────────────────────────────────────────────────
// Mesh packets (realtime)
// ──────────────────────────────────────────────────────────────────────

// Send a mesh with its vertices and start the VU1 routine for its type.
// FF3 0x001e07f8; from MioPan gra3dSGD.c.
static void SetVUMeshData(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDPROCUNITHEADER *pVUVN   = s_ppuhVUVN;

    switch (rVUMeshDesc.ucMeshType & 0xd3)
    {
    case 0x00:                                  // unique
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        SetVUVNData(pVUVN);
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, pVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        gra3dCallMicroSubroutine2(0x12f8);
        return;

    case 0x02:                                  // weighted
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        SetVUVNData(pVUVN);
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, pVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        gra3dCallMicroSubroutine2(0x598);
        return;

    case 0x42:                                  // common: vertices already in VU1
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, pVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        gra3dCallMicroSubroutine2(0x1968);
        return;

    case 0x80:                                  // preloaded unique
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, pVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        g3dDmaAddPacket(&pVUVN[1], pVUVN->VUVNDesc.ucSize);
        gra3dCallMicroSubroutine2(0x12f8);
        return;

    case 0x82:                                  // preloaded weighted
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, pVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        g3dDmaAddPacket(&pVUVN[1], pVUVN->VUVNDesc.ucSize);
        gra3dCallMicroSubroutine2(0x598);
        return;

    default:
        return;
    }
}

// The last block's meshes (FF2: types & 0x53).
// FF3 0x001e0910; from MioPan gra3dSGD.c.
static void SetVUMeshDataPost(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;

    switch (rVUMeshDesc.ucMeshType & 0x53)
    {
    case 0x00:
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        SetVUVNDataPost(s_ppuhVUVN);
        ReiPan_Graph3dDrawRuntimeMeshPost(s_pSGDTop, s_ppuhVUVN, pPUHead, _HostRuntimeMeshTransform(),  // PC
                                          _HostIsPostSkinVertexBufferActive() ? s_pPostVUVNVerts : nullptr);
        gra3dCallMicroSubroutine2(0x12f8);
        return;

    case 0x02:
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        SetVUVNDataPost(s_ppuhVUVN);
        ReiPan_Graph3dDrawRuntimeMeshPost(s_pSGDTop, s_ppuhVUVN, pPUHead, _HostRuntimeMeshTransform(),  // PC
                                          _HostIsPostSkinVertexBufferActive() ? s_pPostVUVNVerts : nullptr);
        gra3dCallMicroSubroutine2(0xc88);
        return;

    case 0x42:
        g3dDmaAddPacket(&pPUHead[1], rVUMeshDesc.iTagSize);
        ReiPan_Graph3dDrawRuntimeMesh(s_pSGDTop, s_ppuhVUVN, pPUHead, _HostRuntimeMeshTransform()); // PC
        gra3dCallMicroSubroutine2(0x1968);
        return;

    default:
        return;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Coordinate cache, materials, coordinate light blocks
// ──────────────────────────────────────────────────────────────────────

// Whether coordinate cn's light block is the one VU1 already has: same edge
// check, same light set, same local->world as the cached coordinate.
// Otherwise make cn the cached one and return 0.
// FF3 0x001e09b8; from MioPan gra3dSGD.c (FF3 tests the light set out of line).
static int CheckCoordCache(int cn)
{
    if (ccahe.cache_on != -1 && ccahe.edge_check == _GetEdgeCheck())
    {
        if (_IsSameMaterialIndexCache(&ccahe.Point, &ccahe.Spot) != 0 &&
            memcmp(s_pCoordBase[cn].matLocalWorld, s_pCoordBase[ccahe.cn0].matLocalWorld, 0x40) == 0)
        {
            return 1;
        }
    }

    ccahe.cache_on   = 1;
    ccahe.edge_check = _GetEdgeCheck();
    ccahe.cn0        = cn;

    if (gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0)
    {
        GRA3DVU1MATERIALCACHE_POINT *pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);

        ccahe.Point.aiIndex[0] = pMatCache->Index.aiIndex[0];
        ccahe.Point.aiIndex[1] = pMatCache->Index.aiIndex[1];
        ccahe.Point.aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    if (gra3dIsLightTypeEnable(G3DLIGHT_SPOT) != 0)
    {
        GRA3DVU1MATERIALCACHE_SPOT *pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);

        ccahe.Spot.aiIndex[0] = pMatCache->Index.aiIndex[0];
        ccahe.Spot.aiIndex[1] = pMatCache->Index.aiIndex[1];
        ccahe.Spot.aiIndex[2] = pMatCache->Index.aiIndex[2];
    }

    return 0;
}

// Send a material's VU1 colour blocks (spot, point, directional). A material
// whose packet was built for the current light set is referenced again from
// where it was built (iTagAddressOld), or skipped when it is the last one sent.
// FF3 0x001e0ad8; from MioPan gra3dSGD.c (FF2 reset the cache when the DMA
// buffers toggled; FF3 leaves that to sgdResetMaterialCache per draw).
static void SetMaterialDataVU(SGDPROCUNITHEADER *pPUHead)
{
    SGDMATERIAL            *pMat = SgdPtr<SGDMATERIAL>(pPUHead->VUMaterialDesc.pMat);
    GRA3DVU1MATERIALPACKET *pPacketSpot;
    GRA3DVU1MATERIALPACKET *pPacketPoint;
    GRA3DVU1MATERIALPACKET *pPacketDir;
    u_char                 *base;
    int                     qwc;

    // PC: iTagAddressOld is 0 when the packet buffer is not in EE memory;
    // rebuild instead of referencing nothing.
    if (pMat->iCacheStatus >= 0 && pMat->iTagAddressOld != 0 &&
        _IsSameMaterialIndexCache(&pMat->aCache[G3DLIGHT_POINT], &pMat->aCache[G3DLIGHT_SPOT]) != 0)
    {
        if (pMat == s_pMatOld)
        {
            return;
        }
        s_pMatOld = pMat;
        gra3dSetMaterial(pMat);
        const auto *packet = static_cast<const GRA3DVU1MATERIALPACKET *>(
            ReiPan_GetHostPointer(pMat->iTagAddressOld));
        const GRA3DVU1MATERIALDATA_SPOT *spot = nullptr;
        const GRA3DVU1MATERIALDATA_POINT *point = nullptr;
        if (pMat->aCache[G3DLIGHT_SPOT].bEnable != 0)
        {
            spot = &packet++->Data;
        }
        if (pMat->aCache[G3DLIGHT_POINT].bEnable != 0)
        {
            point = &packet++->Data;
        }
        gra3dRestoreVu1MaterialData(
            reinterpret_cast<const GRA3DVU1MATERIALDATA_DIRECTIONAL *>(&packet->Data), point, spot);
        g3dDmaAddPacket(ReiPan_GetHostPointer(pMat->iTagAddressOld), pMat->iSizeOld);
        return;
    }

    s_pMatOld = pMat;
    base = (u_char *)g3dDmaOpenPacket();
    pMat->iTagAddressOld = SgdAddr(base) & 0x0fffffff;     // PC: the EE address of the packet

    qwc         = 0;
    pPacketSpot = nullptr;
    pPacketDir  = (GRA3DVU1MATERIALPACKET *)base;
    if (gra3dIsLightTypeEnable(G3DLIGHT_SPOT) != 0)
    {
        pPacketSpot = pPacketDir;
        pPacketDir++;
        qwc = 8;
    }

    pPacketPoint = nullptr;
    if (gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0)
    {
        pPacketPoint = pPacketDir;
        pPacketDir++;
        qwc += 8;
    }
    qwc += 8;

    pMat->iSizeOld = qwc;
    gra3dSetMaterial(SgdPtr<SGDMATERIAL>(pPUHead->VUMaterialDesc.pMat));

    if (pPacketSpot != nullptr)
    {
        GRA3DVU1MATERIALCACHE_SPOT *pMatCache;

        gra3dSetVif1Code_Unpack(pPacketSpot->qwVif1Code, VU1ADDR_MATERIAL_SPOT, 7, VIF1_UNPACK_V4_32);
        gra3dCalcVu1MaterialDataSpot(&pPacketSpot->Data);
        pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[0] = pMatCache->Index.aiIndex[0];
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[1] = pMatCache->Index.aiIndex[1];
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    pMat->aCache[G3DLIGHT_SPOT].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);

    if (pPacketPoint != nullptr)
    {
        GRA3DVU1MATERIALCACHE_POINT *pMatCache;

        gra3dSetVif1Code_Unpack(pPacketPoint->qwVif1Code, VU1ADDR_MATERIAL_POINT, 7, VIF1_UNPACK_V4_32);
        gra3dCalcVu1MaterialDataPoint(&pPacketPoint->Data);
        pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);
        pMat->aCache[G3DLIGHT_POINT].aiIndex[0] = pMatCache->Index.aiIndex[0];
        pMat->aCache[G3DLIGHT_POINT].aiIndex[1] = pMatCache->Index.aiIndex[1];
        pMat->aCache[G3DLIGHT_POINT].aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    pMat->aCache[G3DLIGHT_POINT].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_POINT);

    gra3dSetVif1Code_Unpack(pPacketDir->qwVif1Code, VU1ADDR_MATERIAL_DIRECTIONAL, 7, VIF1_UNPACK_V4_32);
    gra3dCalcVu1MaterialDataDirectional((GRA3DVU1MATERIALDATA_DIRECTIONAL *)&pPacketDir->Data);

    g3dDmaClosePacket(base + qwc * 0x10);
}

// A GS image unit: its data is a ready GIF transfer.
// FF3 0x001e0ce0; from MioPan gra3dSGD.c.
static void GsImageProcess(SGDPROCUNITHEADER *pPUHead)
{
    ReiPan_Graph3dUploadGsImage(pPUHead);                   // PC
    g3dDmaAddPacket(&pPUHead[1], pPUHead->GSImageDesc.iQWordSize);
}

// Send the bounding box's coordinate as a VU1 light block, unless the cache
// says VU1 has it.
// FF3 0x001e0d00; from MioPan gra3dSGD.c.
static void _SetCoordData(SGDPROCUNITHEADER *pPUHead)
{
    GRA3DVU1LIGHTPACKET *pVu1LightPacket;

    if (CheckCoordCache(pPUHead->BoundingBoxDesc.iCoordId) == 0)
    {
        pVu1LightPacket = (GRA3DVU1LIGHTPACKET *)g3dDmaOpenPacket();
        gra3dSetVif1Code_Unpack(pVu1LightPacket->qwVif1Code, VU1ADDR_LIGHT, 0x15, VIF1_UNPACK_V4_32);
        g3dSetVu1LightData(&pVu1LightPacket->Data, gra3dsgdGetCoordinate(pPUHead->BoundingBoxDesc.iCoordId), nullptr);
        g3dDmaClosePacket(pVu1LightPacket + 1);
        gra3dVu1TransGTEOP();
        SetVU1Header();
    }
}

// FF3 0x001e0d98; from MioPan gra3dSGD.c.
void _SetVUVNPRIM(SGDPROCUNITHEADER *ppuhVUVN)
{
    s_ppuhVUVN = ppuhVUVN;
}

// FF3 0x001e0da0; from MioPan gra3dSGD.c.
SGDPROCUNITHEADER *_GetVUVNPRIM(void)
{
    return s_ppuhVUVN;
}

// ──────────────────────────────────────────────────────────────────────
// Culling
// ──────────────────────────────────────────────────────────────────────

// VU0 CLIP: v against +-|w|, bit 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z.
static u_int _Vu0ClipFlags(const float *v)
{
    float w = fabsf(v[3]);
    u_int f = 0;

    if (v[0] > w)  f |= 0x01;
    if (v[0] < -w) f |= 0x02;
    if (v[1] > w)  f |= 0x04;
    if (v[1] < -w) f |= 0x08;
    if (v[2] > w)  f |= 0x10;
    if (v[2] < -w) f |= 0x20;
    return f;
}

// Four CLIPs then CFC2: the clip-flag register keeps the last four results,
// six bits each.
static u_int _Vu0Clip4(float (*av)[4])
{
    u_int uiFlag = 0;
    int   i;

    for (i = 0; i < 4; i++)
    {
        uiFlag = (uiFlag << 6) | _Vu0ClipFlags(av[i]);
    }
    return uiFlag & 0xffffff;
}

// Cull a block by its bounding box (8 corners in the unit's data) against the
// camera's object frustum; mark the coordinate in or out of the view volume
// and set the edge check when a corner leaves the guard-banded polygon frustum.
// FF3 0x001e0da8; from MioPan gra3dSGD.c.
static int CheckBoundingBox(SGDPROCUNITHEADER *pPUHead)
{
    SGDBOUNDINGBOXDESC &rBBDesc = pPUHead->BoundingBoxDesc;
    GRA3DCAMERA        *pCam0;
    GRA3DCAMERA        *pCam1;
    SGDCOORDINATE      *pCoord;
    SGDCOORDINATE       Coord;
    sceVu0FMATRIX       matLocalClipObject;
    sceVu0FMATRIX       matLocalClipPolygon;
    float               avWork[16][4];

    memcpy(&Coord, gra3dsgdGetCoordinate(rBBDesc.iCoordId), sizeof(Coord));
    Coord.bInViewvolume = 0;
    gra3dsgdSetCoordinate(&Coord, rBBDesc.iCoordId);

    pCam0  = gra3dGetCamera();
    pCam1  = gra3dGetCamera();
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);

    // PC: the EE leaves both products in VU0 vf4..vf7 / vf8..vf11 for
    // gra3dIsBBInViewvolume.
    sceVu0MulMatrix(matLocalClipObject, pCam0->matWorldClipObject, pCoord->matLocalWorld);
    sceVu0MulMatrix(matLocalClipPolygon, pCam1->matWorldClipPolygon, pCoord->matLocalWorld);

    if (gra3dIsBBInViewvolume(avWork + 8, avWork, (float (*)[4])&pPUHead[1],
                              matLocalClipObject, matLocalClipPolygon) == 0)
    {
        return 0;
    }

    memcpy(&Coord, gra3dsgdGetCoordinate(rBBDesc.iCoordId), sizeof(Coord));
    Coord.bInViewvolume = 1;
    gra3dsgdSetCoordinate(&Coord, rBBDesc.iCoordId);

    _SetEdgeCheck(((_Vu0Clip4(avWork + 8) | _Vu0Clip4(avWork + 12)) & 0xffffff) != 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Realtime walker
// ──────────────────────────────────────────────────────────────────────

// One block of a realtime model; a culled bounding box ends the block.
// FF3 0x001e1098; from MioPan gra3dSGD.c.
static void SgSortUnitPrim(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;
        case SGDPU_MESH:
            SetVUMeshData(pPUHead);
            break;
        case SGDPU_MATERIAL:
            SetMaterialDataVU(pPUHead);
            break;
        case SGDPU_BOUNDING_BOX:
            if (CheckBoundingBox(pPUHead) == 0)
            {
                return;
            }
            _SetCoordData(pPUHead);
            break;
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// Copy matLocalWorld; [3][3] becomes 3 / (sum of the column lengths) -- the
// ROM transposes and measures rows of the transpose (pextlw / pcpyld).
// FF3 0x001e1148; from MioPan gra3dSGD.c (FF3 returns a stale stack address;
// nobody reads it).
static void _CalcWeightedLocalWorldMatrix(float (*matRet)[4], float (*matLocalWorld)[4])
{
    float fScaleSum = 0.0f;
    int   c;

    memcpy(matRet, matLocalWorld, sizeof(sceVu0FMATRIX));

    for (c = 0; c < 3; c++)
    {
        float x = matLocalWorld[0][c];
        float y = matLocalWorld[1][c];
        float z = matLocalWorld[2][c];

        fScaleSum += sqrtf(x * x + y * y + z * z);
    }
    matRet[3][3] = 3.0f / fScaleSum;                        // sdata 0x370c90
}

// Light block of a coordinate unit in the last block (two bones when skinned).
// FF3 0x001e1258; from MioPan gra3dSGD.c.
static void SetCoordData(GRA3DVU1LIGHTDATA *pVu1LightData, SGDPROCUNITHEADER *pPUHead)
{
    SGDCOORDINATEDESC &rCoordDesc = pPUHead->CoordDesc;
    SGDCOORDINATE     *pCoord0;
    SGDCOORDINATE     *pCoord1;

    if (rCoordDesc.iCoordId1 != 0)
    {
        pCoord0 = (rCoordDesc.iCoordId0 == 0) ? nullptr : gra3dsgdGetCoordinate(rCoordDesc.iCoordId0);
        pCoord1 = (rCoordDesc.iCoordId1 == 0) ? nullptr : gra3dsgdGetCoordinate(rCoordDesc.iCoordId1);
        g3dSetVu1LightData(pVu1LightData, pCoord0, pCoord1);
    }
}

// Blend every run of a weighted list into pvDest (one quadword per vector,
// two source quadwords per vector) with its bone pair.
// FF3 0x001e12f0; from MioPan gra3dSGD.c (FF2's write-size assert dropped).
static void _CalcWeightedVectorBuffer(_ONELIST *aList, int iNumList, float (*pvDest)[4], float (*pvSrc)[4],
                                      LPFUNC_CALCWEIGHTEDVECTORBUFFER pFunc)
{
    sceVu0FMATRIX matWork0;
    sceVu0FMATRIX matWork1;
    int           i;
    int           j;

    for (i = 0; i < iNumList; i++)
    {
        _ONELIST &rList = aList[i];

        _CalcWeightedLocalWorldMatrix(matWork0, gra3dsgdGetCoordinate(rList.sCoordId0)->matLocalWorld);
        _CalcWeightedLocalWorldMatrix(matWork1, gra3dsgdGetCoordinate(rList.sCoordId1)->matLocalWorld);

        for (j = 0; j < rList.usNumVector; j++)
        {
            (*pFunc)(pvDest[0], pvSrc[0], matWork0, matWork1);
            pvDest++;
            pvSrc += 2;
        }
    }
}

// A skinned coordinate (two bones): blend the model's weighted vertices and
// normals into the global buffers.
// FF3 0x001e13d0; from MioPan gra3dSGD.c.
static void CalcVertexBuffer(SGDPROCUNITHEADER *pPUHead)
{
    PHEAD       *pVI = _GetLPHEAD();
    _VERTEXLIST *pVL;
    _VERTEXLIST *pNL;

    if (pPUHead->CoordDesc.iCoordId1 != 0)
    {
        pVL = SgdPtr<_VERTEXLIST>(pVI->pWeightedList);
        if (pVL != nullptr)
        {
            pNL = pVL->Next();
            _CalcWeightedVectorBuffer(pVL->List(), pVL->iNumList, s_pGlobalVertexBuffer,
                                      SgdPtr<float[4]>(pVI->pWeightedVertex), _CalcWeightedVertexBuffer);
            _CalcWeightedVectorBuffer(pNL->List(), pNL->iNumList, s_pGlobalNormalBuffer,
                                      SgdPtr<float[4]>(pVI->pWeightedNormal), _CalcWeightedNormalBuffer);
        }
    }
}

// The last block: coordinate units send their light block and blend the skin.
// FF3 0x001e1468; from MioPan gra3dSGD.c.
static void SgSortUnitPrimPost(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;
        case SGDPU_MESH:
            SetVUMeshDataPost(pPUHead);
            break;
        case SGDPU_MATERIAL:
            SetMaterialDataVU(pPUHead);
            break;
        case SGDPU_COORDINATE:
        {
            GRA3DVU1LIGHTPACKET *pPacket = (GRA3DVU1LIGHTPACKET *)g3dDmaOpenPacket();

            gra3dSetVif1Code_Unpack(pPacket->qwVif1Code, VU1ADDR_LIGHT, 0x15, VIF1_UNPACK_V4_32);
            SetCoordData(&pPacket->Data, pPUHead);
            g3dDmaClosePacket(pPacket + 1);
            gra3dVu1TransGTEOP();
            ccahe.cache_on = -1;
            SetVU1Header();
            CalcVertexBuffer(pPUHead);
            break;
        }
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Prelighting (preset models)
// ──────────────────────────────────────────────────────────────────────

// Bake one preset mesh's vertex colours with the g3d core's current lights
// (bAddColor: add to what earlier light passes left). Colours whose first
// word has 1 in its low half are markers and stay as they are; a computed
// colour that would look like one is bumped by one.
// FF3 0x001e1560; from MioPan gra3dSGD.c.
static void SetPreRenderMeshData(SGDPROCUNITHEADER *pPUHead, int bAddColor)
{
    SGDPROCUNITHEADER    *pVUVN     = s_ppuhVUVN;
    SGDVUVNDESC          &rVUVNDesc = pVUVN->VUVNDesc;
    SGDVUVNDATA_PRESET   *pVUVNData = (SGDVUVNDATA_PRESET *)&pVUVN[1];
    SGDVUMESHDATA_PRESET *pMeshData = (SGDVUMESHDATA_PRESET *)&pPUHead[1];
    int                   mtype     = pPUHead->VUMeshDesc.ucMeshType;
    int                   gloops    = pPUHead->VUMeshDesc.ucNumMesh;
    int                   iVertexCount;
    int                   i;
    int                   j;
    u_int                *prim;

    if (mtype != iMT_0 && mtype != iMT_2 && mtype != iMT_2F)
    {
        return;
    }
    if (pMeshData->sOffsetToPrim == 0)
    {
        return;
    }

    prim         = (u_int *)pPUHead + pMeshData->sOffsetToPrim;
    iVertexCount = 0;
    for (j = 0; j < gloops; j++)
    {
        _SGDVUMESHCOLORDATA *pVMCD = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr(prim);
        int                  loops = pVMCD->aucVifUnpack[2];

        for (i = 0; i < loops; i++)
        {
            float  *pCol = pVMCD->Color()[i];
            float  *pvVSrc;
            float  *pvNSrc;
            float   vV[4];
            float   vN[4];
            float   first[4];
            float   pcol[4];
            u_int   uiWord;

            if (*(short *)pCol == 1)
            {
                continue;
            }

            if (mtype == iMT_0 || mtype == iMT_2)
            {
                pvVSrc = pVUVNData->Type2()[iVertexCount + i].vVertex;
                pvNSrc = pVUVNData->Type2()[iVertexCount + i].vNormal;
            }
            else
            {
                pvNSrc = pVUVNData->Type2F()[j];
                pvVSrc = pVUVNData->Type2F()[iVertexCount + i + rVUVNDesc.sNumNormal];
            }

            vV[0] = pvVSrc[0];
            vV[1] = pvVSrc[1];
            vV[2] = pvVSrc[2];
            vV[3] = 1.0f;                                   // sdata 0x370c94
            vN[0] = pvNSrc[0];
            vN[1] = pvNSrc[1];
            vN[2] = pvNSrc[2];
            vN[3] = 0.0f;

            first[0] = 0.0f;
            first[1] = 0.0f;
            first[2] = 0.0f;
            first[3] = 0.0f;
            if (bAddColor != 0)
            {
                memcpy(first, pCol, sizeof(VECTOR3));
            }

            gra3dCalcVertexColor(pcol, vV, vN, first);

            memcpy(pCol, pcol, sizeof(VECTOR3));
            memcpy(&uiWord, pCol, sizeof(uiWord));
            if ((uiWord & 0xffff) == 1)
            {
                uiWord++;
                memcpy(pCol, &uiWord, sizeof(uiWord));
            }
        }

        iVertexCount += loops;
        prim = (u_int *)&pVMCD->Color()[loops];
    }
}

// callcheck: _SelectLightByType
//   (called as the template instance _SelectLightByType<4>)
// Rank the point and spot lights reaching this block's bounding box and make
// the best three of each the VU1 ones.
// FF3 0x001e1798; from MioPan gra3dSGD.c.
static void SelectLight(SGDPROCUNITHEADER *pPUHead)
{
    float (*mat)[4] = s_pCoordBase[pPUHead->BoundingBoxDesc.iCoordId].matLocalWorld;
    float   avBBWork[9][4];                                 // PC: FF3 uses scratchpad 0x70003900
    int     c;

    gra3dbbApplyMatrix(avBBWork, (float (*)[4])&pPUHead[1], mat);

    // centre = corner 0 * 0.5 + corner 7 * (1 - 0.5); w from corner 0
    for (c = 0; c < 3; c++)
    {
        avBBWork[8][c] = avBBWork[0][c] * 0.5f + avBBWork[7][c] * (1.0f - 0.5f);
    }
    avBBWork[8][3] = avBBWork[0][3];

    _SelectLightByType<4>(G3DLIGHT_POINT, avBBWork);
    _SelectLightByType<4>(G3DLIGHT_SPOT, avBBWork);
}

// Feed the next lights (enabled and switched on for prelighting) into the
// g3d core, up to three per type; bFirst starts over. Returns 0 once every
// light has been used.
// FF3 0x001e1830; from MioPan gra3dSGD.c (FF3: 27 point / 9 spot lights and
// gra3dIsLightEnableToPrelight).
static int _SetG3DLightForPrelighting(int bFirst)
{
    int iCount_G3DDL;
    int iCount_G3DPL;
    int iCount_G3DSL;
    int i;

    if (bFirst != 0)
    {
        iCount_GRA3DDL = 0;
        iCount_GRA3DPL = 0;
        iCount_GRA3DSL = 0;
    }

    if (iCount_GRA3DDL >= GRA3D_NUM_LIGHT_DIRECTIONAL && iCount_GRA3DPL >= GRA3D_NUM_LIGHT_POINT &&
        iCount_GRA3DSL >= GRA3D_NUM_LIGHT_SPOT)
    {
        return 0;
    }

    iCount_G3DDL = 0;
    iCount_G3DPL = 0;
    iCount_G3DSL = 0;

    for (; iCount_GRA3DDL < GRA3D_NUM_LIGHT_DIRECTIONAL && iCount_G3DDL < G3D_NUM_LIGHT_DIRECTIONAL;
         iCount_GRA3DDL++)
    {
        int iId = iCount_GRA3DDL + GRA3D_START_LIGHT_DIRECTIONAL;

        if (gra3dIsLightEnable(iId) != 0 && gra3dIsLightEnableToPrelight(iId) != 0)
        {
            g3dSetLight(G3D_START_LIGHT_DIRECTIONAL + iCount_G3DDL, gra3dGetLightRef(iId));
            iCount_G3DDL++;
        }
    }
    for (i = 0; i < G3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        g3dLightEnable(G3D_START_LIGHT_DIRECTIONAL + i, i < iCount_G3DDL);
    }

    for (; iCount_GRA3DPL < GRA3D_NUM_LIGHT_POINT && iCount_G3DPL < G3D_NUM_LIGHT_POINT; iCount_GRA3DPL++)
    {
        int iId = iCount_GRA3DPL + GRA3D_START_LIGHT_POINT;

        if (gra3dIsLightEnable(iId) != 0 && gra3dIsLightEnableToPrelight(iId) != 0)
        {
            g3dSetLight(G3D_START_LIGHT_POINT + iCount_G3DPL, gra3dGetLightRef(iId));
            iCount_G3DPL++;
        }
    }
    for (i = 0; i < G3D_NUM_LIGHT_POINT; i++)
    {
        g3dLightEnable(G3D_START_LIGHT_POINT + i, i < iCount_G3DPL);
    }

    for (; iCount_GRA3DSL < GRA3D_NUM_LIGHT_SPOT && iCount_G3DSL < G3D_NUM_LIGHT_SPOT; iCount_GRA3DSL++)
    {
        int iId = iCount_GRA3DSL + GRA3D_START_LIGHT_SPOT;

        if (gra3dIsLightEnable(iId) != 0 && gra3dIsLightEnableToPrelight(iId) != 0)
        {
            g3dSetLight(G3D_START_LIGHT_SPOT + iCount_G3DSL, gra3dGetLightRef(iId));
            iCount_G3DSL++;
        }
    }
    for (i = 0; i < G3D_NUM_LIGHT_SPOT; i++)
    {
        g3dLightEnable(G3D_START_LIGHT_SPOT + i, i < iCount_G3DSL);
    }

    g3dApplyLight();
    return 1;
}

// One block of a preset model's prelight bake: every mesh once per batch of
// up to three lights per type.
// FF3 0x001e1a98; from MioPan gra3dSGD.c.
static void SgPreRenderPrim(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;
        case SGDPU_MESH:
        {
            int bFirst = 1;

            while (_SetG3DLightForPrelighting(bFirst) != 0)
            {
                SetPreRenderMeshData(pPUHead, bFirst == 0);
                bFirst = 0;
            }
            break;
        }
        case SGDPU_MATERIAL:
            gra3dSetMaterial(SgdPtr<SGDMATERIAL>(pPUHead->VUMaterialDesc.pMat));
            break;
        case SGDPU_COORDINATE:
            gra3dSetTransform(GRA3DTS_WORLD, s_pCoordBase[pPUHead->CoordDesc.iCoordId0].matLocalWorld);
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Coordinates
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001e1b60; from MioPan gra3dSGD.c.
SGDCOORDINATE *gra3dsgdGetCoordinate(int iIndex)
{
    return s_pCoordBase + iIndex;
}

// FF3 0x001e1b78; from MioPan gra3dSGD.c.
void gra3dsgdSetCoordinate(SGDCOORDINATE *pCU, int iIndex)
{
    if (iIndex == -1)
    {
        s_pCoordBase = pCU;
        return;
    }
    memcpy(&s_pCoordBase[iIndex], pCU, sizeof(SGDCOORDINATE));
}

// Clear one preset mesh's baked colours (markers stay).
// FF3 0x001e1bc0; from MioPan gra3dSGD.c.
static void ClearPreRenderMeshData(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDATA_PRESET *pMeshData = (SGDVUMESHDATA_PRESET *)&pPUHead[1];
    int                   mtype     = pPUHead->VUMeshDesc.ucMeshType;
    int                   gloops    = pPUHead->VUMeshDesc.ucNumMesh;
    u_int                *prim;
    int                   j;
    int                   k;

    if (pMeshData->sOffsetToPrim == 0)
    {
        return;
    }
    prim = (u_int *)pPUHead + pMeshData->sOffsetToPrim;
    if (mtype != iMT_0 && mtype != iMT_2 && mtype != iMT_2F)
    {
        return;
    }

    for (j = 0; j < gloops; j++)
    {
        _SGDVUMESHCOLORDATA *pVMCD = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr(prim);
        int                  loops = pVMCD->aucVifUnpack[2];
        u_int               *pCol  = (u_int *)pVMCD->Color();

        for (k = loops; k != 0; k--)
        {
            if (*pCol != 1)
            {
                pCol[0] = 0;
                pCol[1] = 0;
                pCol[2] = 0;
            }
            pCol += 3;
        }
        prim = (u_int *)&pVMCD->Color()[loops];
    }
}

// FF3 0x001e1c88; from MioPan gra3dSGD.c.
static void SgClearPreRenderPrim(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        if (pPUHead->iCategory == SGDPU_MESH)
        {
            ClearPreRenderMeshData(pPUHead);
        }
        pPUHead = _Next(pPUHead);
    }
}

// Before a realtime draw: forget the cached coordinate, switch the point /
// spot routines by whether any such light is on, and load the camera's
// world->screen and world->clip.
// FF3 0x001e1cd8; from MioPan gra3dSGD.c.
void gra3dsgdSetupVu1(void)
{
    ccahe.cache_on = -1;

    gra3dEnableLightType(G3DLIGHT_POINT, gra3dGetNumEnableLight(G3DLIGHT_POINT) != 0);
    gra3dEnableLightType(G3DLIGHT_SPOT, gra3dGetNumEnableLight(G3DLIGHT_SPOT) != 0);

    gra3dSetTransform(GRA3DTS_WORLDSCREEN, gra3dGetCamera()->matWorldScreen);
    gra3dSetTransform(GRA3DTS_WORLDCLIP, gra3dGetCamera()->matWorldClipPolygon);
}

// A preset block's bounding box: local->world, ->screen and ->clip (the
// object frustum once a corner crosses the guard band), the edge check kept in
// the coordinate, and the block's lights picked.
// FF3 0x001e1d68; from MioPan gra3dSGD.c (MioPan's host light publish left to the bridge).
int BoundingBoxCalcP(SGDPROCUNITHEADER *pPUHead)
{
    SGDBOUNDINGBOXDESC &rBBDesc = pPUHead->BoundingBoxDesc;
    GRA3DCAMERA        *pCam;
    SGDCOORDINATE      *pCoord;
    sceVu0FMATRIX       matWorldScreen;
    sceVu0FMATRIX       matWorldClip;
    SGDCOORDINATE       CU;

    pCam   = gra3dGetCamera();
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);

    gra3dSetTransform(GRA3DTS_WORLD, pCoord->matLocalWorld);

    sceVu0MulMatrix(matWorldScreen, pCam->matWorldScreen, pCoord->matLocalWorld);
    gra3dSetTransform(GRA3DTS_WORLDSCREEN, matWorldScreen);

    if (_GetEdgeCheck() == 0)
    {
        sceVu0MulMatrix(matWorldClip, pCam->matWorldClipPolygon, pCoord->matLocalWorld);
    }
    else
    {
        sceVu0MulMatrix(matWorldClip, pCam->matWorldClipObject, pCoord->matLocalWorld);
    }
    gra3dSetTransform(GRA3DTS_WORLDCLIP, matWorldClip);

    memcpy(&CU, gra3dsgdGetCoordinate(rBBDesc.iCoordId), sizeof(CU));
    CU.edge_check = _GetEdgeCheck();
    gra3dsgdSetCoordinate(&CU, rBBDesc.iCoordId);

    SelectLight(pPUHead);
    return 1;
}

// A preset mesh: its VU1 program, vertices and strips, then the routine
// (the clipping one, VU1 0xf58, when the block crosses the guard band).
// FF3 0x001e1f90; from MioPan gra3dSGD.c (FF3 loads a program per type).
static void SetVUMeshDataP(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDESC     &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDPROCUNITHEADER *pVUVN       = _GetVUVNPRIM();
    SGDVUVNDESC       &rVUVNDesc   = _GetVUVNPRIM()->VUVNDesc;

    switch (rVUMeshDesc.ucMeshType)
    {
    case iMT_0:
        ReiPan_Graph3dOtsRecordPresetMesh(s_pSGDTop, pVUVN, pPUHead, &gra3dGetTransformRef(GRA3DTS_WORLD)[0][0]); // PC
        ReiPan_Graph3dDrawPresetMesh(s_pSGDTop, pVUVN, pPUHead, &gra3dGetTransformRef(GRA3DTS_WORLD)[0][0]); // PC
        gra3dDmaLoadVu1MicroProgram(VU1MPG_PRESET_0, 0);
        g3dDmaAddPacket(&pPUHead[2], rVUMeshDesc.iTagSize);
        g3dDmaAddPacket(&_GetVUVNPRIM()[4], _GetVUVNPRIM()->VUVNDesc.ucSize);
        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2(0x1ba8);
        }
        else
        {
            gra3dCallMicroSubroutine1(0xf58);
        }
        return;

    case iMT_2:
    case iMT_2F:
        ReiPan_Graph3dOtsRecordPresetMesh(s_pSGDTop, pVUVN, pPUHead, &gra3dGetTransformRef(GRA3DTS_WORLD)[0][0]); // PC
        ReiPan_Graph3dDrawPresetMesh(s_pSGDTop, pVUVN, pPUHead, &gra3dGetTransformRef(GRA3DTS_WORLD)[0][0]); // PC
        gra3dDmaLoadVu1MicroProgram(VU1MPG_PRESET_2, 0);
        g3dDmaAddPacket(&pVUVN[1], rVUVNDesc.ucSize);
        g3dDmaAddPacket(&pPUHead[2], rVUMeshDesc.iTagSize);
        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2(0x2108);
        }
        else
        {
            gra3dCallMicroSubroutine1(0xf58);
        }
        return;

    case 0x52:
    case 0x72:
        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2(0x3180);
        }
        return;

    default:
        return;
    }
}

// ──────────────────────────────────────────────────────────────────────
// TRI2 textures, preset walker
// ──────────────────────────────────────────────────────────────────────

// GS pages (32 blocks) a texture of uiBlockSize blocks takes.
// FF3 0x001e2120; from MioPan gra3dSGD.c.
u_int gra3dCalcVRAMPageSize(u_int uiBlockSize)
{
    if ((uiBlockSize & 0x1f) != 0)
    {
        return (uiBlockSize >> 5) + 1;
    }
    return uiBlockSize >> 5;
}

// FF3 0x001e2140; from MioPan gra3dSGD.c.
SGDPROCUNITHEADER *_GetPREVIOUSTRI2PRIM(void)
{
    return previous_tri2_prim;
}

// FF3 0x001e2148; from MioPan gra3dSGD.c.
void _SetPREVIOUSTRI2PRIM(SGDPROCUNITHEADER *p)
{
    previous_tri2_prim = p;
}

// Send a TRI2 unit's textures to VRAM unless it was the last one sent.
// FF3 0x001e2150; from MioPan gra3dSGD.c.
static void LoadTRI2Files(SGDPROCUNITHEADER *pPUHead)
{
    SGDTEXTUREIMAGEDESC *pTexDesc;
    SGDTRI2FILEHEADER   *pTRI2Head;

    if (pPUHead != nullptr && pPUHead != _GetPREVIOUSTRI2PRIM())
    {
        _SetPREVIOUSTRI2PRIM(pPUHead);

        pTexDesc  = &pPUHead->TexDesc;
        pTRI2Head = (SGDTRI2FILEHEADER *)((u_char *)&pPUHead[1] + pTexDesc->iPaddingSize);
        pTRI2Head->uiVif1Code_FLUSH = 0x11000000;           // VIF FLUSH

        gra3dLoadTRI2FileToVRAM(pTexDesc->iNumTexture, pTRI2Head, 1);
        g3dSetGsRegister(0, 0x3f, 1);                       // TEXFLUSH
    }
}

// One block of a preset model; the held TRI2 units go out after the first
// visible bounding box, so a model with nothing on screen sends no texture.
// FF3 0x001e21c8; from MioPan gra3dSGD.c (FF3: category 0x41 ends the walk).
void gra3dsgdDrawPresetDataObject(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;
        case SGDPU_MESH:
            SetVUMeshDataP(pPUHead);
            break;
        case SGDPU_MATERIAL:
            SetMaterialDataVU(pPUHead);
            break;
        case SGDPU_BOUNDING_BOX:
            if (CheckBoundingBox(pPUHead) == 0)
            {
                return;
            }
            BoundingBoxCalcP(pPUHead);
            _SetCoordData(pPUHead);
            if (save_tri2_pointer != nullptr)
            {
                LoadTRI2Files(save_tri2_pointer);
                save_tri2_pointer = nullptr;
            }
            if (save_bw_pointer != nullptr)
            {
                LoadTRI2Files(save_bw_pointer);
                save_bw_pointer = nullptr;
            }
            break;
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        case 0x41:
            return;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// A preset model's texture block: GS images go out, TRI2 units are held for
// the first visible block -- or sent at once when the caller asked for the
// texture block alone (SGD_SAVE_TRI2_PENDING).
// FF3 0x001e22b0; from MioPan gra3dSGD.c.
static void SgSortPreProcessP(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        case SGDPU_TRI2:
            if (save_tri2_pointer == SGD_SAVE_TRI2_PENDING)
            {
                LoadTRI2Files(pPUHead);
                save_tri2_pointer = nullptr;
            }
            else
            {
                save_tri2_pointer = pPUHead;
            }
            break;
        case SGDPU_MONOTONE_TRI2:
            if (gra3dIsMonotoneDrawEnable() != 0)
            {
                if (save_bw_pointer == SGD_SAVE_TRI2_PENDING)
                {
                    LoadTRI2Files(pPUHead);
                    save_bw_pointer = nullptr;
                }
                else
                {
                    save_bw_pointer = pPUHead;
                }
            }
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// FF3 0x001e2360; from MioPan gra3dSGD.c.
int gra3dsgdGetNumBlock(void)
{
    return s_pSGDTop->uiNumBlock;
}

// ──────────────────────────────────────────────────────────────────────
// Shared state
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001e2370; from MioPan gra3dSGD.c.
CoordCache *_GetCoordCache(void)
{
    return &ccahe;
}

// FF3 0x001e2380; from MioPan gra3dSGD.c.
void _SetCoordCache(CoordCache *pCC)
{
    ccahe = *pCC;
}

// FF3 0x001e23d8; from MioPan gra3dSGD.c.
float (*_GetGlobalVertexBuffer(void))[4]
{
    return s_pGlobalVertexBuffer;
}

// FF3 0x001e23e0; from MioPan gra3dSGD.c.
float (*_GetGlobalNormalBuffer(void))[4]
{
    return s_pGlobalNormalBuffer;
}

// FF3 0x001e23e8; from MioPan gra3dSGD.c.
int _GetGlobalBufferSize(void)
{
    return s_iGlobalBufferSize;
}

// Split the work buffer (size quadwords) into the vertex and normal halves.
// FF3 0x001e23f0; from MioPan gra3dSGD.c.
static void SgSetVNBuffer(float (*varray)[4], int size)
{
    s_pGlobalVertexBuffer = varray;
    s_iGlobalBufferSize   = size / 2;
    s_pGlobalNormalBuffer = varray + size / 2;
}

// FF3 0x001e2418; from MioPan gra3dSGD.c.
void gra3dsgdInit(GRA3DSGDCREATIONDATA *pCD)
{
    SgSetVNBuffer((float (*)[4])pCD->vnarray, (int)pCD->size);
    _SetPREVIOUSTRI2PRIM(nullptr);
}

// Make pSGDTop the current model (ignored unless it is a valid SGD file).
// FF3 0x001e2440; from MioPan gra3dSGD.c.
void gra3dsgdSetData(SGDFILEHEADER *pSGDTop)
{
    if (pSGDTop != nullptr && pSGDTop->uiVersionId == SGD_VALID_VERSIONID)
    {
        s_pSGDTop = pSGDTop;
    }
}

// PC: the current model, for the host renderer bridge (FF2 gra3dsgdGetData;
// FF3 has no such function).
SGDFILEHEADER *gra3dsgdGetData(void)
{
    return s_pSGDTop;
}

// FF3 0x001e2468; from MioPan gra3dSGD.c.
int _GetEdgeCheck(void)
{
    return edge_check;
}

// FF3 0x001e2470; from MioPan gra3dSGD.c.
void _SetEdgeCheck(int ec)
{
    edge_check = ec;
}

// A realtime model's texture block: GS images and TRI2 units go out at once.
// FF3 0x001e2478; from MioPan gra3dSGD.c.
void SgSortPreProcess(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_TRI2:
            LoadTRI2Files(pPUHead);
            break;
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        case SGDPU_MONOTONE_TRI2:
            if (gra3dIsMonotoneDrawEnable() != 0)
            {
                LoadTRI2Files(pPUHead);
            }
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Entry
// ──────────────────────────────────────────────────────────────────────

// callcheck: memcpy
//   (the CoordCache copy for SRT_MAPSHADOW is a struct assignment)
// FF3 0x001e2520; from MioPan gra3dSGD.c (FF3: spot lights from gra3d slot 30).
void _gra3dDrawSGD(SGDFILEHEADER *pSGDTop, SGDRENDERTYPE type, SGDCOORDINATE *pCoord, int pnum)
{
    int bPreset;
    int iNumBlock;
    int i;

    if (pSGDTop == nullptr)
    {
        return;
    }
    bPreset = pSGDTop->ucModelType & 1;
    if (pSGDTop->uiVersionId != SGD_VALID_VERSIONID || ((uintptr_t)pCoord & 0xf) != 0)
    {
        return;
    }

    gra3dsgdSetData(pSGDTop);
    if (pCoord == nullptr)
    {
        pCoord = SgdPtr<SGDCOORDINATE>(pSGDTop->pCoord);
    }
    gra3dsgdSetCoordinate(pCoord, -1);

    if (type == SRT_REALTIME)
    {
        sgdResetMaterialCache(pSGDTop);
        gra3dsgdSetupVu1();
    }
    else if (type == SRT_MAPSHADOW)
    {
        CoordCache CC = *_GetCoordCache();

        CC.cache_on = -1;
        _SetCoordCache(&CC);
    }

    if (bPreset == 0)
    {
        if (type == SRT_REALTIME)
        {
            // the g3d core's point / spot slots take gra3d's first three of each
            for (i = 0; i < G3D_NUM_LIGHT_POINT; i++)
            {
                int bEnable = gra3dIsLightEnable(GRA3D_START_LIGHT_POINT + i);

                g3dLightEnable(G3D_START_LIGHT_POINT + i, bEnable);
                if (bEnable != 0)
                {
                    g3dSetLight(G3D_START_LIGHT_POINT + i, gra3dGetLightRef(GRA3D_START_LIGHT_POINT + i));
                }
            }
            for (i = 0; i < G3D_NUM_LIGHT_SPOT; i++)
            {
                int bEnable = gra3dIsLightEnable(GRA3D_START_LIGHT_SPOT + i);

                g3dLightEnable(G3D_START_LIGHT_SPOT + i, bEnable);
                if (bEnable != 0)
                {
                    g3dSetLight(G3D_START_LIGHT_SPOT + i, gra3dGetLightRef(GRA3D_START_LIGHT_SPOT + i));
                }
            }

            gra3dDmaLoadVu1MicroProgram(VU1MPG_REALTIME, 0);

            if (pnum < 0)
            {
                SgSortPreProcess(_BlockHead(pSGDTop, 0));
                iNumBlock = gra3dsgdGetNumBlock();
                for (i = 1; i < iNumBlock - 1; i++)
                {
                    SgSortUnitPrim(_BlockHead(pSGDTop, i));
                }
                // i is iNumBlock - 1 here (1 for a model of fewer blocks, as the ROM)
                if (_BlockHead(pSGDTop, i) != nullptr)
                {
                    SgSortUnitPrimPost(_BlockHead(pSGDTop, i));
                }
                return;
            }
            if (pnum == 0)
            {
                SgSortPreProcess(_BlockHead(pSGDTop, 0));
                return;
            }
            if (pnum == gra3dsgdGetNumBlock() - 1)
            {
                SgSortUnitPrimPost(_BlockHead(pSGDTop, pnum));
                return;
            }
            SgSortUnitPrim(_BlockHead(pSGDTop, pnum));
            return;
        }
    }
    else if (type == SRT_REALTIME)
    {
        if (pnum < 0)
        {
            save_bw_pointer   = nullptr;
            save_tri2_pointer = nullptr;
            SgSortPreProcessP(_BlockHead(pSGDTop, 0));
            iNumBlock = gra3dsgdGetNumBlock();
            for (i = 1; i < iNumBlock; i++)
            {
                gra3dsgdDrawPresetDataObject(_BlockHead(pSGDTop, i));
            }
            return;
        }
        if (pnum != 0)
        {
            save_bw_pointer   = nullptr;
            save_tri2_pointer = nullptr;
            gra3dsgdDrawPresetDataObject(_BlockHead(pSGDTop, pnum));
            return;
        }
        save_bw_pointer   = SGD_SAVE_TRI2_PENDING;
        save_tri2_pointer = SGD_SAVE_TRI2_PENDING;
        SgSortPreProcessP(_BlockHead(pSGDTop, 0));
        return;
    }

    if (type == SRT_PRELIGHTING)
    {
        if (bPreset != 0)
        {
            if (pnum < 0)
            {
                iNumBlock = gra3dsgdGetNumBlock();
                for (i = 1; i < iNumBlock; i++)
                {
                    SgPreRenderPrim(_BlockHead(pSGDTop, i));
                }
            }
            else if (pnum != 0)
            {
                SgPreRenderPrim(_BlockHead(pSGDTop, pnum));
            }
        }
    }
    else if (type == SRT_CLEARPRELIGHTING)
    {
        if (pnum < 0)
        {
            iNumBlock = gra3dsgdGetNumBlock();
            for (i = 1; i < iNumBlock; i++)
            {
                SgClearPreRenderPrim(_BlockHead(pSGDTop, i));
            }
        }
        else if (pnum != 0)
        {
            SgClearPreRenderPrim(_BlockHead(pSGDTop, pnum));
        }
    }
    else if (type == SRT_MAPSHADOW)
    {
        iNumBlock = gra3dsgdGetNumBlock();
        for (i = 1; i < iNumBlock - 1; i++)
        {
            if (gra3dsgdGetCoordinate(i)->bInViewvolume != 0)
            {
                AssignShadowPrim(_BlockHead(pSGDTop, i));
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// ST scrolling
// ──────────────────────────────────────────────────────────────────────

// A realtime mesh's ST stream: the point counts start 0x30 into the unit's
// data (past its VIF code and GIF tags), then a register set, then per strip
// a VIF code and its ST pairs.
// FF3 0x001e2950; from MioPan gra3dSGD.c.
static void ChangeST(float fAddS, float fAddT, u_char *pMeshData, int iNumMesh)
{
    SGDVUMESHPOINTNUM *pMPointNum = (SGDVUMESHPOINTNUM *)(pMeshData + 0x30);
    SGDVUMESHSTDATA   *pMSTData   = (SGDVUMESHSTDATA *)((u_char *)&pMPointNum[iNumMesh] + sizeof(SGDVUMESHSTREGSET));
    int                i;
    int                j;

    for (i = 0; i < iNumMesh; i++)
    {
        for (j = 0; j < pMPointNum->uiPointNum; j++)
        {
            pMSTData->ST()[j].fS += fAddS;
            pMSTData->ST()[j].fT += fAddT;
        }
        pMPointNum++;
        pMSTData = (SGDVUMESHSTDATA *)&pMSTData->ST()[j];
    }
}

// A preset mesh's ST stream (sOffsetToST words from +0xc of the unit).
// FF3 0x001e29c0; from MioPan gra3dSGD.c.
static void ChangeSTP(float fAddS, float fAddT, SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDATA_PRESET *pMeshData = (SGDVUMESHDATA_PRESET *)&pPUHead[1];
    SGDVUMESHSTDATA      *pMSTData  = (SGDVUMESHSTDATA *)((u_char *)pPUHead + 0xc + pMeshData->sOffsetToST * 4);
    int                   i;
    int                   j;

    for (i = pPUHead->VUMeshDesc.ucNumMesh; i != 0; i--)
    {
        int loops = pMSTData->aucVifUnpack[2];

        for (j = 0; j < loops; j++)
        {
            pMSTData->ST()[j].fS += fAddS;
            pMSTData->ST()[j].fT += fAddT;
        }
        pMSTData = (SGDVUMESHSTDATA *)&pMSTData->ST()[j];
    }
}

// Only the weighted realtime meshes (types & 0xd3 == 2 / 0x82) carry ST.
// FF3 0x001e2a38; from MioPan gra3dSGD.c.
static void ChangeSTVUMeshData(float fAddS, float fAddT, SGDPROCUNITHEADER *pPUHead)
{
    switch (pPUHead->VUMeshDesc.ucMeshType & 0xd3)
    {
    case 0x02:
    case 0x82:
        ChangeST(fAddS, fAddT, (u_char *)&pPUHead[1], pPUHead->VUMeshDesc.ucNumMesh);
        return;
    default:
        return;
    }
}

// FF3 0x001e2aa0; from MioPan gra3dSGD.c.
static void ChangeSTVUMeshDataP(float fAddS, float fAddT, SGDPROCUNITHEADER *pPUHead)
{
    switch (pPUHead->VUMeshDesc.ucMeshType)
    {
    case iMT_2:
    case iMT_2F:
        ChangeSTP(fAddS, fAddT, pPUHead);
        break;
    default:
        break;
    }
}

// FF3 0x001e2ae8; from MioPan gra3dSGD.c.
static void SgChangeSTUnitPrim(float fAddS, float fAddT, SGDPROCUNITHEADER *pPUHead, int bPreset)
{
    while (pPUHead != nullptr)
    {
        if (pPUHead->iCategory == SGDPU_MESH)
        {
            if (bPreset == 0)
            {
                ChangeSTVUMeshData(fAddS, fAddT, pPUHead);
            }
            else
            {
                ChangeSTVUMeshDataP(fAddS, fAddT, pPUHead);
            }
        }
        pPUHead = _Next(pPUHead);
    }
}

// FF3 0x001e2b80; from MioPan gra3dSGD.c.
void gra3dChangeST(SGDFILEHEADER *pSgdTop, float s, float t)
{
    int bPreset;
    int i;

    // PC: FF3 reads ucModelType before the null test
    if (pSgdTop == nullptr || pSgdTop->uiVersionId != SGD_VALID_VERSIONID)
    {
        return;
    }
    bPreset = pSgdTop->ucModelType & 1;

    ReiPan_Graph3dNotifyUVChange(pSgdTop);                  // PC
    gra3dsgdSetData(pSgdTop);
    for (i = 1; i < gra3dsgdGetNumBlock() - 1; i++)
    {
        SgChangeSTUnitPrim(s, t, _BlockHead(pSgdTop, i), bPreset);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Preset vertex colours, material alpha
// ──────────────────────────────────────────────────────────────────────

// Set vertex iVertexNo's colour (or every vertex's, iVertexNo < 0) in a preset
// mesh; markers (a first word of 1) stay, a colour that would look like one is
// bumped by one.
// FF3 0x001e2c30; from MioPan gra3dSGD.c.
static void SetVertexColorPresetMeshData(SGDPROCUNITHEADER *pPUHead, int iVertexNo, float *vSetColor)
{
    SGDVUMESHDATA_PRESET *pMeshData = (SGDVUMESHDATA_PRESET *)&pPUHead[1];
    int                   mtype     = pPUHead->VUMeshDesc.ucMeshType;
    int                   gloops    = pPUHead->VUMeshDesc.ucNumMesh;
    u_int                *prim;
    int                   iVertexCount;
    int                   j;
    int                   k;

    if (pMeshData->sOffsetToPrim == 0)
    {
        return;
    }
    prim = (u_int *)pPUHead + pMeshData->sOffsetToPrim;
    if (mtype != iMT_0 && mtype != iMT_2 && mtype != iMT_2F)
    {
        return;
    }

    if (iVertexNo < 0)
    {
        for (j = 0; j < gloops; j++)
        {
            _SGDVUMESHCOLORDATA *pVMCD = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr(prim);
            int                  loops = pVMCD->aucVifUnpack[2];
            u_int               *pCol  = (u_int *)pVMCD->Color();

            for (k = loops; k != 0; k--)
            {
                if (*pCol != 1)
                {
                    memcpy(pCol, vSetColor, sizeof(VECTOR3));
                    if ((*pCol & 0xffff) == 1)
                    {
                        (*pCol)++;
                    }
                }
                pCol += 3;
            }
            prim = (u_int *)&pVMCD->Color()[loops];
        }
        return;
    }

    iVertexCount = 0;
    for (j = 0; j < gloops; j++)
    {
        _SGDVUMESHCOLORDATA *pVMCD = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr(prim);
        int                  loops = pVMCD->aucVifUnpack[2];

        if (iVertexNo <= iVertexCount + loops)
        {
            for (k = 0; k < loops; k++)
            {
                if (iVertexCount == iVertexNo)
                {
                    u_int *pCol = (u_int *)pVMCD->Color()[k];

                    if (*pCol == 1)
                    {
                        return;
                    }
                    memcpy(pCol, vSetColor, sizeof(VECTOR3));
                    if ((*pCol & 0xffff) == 1)
                    {
                        (*pCol)++;
                    }
                    return;
                }
                iVertexCount++;
            }
        }
        else
        {
            iVertexCount += loops;
        }
        prim = (u_int *)&pVMCD->Color()[loops];
    }
}

// FF3 0x001e2e30; from MioPan gra3dSGD.c.
static void SgSetVertexColorUnitPrimP(SGDPROCUNITHEADER *pPUHead, int iVertexNo, float *vSetColor)
{
    while (pPUHead != nullptr)
    {
        if (pPUHead->iCategory == SGDPU_MESH)
        {
            SetVertexColorPresetMeshData(pPUHead, iVertexNo, vSetColor);
        }
        pPUHead = _Next(pPUHead);
    }
}

// FF3 0x001e2ea0; from MioPan gra3dSGD.c.
void gra3dSetVertexColorPreset(SGDFILEHEADER *pSGDTop, int iVertexNo, float *vSetColor)
{
    int i;

    if (pSGDTop == nullptr || pSGDTop->uiVersionId != SGD_VALID_VERSIONID)
    {
        return;
    }

    gra3dsgdSetData(pSGDTop);
    for (i = 1; i < gra3dsgdGetNumBlock() - 1; i++)
    {
        SgSetVertexColorUnitPrimP(_BlockHead(pSGDTop, i), iVertexNo, vSetColor);
    }
}

// FF3 0x001e2f38 (FF3-only).
int gra3dGetSGDMaterialAlpha(SGDFILEHEADER *pSgdTop)
{
    if (pSgdTop->pMaterial != 0)
    {
        return (int)SgdPtr<SGDMATERIAL>(pSgdTop->pMaterial)->vDiffuse[3] & 0xff;
    }
    return 0x80;
}

// The materials run from pMaterial up to the vertex-pool header.
// FF3 0x001e2f60 (FF3-only).
void gra3dSetSGDMaterialAlpha(SGDFILEHEADER *pSgdTop, u_char alpha)
{
    u_int uiMat;

    for (uiMat = pSgdTop->pMaterial; uiMat < pSgdTop->pVectorInfo; uiMat += sizeof(SGDMATERIAL))
    {
        SgdPtr<SGDMATERIAL>(uiMat)->vDiffuse[3] = (float)alpha;
    }
}

// ──────────────────────────────────────────────────────────────────────
// gra3dDrawModelCb's walker
// ──────────────────────────────────────────────────────────────────────

// Walk one block up to its first mesh: a coordinate unit sets WORLDSCREEN from
// its local->world (kept in s_matScreenRectLocalWorld), a bounding box is
// tested and sets WORLDCLIP from the object frustum; at the mesh, local->view
// and the two transforms go to VU0 and the mesh's screen rectangle is taken.
// Returns that result, or the last bounding-box test when no mesh follows.
// FF3 0x001e2fa8 (FF3-only).
int SgCalcScreenRectPrim(GRA3DCAMERA *pCam, SGDPROCUNITHEADER *pPUHead)
{
    int           bResult = 0;
    sceVu0FMATRIX matWork;

    if (pPUHead == nullptr)
    {
        return 0;
    }

    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;

        case SGDPU_MESH:
            sceVu0MulMatrix(matWork, pCam->matWorldView, s_matScreenRectLocalWorld);
            Vu0LoadMatrices_4_25_8(matWork, gra3dGetTransformRef(GRA3DTS_WORLDCLIP),
                                   gra3dGetTransformRef(GRA3DTS_WORLDSCREEN));
            return gra3dCalcMeshScreenRect(pPUHead);

        case SGDPU_COORDINATE:
            memcpy(s_matScreenRectLocalWorld, s_pCoordBase[pPUHead->CoordDesc.iCoordId0].matLocalWorld,
                   sizeof(sceVu0FMATRIX));
            sceVu0MulMatrix(matWork, pCam->matWorldScreen, s_matScreenRectLocalWorld);
            gra3dSetTransform(GRA3DTS_WORLDSCREEN, matWork);
            break;

        case SGDPU_BOUNDING_BOX:
            bResult = CheckBoundingBox(pPUHead);
            sceVu0MulMatrix(matWork, pCam->matWorldClipObject, s_matScreenRectLocalWorld);
            gra3dSetTransform(GRA3DTS_WORLDCLIP, matWork);
            break;

        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
    return bResult;
}

// gra3dsgdDrawPresetDataObject without the cull: every block is drawn, a
// coordinate unit loads its world->screen, TRI2 units go out at once.
// Nothing in FF3 calls it.
// FF3 0x001e3290 (FF3-only).
void gra3dsgdDrawPresetDataObjectNoClip(SGDPROCUNITHEADER *pPUHead)
{
    sceVu0FMATRIX matWorldScreen;

    while (pPUHead != nullptr)
    {
        switch (pPUHead->iCategory)
        {
        case SGDPU_VUVN:
            _SetVUVNPRIM(pPUHead);
            break;
        case SGDPU_MESH:
            SetVUMeshDataP(pPUHead);
            break;
        case SGDPU_MATERIAL:
            SetMaterialDataVU(pPUHead);
            break;
        case SGDPU_COORDINATE:
            sceVu0MulMatrix(matWorldScreen, gra3dGetCamera()->matWorldScreen,
                            s_pCoordBase[pPUHead->CoordDesc.iCoordId0].matLocalWorld);
            gra3dSetTransform(GRA3DTS_WORLDSCREEN, matWorldScreen);
            SetVU1Header();
            break;
        case SGDPU_BOUNDING_BOX:
            if (BoundingBoxCalcP(pPUHead) == 0)
            {
                return;
            }
            break;
        case SGDPU_GS_IMAGE:
            GsImageProcess(pPUHead);
            break;
        case SGDPU_TRI2:
            LoadTRI2Files(pPUHead);
            break;
        default:
            break;
        }
        pPUHead = _Next(pPUHead);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Light ranking (template instances on NUM = 4)
// ──────────────────────────────────────────────────────────────────────

template <int NUM> static void _CalcValidLightIndexByType(int *aiRet, int type, float (*avBB)[4]);

// callcheck: _CalcValidLightIndexByType
//   (called as the template instance _CalcValidLightIndexByType<NUM>)
// Make the best three lights of a type the VU1 ones; the type stays off when
// none reaches the box.
// FF3 0x001e33f8; from MioPan gra3dSGD.c.
template <int NUM> static void _SelectLightByType(int type, float (*avBB)[4])
{
    int aiIndex[NUM];
    int iIndex;

    _CalcValidLightIndexByType<NUM>(aiIndex, type, avBB);

    gra3dEnableLightType(type, 0);
    gra3dSetValidLightId(type, 0, INVALID_GRA3DLIGHTID);
    gra3dSetValidLightId(type, 1, INVALID_GRA3DLIGHTID);
    gra3dSetValidLightId(type, 2, INVALID_GRA3DLIGHTID);

    for (iIndex = 0; iIndex < G3D_MAX_LIGHT_PER_TYPE; iIndex++)
    {
        if (aiIndex[iIndex] >= 0)
        {
            gra3dSetValidLightId(type, iIndex, aiIndex[iIndex]);
            gra3dEnableLightType(type, 1);
        }
    }
}

template <int NUM> static void _SortLightCompareData(_LIGHTCOMPAREDATA *aLCD, float fMaxPower, int iIndex);

// callcheck: _SortLightCompareData, fill_LIGHTCOMPAREDATA
//   (_SortLightCompareData<NUM>; std::fill's instance, FF3 0x002bcad0)
// Rank the lights of a type by the power reaching the box: sum of the
// diffuse colour x fMaxRange / distance to the box centre (avBB[8]). FF3 skips
// spots below gra3dGetDrawParamF when gra3dGetDrawParamI is set.
// FF3 0x001e34c0; from MioPan gra3dSGD.c.
template <int NUM> static void _CalcValidLightIndexByType(int *aiRet, int type, float (*avBB)[4])
{
    _LIGHTCOMPAREDATA aLCD[NUM];
    int               iNum;
    int               iBase;
    int               i;

    std::fill(aLCD, aLCD + NUM, s_NullLightCompareData);

    switch (type)
    {
    case G3DLIGHT_POINT:
        iNum = GRA3D_NUM_LIGHT_POINT;
        break;
    case G3DLIGHT_DIRECTIONAL:
        iNum = GRA3D_NUM_LIGHT_DIRECTIONAL;
        break;
    case G3DLIGHT_SPOT:
        iNum = GRA3D_NUM_LIGHT_SPOT;
        break;
    default:
        iNum = 0;
        break;
    }
    switch (type)
    {
    case G3DLIGHT_POINT:
        iBase = GRA3D_START_LIGHT_POINT;
        break;
    case G3DLIGHT_DIRECTIONAL:
        iBase = GRA3D_START_LIGHT_DIRECTIONAL;
        break;
    case G3DLIGHT_SPOT:
        iBase = GRA3D_START_LIGHT_SPOT;
        break;
    default:
        iBase = INVALID_GRA3DLIGHTID;
        break;
    }

    for (i = 0; i < iNum; i++)
    {
        if (gra3dIsLightEnable(iBase + i) != 0)
        {
            G3DLIGHT *pLight = gra3dGetLightRef(iBase + i);

            if (g3dIsBBLightingup(pLight, avBB) != 0)
            {
                float fColScale = pLight->vDiffuse[0] + pLight->vDiffuse[1] + pLight->vDiffuse[2];
                float dx        = pLight->vPosition[0] - avBB[8][0];
                float dy        = pLight->vPosition[1] - avBB[8][1];
                float dz        = pLight->vPosition[2] - avBB[8][2];
                float fPower    = (fColScale * pLight->fMaxRange) / sqrtf(dx * dx + dy * dy + dz * dz);

                if (type != G3DLIGHT_SPOT || gra3dGetDrawParamI() == 0 || gra3dGetDrawParamF() <= fPower)
                {
                    _SortLightCompareData<NUM>(aLCD, fPower, i);
                }
            }
        }
    }

    for (i = 0; i < 3; i++)
    {
        aiRet[i] = aLCD[i].iIndex;
    }
}

// Insert (fMaxPower, iIndex) into the first NUM - 1 ranks, strongest first.
// FF3 0x001e36c8; from MioPan gra3dSGD.c.
template <int NUM> static void _SortLightCompareData(_LIGHTCOMPAREDATA *aLCD, float fMaxPower, int iIndex)
{
    int j;
    int k;

    for (j = 0; j < NUM - 1; j++)
    {
        if (aLCD[j].fMaxPower < fMaxPower)
        {
            for (k = NUM - 1; j < k; k--)
            {
                _LIGHTCOMPAREDATA tmp = aLCD[k];

                aLCD[k]     = aLCD[k - 1];
                aLCD[k - 1] = tmp;
            }
            aLCD[j].iIndex    = iIndex;
            aLCD[j].fMaxPower = fMaxPower;
            return;
        }
    }
}

// Whether a cached light set (point, spot) matches the current one: same
// type switches and, for an enabled type, the same three lights in VU1.
// FF3 0x001e3750 (FF2 inlined it in CheckCoordCache / SetMaterialDataVU).
static int _IsSameMaterialIndexCache(GRA3DMATERIALINDEXCACHE *pPoint, GRA3DMATERIALINDEXCACHE *pSpot)
{
    int bEnablePoint = gra3dIsLightTypeEnable(G3DLIGHT_POINT);
    int bEnableSpot  = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);
    int i;

    if (pPoint->bEnable != bEnablePoint || pSpot->bEnable != bEnableSpot)
    {
        return 0;
    }

    if (pPoint->bEnable != 0)
    {
        GRA3DVU1MATERIALCACHE_POINT *pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);

        for (i = 0; i < 3; i++)
        {
            if (pPoint->aiIndex[i] != pMatCache->Index.aiIndex[i])
            {
                return 0;
            }
        }
    }
    if (bEnableSpot != 0)
    {
        GRA3DVU1MATERIALCACHE_SPOT *pMatCache = g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);

        for (i = 0; i < 3; i++)
        {
            if (pSpot->aiIndex[i] != pMatCache->Index.aiIndex[i])
            {
                return 0;
            }
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Weighted-blend kernels (VU0 macro mode; addresses taken by CalcVertexBuffer)
// ──────────────────────────────────────────────────────────────────────

// dp = (mat0 v0) w + (mat1 v1) (1 - w), w = v0.w; dp.w = 1.
// FF3 0x002bcb00; from MioPan gra3dSGD.c (PC: matrices as arguments).
static void _CalcWeightedVertexBuffer(float *dp, const float *v, float (*mat0)[4], float (*mat1)[4])
{
    const float *v0 = v + 0;
    const float *v1 = v + 4;
    float        w  = v0[3];
    int          c;

    for (c = 0; c < 3; c++)
    {
        dp[c] = (v0[0] * mat0[0][c] + v0[1] * mat0[1][c] + v0[2] * mat0[2][c] + mat0[3][c]) * w
              + (v1[0] * mat1[0][c] + v1[1] * mat1[1][c] + v1[2] * mat1[2][c] + mat1[3][c]) * (1.0f - w);
    }
    dp[3] = 1.0f;
}

// The rotation-only blend, both weights scaled by mat0[3][3] (3 / column
// length sum from _CalcWeightedLocalWorldMatrix); dp.w = 1.
// FF3 0x002bcb40; from MioPan gra3dSGD.c (PC: matrices as arguments).
static void _CalcWeightedNormalBuffer(float *dp, const float *v, float (*mat0)[4], float (*mat1)[4])
{
    const float *n0 = v + 0;
    const float *n1 = v + 4;
    float        w0 = n0[3] * mat0[3][3];
    float        w1 = (1.0f - n0[3]) * mat0[3][3];
    int          c;

    for (c = 0; c < 3; c++)
    {
        dp[c] = (n0[0] * mat0[0][c] + n0[1] * mat0[1][c] + n0[2] * mat0[2][c]) * w0
              + (n1[0] * mat1[0][c] + n1[1] * mat1[1][c] + n1[2] * mat1[2][c]) * w1;
    }
    dp[3] = 1.0f;
}
