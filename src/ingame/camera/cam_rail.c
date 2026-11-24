/* ==========================================================================
 *  ingame/camera/cam_rail.c
 *
 *  The rail camera, the mode-10 stance camera and their XZ geometry, FF3
 *  0x137cb8..0x139e10 (FF3-only, no FF2 counterpart; see cam_rail.h).
 *  CameraMain and MapCamRectangleCameraCtrl (map_camera.c) run the rail
 *  camera for rectangle types >= 100 and the stance camera in mode 10.
 *
 *  The ROM measures with inline VU0 code: `vaddz.x` after the square is an
 *  XZ length, `vadday.x; vmaddz.x vd, vf1, vd` an XYZ one (vf1 = 1s,
 *  hazards.md); CamDistXZ / CamDist3 below are those.
 * ======================================================================== */

#include "ingame/camera/cam_rail.h"

#include <math.h>                       // sqrtf, fabsf
#include <string.h>                     // memset

#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "common/vu0math.h"             // Vu0Cosf, Vu0Sinf, NormalizeAngle, GetTrgtRot
#include "graphics/ecb.h"               // EcbGetNum, EcbGetData
#include "graphics/effect/effect.h"     // SetEffects
#include "graphics/graph3d/gra3d.h"
#include "ingame/camera/map_camera.h"   // ApproachCameraCrossFadeSW
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/map/MapLight.h"        // MapLBuffForEachQuad
#include "ingame/map/RegDat.h"          // RegDatGetBuffIDG, the rectangle walk
#include "ingame/map/hit_check_base.h"  // HcBaseIsNearSegRect
#include "ingame/plyr/player.h"         // PLYR_WRK
#include "ingame/plyr/plyr_sub.h"       // PlyrGetJointPos
#include "ingame/plyr/unit_ctl.h"       // OutSightChk, GetLength2D
#include "sdk/libvu0.h"
#include "system/eeiop/cd_file_dat.h"   // CAM_RAIL_ECB
#include "system/eeiop/cddat.h"         // GetFileSize
#include "system/os/eecdvd.h"           // LoadReq
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // CheckPadNowPlaySe

// Callees in unassigned runs (no header yet):
void WorldToScreenPos(float *pos, float *sx, float *sy);   // FF3 0x00180710: through gra3dGetCamera's matrices

#define CAM_PI 3.1415927f               /* rdata 0x36bb94 / 0x36bbc4 / 0x36bbd4 */

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

static void *cam_rail_ecb;              /* sdata gp-0x7978: cam_rail.ecb (CamRailDataLoad) */

// The stance camera (data 0x2f2b90).
static CAM_STANCE cam_stance =
{
    -24.0f, { 0.0f, 0.0f, 0.0f },       // height
    { 0.0f, -15.0f, 0.0f, 1.0f },       // _10
    13.0f, 25.0f,                       // radius, _24
    0.005f, 0.0f,                       // yaw_step, yaw
    -0.42f, 0.01f,                      // pitch, pitch_step
    700000.0f, 7.0f,                    // dist_old, cut_dist
    12, { 0, 0, 0 },                    // eff
    { 0.0f, 0.0f, 0.0f, 1.0f },         // base
};

static sceVu0FVECTOR cam_stance_tgt;    /* bss 0x390fe0: the stance camera's target (CamStanceGetTarget) */

static CAM_RAIL_WRK cam_rail;           /* data 0x2f2bf0 */

// ──────────────────────────────────────────────────────────────────────
// The inline VU0 lengths (see the file comment).

static inline float CamDistXZ(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dz * dz);
}

