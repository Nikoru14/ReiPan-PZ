/* ==========================================================================
 *  graphics/graph3d/g3dLightEx.c
 *
 *  Extended light services of the g3d core: emulating any G3DLIGHT, seen
 *  from one target point, as an equivalent directional light (a direction, a
 *  colour and a scalar strength fLength); blending such emulated lights;
 *  reducing many of them to the three directional slots; whether a light can
 *  reach a bounding box; sorting lights by the power reaching a box.
 *
 *  Ported from MioPan's g3dLightEx.c (FF2 0x1a2a00..0x1a3a20); FF3's
 *  functions (0x1ce0f8..0x1cec20) are FF2's minus the asserts and warnings,
 *  with these differences found in FF3's code:
 *  - _IsBBLightingupSpot keeps the ROM test (`< 0`, c.lt.S at 0x1ce144);
 *    MioPan deviates to `> 0` (open-items.md "_IsBBLightingupSpot sense").
 *  - g3dEmulateDirectionalLight scales only the diffuse xyz (vmulx.xyz
 *    0x1ce5c0; w keeps the light's diffuse w) and returns without touching
 *    the record for an unknown type.
 *  - gra3dSetLightIntens has no range warning.
 *  The VU0 macro blocks are written as scalar C. The g3dxVu0 dot products
 *  (vadday.x / vmaddz.x vf1) rely on vf1.x == 1.0 and sum x + y + z.
 * ======================================================================== */

#include "graphics/graph3d/g3dLightEx.h"
#include "common/vu0math.h"          // PC: Vu0Rsqrt / Ps2Divf

#include <math.h>                               // sqrtf
#include <string.h>                             // memset

#include <algorithm>                            // std::max_element, std::sort

#include "graphics/graph3d/g3dGeom.h"           // g3dCalcPlaneFromPointNormal

#define G3D_DEG2RAD_PI 3.1415927f               // rdata 0x370a88 (/ 180.0f at 0x370a8c)

typedef int (*LPFUNC_ISBBLIGHTINGUP)(G3DLIGHT *pLight, float (*avBB)[4]);

// x + y + z of a * b (the g3dxVu0 inner product).
static inline float _Dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The g3dxVu0 normalize: xyz scaled by 1 / |xyz| (vrsqrt), w copied
// (vmul.w by vf0.w).
static inline void _Normalize(float *vDst, const float *vSrc)
{
    float fRcp = Vu0Rsqrt(_Dot3(vSrc, vSrc));     // PC: PS2 rsqrt
    float w = vSrc[3];

    vDst[0] = vSrc[0] * fRcp;
    vDst[1] = vSrc[1] * fRcp;
    vDst[2] = vSrc[2] * fRcp;
    vDst[3] = w;
}

