/* ==========================================================================
 *  graphics/effect/effect_sub.c
 *
 *  The effect helpers every effect file leans on (FF2 effect_sub.c, plus
 *  FF2 effect_spr.c's two screen sprites, which FF3 folded in):
 *
 *    * Geometry   Get2PLength (XZ in FF3), the 3D distance pair, the
 *                 world -> screen projection, Vector2Rot / Get2PosRot(2),
 *                 GetTrgtRotType2 and its negated twin, GetCamFaceRot.
 *    * Sprites    SetEffSQITex / SetEffSQTex (screen quads of the effect
 *                 page), Set3DPosTexure / 2 and their shared body
 *                 Set3DPosTexureSub (out of line at 0x2b8220 in FF3; FF2
 *                 inlined it into both wrappers).
 *    * Frames     EffSetDrawFrame / EffResetDrawFrame / EffDrawFrameFillRect
 *                 point the GS at a work frame and back (room glow, overlap),
 *                 EffDispSprD2Direct draws a DISP_SPRT2 as 64-pixel column
 *                 strips, EffImageHalf32 halves a captured picture on the EE.
 *
 *    * Light      the player's light cone test, and a spotlight falloff
 *                 (cone set-up + rate at a point) nothing in the ELF calls.
 *    * Debug      3D lines (depth-tested or not), a wireframe box, an arrow,
 *                 an XZ circle: also uncalled in the retail ELF; their host
 *                 path is ReiPan_RendererDrawClipLine.
 *    * Probes     CheckPointDepth (the ghosts' "is it hidden" batch) and a
 *                 single-point twin, both reading the GS Z buffer.
 *    * Matrices   camera-facing / camera-heading sprites, EffMatTexQuad,
 *                 EffSpriteSub (0x2b8a38), the GS register save / restore.
 *
 *  PC: nothing walks the DIRECT chain, so every draw here also goes to the
 *  renderer: the 2D quads through g2d_draw.c's host sprite, the 3D sprites
 *  through ReiPan_Graph3dDrawEffectQuad. While a work frame is the GS target
 *  the host draws nothing (it has no such target), and a TEX0 naming a work
 *  frame is skipped (only GS-side passes ever wrote it).
 *
 *  FF3 0x1806a8..0x183d58, 0x2b8220..0x2b8ce0.
 * ======================================================================== */

#include "common/reipan_stub.h"
#include "graphics/effect/effect_sub.h"

#include <math.h>                       // sqrtf (the ROM: vsqrt), tanf
#include <string.h>                     // memcpy
#include <eekernel.h>                   // FlushCache
#include <libdma.h>                     // sceDmaGetChan
#include <libgraph.h>                   // SCE_GS_* / SCE_GIF_*, sceGs*StoreImage, sceGsSyncPath

#include "common/vu0math.h"             // g3dAtan2f, Vu0Sinf, Vu0Cosf
#include "graphics/effect/effect.h"     // eff_wrk
#include "graphics/graph2d/g2d_draw.h"  // DISP_SPRT2, SPRT_DAT, effdat, Start / EndDmaDirectTrans, ReiPan_G2dHostSprite
#include "graphics/graph3d/gra3d.h"     // gra3dGetCamera, gra3dcamGetPosition
#include "graphics/graph3d/g3dGeom.h"   // g3dCalcAngle
#include "graphics/graph3d/gra3dSGDData.h"  // sgdGetBoundingBox
#include "graphics/graph3d/sgd_types.h" // SGDFILEHEADER, SGDMATERIAL
#include "graphics/model_object.h"      // ModelObject::GetSgd
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/plyr/player.h"         // PLYR_WRK
#include "graphics/gs_reg.h"            // GS_REG_SET, SetGsRegsCached, GetGsRegCached
#include "system/os/system.h"           // GetSysWrk
#include "reipan/reipan_debug.h"        // PC: ReiPan_DebugLightConeEnabled
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "reipan/rendering/reipan_renderer.h"   // PC: the 2D quad and clip-space line bridges
#include "reipan/rendering/reipan_graph3d.h"    // PC: ReiPan_Graph3dDrawEffectQuad

// rdata 0x36f240.. / 0x36f2c0..: FF3 wraps with the exact float pi (FF2 3.1415925).
#define EFF_PI      3.1415927f
#define EFF_PI2     6.2831855f

// The GS window origin and the screen centre (XYOFFSET 1728,1824).
#define EFF_GS_ORG  2048.0f
#define EFF_CX      320.0f
#define EFF_CY      224.0f

// VFTOI4: float to 12.4 fixed point, truncated.
static inline u_int EffFtoi4(float f) { return (u_int)(int)(f * 16.0f); }

static inline float EffWrapPi(float a)
{
    if (EFF_PI < a)
        a -= EFF_PI2;
    else if (a < -EFF_PI)
        a += EFF_PI2;
    return a;
}

// ──────────────────────────────────────────────────────────────────────
// PC: work-frame bookkeeping
// ──────────────────────────────────────────────────────────────────────

// A page counts as a work frame only while the pass that writes it keeps
// running: a mark lapses EFF_HOST_WORK_KEEP renderer frames after its last
// EffSetDrawFrame. The room glow reads its other buffer, the one it wrote the
// frame before, hence 2. The pages are shared with real pictures -- 0x2bc0
// is room_glow_buf[0], the enemy aura's frame and movie.c's movie page -- so
// a sticky mark dropped the opening movie's every frame after the room glow
// had run in rs101 (hazards.md "A work-frame mark that never lapses").

#define EFF_HOST_WORK_NUM   8
#define EFF_HOST_WORK_KEEP  2

static int   eff_host_redirect;         // EffSetDrawFrame .. EffResetDrawFrame
static u_int eff_host_work_fbp[EFF_HOST_WORK_NUM];  // the work frames seen (TBP units)
static unsigned long long eff_host_work_frame[EFF_HOST_WORK_NUM];  // renderer frame of the last write
static int   eff_host_work_num;

// The work frames the host does have (ReiPan_EffHostWorkBegin): a renderer
// work frame takes the draws between Begin and End, and a texture naming one
// is served from it -- EffHostTexIsWorkFrame answers 0 for it -- for as long
// as the dead-frame marks above last.
static int   eff_host_work;             // a host work frame is open
static u_int eff_host_backed_fbp[EFF_HOST_WORK_NUM];
static unsigned long long eff_host_backed_frame[EFF_HOST_WORK_NUM];
static int   eff_host_backed_num;

// A mute (ReiPan_EffHostMuteBegin .. End): every host 2D draw dropped and no
// work frame marked. For a GS chain the host replaces wholesale -- the room
// glow -- whose work frames would otherwise be marked dead every frame and
// hide the copies other effects keep at the same addresses (0x2bc0).
static int   eff_host_mute;

// One stamp per frame address in a fixed list, the oldest recycled.
static void EffHostStampFrame(u_int *fbps, unsigned long long *frames, int *num, int fbp)
{
    unsigned long long now = ReiPan_RendererGetFrameIndex();
    int i, oldest = 0;

    for (i = 0; i < *num; i++)
    {
        if (fbps[i] == (u_int)fbp)
        {
            frames[i] = now;
            return;
        }
        if (frames[i] < frames[oldest])
            oldest = i;
    }
    if (*num < EFF_HOST_WORK_NUM)
        i = (*num)++;
    else
        i = oldest;
    fbps[i] = (u_int)fbp;
    frames[i] = now;
}

static void EffHostNoteWorkFrame(int fbp)
{
    unsigned long long now = ReiPan_RendererGetFrameIndex();
    int i, oldest = 0;

    eff_host_redirect = 1;
    if (eff_host_mute != 0)
        return;
    for (i = 0; i < eff_host_work_num; i++)
    {
        if (eff_host_work_fbp[i] == (u_int)fbp)
        {
            eff_host_work_frame[i] = now;
            return;
        }
        if (eff_host_work_frame[i] < eff_host_work_frame[oldest])
            oldest = i;
    }
    if (eff_host_work_num < EFF_HOST_WORK_NUM)
        i = eff_host_work_num++;
    else
        i = oldest;
    eff_host_work_fbp[i] = (u_int)fbp;
    eff_host_work_frame[i] = now;
}

void ReiPan_EffHostRedirectBegin(int fbp)
{
    EffHostNoteWorkFrame(fbp);
}

void ReiPan_EffHostRedirectEnd(void)
{
    eff_host_redirect = 0;
}

// Open a host work frame for GS block fbp (reipan_renderer.h,
// ReiPan_RendererBeginWorkFrame): the draws up to ReiPan_EffHostWorkEnd go
// into it -- the 2D bridges are not muted then, whatever FRAME_1 says -- and a
// texture naming fbp is served from it. 0 when the renderer cannot open one
// this frame; the caller keeps its old, work-frame-less path.
int ReiPan_EffHostWorkBegin(int fbp, int logical_w, int logical_h, int image_space)
{
    if (eff_host_work != 0 ||
        ReiPan_RendererBeginWorkFrame((u_int)fbp, logical_w, logical_h, image_space) == 0)
        return 0;
    eff_host_work = 1;
    EffHostStampFrame(eff_host_backed_fbp, eff_host_backed_frame, &eff_host_backed_num, fbp);
    return 1;
}