static inline float CamDist3(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static inline void CamCopyVector(float *dst, const float *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

// ──────────────────────────────────────────────────────────────────────
// The first row of the rail of (area, type) in cam_rail.ecb, nullptr when
// the file has none.
// FF3 0x00137cb8.

static CAM_RAIL_DAT *CamRailFind(s_char area, s_char type)
{
    CAM_RAIL_DAT *row;
    int i, num;

    num = EcbGetNum(cam_rail_ecb);
    for (i = 0; i < num; i++)
    {
        row = (CAM_RAIL_DAT *)EcbGetData(cam_rail_ecb, i);
        if (row->kind == CAM_RAIL_KIND_FIRST && row->area == area && row->type == type)
        {
            return row;
        }
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// IngameInit: cam_rail.ecb into the system heap for the whole in-game.
// FF3 0x00137d50 (was AlbumViewDataLoad, a link-order guess).

void CamRailDataLoad(void)
{
    HEAP_WRK *heap = GetSystemHeapWrkP();

    cam_rail_ecb = heapCtrlMalloc(heap, GetFileSize(CAM_RAIL_ECB), 0, 0, 0);
    LoadReq(CAM_RAIL_ECB, cam_rail_ecb);
}

// IngameEnd.
// FF3 0x00137da0 (was AlbumViewDataFree).

void CamRailDataFree(void)
{
    if (cam_rail_ecb != nullptr)
    {
        heapCtrlFree(GetSystemHeapWrkP(), cam_rail_ecb, 0, 0, 0);
        cam_rail_ecb = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// cur moved by step toward target, stopping on it.
// FF3 0x00137de8.

static float CamStepTo(float target, float cur, float step)
{
    if (cur < target)
    {
        cur += step;
        if (target < cur)
        {
            return target;
        }
    }
    else if (target < cur)
    {
        cur -= step;
        if (cur < target)
        {
            cur = target;
        }
    }
    return cur;
}

// out = (a - b) * t + b, all four lanes.
// FF3 0x00137e38.

static void CamLerpVector(float t, float *out, const float *a, const float *b)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        out[i] = (a[i] - b[i]) * t + b[i];
    }
}

// ──────────────────────────────────────────────────────────────────────
// Segments a0-a1 and b0-b1 in XZ: both parameters in [0, 1] (compared as
// doubles, the ROM's literals) -> 1 and out = a0 + (a1 - a0) t (all four
// lanes); parallel or apart -> 0, out untouched.
// FF3 0x00137e70.
// callcheck: fptodp, dpcmp (the double compares)

int CamSegCrossXZ(float *out, const float *a0, const float *a1, const float *b0, const float *b1)
{
    float d1[4], d2[4], d3[4];
    float det, inv, t, u;
    int i;

    for (i = 0; i < 4; i++)
    {
        d1[i] = a1[i] - a0[i];
        d2[i] = b0[i] - b1[i];
        d3[i] = b0[i] - a0[i];
    }

    det = d1[0] * d2[2] - d2[0] * d1[2];
    if (det == 0.0f)
    {
        return 0;
    }

    inv = 1.0f / det;                                       /* rdata 0x36bb7c */
    t   = d2[2] * inv * d3[0] + -d2[0] * inv * d3[2];
    u   = -d1[2] * inv * d3[0] + d1[0] * inv * d3[2];

    if ((double)t < 0.0 || 1.0 < (double)t)
    {
        return 0;
    }
    if ((double)u < 0.0 || 1.0 < (double)u)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)
    {
        out[i] = d1[i] * t + a0[i];
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The point at turn (whole turns) on the XZ circle r about center; w is
// 1 + center's.
// FF3 0x00138038.

void CamCirclePos(float r, float turn, float *out, float *center)
{
    float a = turn * 6.2831855f;                            /* rdata 0x36bb80 */

    out[1] = 0.0f;
    out[0] = Vu0Cosf(a) * r;
    out[2] = Vu0Sinf(a) * r;
    out[3] = 1.0f;                                          /* rdata 0x36bb84 */
    sceVu0AddVector(out, out, center);
}

// ──────────────────────────────────────────────────────────────────────
// p's height on segment a-b, by the XZ distances: p.y = a.y + (b.y - a.y)
// |p - a| / |b - a|.
// FF3 0x001380d0.

static void CamRailSegHeight(float *p, const float *a, const float *b)
{
    float len = CamDistXZ(b, a);
    float d   = CamDistXZ(p, a);

    p[1] = Ps2Divf((b[1] - a[1]) * d, len) + a[1];     // PC: PS2 division
}

// The XZ line through a and b: out = (A, B, C), A x + B z + C = 0.
// FF3 0x00138148.

static void CamLineXZ(float *out, const float *a, const float *b)
{
    out[0] = b[2] - a[2];
    out[1] = a[0] - b[0];
    out[2] = b[0] * a[2] - a[0] * b[2];
}

// p inside the XZ box of a-b grown by margin (bounds included).
// FF3 0x00138188.

static int CamInSegBoxXZ(float margin, const float *a, const float *b, const float *p)
{
    float mn_x = (a[0] < b[0]) ? a[0] : b[0];
    float mn_z = (a[2] < b[2]) ? a[2] : b[2];
    float mx_x = (a[0] < b[0]) ? b[0] : a[0];
    float mx_z = (a[2] < b[2]) ? b[2] : a[2];

    if (p[0] < mn_x - margin || p[2] < mn_z - margin)
    {
        return 0;
    }
    if (mx_x + margin < p[0] || mx_z + margin < p[2])
    {
        return 0;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Where the XZ circle r about center meets line a-b: 0 (none: a
// discriminant below -100000 -- the ROM's threshold, not 0), 1 (both
// discriminants exactly 0) or 2, the points in out0 / out1 with y 0 (w
// untouched). Solved for x when |A| <= |B|, else for z; the VU0 square
// root takes the magnitude, so a slightly negative discriminant still
// gives points.
// FF3 0x00138200.

static int CamCircleCrossLine(float r, float *out0, float *out1, const float *a, const float *b, const float *center)
{
    float L[3];
    float aa, bb, cc, ab2, ac, bc, dx, dz, s, p0, p1;
    int ret;

    CamLineXZ(L, a, b);
    L[2] = L[1] * center[2] + L[0] * center[0] + L[2];     // the line about center

    aa  = L[0] * L[0];
    bb  = L[1] * L[1];
    cc  = L[2] * L[2];
    ab2 = aa + bb;
    ac  = L[0] * L[2];
    bc  = L[1] * L[2];
    r   = r * r;

    dx = ac * ac - ab2 * (cc - bb * r);
    if (dx < -100000.0f)                                    /* rdata 0x36bb88 */
    {
        return 0;
    }
    dz = bc * bc - ab2 * (cc - aa * r);
    if (dz < -100000.0f)
    {
        return 0;
    }

    ret = 2;
    if (dx == 0.0f && dz == 0.0f)
    {
        ret = 1;
    }

    if (aa <= bb)
    {
        s  = sqrtf(fabsf(dx));
        p0 = (s - ac) / ab2;
        p1 = (-ac - s) / ab2;
        out0[0] = p0;
        out0[2] = -(L[0] * p0 + L[2]) / L[1];
        out1[0] = p1;
        out1[2] = -(L[0] * p1 + L[2]) / L[1];
    }
    else
    {
        s  = sqrtf(fabsf(dz));
        p0 = (s - bc) / ab2;
        p1 = (-bc - s) / ab2;
        out0[2] = p0;
        out0[0] = -(L[1] * p0 + L[2]) / L[0];
        out1[2] = p1;
        out1[0] = -(L[1] * p1 + L[2]) / L[0];
    }

    out0[0] += center[0];
    out0[2] += center[2];
    out1[0] += center[0];
    out1[1]  = 0.0f;
    out1[2] += center[2];
    out0[1]  = 0.0f;
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00138490 (no callers; mkfunc'd 2026-10-16).

void CamStanceSetBase(float *pos)
{
    sceVu0CopyVector(cam_stance.base, pos);
}

// Eight points around center into a local array nobody reads.
// FF3 0x001384a8 (no callers).

[[maybe_unused]] static void CamCirclePoints8(float r, float *center)
{
    sceVu0FVECTOR pts[8];
    int i;

    for (i = 0; i < 8; i++)
    {
        CamCirclePos(r, (float)i * 0.125f, pts[i], center);  /* rdata 0x36bb8c */
    }
}

// ──────────────────────────────────────────────────────────────────────
// The crossings of hit's segment with the four sides of quad (when their
// XZ boxes meet): the one nearest (XZ) to the stance base is kept.
// FF3 0x00138530.

static int CamStanceQuadHit(CAM_STANCE_HIT *hit, float *quad)
{
    sceVu0FVECTOR p;
    float d;
    int i;

    if (HcBaseIsNearSegRect(quad, hit->p0, hit->p1) != 0)
    {
        for (i = 0; i < 4; i++)
        {
            if (CamSegCrossXZ(p, hit->p0, hit->p1, &quad[i * 4], &quad[((i + 1) & 3) * 4]) == 0)
            {
                continue;
            }
            d = CamDistXZ(cam_stance.base, p);
            if (hit->found != 0 && hit->dist < d)
            {
                continue;
            }
            hit->dist = d;
            sceVu0CopyVector(hit->pos, p);
            hit->found = 1;
        }
    }
    return hit->found;
}

// MapLBuffForEachQuad's callback.
// FF3 0x00138648.

static int CamStanceQuadHitCB(float *quad, void *arg)
{
    CamStanceQuadHit((CAM_STANCE_HIT *)arg, quad);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The wall nearest the stance base on from-to: the rectangles of type
// 0x18 of the RegDat slots the player's area covers (their section
// records are looked up and dropped), then both light buffers' quads of
// floor. Found: out = the crossing at the base's height, 1.
// FF3 0x00138670.

static int CamStanceWallHit(float *out, float *from, float *to, short floor)
{
    CAM_STANCE_HIT hit;
    MB_OUT_RECT *rec;
    int *list;
    int i, n, num;

    if (RegDatGetBuffIDG(floor, from) == -1)
    {
        return 0;
    }

    memset(&hit, 0, sizeof(hit));
    sceVu0CopyVector(hit.p0, from);
    sceVu0CopyVector(hit.p1, to);

    list = RegDatGetHitList();
    num  = RegDatGetHitNum();
    for (i = 0; i < num; i++, list++)
    {
        n   = RegDatGetVecNum(*list, 0x18);
        rec = RegDatGetVecPtr(*list, 0x18);
        for (; n > 0; n--)
        {
            RegDatGetStPtr(*list, rec->reg_id);
            CamStanceQuadHit(&hit, &rec->vec[0][0]);
            rec = RegDatNextVec(rec);
        }
    }

    for (i = 0; i < 2; i++)
    {
        MapLBuffForEachQuad(i, floor, CamStanceQuadHitCB, &hit);
    }

    if (hit.found == 0)
    {
        return 0;
    }
    sceVu0CopyVector(out, hit.pos);
    out[1] = cam_stance.base[1];
    return hit.found;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrMode10Start: the eye starts behind the player (yaw 0.75 turns minus
// the facing in turns), the target pitched -0.29, the base at joint 15.
// FF3 0x00138800.

void CamStanceInit(float rot)
{
    float deg = (rot * 180.0f) / CAM_PI;                    /* rdata 0x36bb90 */

    if (rot < 0.0f)
    {
        deg += 360.0f;                                      /* rdata 0x36bb98 */
    }
    cam_stance.pitch = -0.29f;                              /* rdata 0x36bb9c */
    cam_stance.yaw   = 0.75f - deg * 0.0027777776f;         /* rdata 0x36bba4, 0x36bba0 */
    PlyrGetJointPos(15, cam_stance.base, nullptr);
}

// out = center + RotZ(rot[2]) RotY(rot[1]) RotX(rot[0]) (0, 0, 20): a point
// 20 ahead of center (each angle normalised twice, as the ROM does).
// FF3 0x001388a0.

static void CamStanceLookPos(float *out, float *rot, float *center)
{
    sceVu0FMATRIX m;
    sceVu0FVECTOR ahead = { 0.0f, 0.0f, 20.0f, 1.0f };      /* rdata 0x36bba8, 0x36bbac */

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(NormalizeAngle(rot[0])));
    sceVu0RotMatrixY(m, m, NormalizeAngle(NormalizeAngle(rot[1])));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(NormalizeAngle(rot[2])));
    sceVu0TransMatrix(m, m, center);
    sceVu0ApplyMatrix(out, m, ahead);
}

// Up / down tilt the target (pitch in [-0.58, 0]); tgt becomes the point
// 20 ahead of cam toward tgt's direction at that pitch.
// FF3 0x001389c0.

static void CamStancePitchCtrl(float *tgt, float *cam)
{
    sceVu0FVECTOR rot;

    // PC: GetTrgtRot(.., 2) writes only rot[1]; the ROM leaves rot[2] (the
    // roll fed to RotZ) as stack garbage.
    rot[2] = 0.0f;
    GetTrgtRot(cam, tgt, rot, 2);

    if (CheckPadNowPlaySe(0x1000, -1, 0) != 0)              // up
    {
        cam_stance.pitch += cam_stance.pitch_step;
    }
    if (CheckPadNowPlaySe(0x4000, -1, 0) != 0)              // down
    {
        cam_stance.pitch -= cam_stance.pitch_step;
    }
    if (0.0f < cam_stance.pitch)
    {
        cam_stance.pitch = 0.0f;
    }
    if (cam_stance.pitch < -0.58f)                          /* rdata 0x36bbb0 */
    {
        cam_stance.pitch = -0.58f;
    }

    rot[0] = cam_stance.pitch;
    CamStanceLookPos(tgt, rot, cam);
}

// ──────────────────────────────────────────────────────────────────────
// CameraMain while mode 10 starts: left / right turn the eye around the
// base (joint 15, tracked while mode10_time runs) on a circle of radius 13,
// a wall on the way pulls it in; the eye sits 24 above the player's feet.
// A jump in its distance from the base cross-fades. The target is the
// base pitched by CamStancePitchCtrl. The left stick moves the d-pad
// meanwhile (stick_dpad).
// FF3 0x00138ab0.

void CamStanceMain(void)
{
    PLYR_WRK *plyr;
    sceVu0FVECTOR base, ring, cam, tgt;
    float d, diff;
    short floor;

    pad_wrk[0].stick_dpad = 1;

    plyr = ingame_wrk.plyr_wrk;
    if (0.0f < plyr->mode10_time)
    {
        PlyrGetJointPos(15, cam_stance.base, nullptr);
    }
    sceVu0CopyVector(base, cam_stance.base);
    floor = ingame_wrk.plyr_wrk->floor;

    if (CheckPadNowPlaySe(0x2000, -1, 0) != 0)              // right
    {
        cam_stance.yaw += cam_stance.yaw_step;
    }
    if (CheckPadNowPlaySe(0x8000, -1, 0) != 0)              // left
    {
        cam_stance.yaw -= cam_stance.yaw_step;
    }
    while (1.0f < cam_stance.yaw)                           /* rdata 0x36bbb4 */
    {
        cam_stance.yaw -= 1.0f;
    }
    while (cam_stance.yaw < 0.0f)
    {
        cam_stance.yaw += 1.0f;
    }

    CamCirclePos(cam_stance.radius, cam_stance.yaw, ring, base);
    sceVu0CopyVector(cam, ring);
    CamStanceWallHit(cam, base, ring, floor);
    cam[1] = cam_stance.height + ingame_wrk.plyr_wrk->pos[1];

    d    = CamDistXZ(cam_stance.base, cam);
    diff = cam_stance.dist_old - d;
    if (diff < 0.0f)
    {
        diff = -diff;
    }
    if (cam_stance.cut_dist <= diff)
    {
        ApproachCameraCrossFadeSW(1);
    }
    SetEffects(6, 1, cam_stance.eff);
    cam_stance.dist_old = d;

    sceVu0CopyVector(tgt, cam_stance.base);
    CamStancePitchCtrl(tgt, cam);
    sceVu0CopyVector(cam_stance_tgt, tgt);

    gra3dcamSetPosition(cam);
    gra3dcamSetTarget(tgt, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(0.8f);                                   /* rdata 0x36bbb8 (0x3f4ccccc) */
    gra3dApplyCamera(nullptr, 1, 1);
}

// FF3 0x00138d18.

float *CamStanceGetTarget(void)
{
    return cam_stance_tgt;
}

// ──────────────────────────────────────────────────────────────────────
// The slot of a two-entry candidate list to fill: the first free one,
// else the farther.
// FF3 0x00138d28.

static CAM_RAIL_CAND *CamRailCandSlot(CAM_RAIL_CAND *list)
{
    CAM_RAIL_CAND *far_one = nullptr;
    int i;

    for (i = 0; i < 2; i++, list++)
    {
        if (list->seg < 0)
        {
            return list;
        }
        if (far_one == nullptr || far_one->dist < list->dist)
        {
            far_one = list;
        }
    }
    return far_one;
}

// pt (on segment seg, whose points are seg_pts[0..1]) into list when it is
// nearer to pos than the slot it would take; its height from the segment.
// FF3 0x00138d78.

static int CamRailCandAdd(float *pos, int seg, float *pt, CAM_RAIL_CAND *list, sceVu0FVECTOR *seg_pts)
{
    CAM_RAIL_CAND *slot;
    float d;

    d    = CamDist3(pos, pt);
    slot = CamRailCandSlot(list);
    if (slot->seg < 0 || d < slot->dist)
    {
        // (The ROM also stores two uninitialised stack words in _08.)
        slot->seg  = seg;
        slot->dist = d;
        CamCopyVector(slot->pos, pt);
        CamRailSegHeight(slot->pos, seg_pts[0], seg_pts[1]);
        slot->pos[3] = 1.0f;                                /* rdata 0x36bbbc */
    }
    return 0;
}

// The points of the n segments of pts at distance r (XZ circle) from the
// player, inside their segment's box: those in front of the player (a
// half-plane, OutSightChk) to lists[0], those behind to lists[1].
// FF3 0x00138e40.

static int CamRailCandSearch(float r, CAM_RAIL_CAND (*lists)[2], sceVu0FVECTOR *pts, int n)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR res[2] = {};
    int i, j, cnt, behind;

    for (i = 0; i < 2; i++)
    {
        lists[i][1].seg = -1;
        lists[i][0].seg = -1;
    }

    for (i = 0; i < n; i++, pts++)
    {
        cnt = CamCircleCrossLine(r, res[0], res[1], pts[0], pts[1], plyr->pos);
        for (j = 0; j < cnt; j++)
        {
            if (CamInSegBoxXZ(1.0f, pts[0], pts[1], res[j]) == 0)  /* rdata 0x36bbc0 */
            {
                continue;
            }
            behind = OutSightChk(res[j], plyr->pos, plyr->rot[1], CAM_PI, 0.0f);
            CamRailCandAdd(plyr->pos, i, res[j], lists[behind != 0], pts);
        }
    }
    return 0;
}

// The nearest (3D) of n points to pos, -1 for none.
// FF3 0x00138fb0.

static int CamRailNearPoint(float *pos, sceVu0FVECTOR *pts, int n)
{
    float min = 2000000.0f;                                 /* rdata 0x36bbc8 */
    float d;
    int best = -1;
    int i;

    for (i = 0; i < n; i++, pts++)
    {
        d = CamDist3(pos, *pts);
        if (d < min || best == -1)
        {
            min  = d;
            best = i;
        }
    }
    return best;
}

// ──────────────────────────────────────────────────────────────────────
// Following: of the rail points at the nearest row's radius plus the
// added distance from the player (inside their segment's box grown by
// 0.5), the one nearest (3D) to the camera, into best. Returns the
// nearest row, -1 when no point qualified.
// FF3 0x00139030.
// callcheck: fptodp, dpcmp (the double compares)

static int CamRailFollowSearch(CAM_RAIL_CAND *best, float *cam)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    CAM_RAIL_CAND c = {};
    sceVu0FVECTOR res[2] = {};
    int near_no, found, i, j, cnt;

    found   = 0;
    near_no = CamRailNearPoint(plyr->pos, cam_rail.pt, cam_rail.pt_num - 1);

    // (near_no is -1 only without segments, so the loop never reads row -1.)
    for (i = 0; i < cam_rail.pt_num - 1; i++)
    {
        cnt = CamCircleCrossLine(cam_rail.rec[near_no].radius + cam_rail.dist, res[0], res[1],
                                 cam_rail.pt[i], cam_rail.pt[i + 1], plyr->pos);
        for (j = 0; j < cnt; j++)
        {
            if (CamInSegBoxXZ(0.5f, cam_rail.pt[i], cam_rail.pt[i + 1], res[j]) == 0)  /* rdata 0x36bbd0 */
            {
                continue;
            }
            c.seg = i;
            CamCopyVector(c.pos, res[j]);
            CamRailSegHeight(c.pos, cam_rail.pt[i], cam_rail.pt[i + 1]);
            c.pos[3] = 1.0f;                                /* rdata 0x36bbcc */
            c.dist   = CamDist3(c.pos, cam);
            // Compared as doubles (fabs), as the ROM does.
            if (found != 0 && fabs((double)c.dist) >= fabs((double)best->dist))
            {
                continue;
            }
            found = 1;
            *best = c;
        }
    }

    return (found != 0) ? near_no : -1;
}

// ──────────────────────────────────────────────────────────────────────
// The added distance: behind the player (states 0 / 1) the row's
// dist_back; once the camera has stayed in front of the player while they
// walked 10 (state 1 -> 2) dist_front, until it has stayed behind for 10
// (state 3 -> 0).
// FF3 0x001392a8.

static void CamRailDistCtrl(float *cam, CAM_RAIL_DAT *row)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int behind;

    behind = OutSightChk(cam, plyr->pos, plyr->rot[1], CAM_PI, 0.0f);

    if (cam_rail.state == 1)
    {
        if (behind == 0)
        {
            if (10.0f <= CamDist3(plyr->pos, cam_rail.latch))   /* rdata 0x36bbd8 */
            {
                cam_rail.state = 2;
            }
        }
        else
        {
            cam_rail.state = 0;
        }
    }
    else if (cam_rail.state < 2)
    {
        if (cam_rail.state == 0)
        {
            if (behind == 0)
            {
                sceVu0CopyVector(cam_rail.latch, plyr->pos);
                cam_rail.state = 1;
            }
            cam_rail.dist_tgt = row->dist_back;
        }
    }
    else if (cam_rail.state == 2)
    {
        if (behind != 0)
        {
            sceVu0CopyVector(cam_rail.latch, plyr->pos);
            cam_rail.state = 3;
        }
        cam_rail.dist_tgt = row->dist_front;
    }
    else if (cam_rail.state == 3)
    {
        if (behind == 0)
        {
            cam_rail.state = 2;
        }
        else if (10.0f <= CamDist3(plyr->pos, cam_rail.latch))
        {
            cam_rail.state = 0;
        }
    }
}

// The nearest row to the player and the candidates at its radius.
// FF3 0x00139460.

static int CamRailStartSearch(CAM_RAIL_CAND (*lists)[2])
{
    int near_no;

    near_no = CamRailNearPoint(ingame_wrk.plyr_wrk->pos, cam_rail.pt, cam_rail.pt_num - 1);
    // PC: with fewer than two points near_no is -1 and the ROM reads the
    // radius of the row before the rail (unused: there are no segments).
    CamRailCandSearch((near_no < 0) ? 0.0f : cam_rail.rec[near_no].radius, lists, cam_rail.pt, cam_rail.pt_num - 1);
    return near_no;
}

// The nearest candidate behind the player, else in front, else nullptr.
// FF3 0x001394f8.

static CAM_RAIL_CAND *CamRailCandPick(CAM_RAIL_CAND (*lists)[2])
{
    CAM_RAIL_CAND *best;
    int k = 1;
    int i;

    for (;;)
    {
        best = nullptr;
        for (i = 0; i < 2; i++)
        {
            if (lists[k][i].seg < 0)
            {
                break;
            }
            if (best == nullptr || lists[k][i].dist < best->dist)
            {
                best = &lists[k][i];
            }
        }
        if (best != nullptr || k != 1)
        {
            return best;
        }
        k = 0;
    }
}

// Places the camera on a candidate and takes its row's fov: the nearest
// row, -1 without a candidate, -2 without a row.
// FF3 0x00139560.

static int CamRailStartPos(void)
{
    CAM_RAIL_CAND lists[2][2];
    CAM_RAIL_CAND *c;
    int near_no;

    near_no = CamRailStartSearch(lists);
    if (near_no < 0)
    {
        return -2;
    }
    c = CamRailCandPick(lists);
    if (c == nullptr)
    {
        return -1;
    }
    sceVu0CopyVector(cam_rail.cam, c->pos);
    cam_rail.fov = cam_rail.rec[c->seg].fov;
    return near_no;
}

// ──────────────────────────────────────────────────────────────────────
// Moves the camera to the point CamRailFollowSearch finds; a jump of 50 or
// more snaps the direction next frame. On a segment with the lock byte the
// camera parks on a row point instead (the next row's when the segment is
// the first) until CamRailLockKeep lets it go. Returns the nearest row,
// -1 for none.
// FF3 0x001395e8.

static int CamRailFollow(void)
{
    CAM_RAIL_CAND best;
    CAM_RAIL_DAT *row, *park;
    int near_no;

    near_no = CamRailFollowSearch(&best, cam_rail.cam);
    if (near_no < 0)
    {
        return -1;
    }

    row = &cam_rail.rec[best.seg];
    if (row->lock == 0)
    {
        if (50.0f <= CamDist3(best.pos, cam_rail.cam))      /* rdata 0x36bbdc */
        {
            cam_rail.cnt = -1;
        }
        sceVu0CopyVector(cam_rail.cam, best.pos);
    }
    else
    {
        park = &cam_rail.rec[best.seg + (row->kind == CAM_RAIL_KIND_FIRST)];
        CamCopyVector(cam_rail.cam, park->pos);
        cam_rail.lock_row  = near_no;
        cam_rail.lock_dist = row->radius;
        cam_rail.lock      = 1;
    }
    return near_no;
}

// The lock ends once the player is farther (XZ) from the camera than the
// locked radius plus the added distance. Returns the locked row.
// FF3 0x00139700.

static int CamRailLockKeep(void)
{
    if (cam_rail.lock_dist + cam_rail.dist < CamDistXZ(ingame_wrk.plyr_wrk->pos, cam_rail.cam))
    {
        cam_rail.lock = 0;
    }
    return cam_rail.lock_row;
}

// ──────────────────────────────────────────────────────────────────────
// The target: the look point is the player's pos plus the row's offset.
// After a snap (cnt < 0) the camera aims straight at it. Otherwise, while
// the look point is off the screen centre by more than the screen radius
// (less 10 after the first frame), the direction is turned from the old
// one toward the new by bisection -- each try applies the camera to
// measure the point's screen offset -- and the last try is kept. The
// screen radius jumps up to the row's at once and comes down 1 a frame.
// FF3 0x00139760.

static int CamRailFrame(float fov, CAM_RAIL_DAT *row, float *tgt, float *cam)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    GRA3DCAMERA *camera;
    sceVu0FVECTOR look, dir, tmp, scr;
    float sx, sy, off, margin, step, t, t1, len;
    int i;

    look[0] = plyr->pos[0] + row->tgt_ofs[0];
    cam_rail.scr_r_tgt = row->scr_r;
    look[1] = plyr->pos[1] + row->tgt_ofs[1];
    look[2] = plyr->pos[2] + row->tgt_ofs[2];
    look[3] = 1.0f;                                         /* rdata 0x36bbe0 */

    if (cam_rail.scr_r < cam_rail.scr_r_tgt)
    {
        cam_rail.scr_r = cam_rail.scr_r_tgt;
    }
    else
    {
        cam_rail.scr_r = CamStepTo(cam_rail.scr_r_tgt, cam_rail.scr_r, 1.0f);
    }

    // PC: WorldToScreenPos is still a stub and leaves sx / sy unset; the
    // screen centre makes the look point "on centre".
    sx = 320.0f;
    sy = 224.0f;
    WorldToScreenPos(look, &sx, &sy);
    off = GetLength2D(320.0f - sx, 224.0f - sy);            /* rdata 0x36bbe4, 0x36bbe8 */

    if (cam_rail.cnt < 0)
    {
        for (i = 0; i < 4; i++)
        {
            cam_rail.dir[i] = look[i] - cam[i];
        }
        len = sqrtf(cam_rail.dir[0] * cam_rail.dir[0] + cam_rail.dir[1] * cam_rail.dir[1]
                    + cam_rail.dir[2] * cam_rail.dir[2]);
        cam_rail.dir[0] /= len;
        cam_rail.dir[1] /= len;
        cam_rail.dir[2] /= len;
        sceVu0AddVector(tgt, cam_rail.dir, cam);
        cam_rail.cnt = 0;
        return 0;
    }

    margin = (0 < cam_rail.cnt) ? 10.0f : 0.0f;             /* rdata 0x36bbec */
    if (cam_rail.scr_r - margin < off)
    {
        cam_rail.cnt++;

        for (i = 0; i < 4; i++)
        {
            dir[i] = look[i] - cam[i];
        }
        len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;

        camera = gra3dGetCamera();
        step   = 0.5f;                                      /* rdata 0x36bbf0 */
        t      = 0.0f;
        // (0.5 > 0.001: the ROM's entry test always passes.)
        while (0.001f < step)                               /* rdata 0x36bbf4 */
        {
            t1 = t + step;
            CamLerpVector(t1, tmp, dir, cam_rail.dir);
            sceVu0AddVector(tgt, tmp, cam);
            gra3dcamSetPosition(cam);
            gra3dcamSetTarget(tgt, 1);
            gra3dcamSetFov(fov);
            gra3dApplyCamera(nullptr, 1, 1);

            sceVu0ApplyMatrix(scr, camera->matWorldScreen, look);
            sceVu0ScaleVector(scr, scr, 1.0f / scr[3]);
            // GS coordinates: 2048 is the screen centre.
            off = GetLength2D(320.0f - ((scr[0] - 2048.0f) + 320.0f),   /* rdata 0x36bbf8 */
                              224.0f - ((scr[1] - 2048.0f) + 224.0f));
            if (off <= cam_rail.scr_r)
            {
                step *= 0.5f;
            }
            else
            {
                t = t1;
            }
        }
        sceVu0CopyVector(cam_rail.dir, tmp);
    }
    else
    {
        cam_rail.cnt = 0;
    }

    sceVu0AddVector(tgt, cam_rail.dir, cam);
    return 0;
}

// The fov eased toward the row's by fov_step (never set: it holds).
// FF3 0x00139b20.

static float CamRailFovStep(CAM_RAIL_DAT *row)
{
    cam_rail.fov = CamStepTo(row->fov, cam_rail.fov, cam_rail.fov_step);
    return cam_rail.fov;
}

// ──────────────────────────────────────────────────────────────────────
// IngameInit, CamRailStart.
// FF3 0x00139b58 (was AlbumViewWrkClear).

void CamRailWrkClear(void)
{
    memset(&cam_rail, 0, sizeof(cam_rail));
}

// FF3 0x00139b80.

void CamRailOff(void)
{
    cam_rail.rec = nullptr;
}

// FF3 0x00139b90.

int CamRailGetCutFlg(void)
{
    return cam_rail.cut;
}

// ──────────────────────────────────────────────────────────────────────
// A new rail (area, type): the work cleared, its points copied (the LAST
// row ends the run and is not copied), the camera placed on it (without a
// place the fov falls back to 0.89), the added distance 4, the screen
// radius 180, the direction to snap.
// FF3 0x00139ba0.

void CamRailStart(int area, int type)
{
    CAM_RAIL_DAT *row;

    row = CamRailFind((s_char)area, (s_char)type);
    if (cam_rail.rec == row)
    {
        return;
    }

    CamRailWrkClear();
    // PC: the ROM goes on with a null row (reading EE address 8 on);
    // leave the rail off instead.
    if (row == nullptr)
    {
        return;
    }
    cam_rail.rec = row;
    cam_rail.fov = row->fov;
    if (row->kind != CAM_RAIL_KIND_LAST)
    {
        do
        {
            cam_rail.pt[cam_rail.pt_num][0] = row->pos[0];
            cam_rail.pt[cam_rail.pt_num][1] = row->pos[1];
            cam_rail.pt[cam_rail.pt_num][2] = row->pos[2];
            cam_rail.pt[cam_rail.pt_num][3] = 1.0f;          /* rdata 0x36bbfc */
            cam_rail.pt_num++;
            row++;
        } while (row->kind != CAM_RAIL_KIND_LAST);
    }

    if (CamRailStartPos() < 0)
    {
        cam_rail.fov = 0.89f;                               /* rdata 0x36bc00 */
    }
    cam_rail.lock      = 0;
    cam_rail.dist_tgt  = 0.0f;
    cam_rail.state     = 0;
    cam_rail.dist      = 4.0f;                              /* rdata 0x36bc04 */
    cam_rail._334      = 4.0f;
    cam_rail.cut       = 0;
    cam_rail.scr_r     = 180.0f;                            /* rdata 0x36bc08 */
    cam_rail.cnt       = -1;
    cam_rail.scr_r_tgt = 180.0f;
    cam_rail._330      = 18.0f;                             /* rdata 0x36bc0c */
}

// ──────────────────────────────────────────────────────────────────────
// The rail camera's frame: follow the rail (or keep the lock), the added
// distance eased 0.3 toward its target, the fov, the target, then the
// gra3d camera.
// FF3 0x00139cd8.

void CamRailMain(void)
{
    CAM_RAIL_DAT *row;
    sceVu0FVECTOR tgt;
    float fov;
    int near_no;

    cam_rail.cut = 0;
    if (cam_rail.lock == 0)
    {
        near_no = CamRailFollow();
        if (near_no < 0)
        {
            return;
        }
    }
    else
    {
        near_no = CamRailLockKeep();
    }

    if (cam_rail.cnt < 0)
    {
        cam_rail.cut = 1;
    }

    row = cam_rail.rec + near_no;
    if (cam_rail.lock == 0)
    {
        CamRailDistCtrl(cam_rail.cam, row);
    }
    cam_rail.dist = CamStepTo(cam_rail.dist_tgt, cam_rail.dist, 0.3f);  /* rdata 0x36bc10 */
    fov = CamRailFovStep(row);
    CamRailFrame(fov, row, tgt, cam_rail.cam);

    gra3dcamSetPosition(cam_rail.cam);
    gra3dcamSetTarget(tgt, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(fov);
    gra3dApplyCamera(nullptr, 1, 1);
}