static inline void _ZeroVector(float *v)
{
    v[0] = 0.0f;                                // g_xv0000 (data 0x328840)
    v[1] = 0.0f;
    v[2] = 0.0f;
    v[3] = 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// Can a spot light reach the box? Builds the plane through the light's
// position with its direction as normal and asks whether any of the box's 8
// corners lies at negative distance (the ROM's sense; see the file comment).
// FF3 0x001ce0f8; FF2 _IsBBLightingupSpot 0x1a2a00.

static int _IsBBLightingupSpot(G3DLIGHT *pLight, float (*avBB)[4])
{
    float plane[4];
    int   i;

    g3dCalcPlaneFromPointNormal(plane, pLight->vPosition, pLight->vDirection);

    for (i = 0; i < 8; i++)
    {
        float fDist = _Dot3(plane, avBB[0]) + plane[3];

        avBB++;

        if (fDist < 0.0f)
        {
            return 1;
        }
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// A point light is omnidirectional: always potentially lights the box.
// FF3 0x001ce178; FF2 _IsBBLightingupPoint.

static int _IsBBLightingupPoint(G3DLIGHT *pLight, float (*avBB)[4])
{
    (void)pLight;
    (void)avBB;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// An emulated record back into a real directional G3DLIGHT: defaulted, the
// diffuse copied, the direction normalised.
// FF3 0x001ce180; FF2 _ConvertEmulateDirectionallightDataToG3DLIGHT 0x1a2c48.

static void _ConvertEmulateDirectionallightDataToG3DLIGHT(G3DLIGHT *pDest,
                                                          G3D_EMULATE_DIRECTIONALLIGHT_DATA *pEDD)
{
    g3dutilSetLightDefault(pDest, G3DLIGHT_DIRECTIONAL);

    pDest->vDiffuse[0] = pEDD->vDiffuse[0];
    pDest->vDiffuse[1] = pEDD->vDiffuse[1];
    pDest->vDiffuse[2] = pEDD->vDiffuse[2];
    pDest->vDiffuse[3] = pEDD->vDiffuse[3];

    _Normalize(pDest->vDirection, pEDD->vDirection);

    pDest->Type = G3DLIGHT_DIRECTIONAL;
}

// ──────────────────────────────────────────────────────────────────────
// "Fold pSrc1 into pSrc0 by their strength" -- but, as in FF2, both
// operands of each interpolation are loaded from pSrc0 (lqc2 vf4 / vf5 both
// 0x0(a1) at 0x1ce238 / 0x1ce23c, both 0x0(t6) at 0x1ce278 / 0x1ce27c), so
// the result is pSrc0's colour and direction, and fLength = fTotal *
// (pSrc0->fLength / fTotal) = pSrc0->fLength. The only caller passes
// pDest == pSrc0, so lights past the third are dropped, not folded. MioPan
// documents why the "fixed" blend darkens characters; keep the ROM's.
// fTotal <= 0 zeroes colour and direction and leaves fLength.
// FF3 0x001ce1f0; FF2 _BlendEmulatedDirectionalLight 0x1a2cf8.

static void _BlendEmulatedDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pDest,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *pSrc0,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *pSrc1)
{
    float fTotal;
    float fRatioSrc0;
    float t;
    int   k;

    fTotal = pSrc0->fLength + pSrc1->fLength;

    if (0.0f < fTotal)
    {
        fRatioSrc0 = pSrc0->fLength / fTotal;

        // vmulax.xyz / vmaddx.xyz, vmove.w
        t = fRatioSrc0;
        for (k = 0; k < 3; k++)
        {
            pDest->vDiffuse[k] = pSrc0->vDiffuse[k] * t + pSrc0->vDiffuse[k] * (1.0f - t);
        }
        pDest->vDiffuse[3] = pSrc0->vDiffuse[3];

        t = 1.0f - fRatioSrc0;
        for (k = 0; k < 3; k++)
        {
            pDest->vDirection[k] = pSrc0->vDirection[k] * t + pSrc0->vDirection[k] * (1.0f - t);
        }
        pDest->vDirection[3] = pSrc0->vDirection[3];
        _Normalize(pDest->vDirection, pDest->vDirection);

        pDest->fLength = fTotal * fRatioSrc0;
        return;
    }

    _ZeroVector(pDest->vDiffuse);
    _ZeroVector(pDest->vDirection);
}

// ──────────────────────────────────────────────────────────────────────
// Strength-weighted blend of iNumSrc emulated lights: diffuse = sum(diffuse
// * fLength) with xyz clamped to [0, 1] (vmaxx.xyz / vminibcx.xyz, w passed
// through), direction = normalize(sum(dir * fLength)), fLength = fTotal *
// iNumSrc. All zero when the total strength is not positive.
// FF3 0x001ce2d0; FF2's N-source _BlendEmulatedDirectionalLight overload.

static void _BlendEmulatedDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pDest,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *aSrc,
                                           int iNumSrc)
{
    float fTotal;
    float fLength;
    float vWork[4];
    int   i;
    int   k;

    fTotal = 0.0f;
    for (i = 0; i < iNumSrc; i++)
    {
        fTotal = fTotal + aSrc[i].fLength;
    }

    fLength = 0.0f;
    if (fTotal <= 0.0f)
    {
        _ZeroVector(pDest->vDiffuse);
        _ZeroVector(pDest->vDirection);
    }
    else
    {
        memset(vWork, 0, sizeof(vWork));
        for (i = 0; i < iNumSrc; i++)
        {
            for (k = 0; k < 4; k++)
            {
                vWork[k] = aSrc[i].vDiffuse[k] * aSrc[i].fLength + vWork[k];
            }
        }
        for (k = 0; k < 3; k++)
        {
            float f = vWork[k];

            if (f < 0.0f)
            {
                f = 0.0f;
            }
            if (1.0f < f)                       // rdata 0x370a7c
            {
                f = 1.0f;
            }
            pDest->vDiffuse[k] = f;
        }
        pDest->vDiffuse[3] = vWork[3];

        _ZeroVector(vWork);                     // g_v0000 (data 0x329530)
        for (i = 0; i < iNumSrc; i++)
        {
            for (k = 0; k < 4; k++)
            {
                vWork[k] = aSrc[i].vDirection[k] * aSrc[i].fLength + vWork[k];
            }
        }
        _Normalize(pDest->vDirection, vWork);

        fLength = fTotal * (float)iNumSrc;
    }

    pDest->fLength = fLength;
}

// ──────────────────────────────────────────────────────────────────────
// Store the intensity in afPad0[0]; for a spot light map it to the cone:
// outer half-angle (90 - 90 * intens) degrees, inner half of that.
// FF3 0x001ce430; FF2 gra3dSetLightIntens 0x1a3048.

void gra3dSetLightIntens(G3DLIGHT *pLight, float fIntens)
{
    float fAngle;

    pLight->afPad0[0] = fIntens;

    if (pLight->Type == G3DLIGHT_SPOT)
    {
        fAngle = ((fIntens * -90.0f + 90.0f) * G3D_DEG2RAD_PI) / 180.0f;
        pLight->fAngleOutside = fAngle;
        pLight->fAngleInside  = fAngle * 0.5f;
    }
}

// ──────────────────────────────────────────────────────────────────────
// pLight as seen from vPos, as a directional light:
//   directional: its direction and diffuse, strength 1;
//   point:       direction normalize(position - vPos), strength the
//                distance attenuation, diffuse.xyz scaled by it;
//   spot:        as point, strength also times the cone falloff.
// Another type leaves pEDD as it was.
// FF3 0x001ce4a8; FF2 g3dEmulateDirectionalLight 0x1a3160.

void g3dEmulateDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pEDD, G3DLIGHT *pLight, float *vPos)
{
    float vDir[4];
    int   k;

    switch (pLight->Type)
    {
        case G3DLIGHT_DIRECTIONAL:
            for (k = 0; k < 4; k++)
            {
                pEDD->vDirection[k] = pLight->vDirection[k];
                pEDD->vDiffuse[k]   = pLight->vDiffuse[k];
            }
            pEDD->fLength = 1.0f;               // rdata 0x370a94
            return;

        case G3DLIGHT_POINT:
            for (k = 0; k < 4; k++)
            {
                vDir[k] = pLight->vPosition[k] - vPos[k];
            }
            _Normalize(pEDD->vDirection, vDir);
            pEDD->fLength = g3dCalcLightDistanceAttenuation(pLight, vPos);
            break;

        case G3DLIGHT_SPOT:
            for (k = 0; k < 4; k++)
            {
                vDir[k] = pLight->vPosition[k] - vPos[k];
            }
            _Normalize(pEDD->vDirection, vDir);
            pEDD->fLength = g3dCalcLightDistanceAttenuation(pLight, vPos);
            pEDD->fLength = pEDD->fLength * g3dCalcSpotlightFalloff(pLight, vPos);
            break;

        default:
            return;
    }

    // vmulx.xyz: w keeps the light's diffuse w
    pEDD->vDiffuse[0] = pLight->vDiffuse[0] * pEDD->fLength;
    pEDD->vDiffuse[1] = pLight->vDiffuse[1] * pEDD->fLength;
    pEDD->vDiffuse[2] = pLight->vDiffuse[2] * pEDD->fLength;
    pEDD->vDiffuse[3] = pLight->vDiffuse[3];
}

// ──────────────────────────────────────────────────────────────────────
// How strongly pLight lights vPos: strength * |diffuse.xyz| of its
// emulated directional light.
// FF3 0x001ce620; FF2 g3dCalcLightPower 0x1a3458.

float g3dCalcLightPower(G3DLIGHT *pLight, float *vPos)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA edd;

    g3dEmulateDirectionalLight(&edd, pLight, vPos);

    return edd.fLength * sqrtf(_Dot3(edd.vDiffuse, edd.vDiffuse));
}