void ReiPan_EffHostWorkEnd(void)
{
    if (eff_host_work == 0)
        return;
    ReiPan_RendererEndWorkFrame();
    eff_host_work = 0;
}

void ReiPan_EffHostMuteBegin(void)
{
    eff_host_mute++;
}

void ReiPan_EffHostMuteEnd(void)
{
    if (eff_host_mute > 0)
        eff_host_mute--;
}

int EffHostDrawFrameRedirected(void)
{
    return eff_host_mute != 0 || (eff_host_redirect != 0 && eff_host_work == 0);
}

// An indexed TEX0 is never a work frame: a GS-side pass renders colour
// formats only, so a PSMT8 / PSMT4 picture at a work page's address is one
// the EE uploaded there (the screen saver's sheet and the film noise sheet
// at 0x2bc0, effect_scrsaver.c, while the room glow writes 0x2bc0 every
// frame).
int EffHostTexIsWorkFrame(u_long tex0)
{
    u_int tbp = (u_int)(tex0 & 0x3fff);
    u_int psm = (u_int)((tex0 >> 20) & 0x3f);
    unsigned long long now = ReiPan_RendererGetFrameIndex();
    int   i;

    if (psm == SCE_GS_PSMT8 || psm == SCE_GS_PSMT4 || psm == SCE_GS_PSMT8H
        || psm == SCE_GS_PSMT4HL || psm == SCE_GS_PSMT4HH)
        return 0;
    // A frame the host drew into is a real picture (a renderer work frame).
    for (i = 0; i < eff_host_backed_num; i++)
    {
        if (eff_host_backed_fbp[i] == tbp && now - eff_host_backed_frame[i] <= EFF_HOST_WORK_KEEP)
            return 0;
    }
    for (i = 0; i < eff_host_work_num; i++)
    {
        if (eff_host_work_fbp[i] == tbp && now - eff_host_work_frame[i] <= EFF_HOST_WORK_KEEP)
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Distances and directions
// ──────────────────────────────────────────────────────────────────────

// vsub.xyz, vmul.xyz, vaddz.x, vsqrt: x and z only.
// FF3 0x001806a8; FF2 Get2PLength (x, y and z there).
float Get2PLength(const float *v1, const float *v2)
{
    float dx = v1[0] - v2[0];
    float dz = v1[2] - v2[2];

    return sqrtf(dx * dx + dz * dz);
}

// vadday.x + vmaddz.x with vf1 = (1, 1, 1, -1): x + y + z.
// FF3 0x001806d8.
float Get2PDist3D(const float *v1, const float *v2)
{
    float dx = v1[0] - v2[0];
    float dy = v1[1] - v2[1];
    float dz = v1[2] - v2[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// matWorldScreen x (pos, 1) (FF3 builds a translation matrix and multiplies
// it in, the same thing), divided by w; GS window -> screen pixels.
// FF3 0x00180710.
void WorldToScreenPos(float *pos, float *sx, float *sy)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0FVECTOR v, out;
    float         q;
    int           i;

    v[0] = pos[0];
    v[1] = pos[1];
    v[2] = pos[2];
    v[3] = 1.0f;
    sceVu0ApplyMatrix(out, cam->matWorldScreen, v);
    q = 1.0f / out[3];
    for (i = 0; i < 4; i++)
        out[i] *= q;
    *sx = (out[0] - EFF_GS_ORG) + EFF_CX;
    *sy = (out[1] - EFF_GS_ORG) + EFF_CY;
}

// y = atan2(dx, dz); x = atan2(|xz|, dy) + pi/2; both wrapped. A zero vector
// gives 0, 0. FF3 0x001808d0; FF2 Vector2Rot (no zero test there).
void Vector2Rot(const float *dir, float *x, float *y)
{
    float len;

    if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f)
    {
        *y = 0.0f;
        *x = 0.0f;
        return;
    }

    len = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
    *y = g3dAtan2f(dir[0], dir[2]);
    *x = g3dAtan2f(len, dir[1]) + 1.5707964f;          // rdata 0x36f240
    *x = EffWrapPi(*x);
    *y = EffWrapPi(*y);
}

// FF3 0x00180a40; FF2 Get2PosRot.
void Get2PosRot(const float *v1, const float *v2, float *x, float *y)
{
    sceVu0FVECTOR dir;

    sceVu0SubVector(dir, (float *)v2, (float *)v1);
    Vector2Rot(dir, x, y);
}

// FF3 0x00180a78; FF2 Get2PosRot2.
void Get2PosRot2(const float *v1, const float *v2, float *x, float *z)
{
    float dy = v2[1] - v1[1];

    *z = g3dAtan2f(v2[0] - v1[0], dy);
    *x = g3dAtan2f(v2[2] - v1[2], dy);
    *x = EffWrapPi(*x);
    *z = EffWrapPi(*z);
}

// FF2 GetTrgtRotType2 with the pitch negated and both angles wrapped.
// FF3 0x00180ba8.
void GetTrgtRotType2Neg(float *p0, float *p1, float *rot, int id)
{
    sceVu0FVECTOR dist;

    sceVu0SubVector(rot, rot, rot);
    sceVu0SubVector(dist, p1, p0);
    if (id & 1)
    {
        dist[3] = Get2PLength(p0, p1);
        rot[0]  = EffWrapPi(g3dAtan2f(-dist[1], dist[3]));
    }
    if (id & 2)
        rot[1] = EffWrapPi(g3dAtan2f(dist[0], dist[2]));
}

// FF3 0x00180ce8; FF2 GetTrgtRotType2 (FF3 adds the equal-points case,
// which zeroes only the asked angles and leaves rot otherwise alone).
void GetTrgtRotType2(float *p0, float *p1, float *rot, int id)
{
    sceVu0FVECTOR dist;

    if (p0[0] == p1[0] && p0[1] == p1[1] && p0[2] == p1[2])
    {
        if (id & 1)
            rot[0] = 0.0f;
        if (id & 2)
            rot[1] = 0.0f;
        return;
    }

    sceVu0SubVector(rot, rot, rot);
    sceVu0SubVector(dist, p1, p0);
    if (id & 1)
    {
        dist[3] = Get2PLength(p0, p1);
        rot[0]  = g3dAtan2f(dist[1], dist[3]);
    }
    if (id & 2)
        rot[1] = g3dAtan2f(dist[0], dist[2]);
}

// FF3 0x00180dd0: Get2PDist3D's body again.
float Get2PDist3D2(const float *v1, const float *v2)
{
    float dx = v1[0] - v2[0];
    float dy = v1[1] - v2[1];
    float dz = v1[2] - v2[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// FF3 0x00180e08; FF2 CamChangeCheck.
int CamChangeCheck(void)
{
    return 0;
}

// FF3 0x00180e10.
void GetCamFaceRot(float *rot, float *pos)
{
    Get2PosRot(pos, gra3dcamGetPosition(), &rot[0], &rot[1]);
    rot[2] = 0.0f;
    rot[3] = 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// Screen sprites of the effect page
// ──────────────────────────────────────────────────────────────────────

// PC: the quad on the renderer (MioPan's EffSQHostQuad). x / y are GS window
// 12.4, the UVs the ROM's half-texel inset. GS Z above the PSMZ16S range
// clamps to 0xffff on the GS, so it does here.
static void EffSQHostQuad(u_long tex0, u_long tex1, int x0, int x1, int y0, int y1,
                          int tw, int th, int z, u_char r, u_char g, u_char b, u_char a)
{
    float xy[8], uv[8];
    int   i;

    if (EffHostDrawFrameRedirected())
        return;
    for (i = 0; i < 4; i++)
    {
        xy[i * 2 + 0] = (float)((i & 1) ? x1 : x0) / 16.0f - EFF_GS_ORG + EFF_CX;
        xy[i * 2 + 1] = (float)((i / 2) ? y1 : y0) / 16.0f - EFF_GS_ORG + EFF_CY;
        uv[i * 2 + 0] = (float)((i & 1) ? tw - 8 : 8) / 16.0f;
        uv[i * 2 + 1] = (float)((i / 2) ? th - 8 : 8) / 16.0f;
    }
    ReiPan_RendererSetGs2dDepth((u_int)z > 0xffffu ? 0xffffu : (u_int)z);
    ReiPan_RendererDrawTexturedQuad((const sceGsTex0 *)&tex0, (const sceGsTex1 *)&tex1,
                                    xy, uv, r, g, b, a);
}

// The A+D head both sprites send (TEXFLUSH, TEX0), then a 4-vertex strip
// of {RGBAQ, UV, XYZF2} (PRIM 0x154 textured blended UV strip).
static void EffSQSendPacket(u_long tex0, const int *xx, const int *yy, int z, int tw, int th,
                            u_char r, u_char g, u_char b, u_char a)
{
    u_long *p = (u_long *)StartDmaDirectTrans();
    u_int  *q;
    int     i;

    p[0] = 0x1000000000008002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = tex0;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x30aa400000008004ULL;
    p[7] = 0x431;

    q = (u_int *)&p[8];
    for (i = 0; i < 4; i++, q += 12)
    {
        q[0]  = r;
        q[1]  = g;
        q[2]  = b;
        q[3]  = a;
        q[4]  = (i & 1) ? (u_int)(tw * 16 - 8) : 8;
        q[5]  = (i / 2) ? (u_int)(th * 16 - 8) : 8;
        q[6]  = 0;
        q[7]  = 0;
        q[8]  = (u_int)xx[i];
        q[9]  = (u_int)yy[i];
        q[10] = (u_int)z;
        q[11] = (i < 2) ? 0x8000 : 0;   // ADC: the first two only prime the strip
    }
    EndDmaDirectTrans((qword *)q);
}

// FF2 effect_spr.c SetEffSQITex; FF3 0x00180e60. FF3 differences: the
// texture is effdat[n + mono] (the callers already add mono, so it is
// added twice in mono mode, as in the ROM), no interlace halving, the
// registers go through SetGsRegsCached.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (in EffSQSendPacket, which
// FF3 inlines).
void SetEffSQITex(int n, int *v, int tp, float w, float h,
                  u_char r, u_char g, u_char b, u_char a)
{
    const SPRT_DAT *spr = &effdat[n + eff_wrk.monochro_mode];
    GS_REG_SET regs[5];
    int        xx[4], yy[4];
    int        i;

    xx[0] = v[0] - (int)(w * 16.0f);
    xx[1] = v[0] + (int)(w * 16.0f);
    yy[0] = v[1] - (int)(h * 16.0f);
    yy[1] = v[1] + (int)(h * 16.0f);

    if (eff_wrk.monochro_mode != 0)
    {
        r = (u_char)(((int)r + (int)g + (int)b) / 3);
        g = r;
        b = r;
    }

    regs[0].data = ((u_long)((tp & 2) ? 2 : 1) << 2) | ((u_long)((tp & 4) ? 2 : 1) << 6);
    regs[0].reg  = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;                                   regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;                                       regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x5000d;                                 regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = ((u_long)(tp & 1) << 32) | 0xa000118;    regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    // PC: the renderer draws it; the packet below is FF3's, unread.
    EffSQHostQuad(spr->tex0, 0x161, xx[0], xx[1], yy[0], yy[1], spr->w, spr->h, v[2],
                  r, g, b, a);

    // The ROM indexes xx[i % 2] / yy[i / 2] while it writes; spread them out.
    xx[2] = xx[0];
    xx[3] = xx[1];
    yy[2] = yy[1];
    yy[3] = yy[1];
    yy[1] = yy[0];
    EffSQSendPacket(spr->tex0, xx, yy, v[2], spr->w, spr->h, r, g, b, a);
}

// FF2 effect_spr.c SetEffSQTex; FF3 0x00181128. FF3 differences: effdat[n
// + mono], no interlace halving, no DRAW_ENV of its own (the caller's
// registers draw it; tp is unused), the guard band test first.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (in EffSQSendPacket, which
// FF3 inlines).
void SetEffSQTex(int n, float *pos, int tp, float w, float h,
                 u_char r, u_char g, u_char b, u_char a)
{
    const SPRT_DAT *spr = &effdat[n + eff_wrk.monochro_mode];
    int xx[4], yy[4];
    int z, i;

    (void)tp;
    xx[0] = xx[2] = (int)((pos[0] - w) * 16.0f);
    xx[1] = xx[3] = (int)((pos[0] + w) * 16.0f);
    yy[0] = yy[1] = (int)((pos[1] - h) * 16.0f);
    yy[2] = yy[3] = (int)((pos[1] + h) * 16.0f);

    for (i = 0; i < 4; i++)
    {
        if ((u_int)(xx[i] - 0x4000) > 0x8000)
            return;
        if ((u_int)(yy[i] - 0x4000) > 0x8000)
            return;
    }
    z = (int)(pos[2] * 16.0f);
    if ((u_int)(z - 0xff) >= 0xfffff01)
        return;

    if (eff_wrk.monochro_mode != 0)
    {
        g = (u_char)(((int)r + (int)g + (int)b) / 3);
        b = g;
        r = g;
    }

    // PC: the renderer draws it with the caller's TEX1; the packet is FF3's, unread.
    EffSQHostQuad(spr->tex0, *GetGsRegCached(SCE_GS_TEX1_1), xx[0], xx[1], yy[0], yy[2],
                  spr->w, spr->h, z, r, g, b, a);

    EffSQSendPacket(spr->tex0, xx, yy, z, spr->w, spr->h, r, g, b, a);
}

// ──────────────────────────────────────────────────────────────────────
// 3D sprites
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00181868; FF2 Set3DPosTexure. The ROM passes its caller's t3
// through as the shared body's mono flag (it never loads one), so a caller
// with a non-zero t3 greys the sprite; the port passes the mono mode, which
// is what the index already uses (FF2's behaviour).
void Set3DPosTexure(float w, float h, sceVu0FMATRIX mat, u_long *regs, int n,
                    u_char r, u_char g, u_char b, u_char a)
{
    Set3DPosTexureSub(w, h, mat, regs, &effdat[n + eff_wrk.monochro_mode], r, g, b, a,
                      eff_wrk.monochro_mode);
}

// FF3 0x001818a8; FF2 Set3DPosTexure2.
void Set3DPosTexure2(float w, float h, sceVu0FMATRIX mat, u_long *regs, int n,
                     u_char r, u_char g, u_char b, u_char a, int mono)
{
    Set3DPosTexureSub(w, h, mat, regs, &effdat[n + mono], r, g, b, a, mono);
}

// FF3 0x001818e0 (no callers).
void Set3DPosTexureSpr(float w, float h, sceVu0FMATRIX mat, u_long *regs, SPRT_DAT *sprt,
                       u_char r, u_char g, u_char b, u_char a, int mono)
{
    Set3DPosTexureSub(w, h, mat, regs, sprt, r, g, b, a, mono);
}

// FF2's inlined Set3DPosTexure body (MioPan Set3DPosTexureSub). FF3: the
// sprite record is passed, the corners run x = +w/2 for 0, 1 and y = -h/2
// for 1, 3 (FF2: x by i & 1, y by i / 2; the ST order is unchanged), the
// registers come from regs (TEX1, ALPHA, ZBUF, TEST, CLAMP) and the strip's
// GIF tag is regs[5]. Dropped when a corner leaves the guard band.
// FF3 0x002b8220.
void Set3DPosTexureSub(float w, float h, sceVu0FMATRIX mat, u_long *regs, SPRT_DAT *sprt,
                       u_char r, u_char g, u_char b, u_char a, int mono)
{
    static const float twoby[12] = {                    // rdata 0x354b00
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f
    };
    static const float stq[2] = { 0.01f, 0.99f };       // sdata 0x372a30
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0FMATRIX slm;
    sceVu0FVECTOR ppos[4];
    sceVu0IVECTOR ivec[4];
    float         ts[4], tt[4], tq[4];
    float         tw  = (float)sprt->w;
    float         th  = (float)sprt->h;
    float         tdw = twoby[(sprt->tex0 >> 26) & 0xf];
    float         tdh = twoby[(sprt->tex0 >> 30) & 0xf];
    u_long       *p;
    u_int        *q;
    int           i;

    for (i = 0; i < 4; i++)
    {
        ppos[i][0] = ((i / 2) == 0 ? w : -w) * 0.5f;
        ppos[i][1] = ((i & 1) ? -h : h) * 0.5f;
        ppos[i][2] = 0.0f;
        ppos[i][3] = 1.0f;
    }
    sceVu0MulMatrix(slm, cam->matWorldScreen, mat);
    sceVu0RotTransPersN(ivec, slm, ppos, 4, 1);

    for (i = 0; i < 4; i++)
    {
        if ((u_int)(ivec[i][0] - 0x4000) > 0x8000 || (u_int)(ivec[i][1] - 0x4000) > 0x8000
            || (u_int)(ivec[i][2] - 1) > 0xfffffe)
            return;
        tq[i] = 1.0f / (float)ivec[i][3];
        ts[i] = (tw * stq[i % 2] * tq[i]) / tdw;
        tt[i] = (th * stq[i / 2] * tq[i]) / tdh;
    }

    if (mono != 0)
    {
        // As in FF2: the /3 binds to b alone.
        int col = (int)r + (int)g + (int)b / 3;

        if (col > 0xff)
            col = 0xff;
        r = (u_char)col;
        g = r;
        b = r;
    }

    if (regs != nullptr)
    {
        GS_REG_SET set[5];

        set[0].data = regs[1];  set[0].reg = SCE_GS_ALPHA_1;
        set[1].data = regs[0];  set[1].reg = SCE_GS_TEX1_1;
        set[2].data = regs[4];  set[2].reg = SCE_GS_CLAMP_1;
        set[3].data = regs[3];  set[3].reg = SCE_GS_TEST_1;
        set[4].data = regs[2];  set[4].reg = SCE_GS_ZBUF_1;
        SetGsRegsCached(set, 5, 1);
    }

    // PC: the quad in world space through the 3D effect path.
    {
        sceVu0FVECTOR world[4];
        float         st[8];

        for (i = 0; i < 4; i++)
        {
            sceVu0ApplyMatrix(world[i], mat, ppos[i]);
            st[i * 2 + 0] = tw * stq[i % 2] / tdw;
            st[i * 2 + 1] = th * stq[i / 2] / tdh;
        }
        ReiPan_Graph3dDrawEffectQuad(world, 4, st, r, g, b, a, sprt->tex0, 0);
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000008002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = sprt->tex0;
    p[5] = SCE_GS_TEX0_1;
    p[6] = regs[5];
    p[7] = 0x412;                       // ST, RGBAQ, XYZ2

    q = (u_int *)&p[8];
    for (i = 0; i < 4; i++, q += 12)
    {
        float *f = (float *)q;

        f[0]  = ts[i];
        f[1]  = tt[i];
        f[2]  = tq[i];
        q[3]  = 0;
        q[4]  = r;
        q[5]  = g;
        q[6]  = b;
        q[7]  = a;
        q[8]  = (u_int)ivec[i][0];
        q[9]  = (u_int)ivec[i][1];
        q[10] = (u_int)ivec[i][2] << 4;
        q[11] = (i < 2) ? 0x8000 : 0;
    }
    EndDmaDirectTrans((qword *)q);
}

// ──────────────────────────────────────────────────────────────────────
// Frame-buffer passes
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00182678.
void EffSetDrawFrame(int fbp, int psm, int w, int h)
{
    u_long *p = (u_long *)StartDmaDirectTrans();

    p[0] = 0x1000000000008003ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = (u_long)(fbp / 32) | ((u_long)(w / 64) << 16) | ((u_long)psm << 24);
    p[3] = SCE_GS_FRAME_1;
    p[4] = 0x720800006c08ULL;           // XYOFFSET 1728.5, 1824.5
    p[5] = SCE_GS_XYOFFSET_1;
    p[6] = ((u_long)(w - 1) << 16) | ((u_long)(h - 1) << 48);
    p[7] = SCE_GS_SCISSOR_1;
    EndDmaDirectTrans((qword *)&p[8]);

    EffHostNoteWorkFrame(fbp);          // PC
}

// FF3 0x00182760.
void EffResetDrawFrame(void)
{
    u_long *p = (u_long *)StartDmaDirectTrans();

    p[0] = 0x1000000000008003ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = (u_long)(int)(((GetSysWrk()->count & 1) * 0x1180) >> 5) | 0x10a0000;
    p[3] = SCE_GS_FRAME_1;
    p[4] = 0x720000006c00ULL;           // XYOFFSET 1728, 1824
    p[5] = SCE_GS_XYOFFSET_1;
    p[6] = 0x1bf0000027f0000ULL;        // rdata 0x36f2a0: 0..639, 0..447
    p[7] = SCE_GS_SCISSOR_1;
    EndDmaDirectTrans((qword *)&p[8]);

    eff_host_redirect = 0;              // PC
}

// An untextured blended sprite (PRIM 0x46) into the work frame at fbp.
// FF3 0x00182828.
void EffDrawFrameFillRect(int fbp, int psm, int x, int y, int w, int h,
                          u_char r, u_char g, u_char b, u_char a)
{
    u_long *p;
    u_int  *q;

    EffSetDrawFrame(fbp, psm, 320, 448);

    // PC: drawn into an open host work frame (ReiPan_EffHostWorkBegin), in
    // its own pixels; without one the work frame does not exist on the host.
    if (eff_host_work != 0)
    {
        float  xy[8] = { (float)x, (float)y, (float)(x + w), (float)y,
                         (float)x, (float)(y + h), (float)(x + w), (float)(y + h) };
        u_char rgba[16];
        int    k;

        for (k = 0; k < 4; k++)
        {
            rgba[k * 4 + 0] = r;
            rgba[k * 4 + 1] = g;
            rgba[k * 4 + 2] = b;
            rgba[k * 4 + 3] = a;
        }
        ReiPan_RendererDrawSolidQuad(xy, rgba);
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x3023400000008001ULL;
    p[1] = 0x441;                       // RGBAQ, XYZ2, XYZ2
    q = (u_int *)&p[2];
    q[0]  = r;
    q[1]  = g;
    q[2]  = b;
    q[3]  = a;
    q[4]  = (u_int)(x * 16 + 0x6c00);
    q[5]  = (u_int)(y * 16 + 0x7200);
    q[6]  = 0;
    q[7]  = 0;
    q[8]  = (u_int)((x + w) * 16 + 0x6c00);
    q[9]  = (u_int)((y + h) * 16 + 0x7200);
    q[10] = 0;
    q[11] = 0;
    EndDmaDirectTrans((qword *)&p[8]);
}


// DispSprD2 (g2d_draw.c) with the packet cut into 64-pixel column strips:
// the corners are built, scaled and rotated exactly as there, their
// bounding box (whose min starts at 0, so the strips always start at
// column 0) clamped to the screen, and each strip sends SCISSOR_1 for its
// columns, RGBAQ and the four {UV, XYZ2}; SCISSOR_1 goes back to the full
// screen at the end. The strips only keep the texture cache warm: together
// they cover the sprite, so the host draws it once.
// FF3 0x00182930.
void EffDispSprD2Direct(DISP_SPRT2 *s)
{
    u_int  matt  = s->att;
    float  mcrx  = s->crx - EFF_CX;
    float  mcry  = s->cry - EFF_CY;
    float  mcsx  = s->csx - EFF_CX;
    float  mcsy  = s->csy - EFF_CY;
    float  mrot  = (s->rot * EFF_PI) / 180.0f;
    float  msw   = s->scw;
    float  msh   = s->sch;
    u_long mtex0 = s->tex0;
    float  x[4], y[4], x2[4], y2[4];
    u_int  xx[4], yy[4], uu[4], vv[4];
    int    bx0, bx1, by0, by1;
    int    strip0, strip1, col;
    GS_REG_SET regs[6];
    u_long *p;
    u_int  *q;
    float  ss, cc;
    int    i;

    x[2] = s->x - EFF_CX;
    y[1] = s->y - EFF_CY;

    if (msw == 1.0f)
    {
        x[1] = x[2] + s->w * msw;
        x[0] = x[2];
    }
    else
    {
        x[0] = mcsx - (mcsx - x[2]) * msw;
        x[1] = mcsx + ((x[2] + s->w) - mcsx) * msw;
        x[2] = x[0];
    }
    x[3] = x[1];

    if (msh == 1.0f)
    {
        y[2] = y[1] + s->h * msh;
        y[0] = y[1];
    }
    else
    {
        y[0] = mcsy - (mcsy - y[1]) * msh;
        y[2] = mcsy + ((y[1] + s->h) - mcsy) * msh;
        y[1] = y[0];
    }
    y[3] = y[2];

    if (mrot == 0.0f)
    {
        for (i = 0; i < 4; i++)
        {
            x2[i] = x[i];
            y2[i] = y[i];
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            x[i] -= mcrx;
            y[i] -= mcry;
        }
        ss = Vu0Sinf(mrot);
        cc = Vu0Cosf(mrot);
        for (i = 0; i < 4; i++)
        {
            x2[i] = x[i] * cc - y[i] * ss;
            y2[i] = y[i] * cc + x[i] * ss;
        }
        for (i = 0; i < 4; i++)
        {
            x2[i] += mcrx;
            y2[i] += mcry;
        }
    }

    for (i = 0; i < 4; i++)
    {
        xx[i] = EffFtoi4(x2[i] + EFF_GS_ORG);
        yy[i] = EffFtoi4(y2[i] + EFF_GS_ORG);
    }

    if (matt & 2)
    {
        uu[2] = s->u2;
        uu[3] = s->u1;
    }
    else
    {
        uu[2] = s->u1;
        uu[3] = s->u2;
    }
    vv[1] = s->v1;
    vv[3] = s->v2;
    if (matt & 1)
    {
        vv[1] = s->v2;
        vv[3] = s->v1;
    }
    uu[0] = uu[2];
    uu[1] = uu[3];
    vv[0] = vv[1];
    vv[2] = vv[3];

    bx0 = 0;
    bx1 = 0;
    by0 = 0;
    by1 = 0;
    for (i = 0; i < 4; i++)
    {
        int ix = (int)(x2[i] + EFF_CX);
        int iy = (int)(y2[i] + EFF_CY);

        if (bx1 < ix)
            bx1 = ix;
        if (iy < by0)
            by0 = iy;
        if (ix < bx0)
            bx0 = ix;
        if (by1 < iy)
            by1 = iy;
    }
    if (bx0 < 0)
        bx0 = 0;
    if (by0 < 0)
        by0 = 0;
    if (bx1 < 0)
        bx1 = 0;
    if (by1 < 0)
        by1 = 0;
    if (bx1 > 640)
        bx1 = 640;
    if (by1 > 448)
        by1 = 448;
    strip1 = bx1 / 64 + ((bx1 & 0x3f) != 0);
    strip0 = bx0 / 64;

    regs[0].data = s->alpreg;   regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = s->tex1;     regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = s->clmp;     regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = s->test;     regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = s->zbuf;     regs[4].reg = SCE_GS_ZBUF_1;
    regs[5].data = s->texa;     regs[5].reg = SCE_GS_TEXA;
    SetGsRegsCached(regs, 6, 1);

    // PC: the renderer draws it once, with the sprite's CLAMP (the dither
    // tiles its noise sheet with REPEAT); the packet below is FF3's, unread.
    ReiPan_G2dHostSprite(x2, y2, uu, vv, mtex0, s->tex1, s->clmp, 1, s->gftg, s->z,
                         s->r, s->g, s->b, s->alp);

    p = (u_long *)StartDmaDirectTrans();
    p[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1);
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    if (((mtex0 >> 20) & 0x3f) == SCE_GS_PSMT4)
    {
        // Named as PSMT8 with CLD 1 first so the CLUT loads, then as itself.
        p[4] = (mtex0 & 0x00fffffffc0fffffULL) | 0x2000000001300000ULL;
        p[5] = SCE_GS_TEX0_1;
        p[6] = mtex0 & 0x00ffffffffffffffULL;
        p[7] = SCE_GS_TEX0_1;
    }
    else
    {
        p[4] = mtex0;
        p[5] = SCE_GS_TEX0_1;
        p[6] = 0;
        p[7] = 0x7f;                    // A+D to 0x7f: no register (pad)
    }
    p[8] = SCE_GS_SET_TEXCLUT(4, 0, s->col);
    p[9] = SCE_GS_TEXCLUT;

    p += 10;
    for (col = strip0; col < strip1; col++)
    {
        p[0] = ((u_long)s->gftg << 47) | 0xa000400000008001ULL;
        p[1] = 0x434343431eULL;         // A+D, RGBAQ, (UV, XYZ2) x 4
        p[2] = (u_long)(col * 64) | ((u_long)(col * 64 + 63) << 16)
             | ((u_long)by0 << 32) | ((u_long)(by1 - 1) << 48);
        p[3] = SCE_GS_SCISSOR_1;
        q = (u_int *)&p[4];
        q[0] = s->r;
        q[1] = s->g;
        q[2] = s->b;
        q[3] = s->alp;
        q += 4;
        for (i = 0; i < 4; i++, q += 8)
        {
            q[0] = uu[i];
            q[1] = vv[i];
            q[2] = 0;
            q[3] = 0;
            q[4] = xx[i];
            q[5] = yy[i];
            q[6] = s->z;
            q[7] = 0;
        }
        p = (u_long *)q;
    }
    p[0] = 0x1000000000008001ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0x1bf0000027f0000ULL;        // rdata 0x36f2b8: 0..639, 0..447
    p[3] = SCE_GS_SCISSOR_1;
    EndDmaDirectTrans((qword *)&p[4]);
}

// FF2 EffImageHalf32 (FF2 effect.c); FF3 0x00183198 adds the blend: each
// byte the average of the pair (low bit dropped) instead of the odd pixel.
void EffImageHalf32(u_int *image, u_int width, u_int height, int blend)
{
    u_int *src = image;
    u_int *dst = image;
    u_int  x, y;

    width >>= 1;
    if (blend == 0)
    {
        src = image + 1;
        for (y = 0; y < height; y++)
        {
            for (x = 0; x < width; x++)
            {
                *dst++ = *src;
                src += 2;
            }
        }
        return;
    }

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            u_int c0 = src[0];
            u_int c1 = src[1];

            src += 2;
            *dst++ = (((c0 & 0xff) + (c1 & 0xff)) >> 1)
                   | ((((c0 >> 8) & 0xff) + ((c1 >> 8) & 0xff)) * 0x80 & 0xff00)
                   | ((((c0 >> 16) & 0xff) + ((c1 >> 16) & 0xff)) * 0x8000 & 0xff0000)
                   | (((c0 >> 24) + (c1 >> 24)) * 0x800000 & 0xff000000);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The player's light cone
// ──────────────────────────────────────────────────────────────────────

// The light frame: X then Y rotation by the player's rot[0] / rot[1] (each
// wrapped into [-pi, pi) by whole turns, then NormalizeAngle), at the
// player's position; pos is taken into it and tested against a 40-degree
// cone along +z. FF3 0x00181420.
int EffPlyrLightConeCheck(float dist, float *pos, float *off, float *depth)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    float         rx   = plyr->rot[0];
    float         ry   = plyr->rot[1];
    sceVu0FMATRIX m, inv;
    sceVu0FVECTOR local;
    float         tn, r;

    while (rx < -EFF_PI)
        rx += EFF_PI2;
    while (EFF_PI <= rx)
        rx -= EFF_PI2;
    while (ry < -EFF_PI)
        ry += EFF_PI2;
    while (EFF_PI <= ry)
        ry -= EFF_PI2;

    sceVu0UnitMatrix(m);                // the ROM rotates rdata 0x3293c0, the identity
    sceVu0RotMatrixX(m, m, NormalizeAngle(rx));
    sceVu0RotMatrixY(m, m, NormalizeAngle(ry));
    m[3][0] += plyr->pos[0];
    m[3][1] += plyr->pos[1];
    m[3][2] += plyr->pos[2];
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(local, inv, pos);

    tn = tanf(0.6981317f);              // rdata 0x36f27c: 40 degrees
    r  = sqrtf(local[0] * local[0] + local[1] * local[1]);
    *off   = r / (local[2] * tn);
    *depth = local[2] / dist;
    if (local[2] > 0.0f && local[2] < dist && r < local[2] * tn)
        return 1;
    return 0;
}

// FF3 0x00181400.
int EffPlyrLightConeIn(float dist, float *pos)
{
    float off, depth;

    return EffPlyrLightConeCheck(dist, pos, &off, &depth);
}

// ──────────────────────────────────────────────────────────────────────
// The spotlight falloff (no callers in the retail ELF)
// ──────────────────────────────────────────────────────────────────────

struct EFF_SPOT_LIGHT                   /* 0x38 (0x40 with the quadword padding) */
{
    /* 0x00 */ float         inner;
    /* 0x04 */ float         outer;
    /* 0x08 */ float         p2;        // stored, never read
    /* 0x0c */ float         p3;        // stored, never read
    /* 0x10 */ sceVu0FVECTOR pos;
    /* 0x20 */ sceVu0FVECTOR dir;
    /* 0x30 */ float         cos_outer;
    /* 0x34 */ float         cos_inner;
};

static_assert(sizeof(EFF_SPOT_LIGHT) == 0x40, "EFF_SPOT_LIGHT: 0x38 used, padded to the quadword");
static_assert(offsetof(EFF_SPOT_LIGHT, pos) == 0x10, "EFF_SPOT_LIGHT.pos");
static_assert(offsetof(EFF_SPOT_LIGHT, cos_outer) == 0x30, "EFF_SPOT_LIGHT.cos_outer");

static EFF_SPOT_LIGHT eff_spot;         // sbss 0x3df110

// FF3 0x00181698.
void EffSpotLightSet(float inner, float outer, float p2, float p3, float *pos, float *dir)
{
    float q;
    int   i;

    for (i = 0; i < 4; i++)
        eff_spot.pos[i] = pos[i];
    q = Vu0Rsqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);   // vrsqrt (PC: PS2 rsqrt)
    eff_spot.dir[0] = dir[0] * q;
    eff_spot.dir[1] = dir[1] * q;
    eff_spot.dir[2] = dir[2] * q;
    eff_spot.dir[3] = dir[3];

    eff_spot.inner = inner;
    if (outer < 0.0f)
        outer = 0.0f;
    else if (1.5707964f < outer)        // rdata 0x36f280
        outer = 1.5707964f;
    eff_spot.outer = outer;
    eff_spot.p2 = p2;
    eff_spot.p3 = p3;
    eff_spot.cos_outer = Vu0Cosf(eff_spot.outer);
    eff_spot.cos_inner = Vu0Cosf(eff_spot.inner);
}

// FF3 0x00181750.
float EffSpotLightRate(float *point)
{
    float c;

    if (eff_spot.outer < eff_spot.inner)
        return 0.0f;
    c = Vu0Cosf(g3dCalcAngle(eff_spot.dir, eff_spot.pos, point));
    if (c < eff_spot.cos_outer)
        return 0.0f;
    if (c < eff_spot.cos_inner)
        return (c - eff_spot.cos_outer) / (eff_spot.cos_inner - eff_spot.cos_outer);
    return 1.0f;
}

// ──────────────────────────────────────────────────────────────────────
// 3D debug primitives (no callers in the retail ELF)
// ──────────────────────────────────────────────────────────────────────

// p through matWorldScreen as the ROM does it: the origin of a translation
// matrix at p, RotTransPers'd (rdata 0x329500).
static void EffProjectPoint(sceVu0IVECTOR iv, GRA3DCAMERA *cam, const float *p)
{
    sceVu0FMATRIX m, slm;
    sceVu0FVECTOR origin = { 0.0f, 0.0f, 0.0f, 1.0f };

    sceVu0UnitMatrix(m);
    m[3][0] += p[0];
    m[3][1] += p[1];
    m[3][2] += p[2];
    sceVu0MulMatrix(slm, cam->matWorldScreen, m);
    sceVu0RotTransPers(iv, slm, origin, 0);
}

// Both ends projected; dropped when either leaves the GS window (x / y
// outside 0x300..0xfd00, z outside 0xf..0xfffff); one DIRECT LINE (PRIM
// 0x49: gouraud, blended) of {RGBAQ, XYZ2} x 2. FF3 0x002b8698.
// callcheck: sceVu0RotTransPers (in EffProjectPoint, which FF3 inlines).
void EffDrawLine3DSub(float *p0, u_char r0, u_char g0, u_char b0, u_char a0,
                      float *p1, u_char r1, u_char g1, u_char b1, u_char a1)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0IVECTOR iv[2];
    u_int         clip = 0;
    u_long       *p;
    u_int        *q;
    int           i;

    EffProjectPoint(iv[0], cam, p0);
    EffProjectPoint(iv[1], cam, p1);
    for (i = 0; i < 2; i++)
    {
        if ((u_int)(iv[i][0] - 0x300) > 64000)
            clip |= 1u << (i * 4);
        if ((u_int)(iv[i][1] - 0x300) > 64000)
            clip |= 2u << (i * 4);
        if ((u_int)(iv[i][2] - 0xf) > 0xffff0)
            clip |= 4u << (i * 4);
    }
    if (clip != 0)
        return;

    // PC: the line on the renderer in clip space (the camera's
    // matWorldClipObject, as the 3D effect quads), depth-tested when the
    // cached TEST asks for ZTST GEQUAL / GREATER; the packet below is FF3's,
    // unread.
    if (!EffHostDrawFrameRedirected())
    {
        u_long        test = *GetGsRegCached(SCE_GS_TEST_1);
        sceVu0FVECTOR w0, w1;
        float         clip_pos[8];
        u_char        rgba[8];

        for (i = 0; i < 3; i++)
        {
            w0[i] = p0[i];
            w1[i] = p1[i];
        }
        w0[3] = 1.0f;
        w1[3] = 1.0f;
        sceVu0ApplyMatrix(&clip_pos[0], cam->matWorldClipObject, w0);
        sceVu0ApplyMatrix(&clip_pos[4], cam->matWorldClipObject, w1);
        rgba[0] = r0;  rgba[1] = g0;  rgba[2] = b0;  rgba[3] = a0;
        rgba[4] = r1;  rgba[5] = g1;  rgba[6] = b1;  rgba[7] = a1;
        ReiPan_RendererDrawClipLine(clip_pos, rgba, 1.0f,
                                    ((test >> 16) & 1) != 0 && ((test >> 17) & 3) >= 2);
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x4024c00000008001ULL;       // rdata 0x36f2f8
    p[1] = 0x4141;                      // RGBAQ, XYZ2, RGBAQ, XYZ2
    q = (u_int *)&p[2];
    q[0]  = r0;
    q[1]  = g0;
    q[2]  = b0;
    q[3]  = a0;
    q[4]  = (u_int)iv[0][0];
    q[5]  = (u_int)iv[0][1];
    q[6]  = (u_int)iv[0][2];
    q[7]  = 0;
    q[8]  = r1;
    q[9]  = g1;
    q[10] = b1;
    q[11] = a1;
    q[12] = (u_int)iv[1][0];
    q[13] = (u_int)iv[1][1];
    q[14] = (u_int)iv[1][2];
    q[15] = 0;
    EndDmaDirectTrans((qword *)&p[10]);
}

// Blended, depth-tested (TEST 0x5000d, Z writes on). FF3 0x00181908.
void EffDrawLine3DZ(float *p0, u_char r0, u_char g0, u_char b0, u_char a0,
                    float *p1, u_char r1, u_char g1, u_char b1, u_char a1)
{
    SetGsRegCached(0x44, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x5000d, SCE_GS_TEST_1, 1);
    SetGsRegCached(0xa000118, SCE_GS_ZBUF_1, 1);
    EffDrawLine3DSub(p0, r0, g0, b0, a0, p1, r1, g1, b1, a1);
}

// Blended, depth ALWAYS (TEST 0x30003). FF3 0x001819f8.
void EffDrawLine3D(float *p0, u_char r0, u_char g0, u_char b0, u_char a0,
                   float *p1, u_char r1, u_char g1, u_char b1, u_char a1)
{
    SetGsRegCached(0x44, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x30003, SCE_GS_TEST_1, 1);
    SetGsRegCached(0xa000118, SCE_GS_ZBUF_1, 1);
    EffDrawLine3DSub(p0, r0, g0, b0, a0, p1, r1, g1, b1, a1);
}

// The ROM's edge order: three from min, three from (max.x, min.y, max.z),
// three from (max.x, max.y, min.z), three from (min.x, max.y, max.z).
// FF3 0x00181af0.
void EffDrawBox3D(float *min, float *max, u_char r, u_char g, u_char b, u_char a)
{
    static const u_char edge[12][6] = {     // 0: min, 1: max; x y z of A, then of B
        { 0, 0, 0,  1, 0, 0 }, { 0, 0, 0,  0, 1, 0 }, { 0, 0, 0,  0, 0, 1 },
        { 1, 0, 1,  0, 0, 1 }, { 1, 0, 1,  1, 0, 0 }, { 1, 0, 1,  1, 1, 1 },
        { 1, 1, 0,  0, 1, 0 }, { 1, 1, 0,  1, 0, 0 }, { 1, 1, 0,  1, 1, 1 },
        { 0, 1, 1,  0, 1, 0 }, { 0, 1, 1,  1, 1, 1 }, { 0, 1, 1,  0, 0, 1 },
    };
    sceVu0FVECTOR pa, pb;
    int           i, k;

    for (i = 0; i < 12; i++)
    {
        for (k = 0; k < 3; k++)
        {
            pa[k] = edge[i][k] ? max[k] : min[k];
            pb[k] = edge[i][3 + k] ? max[k] : min[k];
        }
        pa[3] = 1.0f;                   // rdata 0x36f28c
        pb[3] = 1.0f;
        EffDrawLine3DZ(pa, r, g, b, a, pb, r, g, b, a);
    }
}

// FF3 0x00181f28.
void EffDrawArrow3D(float *p0, float *p1, u_char r, u_char g, u_char b, u_char a)
{
    sceVu0FVECTOR back, tip;
    int           i;

    EffDrawLine3D(p0, r, g, b, a, p1, r, g, b, a);
    for (i = 0; i < 3; i++)
        back[i] = (p0[i] - p1[i]) * 0.2f;
    back[3] = 0.0f;

    RotVectorY(0.5235988f, tip, back);  // rdata 0x36f290
    for (i = 0; i < 4; i++)
        tip[i] += p1[i];
    EffDrawLine3D(p1, r, g, b, a, tip, r, g, b, a);
    RotVectorY(-0.5235988f, tip, back); // rdata 0x36f294
    for (i = 0; i < 4; i++)
        tip[i] += p1[i];
    EffDrawLine3D(p1, r, g, b, a, tip, r, g, b, a);
}

// FF3 0x00182090.
void EffDrawCircleXZ(float radius, float *centre, u_char r, u_char g, u_char b, u_char a,
                     int spokes, int div)
{
    sceVu0FVECTOR cur, prev;
    float         ang, step;
    int           i, k;

    if (div <= 0)
        return;
    for (k = 0; k < 4; k++)
    {
        cur[k]  = centre[k];
        prev[k] = centre[k];
    }
    ang  = 0.0f;
    step = 6.2831855f / (float)div;     // rdata 0x36f298
    for (i = 0; i < div + 1; i++)
    {
        cur[0] = centre[0] + Vu0Cosf(ang) * radius;
        cur[2] = centre[2] + Vu0Sinf(ang) * radius;
        if (i > 0)
            EffDrawLine3D(cur, r, g, b, a, prev, r, g, b, a);
        if (spokes != 0)
            EffDrawLine3D(centre, r, g, b, a, cur, r, g, b, a);
        for (k = 0; k < 4; k++)
            prev[k] = cur[k];
        ang = NormalizeAngle(ang + step);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Depth probes
// ──────────────────────────────────────────────────────────────────────

// Read the 8-pixel run of the PSMZ16S Z buffer (GS 0x2300) holding screen
// pixel (x, y) into z8. PC: the store reads emulated GS memory, which no
// host draw writes, so it holds 0 and every on-screen point reads as clear
// (MioPan's CheckPointDepth degradation).
static void EffReadZ8(sceGsStoreImage *si, int x, int y, u_short *z8)
{
    sceDmaChan *dma = sceDmaGetChan(1);
    u_long128   buf[1];

    sceGsSetDefStoreImage(si, 0x2300, 10, SCE_GS_PSMZ16S, (short)((x / 8) * 8), (short)y, 8, 1);
    FlushCache(0);
    sceGsExecStoreImage(si, buf);
    sceGsSyncPath(0, 0);
    dma->chcr.TTE = 1;                  // the store ran the bus backwards; tags on again
    memcpy(z8, buf, sizeof(buf));
}

// 1 when pos is on screen and nearer than the Z buffer there. The ROM picks
// the pixel of the 8-wide run by y % 8, not x % 8 (CheckPointDepth uses x);
// kept. FF3 0x00182220.
// callcheck: sceVu0RotTransPers, sceDmaGetChan, sceGsSetDefStoreImage, FlushCache (inlined helpers)
// callcheck: sceGsExecStoreImage, sceGsSyncPath (in EffProjectPoint / EffReadZ8, which FF3 inlines)
int EffPointIsVisible(float *pos)
{
    static sceGsStoreImage si;          // sbss 0x3df150
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0IVECTOR iv;
    u_short       z8[8];
    int           x, y;

    EffProjectPoint(iv, cam, pos);
    if ((u_int)(iv[0] - 0x6c00) > 0x2800 || (u_int)(iv[1] - 0x7200) > 0x1c00
        || (u_int)(iv[2] - 0xff) > 0xfff00)
        return 0;
    x = (iv[0] - 0x6bf8) >> 4;          // -8: rounded to the nearest pixel
    y = (iv[1] - 0x71f8) >> 4;
    EffReadZ8(&si, x, y, z8);
    return z8[y % 8] < (u_short)((iv[2] >> 4) & 0xffff);
}

// FF2 CheckPointDepth (MioPan effect_sub.c); FF3 drops the 25x scale of the
// placement matrix (dead: only its origin is projected). FF3 0x00182428.
// callcheck: sceVu0RotTransPers, sceDmaGetChan, sceGsSetDefStoreImage, FlushCache (inlined helpers)
// callcheck: sceGsExecStoreImage, sceGsSyncPath (in EffProjectPoint / EffReadZ8, which FF3 inlines)
void CheckPointDepth(PP_JUDGE *ppj, int probe_slot)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0IVECTOR iv;
    int           clip, i;

    for (i = 0; i < ppj->num; i++)
    {
        EffProjectPoint(iv, cam, ppj->p[i]);
        clip  = (u_int)(iv[0] - 0x6c00) > 0x2800;
        clip += (u_int)(iv[1] - 0x7200) > 0x1c00;
        clip += (u_int)(iv[2] - 0xff) > 0xfff00;
        if (clip == 0)
        {
            const int occluded = ReiPan_RendererQueryPointOccluded(probe_slot + i, ppj->p[i]);
            clip = occluded > 0; // No completed host probe yet: retain the on-screen fallback.
        }
        ppj->result[i] = (clip == 0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Sprites through a matrix
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001832c8.
void EffSgdSetMaterialAlpha(SGDFILEHEADER *sgd, u_char alpha)
{
    SGDMATERIAL *mat = (SGDMATERIAL *)ReiPan_GetHostPointer(sgd->pMaterial);
    int          i;

    for (i = 0; i < sgd->usNumMaterial; i++)
        mat[i].vDiffuse[3] = (float)alpha;
}

// The 2w x 2h corner set both camera sprites use (strip order).
static void EffSpriteCorners(sceVu0FVECTOR *v, float w, float h)
{
    v[0][0] = -w;  v[0][1] = -h;
    v[1][0] =  w;  v[1][1] = -h;
    v[2][0] = -w;  v[2][1] =  h;
    v[3][0] =  w;  v[3][1] =  h;
    v[0][2] = v[1][2] = v[2][2] = v[3][2] = 0.0f;
    v[0][3] = v[1][3] = v[2][3] = v[3][3] = 1.0f;   // rdata 0x36f2d8 / 0x36f2dc
}

// PC: the sprite of EffSpriteSub in world space through the 3D effect path
// (lw: local -> world), its ST the same 0.01 / 0.99 inset.
static void EffSpriteHost(sceVu0FMATRIX lw, sceVu0FVECTOR *v, SPRT_DAT *sprt,
                          u_char r, u_char g, u_char b, u_char a)
{
    static const float twoby[12] = {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f
    };
    static const float stq[2] = { 0.01f, 0.99f };
    sceVu0FVECTOR world[4];
    float         st[8];
    float         tdw = twoby[(sprt->tex0 >> 26) & 0xf];
    float         tdh = twoby[(sprt->tex0 >> 30) & 0xf];
    int           i;

    for (i = 0; i < 4; i++)
    {
        sceVu0ApplyMatrix(world[i], lw, v[i]);
        st[i * 2 + 0] = (float)sprt->w * stq[i % 2] / tdw;
        st[i * 2 + 1] = (float)sprt->h * stq[i / 2] / tdh;
    }
    ReiPan_Graph3dDrawEffectQuad(world, 4, st, r, g, b, a, sprt->tex0, 0);
}

// The camera's heading from its position to its target; rotated by rot
// about z, then that heading about y. FF3 0x00183308.
void EffCamYawSprite(float rot, float w, float h, SPRT_DAT *sprt, float *pos,
                     u_char r, u_char g, u_char b, u_char a)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0FVECTOR v[4];
    sceVu0FMATRIX m, slm;
    float         rx, ry;

    EffSpriteCorners(v, w, h);
    Get2PosRot(gra3dcamGetPosition(), cam->vTarget, &rx, &ry);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixZ(m, m, NormalizeAngle(rot));
    sceVu0RotMatrixY(m, m, NormalizeAngle(ry));
    m[3][0] += pos[0];
    m[3][1] += pos[1];
    m[3][2] += pos[2];
    sceVu0MulMatrix(slm, cam->matWorldScreen, m);
    if (EffSpriteSub(sprt, slm, v, r, g, b, a))
        EffSpriteHost(m, v, sprt, r, g, b, a);
}

// Each angle goes through NormalizeAngle twice, as in the ROM. FF3 0x001834f8.
void Eff3DFaceSprite(float rot, float w, float h, SPRT_DAT *sprt, float *pos,
                     u_char r, u_char g, u_char b, u_char a)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0FVECTOR v[4];
    sceVu0FVECTOR face;
    sceVu0FMATRIX m, slm;

    (void)rot;
    EffSpriteCorners(v, w, h);
    GetCamFaceRot(face, pos);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(NormalizeAngle(face[0])));
    sceVu0RotMatrixY(m, m, NormalizeAngle(NormalizeAngle(face[1])));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(NormalizeAngle(face[2])));
    m[3][0] += pos[0];
    m[3][1] += pos[1];
    m[3][2] += pos[2];
    sceVu0MulMatrix(slm, cam->matWorldScreen, m);
    if (EffSpriteSub(sprt, slm, v, r, g, b, a))
        EffSpriteHost(m, v, sprt, r, g, b, a);
}

// FF3 0x00183700.
void EffMatSprite(SPRT_DAT *sprt, sceVu0FVECTOR *v, sceVu0FMATRIX m,
                  u_char r, u_char g, u_char b, u_char a)
{
    sceVu0FMATRIX slm;

    sceVu0MulMatrix(slm, gra3dGetCamera()->matWorldScreen, m);
    if (EffSpriteSub(sprt, slm, v, r, g, b, a))
        EffSpriteHost(m, v, sprt, r, g, b, a);
}

// Each corner through slm (dropped off the guard band), then TEXFLUSH /
// TEX0 (the A+D tag without EOP, as in the ROM) and a 4-vertex strip (PRIM
// 0x5c: gouraud, textured, blended, STQ) of {ST, RGBAQ, XYZ2}; ST with the
// 0.01 / 0.99 inset (sdata 0x372a38). FF3 0x002b8a38.
int EffSpriteSub(SPRT_DAT *sprt, sceVu0FMATRIX slm, sceVu0FVECTOR *v,
                 u_char r, u_char g, u_char b, u_char a)
{
    static const float twoby[12] = {    // rdata 0x354b00
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f
    };
    static const float stq[2] = { 0.01f, 0.99f };
    sceVu0IVECTOR iv[4];
    u_long       *p;
    u_int        *q;
    int           i;

    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(iv[i], slm, v[i], 0);
        if ((u_int)(iv[i][0] - 0x4000) > 0x8000 || (u_int)(iv[i][1] - 0x4000) > 0x8000
            || (u_int)(iv[i][2] - 1) > 0xfffffe)
            return 0;
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000000002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = sprt->tex0;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x302e400000008004ULL;
    p[7] = 0x412;                       // ST, RGBAQ, XYZ2

    q = (u_int *)&p[8];
    for (i = 0; i < 4; i++, q += 12)
    {
        float *f  = (float *)q;
        float  qq = 1.0f / (float)iv[i][3];   // rdata 0x36f304

        f[0]  = ((float)sprt->w * stq[i % 2] * qq) / twoby[(sprt->tex0 >> 26) & 0xf];
        f[1]  = ((float)sprt->h * stq[i / 2] * qq) / twoby[(sprt->tex0 >> 30) & 0xf];
        f[2]  = qq;
        q[3]  = 0;
        q[4]  = r;
        q[5]  = g;
        q[6]  = b;
        q[7]  = a;
        q[8]  = (u_int)iv[i][0];
        q[9]  = (u_int)iv[i][1];
        q[10] = (u_int)iv[i][2];
        q[11] = (i < 2) ? 0x8000 : 0;
    }
    EndDmaDirectTrans((qword *)q);
    return 1;
}

// No TEXFLUSH / TEX0 of its own: the caller's TEX0 draws it. ST from the
// texel rect uv (u, v, w, h) with no inset (sdata 0x36f2e0: 0, 1).
// FF3 0x00183800.
void EffMatTexQuad(u_long tex0, sceVu0FMATRIX m, sceVu0FVECTOR *v, int *uv, int *col)
{
    static const float twoby[12] = {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f
    };
    static const float stq[2] = { 0.0f, 1.0f };
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0FMATRIX slm;
    sceVu0IVECTOR iv[4];
    float         tdw = twoby[(tex0 >> 26) & 0xf];
    float         tdh = twoby[(tex0 >> 30) & 0xf];
    u_long       *p;
    u_int        *q;
    int           i;

    sceVu0MulMatrix(slm, cam->matWorldScreen, m);
    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(iv[i], slm, v[i], 0);
        if ((u_int)(iv[i][0] - 0x4000) > 0x8000 || (u_int)(iv[i][1] - 0x4000) > 0x8000
            || (u_int)(iv[i][2] - 1) > 0xfffffe)
            return;
    }

    // PC: the quad in world space through the 3D effect path.
    {
        sceVu0FVECTOR world[4];
        float         st[8];

        for (i = 0; i < 4; i++)
        {
            sceVu0ApplyMatrix(world[i], m, v[i]);
            st[i * 2 + 0] = ((float)uv[2] * stq[i % 2] + (float)uv[0]) / tdw;
            st[i * 2 + 1] = ((float)uv[3] * stq[i / 2] + (float)uv[1]) / tdh;
        }
        ReiPan_Graph3dDrawEffectQuad(world, 4, st, col[0], col[1], col[2], col[3], tex0, 0);
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x302e400000008004ULL;
    p[1] = 0x412;
    q = (u_int *)&p[2];
    for (i = 0; i < 4; i++, q += 12)
    {
        float *f  = (float *)q;
        float  qq = 1.0f / (float)iv[i][3];   // rdata 0x36f2e8

        f[0]  = (((float)uv[2] * stq[i % 2] + (float)uv[0]) * qq) / tdw;
        f[1]  = (((float)uv[3] * stq[i / 2] + (float)uv[1]) * qq) / tdh;
        f[2]  = qq;
        q[3]  = 0;
        q[4]  = (u_int)col[0];
        q[5]  = (u_int)col[1];
        q[6]  = (u_int)col[2];
        q[7]  = (u_int)col[3];
        q[8]  = (u_int)iv[i][0];
        q[9]  = (u_int)iv[i][1];
        q[10] = (u_int)iv[i][2];
        q[11] = (i < 2) ? 0x8000 : 0;
    }
    EndDmaDirectTrans((qword *)q);
}

// FF3 0x00183af8 (effect_oth.c's 0x17fdb0 passes the work frame 0x3020 as
// fbp; it is not read).
void EffDispSprCentreScale(float scw, float sch, int fbp, SPRT_DAT *sprt, u_char aref,
                           int x, int y, int *col, u_int pri, u_long alpha)
{
    DISP_SPRT ds;

    (void)fbp;
    CopySprDToSpr(&ds, sprt);
    ds.test   = ((u_long)aref << 4) | 0x3000d;
    ds.alphar = alpha;
    ds.scw    = scw;
    ds.sch    = sch;
    ds.pri    = pri;
    ds.tex1   = 0x161;
    ds.z      = 0xfffff - (pri & 0xfffff);
    ds.r      = (u_char)col[0];
    ds.g      = (u_char)col[1];
    ds.b      = (u_char)col[2];
    ds.alpha  = (u_char)col[3];
    ds.x      = (float)x;
    ds.y      = (float)y;
    ds.csx    = (float)(int)(x + (sprt->w >> 1));
    ds.csy    = (float)(int)(y + (sprt->h >> 1));
    DispSprD(&ds);
}

static u_long eff_gs_regs_saved[5];     // sdata 0x372f98..0x372fc0

// FF3 0x00183c38.
void EffGsRegsSave(void)
{
    eff_gs_regs_saved[0] = *GetGsRegCached(SCE_GS_ALPHA_1);
    eff_gs_regs_saved[1] = *GetGsRegCached(SCE_GS_TEX1_1);
    eff_gs_regs_saved[2] = *GetGsRegCached(SCE_GS_CLAMP_1);
    eff_gs_regs_saved[3] = *GetGsRegCached(SCE_GS_TEST_1);
    eff_gs_regs_saved[4] = *GetGsRegCached(SCE_GS_ZBUF_1);
}

// FF3 0x00183ca0.
void EffGsRegsRestore(void)
{
    SetGsRegCached(eff_gs_regs_saved[0], SCE_GS_ALPHA_1, 1);
    SetGsRegCached(eff_gs_regs_saved[1], SCE_GS_TEX1_1, 1);
    SetGsRegCached(eff_gs_regs_saved[2], SCE_GS_CLAMP_1, 1);
    SetGsRegCached(eff_gs_regs_saved[3], SCE_GS_TEST_1, 1);
    SetGsRegCached(eff_gs_regs_saved[4], SCE_GS_ZBUF_1, 1);
}

// FF3 0x00183d00.
int EffModelIsOutOfView(ModelObject *obj)
{
    static sceVu0FVECTOR bb[8];         // sbss 0x3df230

    if (obj == nullptr)
        return 1;
    sgdGetBoundingBox(obj->GetSgd(0), bb);
    return CheckModelBoundingBox(bb) == 0;
}

// ──────────────────────────────────────────────────────────────────────
// PC: debug overlay (not FF3)
// ──────────────────────────────────────────────────────────────────────

// The Debug menu's "Player light cone" (reipan_debug.h): every in-game frame,
// the player's light cone as EffPlyrLightConeCheck tests it (apex at pos,
// axis +z of RotX(rot[0]) then RotY(rot[1]), 40-degree half-angle, 1200
// units: effect_obj.c's range) in blue, its reach on the ground
// (EffDrawCircleXZ) in yellow, a box around the player (EffDrawBox3D) in
// green and the facing (EffDrawArrow3D) in red. Drawn with FF3's own
// uncalled debug primitives above.
void ReiPan_EffDebugOverlay(void)
{
    PLYR_WRK     *plyr;
    sceVu0FMATRIX m;
    sceVu0FVECTOR apex, local, rim, rim_old, mn, mx, tip;
    float         rx, ry, len = 1200.0f, rad;
    int           i;

    plyr = ingame_wrk.plyr_wrk;
    if (ReiPan_DebugLightConeEnabled() == 0 || plyr == nullptr)
        return;

    rx = plyr->rot[0];
    ry = plyr->rot[1];
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(rx));
    sceVu0RotMatrixY(m, m, NormalizeAngle(ry));
    m[3][0] += plyr->pos[0];
    m[3][1] += plyr->pos[1];
    m[3][2] += plyr->pos[2];
    for (i = 0; i < 3; i++)
        apex[i] = plyr->pos[i];
    apex[3] = 1.0f;

    rad = len * tanf(0.6981317f);
    for (i = 0; i <= 24; i++)
    {
        float a = 6.2831855f * (float)i / 24.0f;

        local[0] = Vu0Cosf(a) * rad;
        local[1] = Vu0Sinf(a) * rad;
        local[2] = len;
        local[3] = 1.0f;
        sceVu0ApplyMatrix(rim, m, local);
        if (i > 0)
            EffDrawLine3D(rim_old, 0x40, 0x80, 0xff, 0x80, rim, 0x40, 0x80, 0xff, 0x80);
        if ((i % 6) == 0)
            EffDrawLine3D(apex, 0x40, 0x80, 0xff, 0x80, rim, 0x40, 0x80, 0xff, 0x80);
        memcpy(rim_old, rim, sizeof(rim));
    }

    EffDrawCircleXZ(len, apex, 0xff, 0xff, 0x20, 0x80, 0, 32);

    for (i = 0; i < 3; i++)
    {
        mn[i] = plyr->pos[i] - 5.0f;
        mx[i] = plyr->pos[i] + 5.0f;
    }
    mn[1] = plyr->pos[1] - 18.0f;
    mx[1] = plyr->pos[1];
    mn[3] = mx[3] = 1.0f;
    EffDrawBox3D(mn, mx, 0x20, 0xff, 0x20, 0x80);

    tip[0] = apex[0] + Vu0Sinf(ry) * 30.0f;
    tip[1] = apex[1];
    tip[2] = apex[2] + Vu0Cosf(ry) * 30.0f;
    tip[3] = 1.0f;
    EffDrawArrow3D(apex, tip, 0xff, 0x40, 0x40, 0x80);
}