// ──────────────────────────────────────────────────────────────────────
// pLight emulated at vPos, written into pDest as a directional light.
// FF3 0x001ce670; FF2 gra3dEmulateDirectionalLight 0x1a34b0.

void gra3dEmulateDirectionalLight(G3DLIGHT *pDest, G3DLIGHT *pLight, int iLightType, float *vPos)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA edd;

    (void)iLightType;
    memset(&edd, 0, sizeof(edd));               // PC: the ROM leaves an unknown type's record unset
    g3dEmulateDirectionalLight(&edd, pLight, vPos);
    _ConvertEmulateDirectionallightDataToG3DLIGHT(pDest, &edd);
}

// ──────────────────────────────────────────────────────────────────────
// Reduce iNumEmulated emulated lights to the three directional slots of
// aDest: the first three are the top set; each later light is matched to
// the top light whose direction it is closest to (largest cos, against
// aSrc[0..2]) and "blended" into it -- a no-op (see the two-source blend).
// Slots past iNumEmulated get a default directional light.
// FF3 0x001ce6a8; FF2 g3dGenerateDirectionalLightByEmulatedData 0x1a34e8.

void g3dGenerateDirectionalLightByEmulatedData(G3DLIGHT *aDest, G3D_EMULATE_DIRECTIONALLIGHT_DATA *aSrc,
                                               int iNumEmulated)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA aTop[G3D_NUM_LIGHT_DIRECTIONAL];
    float                             afCos[G3D_NUM_LIGHT_DIRECTIONAL];
    float                            *pfMax;
    int                               iIndex;
    int                               i;
    int                               j;

    for (i = 0; i < G3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        if (i < iNumEmulated)
        {
            memcpy(&aTop[i], &aSrc[i], sizeof(G3D_EMULATE_DIRECTIONALLIGHT_DATA));
        }
        else
        {
            memset(&aTop[i], 0, sizeof(G3D_EMULATE_DIRECTIONALLIGHT_DATA));
        }
    }

    for (i = G3D_NUM_LIGHT_DIRECTIONAL; i < iNumEmulated; i++)
    {
        for (j = 0; j < G3D_NUM_LIGHT_DIRECTIONAL; j++)
        {
            afCos[j] = _Dot3(aSrc[j].vDirection, aSrc[i].vDirection);
        }

        pfMax = std::max_element(afCos, &afCos[G3D_NUM_LIGHT_DIRECTIONAL]);    // FF3 0x002bb1d0
        if (0.0f < *pfMax)
        {
            iIndex = (int)(pfMax - afCos);
            _BlendEmulatedDirectionalLight(&aTop[iIndex], &aTop[iIndex], &aSrc[i]);
        }
    }

    for (i = 0; i < G3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        if (i < iNumEmulated)
        {
            _ConvertEmulateDirectionallightDataToG3DLIGHT(&aDest[i], &aTop[i]);
        }
        else
        {
            g3dutilSetLightDefault(&aDest[i], G3DLIGHT_DIRECTIONAL);
            aDest[i].Type = G3DLIGHT_DIRECTIONAL;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Can pLight light the box avBB (8 corners + centre)? Dispatched by type:
// directional (no test) -> yes, point -> yes, spot -> the plane test.
// FF3 0x001ce870; FF2 g3dIsBBLightingup.

int g3dIsBBLightingup(G3DLIGHT *pLight, float (*avBB)[4])
{
    LPFUNC_ISBBLIGHTINGUP aFunc[NUM_G3DLIGHTTYPE];

    aFunc[G3DLIGHT_DIRECTIONAL] = nullptr;
    aFunc[G3DLIGHT_POINT]       = _IsBBLightingupPoint;
    aFunc[G3DLIGHT_SPOT]        = _IsBBLightingupSpot;

    if (aFunc[pLight->Type] != nullptr)
    {
        return aFunc[pLight->Type](pLight, avBB);
    }

    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// One record per light: power = sum(diffuse.xyz) * fMaxRange / distance from
// the light to the box centre avBB[8], 0 (and no light) when the light is
// null or cannot reach the box; then sorted strongest first.
// FF3 0x001ce8c8 (no callers); FF2 g3dSortLightForBoundingBoxByPowerOrder.
// callcheck: __introsort_loop, __final_insertion_sort (std::sort)

void g3dSortLightForBoundingBoxByPowerOrder(int iNum, LIGHTCOMPAREDATA *aCD, G3DLIGHT **apLightSrc,
                                            float (*avBB)[4])
{
    int i;

    for (i = 0; i < iNum; i++)
    {
        G3DLIGHT *pL = apLightSrc[i];

        aCD[i].iIndex = i;
        aCD[i].fPower = 0.0f;
        aCD[i].pLight = nullptr;

        if ((pL != nullptr) && (g3dIsBBLightingup(pL, avBB) != 0))
        {
            float fColScale = pL->vDiffuse[0] + pL->vDiffuse[1] + pL->vDiffuse[2];
            float vD[3];

            vD[0] = pL->vPosition[0] - avBB[8][0];
            vD[1] = pL->vPosition[1] - avBB[8][1];
            vD[2] = pL->vPosition[2] - avBB[8][2];

            aCD[i].pLight = pL;
            aCD[i].fPower = Ps2Divf(fColScale * pL->fMaxRange, sqrtf(_Dot3(vD, vD)));   // PC: PS2 division
        }
    }

    std::sort(aCD, aCD + iNum, LIGHTCOMPAREDATA::greater());
}

// ──────────────────────────────────────────────────────────────────────
// Emulate iNumSrc lights at vTarget (a null entry counts as a zero light),
// blend them into one and write it into pLight as a directional light on
// top of the null light (position w 1, fFalloff 1, inlined at 0x1ceb20).
// FF3 0x001cea78; FF2 g3dBlendLight.

void g3dBlendLight(G3DLIGHT *pLight, G3DLIGHT **apSrc, int iNumSrc, float *vTarget)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA EDDDest;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA aEDDSrc[G3D_MAX_BLENDLIGHT];
    int                               i;

    for (i = 0; i < iNumSrc; i++)
    {
        if (apSrc[i] == nullptr)
        {
            _ZeroVector(aEDDSrc[i].vDirection);
            _ZeroVector(aEDDSrc[i].vDiffuse);
            aEDDSrc[i].fLength = 0.0f;
        }
        else
        {
            g3dEmulateDirectionalLight(&aEDDSrc[i], apSrc[i], vTarget);
        }
    }

    _BlendEmulatedDirectionalLight(&EDDDest, aEDDSrc, iNumSrc);

    memset(pLight, 0, sizeof(G3DLIGHT));        // g_NullLight
    pLight->vPosition[3] = 1.0f;
    pLight->Type         = INVALID_G3DLIGHTTYPE;
    pLight->fFalloff     = 1.0f;

    for (i = 0; i < 4; i++)
    {
        pLight->vDiffuse[i]   = EDDDest.vDiffuse[i];
        pLight->vDirection[i] = EDDDest.vDirection[i];
    }

    pLight->Type = G3DLIGHT_DIRECTIONAL;
}
