/* ==========================================================================
 *  graphics/effect/effect_ene.c
 *
 *  The ghost-damage effects (FF3 0x15b658..0x1622d0, FF2 effect_ene.c; see
 *  effect_ene.h). Reconstructed so far: the init / release, EfGetMpos, the
 *  damage overlay (SetEneDmgEffect1_Sub / _Sub2), the spirit strands
 *  (SetEneDmgEffect2_Sub / _Sub2) with their ribbon (DrawNewPerticleSub), the
 *  spirit particle bursts (requests, the bloom, the flight home with its
 *  comet tails and the lens charge), the camera hit's screen half
 *  (EneHitEffectReq / Main), the damage sprite's clock (EneDmgMain), the
 *  large-hit flash with its 50 presets and 26 blur presets, and the enemy
 *  aura (FF3-only). Ported from MioPan's effect_ene.c and checked against
 *  FF3; differences noted per function. FF3-only and unreferenced, written
 *  for completeness: EffEneTimerRearm (0x161af0) and the finder sparks
 *  (CEffectFinderMgr 0x161f38..0x1622d0, the classes' methods out of line
 *  at 0x2b0b98..0x2b12a0).
 *
 *  PC: the GS packets are built as FF3 builds them (StartDmaDirectTrans), but
 *  nothing on the host runs a 3D effect's DIRECT chain, so the overlay quad
 *  also goes through ReiPan_Graph3dDrawEffectQuad and the ribbon through
 *  ReiPan_Graph3dDrawGouraudTriangles, from their world points.
 * ======================================================================== */

#include "common/reipan_stub.h"
#include "graphics/effect/effect_ene.h"

#include <math.h>                       // sqrtf (the ROM: vsqrt)
#include <stdint.h>                     // int64_t (the EE's 64-bit packet words)
#include <string.h>                     // memset, memcmp
#include <libgraph.h>                   // SCE_GS_* / SCE_GIF_PACKED_AD

#include "graphics/effect/effect.h"     // eff_wrk, EFFECT_FREE
#include "graphics/effect/effect_sub.h" // Get2PosRot, GetTrgtRotType2Neg, Set3DPosTexure, WorldToScreenPos, Vector2Rot
#include "graphics/effect/effect_obj.h" // CalcPartsDeformXYZ
#include "graphics/graph2d/g2d_draw.h"  // SPRT_DAT, StartDmaDirectTrans, EndDmaDirectTrans
#include "graphics/graph2d/tim2.h"      // MakeTim2SendPacket_3Dpkt
#include "graphics/graph3d/gra3d.h"     // gra3dGetCamera, gra3dcamGetPosition
#include "graphics/gs_reg.h"            // GS_REG_SET, SetGsRegsCached
#include "common/vu0math.h"             // Vu0Sinf
#include "ingame/enemy/ene_main.h"      // ene_wrk
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/enemy/enemy.h"         // ENE_AURA_DAT
#include "system/os/system.h"           // GetSysWrk
#include "ingame/plyr/player.h"         // PLYR_WRK, PLYR_FLG_800000
#include "ingame/photo/finder_photo.h"  // GetLensWrk
#include "ingame/photo/fndr_parts.h"    // LENS_WRK::AddCharge / SeEnd
#include "ingame/ingame_effect.h"      // IgEffectSubFunc*Req, IgEffectZeroCamFreqReq
#include "outgame/option.h"             // GetOptionWrk
#include "system/pad/pad.h"             // pad_wrk (the rumble)
#include "reipan/gameplay/reipan_haptic_cues.h"     // PC: what the rumble is for
#include "reipan/os/reipan_rand.h"      // ReiPan_Rand, REIPAN_RAND_MAXF
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "reipan/rendering/reipan_graph3d.h" // PC: ReiPan_Graph3dDrawEffectQuad / DrawGouraudTriangles
#include "reipan/rendering/reipan_renderer.h" // PC: ReiPan_RendererDrawTexturedTriangles2D

// ---- data -------------------------------------------------------------------

ENDMG1 enedmg1[ENDMG1_NUM];                     // bss 0x399fa0
ENDMG2 enedmg2;                                 // bss 0x39a8a0
ENE_DMG_LARGE_HIT_CTRL EneDmgLargeHitCtrl;      // bss 0x39a8b0
ENE_HIT_EFFECT_CTRL EneHitEffectCtrl;           // bss 0x39a980
static TAIL_DMG2_DAT enedmg2_tail[NEW_PERTICLE_NUM];    // bss 0x39a9c0
static NEW_PERTICLE new_perticle[NEW_PERTICLE_NUM];     // bss 0x3a5dc0
ENE_DMG_EFF ene_dmg_eff;                        // bss 0x3a69d0
SINGLE_LINK_LIST EneParticleList;               // bss 0x3a6a20
SINGLE_LINK_LIST EffectEndParticleList;         // bss 0x3a6a30
int enedmg_status;                              // sbss 0x372f28
int EneDmgParticleSuctionNum;                   // sbss 0x372f2c

// The strands' clock speed. FF3 reads the same word gp-relative
// (SetEneDmgEffect2_Sub) and as a literal (SetEneDmgEffect2_Sub2).
static float enedmg2_sp = 1.3f;                 // sdata 0x36ddc8

// The hit ramps' frame counts (sdata 0x36de90..0x36dea4; FF2 had them as
// constants): SEC0 the arming delay, SEC1..SEC5 the damage sprite's legs.
static int SEC0 = 1;
static int SEC1 = 2;
static int SEC2 = 4;
static int SEC3 = 10;
static int SEC4 = 30;
static int SEC5 = 15;

// rand() / RAND_MAX as the ROM writes it (2147483520.0f, rdata 0x36de4c).
// callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
static inline float EffEneRand01(void)
{
    return (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
}

// The current player is in a camera view (finder up, or a view-pair camera):
// FF2's plyr_wrk.cmn_wrk.mode == 6.
static inline int EffEnePlyrIsFinderView(void)
{
    PLYR_WRK *pw = ingame_wrk.plyr_wrk;

    return (pw->flags & PLYR_FLG_800000) != 0 || (pw->_0e0 & 1) != 0;
}

// ---- init / release ------------------------------------------------------------

static void EneDmgLargeHitCtrlInit(void);
static void EneDmgParticleWorkInit(void);
static void EffectEndParticleWorkInit(void);
static void EneDmgScreenWorkInit(void);
static void EneHitEffectCtrlInit(void);
static void EneDmgLargeHitAllOff(void);
static void EneDmgLargeHitCtrlMain(void);
static void EneHitEffectBlurContrastCalc(ENE_HIT_EFFECT_CTRL *pCtrl);
static void EneHitEffectSubFuncReq(int EneWrkNo, int HitEffectLabel);
static void EneDmgParticleWorkRelease(void);
static void EffectEndParticleWorkRelease(void);
static void DrawNewPerticleSubHost(int num, sceVu0FVECTOR *pos, const int *clip, u_char rr1, u_char gg1,
                                   u_char bb1, u_char rr2, u_char gg2, u_char bb2, u_char a);

// FF2's, with 16 overlay records and without the sword line / seal status.
// FF3 0x0015b658.

void InitEffectEne(void)
{
    int i;

    EneDmgLargeHitCtrlInit();
    EneDmgParticleWorkInit();
    EffectEndParticleWorkInit();
    EneDmgScreenWorkInit();
    EneHitEffectCtrlInit();

    for (i = 0; i < ENDMG1_NUM; i++)
        enedmg1[i].enedmg1_flg = 0;

    enedmg_status = 0;
    enedmg2.enedmg2_flg = 0;
    EneDmgParticleSuctionNum = 0;
}

// Empty, as in FF2. FF3 0x0015b6d0.

void InitEffectEneEF(void)
{
}

// FF3-only: EffectsReleaseAll's share. The flashes go off and both burst
// lists free their cells (with their heap arrays). FF3 0x0015b6d8.

void EffectEneReleaseAll(void)
{
    EneDmgLargeHitAllOff();
    EneDmgParticleWorkRelease();
    EffectEndParticleWorkRelease();
}

// ---- helpers --------------------------------------------------------------------

// One of the ten tracked points of a ghost (ENE_WRK's first ten vectors);
// another id gives the zero point. FF3 0x0015b700 (jump table rdata 0x352830).

static float *EfGetMpos(int eneno, int id)
{
    static float work[4] = { 0.0f, 0.0f, 0.0f, 1.0f };     // rdata 0x329500
    ENE_WRK *ew = &ene_wrk[eneno];

    switch (id)
    {
    case 0: return ew->mpos;
    case 1: return ew->jpos1;
    case 2: return ew->jpos13;
    case 3: return ew->jpos7;
    case 4: case 5: case 6: case 7: case 8: case 9:
        return ew->jpos_ex[id - 4];
    }
    return work;
}

// The ghost's previous damage: cut down to 0 before this build, as in FF2
// (the bloom and the strand count run at their minimum). FF3 0x0015b800.

static u_short EfGetDmgOld(int eneno)
{
    (void)eneno;
    return 0;
}

// ---- the damage overlay -----------------------------------------------------------

// One ghost's overlay (FF2's, effect_ene.h ENDMG1). The two quads are
// billboards pulled along the eye ray (dist) and turned to the camera's
// heading; only quad 1 is emitted, as a 4-vertex textured strip of camdat[1]
// in the record's chance colour. Monochrome is off for the call.
// FF3 differences: the sprite table is the file's own (camdat, rdata
// 0x352858) and textbl picks entry 1 (FF2 12); the TIM2 sent first is the one
// at EE 0x1fa8000 (MakeTim2SendPacket_3Dpkt; FF2 SetSprFile2); wlm[2][2] is
// 1 (FF2 25); pi is 3.1415927. FF3 0x0015b808.
// callcheck: __fixunssfsi (the (u_int) cast of the alpha)

static void SetEneDmgEffect1_Sub2(int num)
{
    static const SPRT_DAT camdat[14] = {                    // rdata 0x352858
        { 0x20050d059930a710ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x20050d859930a710ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x20050f059930a730ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x20050f859930a730ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x20050e059930a720ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x20050e859930a720ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x200510059930a740ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x200510859930a740ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x200511059930a750ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x200511859930a750ULL, 0, 0, 64,  64,  0, 0, 0, 0x80, 0, 1 },
        { 0x200512055930a760ULL, 0, 0, 64,  32,  0, 0, 0, 0x80, 0, 1 },
        { 0x200512855930a760ULL, 0, 0, 64,  32,  0, 0, 0, 0x80, 0, 1 },
        { 0x2005130621312768ULL, 0, 0, 192, 192, 0, 0, 0, 0x80, 0, 1 },
        { 0x2005138621312768ULL, 0, 0, 192, 192, 0, 0, 0, 0x80, 0, 1 },
    };
    // [mono][quad][chance][rgb] (rdata 0x352a18).
    static const u_char rgb[2][2][3][3] = {
        { { { 128, 128, 128 }, { 255,  32,  32 }, { 255, 240, 255 } },
          { {  73, 138, 234 }, { 255,  80,  48 }, { 255, 250, 255 } } },
        { { { 128, 128, 128 }, { 106, 106, 106 }, { 106, 106, 106 } },
          { { 148, 148, 148 }, { 128, 128, 128 }, { 128, 128, 128 } } },
    };
    static const float dist[4] = {  100.0f, -200.0f, -450.0f, -460.0f };
    static const float bww[4]  = {  450.0f,   16.0f,   80.0f,   80.0f };
    static const float bhh[4]  = {  450.0f,   16.0f,   80.0f,   80.0f };
    static const float szw[4]  = {  256.0f,  256.0f,  128.0f,  128.0f };
    static const float szh[4]  = {  256.0f,  256.0f,  128.0f,  128.0f };
    static const int   textbl[4] = { 0, 1, 0, 0 };
    GRA3DCAMERA  *cam = gra3dGetCamera();
    float        *cam_pos = gra3dcamGetPosition();
    ENDMG1       *dmg1;
    sceVu0FVECTOR wpos[2];
    sceVu0FVECTOR cpos;
    sceVu0FVECTOR ppos[2][4];
    sceVu0FVECTOR wworld[2][4];         // PC: the host draw's corners
    sceVu0IVECTOR ivec[2][4];
    sceVu0FMATRIX wlm, slm;
    float         scl[2];
    float         ts[2][4], tt[2][4], tq[2][4];
    u_int         tw[2] = { 0, 0 }, th[2] = { 0, 0 };
    u_long        tex0[2] = { 0, 0 };
    int           clip[2];
    float         rot_x, rot_y;
    int           mono, st, dmg, i, j, ndpkt;
    float         f;
    u_long       *p;
    u_int        *q;

    st = 0;
    mono = eff_wrk.monochro_mode;
    if (eff_wrk.stop_flg != 0)
        return;

    dmg1 = &enedmg1[num];
    if (dmg1->enedmg1_flg == 0)
        return;

    eff_wrk.monochro_mode = 0;

    if (dmg1->enedmg1_flg == 1)
    {
        // EfGetMpos is called once per slot.
        memcpy(dmg1->wbpos[0], EfGetMpos(dmg1->enedmg_no, 0), sizeof(sceVu0FVECTOR));
        memcpy(dmg1->wbpos[1], EfGetMpos(dmg1->enedmg_no, 0), sizeof(sceVu0FVECTOR));

        for (i = 0; i < 2; i++)
        {
            dmg1->scw[i]  = 0.0f;
            dmg1->sch[i]  = 0.0f;
            dmg1->alp[i]  = 0.0f;
            dmg1->flow[i] = 0;
            dmg1->cnt[i]  = 0;
        }
        dmg1->rot_z[0] = 0.0f;          // FF2's loop wrote rot_z[0] twice; FF3 once, after it

        dmg1->enedmg1_flg = 2;
        enedmg_status = 0;
    }

    // 0.6 at 10 damage, 1.6 at 80: quad 0's extra bloom.
    dmg = EfGetDmgOld(dmg1->enedmg_no);
    if (dmg < 10)
        dmg = 10;
    else if (dmg > 80)
        dmg = 80;
    f = (float)(dmg - 10) / 70.0f + 0.6f;

    // Quad 0: hold 15 frames, then bloom and fade from 80 over 40.
    if (dmg1->flow[0] == 1)
    {
        dmg1->scw[0] = ((float)dmg1->cnt[0] * f * 0.5f) / 40.0f + 1.0f;
        dmg1->sch[0] = ((float)dmg1->cnt[0] * f) / 40.0f + 1.0f;
        dmg1->alp[0] = 80.0f - ((float)dmg1->cnt[0] * 80.0f) / 40.0f;

        if (dmg1->cnt[0] >= 40)
        {
            dmg1->cnt[0]  = 0;
            dmg1->flow[0] = 2;
            dmg1->alp[0]  = 0.0f;
        }
        else if (eff_wrk.stop_flg == 0)
        {
            dmg1->cnt[0]++;
        }
    }
    else if (dmg1->flow[0] == 0)
    {
        dmg1->alp[0] = 0.0f;
        dmg1->scw[0] = 1.0f;
        dmg1->sch[0] = 1.0f;

        if (dmg1->cnt[0] >= 15)
        {
            dmg1->flow[0] = 1;
            dmg1->cnt[0]  = 0;
            dmg1->alp[0]  = 80.0f;
            SetEneDmgEffect2();
        }
        else if (eff_wrk.stop_flg == 0)
        {
            dmg1->cnt[0]++;
        }
    }

    // Quad 1: hold 15 frames, then spin 2 degrees a frame and fade from 64
    // over 40; its end clears the record.
    if (dmg1->flow[1] == 1)
    {
        dmg1->rot_z[1] += 0.034906585f;
        if (dmg1->rot_z[1] >= 3.1415927f)
            dmg1->rot_z[1] -= 6.2831855f;

        dmg1->alp[1] = 64.0f - ((float)dmg1->cnt[1] * 64.0f) / 40.0f;

        if (dmg1->cnt[1] >= 40)
        {
            dmg1->cnt[1]  = 0;
            dmg1->flow[1] = 2;
            dmg1->alp[1]  = 0.0f;
        }
        else if (eff_wrk.stop_flg == 0)
        {
            dmg1->cnt[1]++;
        }
    }
    else if (dmg1->flow[1] == 0)
    {
        dmg1->alp[1] = 0.0f;
        dmg1->scw[1] = 1.0f;
        dmg1->sch[1] = 1.0f;

        if (dmg1->cnt[1] >= 15)
        {
            dmg1->flow[1] = 1;
            dmg1->cnt[1]  = 0;
            dmg1->alp[1]  = 64.0f;
        }
        else if (eff_wrk.stop_flg == 0)
        {
            dmg1->cnt[1]++;
        }
    }
    else if (dmg1->flow[1] == 2)
    {
        dmg1->enedmg1_flg = 0;
    }

    // PC: nothing loads a TIM2 at EE 0x1fa8000 that this port knows of (it
    // sits 0x8000 past the system heap's end); only a real one is sent.
    {
        void *tim2 = ReiPan_GetHostPointer(0x1fa8000);

        if (tim2 != nullptr && memcmp(tim2, "TIM2", 4) == 0)
            MakeTim2SendPacket_3Dpkt(tim2, 0, -1, -1);
    }

    cpos[0] = cam_pos[0];
    cpos[1] = cam_pos[1];
    cpos[2] = cam_pos[2];
    cpos[3] = 1.0f;

    for (i = 0; i < 2; i++)
    {
        float dx  = dmg1->wbpos[i][0] - cpos[0];
        float dy  = dmg1->wbpos[i][1] - cpos[1];
        float dz  = dmg1->wbpos[i][2] - cpos[2];
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        float k;

        // Quad 1 grows with the distance (1 under 200, 25 past 2000).
        if (i == 1)
        {
            if (len < 200.0f)
                scl[1] = 1.0f;
            else if (len <= 2000.0f)
                scl[1] = ((len - 200.0f) * 24.0f) / 1800.0f + 1.0f;
            else
                scl[1] = 25.0f;
        }
        else
        {
            scl[i] = 1.0f;
        }

        ppos[i][0][0] = -bww[i] * scl[i];  ppos[i][0][1] = -bhh[i] * scl[i];
        ppos[i][1][0] =  bww[i] * scl[i];  ppos[i][1][1] = -bhh[i] * scl[i];
        ppos[i][2][0] = -bww[i] * scl[i];  ppos[i][2][1] =  bhh[i] * scl[i];
        ppos[i][3][0] =  bww[i] * scl[i];  ppos[i][3][1] =  bhh[i] * scl[i];
        for (j = 0; j < 4; j++)
        {
            ppos[i][j][2] = 0.0f;
            ppos[i][j][3] = 1.0f;
        }

        // Slide the billboard along the eye ray by dist[i].
        k = len + dist[i];
        wpos[i][0] = cpos[0] + (dx * k) / len;
        wpos[i][1] = cpos[1] + (dy * k) / len;
        wpos[i][2] = cpos[2] + (dz * k) / len;
        wpos[i][3] = 1.0f;
    }

    Get2PosRot(cam_pos, cam->vTarget, &rot_x, &rot_y);

    for (i = 0; i < 2; i++)
    {
        sceVu0UnitMatrix(wlm);
        wlm[0][0] = dmg1->scw[i];
        wlm[1][1] = dmg1->sch[i];
        wlm[2][2] = 1.0f;
        sceVu0RotMatrixZ(wlm, wlm, dmg1->rot_z[i]);
        sceVu0RotMatrixY(wlm, wlm, rot_y);
        sceVu0TransMatrix(wlm, wlm, wpos[i]);
        sceVu0MulMatrix(slm, cam->matWorldScreen, wlm);
        sceVu0RotTransPersN(ivec[i], slm, ppos[i], 4, 1);

        for (j = 0; j < 4; j++)         // PC
            sceVu0ApplyMatrix(wworld[i][j], wlm, ppos[i][j]);

        if (i == 0)
        {
            st = 1;
        }
        else
        {
            const SPRT_DAT *spr = &camdat[textbl[i] + eff_wrk.monochro_mode];

            tex0[i] = spr->tex0;
            tw[i]   = spr->w;
            th[i]   = spr->h;
        }

        clip[i] = 1;
        for (j = 0; j < 4; j++)
        {
            if ((u_int)(ivec[i][j][0] - 0x300) > 64000)
                clip[i] = 0;
            if ((u_int)(ivec[i][j][1] - 0x300) > 64000)
                clip[i] = 0;
            if ((u_int)(ivec[i][j][2] - 1) > 0xfffffe)
                clip[i] = 0;
            tq[i][j] = 1.0f / (float)ivec[i][j][3];
            ts[i][j] = ((float)tw[i] * tq[i][j]) / szw[i];
            tt[i][j] = ((float)th[i] * tq[i][j]) / szh[i];
        }
    }

    {
        GS_REG_SET regs[5];

        regs[0].data = 0x44;            regs[0].reg = SCE_GS_ALPHA_1;
        regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
        regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
        regs[3].data = 0x5000d;         regs[3].reg = SCE_GS_TEST_1;
        regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
        SetGsRegsCached(regs, 5, 1);
    }

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0;
    p[1] = 0;
    p[2] = 0x1000000000008001ULL;
    p[3] = SCE_GIF_PACKED_AD;
    p[4] = 0;
    p[5] = SCE_GS_TEXFLUSH;
    ndpkt = 3;

    for (i = st; i < 2; i++)
    {
        const u_char *col = rgb[eff_wrk.monochro_mode][i][dmg1->enedmg_chance];

        if (clip[i] == 0)
            continue;

        // PC: the same quad from its world corners; ST spans the sprite
        // over the strip's TL, TR, BL, BR.
        {
            float stq[8];

            for (j = 0; j < 4; j++)
            {
                stq[j * 2 + 0] = (j & 1) ? (float)tw[i] / szw[i] : 0.0f;
                stq[j * 2 + 1] = (j / 2) ? (float)th[i] / szh[i] : 0.0f;
            }
            ReiPan_Graph3dDrawEffectQuad(wworld[i], 4, stq, col[0], col[1], col[2],
                                         (int)dmg1->alp[i], tex0[i], 0);
        }

        p[ndpkt * 2 + 0] = 0x1000000000008001ULL;
        p[ndpkt * 2 + 1] = SCE_GIF_PACKED_AD;
        p[ndpkt * 2 + 2] = tex0[i];
        p[ndpkt * 2 + 3] = SCE_GS_TEX0_1;
        p[ndpkt * 2 + 4] = 0x302a400000008004ULL;  // 4 loops, PRIM 0x54 (strip, textured, blended)
        p[ndpkt * 2 + 5] = 0x412;                  // ST, RGBAQ, XYZF2
        ndpkt += 3;

        for (j = 0; j < 4; j++)
        {
            q = (u_int *)&p[ndpkt * 2];
            ((float *)q)[0] = (j & 1) ? ts[i][j] : 0.0f;
            ((float *)q)[1] = (j / 2) ? tt[i][j] : 0.0f;
            ((float *)q)[2] = tq[i][j];
            q[3]  = 0;
            q[4]  = col[0];
            q[5]  = col[1];
            q[6]  = col[2];
            q[7]  = (u_int)dmg1->alp[i];
            q[8]  = (u_int)ivec[i][j][0];
            q[9]  = (u_int)ivec[i][j][1];
            q[10] = (u_int)ivec[i][j][2] << 4;
            q[11] = (j < 2) ? 0x8000 : 0;  // ADC on the first two
            ndpkt += 3;
        }
    }

    EndDmaDirectTrans((qword *)&p[ndpkt * 2]);

    eff_wrk.monochro_mode = (mono != 0);
}

// Every overlay record (FF2 10). FF3 0x0015c528.

void SetEneDmgEffect1_Sub(void)
{
    int i;

    for (i = 0; i < ENDMG1_NUM; i++)
        SetEneDmgEffect1_Sub2(i);
}

// ---- the spirit strands ----------------------------------------------------------

// One strand: draw its trail (the ring of the last twelve camera-facing frames,
// 8 units wide at the head tapering to 0), then step it along the spiral from
// bpos1 to bpos2 (a versine of cnt / 180; the clock runs fast in the middle
// and crawls at the end). The stop flag freezes the step, not the draw.
// FF3 differences: GetTrgtRotType2Neg for FF2's GetTrgtRot; twelve ring
// slots read through a 12-entry table (FF2's had 10 entries for 12 slots);
// the swing's sine takes f * pi / 2 directly (FF2 rot_z * 0.5).
// FF3 0x0015c560.

static void SetEneDmgEffect2_Sub2(NEW_PERTICLE *np, float *bpos1, float *bpos2,
                                  u_char r1, u_char g1, u_char b1, u_char r2, u_char g2, u_char b2)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    float        *cam_pos = gra3dcamGetPosition();
    int           tbl[12];
    sceVu0FVECTOR wwpos[36];
    sceVu0FVECTOR wpos[3] = {
        {  5.0f, 0.0f, 0.0f, 1.0f },
        {  0.0f, 0.0f, 0.0f, 1.0f },
        { -5.0f, 0.0f, 0.0f, 1.0f },
    };
    sceVu0FVECTOR opos1 = { 0.0f, 100.0f, 0.0f, 1.0f };
    sceVu0FVECTOR bpos3, wkpos;
    sceVu0FVECTOR rot, rottt;
    sceVu0FMATRIX wlm;
    float         fx, fy, fz, length, r1l, r2l;
    float         s, f2, rot_z;
    int           i, n;

    fx = bpos2[0] - bpos1[0];
    fy = bpos2[1] - bpos1[1];
    fz = bpos2[2] - bpos1[2];
    length = sqrtf(fx * fx + fy * fy + fz * fz);
    r1l = length / 6.0f;
    r2l = (length * 5.0f) / 6.0f;

    n = np->top;
    for (i = 0; i < 12; i++)
    {
        tbl[i] = n;
        n--;
        if (n < 0)
            n = 11;
    }

    // Measured, wrapped and never read, as in FF2.
    GetTrgtRotType2Neg(np->oposp[tbl[1]], np->npos, rot, 3);
    while (rot[0] < -3.1415927f)
        rot[0] += 6.2831855f;
    while (rot[0] >= 3.1415927f)
        rot[0] -= 6.2831855f;
    while (rot[1] < -3.1415927f)
        rot[1] += 6.2831855f;
    while (rot[1] >= 3.1415927f)
        rot[1] -= 6.2831855f;

    GetTrgtRotType2Neg(cam_pos, cam->vTarget, rottt, 3);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixY(wlm, wlm, rottt[1]);
    sceVu0RotMatrixX(wlm, wlm, rottt[0]);
    sceVu0TransMatrix(wlm, wlm, np->npos);
    sceVu0CopyMatrix(np->wmtxp[tbl[0]], wlm);

    n = 0;
    for (i = 0; i < np->num; i++)
    {
        wpos[0][1] = ((float)(np->num - i - 1) * 8.0f) / (float)np->num;
        wpos[2][1] = (-(float)(np->num - i - 1) * 8.0f) / (float)np->num;
        sceVu0ApplyMatrix(wwpos[n * 3 + 0], np->wmtxp[tbl[i]], wpos[0]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 1], np->wmtxp[tbl[i]], wpos[1]);
        sceVu0ApplyMatrix(wwpos[n * 3 + 2], np->wmtxp[tbl[i]], wpos[2]);
        n++;
    }

    DrawNewPerticleSub(n, wwpos, r1, g1, b1, r2, g2, b2, 0x30);

    if (eff_wrk.stop_flg != 0)
        return;

    if (np->cnt < 180.0f)
    {
        np->top = (short)((np->top + 1) % 12);
        np->num = (short)(np->num + 1 > 12 ? 12 : np->num + 1);
    }
    else
    {
        np->num = (short)(np->num - 1 < 0 ? 0 : np->num - 1);
    }

    s = Vu0Sinf((np->cnt * 3.1415927f) / 180.0f);
    f2 = sqrtf(fabsf(1.0f - (1.0f - s) * (1.0f - s)));   // PC: the PS2 sqrt takes |x|
    if (np->cnt < 90.0f)
    {
        np->x = f2 * r1l;
        rot_z = f2 * 1.5707964f;
    }
    else
    {
        f2 = 1.0f - f2;
        np->x = f2 * r2l + r1l;
        rot_z = (f2 + 1.0f) * 1.5707964f;
    }
    np->y = Vu0Sinf(rot_z) * np->n;

    bpos3[0] = bpos1[0] + ((bpos2[0] - bpos1[0]) * np->x) / length;
    bpos3[1] = bpos1[1] + ((bpos2[1] - bpos1[1]) * np->x) / length;
    bpos3[2] = bpos1[2] + ((bpos2[2] - bpos1[2]) * np->x) / length;
    bpos3[3] = 1.0f;

    opos1[0] = 0.0f;
    opos1[1] = np->y;
    opos1[2] = 0.0f;
    opos1[3] = 1.0f;

    rot_z = np->rot + (np->rotp * np->x * 135.0f) / (r1l + r2l);
    while (rot_z < -3.1415927f)
        rot_z += 6.2831855f;
    while (rot_z > 3.1415927f)
        rot_z -= 6.2831855f;

    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixZ(wlm, wlm, rot_z);
    sceVu0ApplyMatrix(opos1, wlm, opos1);

    GetTrgtRotType2Neg(bpos1, bpos2, rot, 3);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixX(wlm, wlm, rot[0]);
    sceVu0RotMatrixY(wlm, wlm, rot[1]);
    sceVu0TransMatrix(wlm, wlm, bpos3);
    sceVu0ApplyMatrix(wkpos, wlm, opos1);

    if (np->cnt < 40.0f)
        np->cnt += np->xp * 3.0f * enedmg2_sp;
    else if (np->cnt < 70.0f)
        np->cnt += np->xp * 10.0f * enedmg2_sp;
    else if (np->cnt < 90.0f)
        np->cnt += np->xp * 15.0f * enedmg2_sp;
    else if (np->cnt < 105.0f)
        np->cnt += (np->xp + np->xp) * enedmg2_sp;
    else if (np->cnt < 120.0f)
        np->cnt += np->xp * enedmg2_sp;
    else if (np->cnt < 180.0f)
    {
        np->cnt += np->xp * 0.5f * enedmg2_sp;
        if (np->cnt >= 180.0f)
            np->cnt = 180.0f;
    }
    else
        np->cnt = 180.0f;

    memcpy(np->oposp[np->top], np->npos, sizeof(sceVu0FVECTOR));
    memcpy(np->npos, wkpos, sizeof(sceVu0FVECTOR));
}

// The strand pass (FF2's). The frame enedmg2_flg turns 1 it seeds
// (dmg * 40 - 400) / 70 + 4 strands at the ghost (4: EfGetDmgOld is 0) and
// steps each once; then every frame it draws and steps them. They fly to a
// point before the player, or before the lens in a camera view (scaled by
// the field of view). Each live head is two camera-facing sprites (effdat
// 0xe and 0x56 + mono, 72 and 20 wide), fading with the clock outside a
// camera view. enedmg_status: 1 while strands trail, 2 (and the pass ends)
// once one has landed and none trails. Monochrome is off for the pass.
// FF3 differences: colour tables in .rodata (0x352ac0 / 0x352ad8), sprites
// 0xe / 0x56 (FF2 0xc / 0x5c), GetTrgtRotType2Neg, the camera-view test.
// FF3 0x0015ce80.
// callcheck: rand (EffEneRand01: ReiPan_Rand)

void SetEneDmgEffect2_Sub(void)
{
    // [chance][strand rgb, alpha, core rgb, alpha] (rdata 0x352ac0 / 0x352ad8).
    static const u_char rgb1[3][8] = {
        { 0x80, 0xa0, 0xff, 0x48, 0x40, 0x60, 0x90, 0x80 },
        { 0xff, 0x32, 0x32, 0x48, 0x90, 0x20, 0x20, 0x80 },
        { 0xff, 0xef, 0xff, 0x48, 0x90, 0x88, 0x90, 0x80 },
    };
    static const u_char rgb2[3][8] = {
        { 0x90, 0xb0, 0xff, 0x00, 0x40, 0x60, 0x90, 0x00 },
        { 0xff, 0x48, 0x48, 0x00, 0x90, 0x20, 0x20, 0x00 },
        { 0xff, 0xef, 0xff, 0x00, 0x90, 0x88, 0x90, 0x00 },
    };
    static sceVu0FVECTOR bpos1;                 // bss 0x3a69c0: the strands' origin
    GRA3DCAMERA  *cam = gra3dGetCamera();
    float        *cam_pos = gra3dcamGetPosition();
    PLYR_WRK     *pw;
    sceVu0FVECTOR bpos2, wpos, rot;
    sceVu0FVECTOR opos1 = { 0.0f, 100.0f, 0.0f, 1.0f };
    sceVu0FVECTOR ppp2  = { -45.0f, 24.0f, 80.0f, 1.0f };
    sceVu0FVECTOR ppp   = { -22.0f, -690.0f, 80.0f, 1.0f };
    sceVu0FMATRIX wlm;
    u_long        de[6];
    float         length, r1l, r2l, span1, span2, f, s, rot_z, rot_x, rot_y;
    int           mono, fl, c, nyoro_num, i, j, k;

    if (eff_wrk.stop_flg != 0 || enedmg2.enedmg2_flg == 0)
        return;

    mono = eff_wrk.monochro_mode;
    ppp2[0] *= cam->fFov / 0.89f;
    ppp2[1] *= cam->fFov / 0.89f;
    eff_wrk.monochro_mode = 0;

    fl = enedmg2.enedmg2_flg == 1;
    if (fl)
    {
        memcpy(bpos1, EfGetMpos(enedmg2.enedmg_no, 0), sizeof(sceVu0FVECTOR));
        enedmg2.enedmg2_flg = 2;
    }

    c = enedmg2.enedmg_chance;

    nyoro_num = EfGetDmgOld(enedmg2.enedmg_no);
    if (nyoro_num < 10)
        nyoro_num = 10;
    else if (nyoro_num > 80)
        nyoro_num = 80;
    nyoro_num = (nyoro_num * 40 - 400) / 70 + 4;

    pw = ingame_wrk.plyr_wrk;
    sceVu0UnitMatrix(wlm);
    if (!EffEnePlyrIsFinderView())
    {
        sceVu0RotMatrixX(wlm, wlm, pw->rot[0]);
        sceVu0RotMatrixY(wlm, wlm, pw->rot[1]);
        sceVu0TransMatrix(wlm, wlm, pw->pos);
        sceVu0ApplyMatrix(bpos2, wlm, ppp);
    }
    else
    {
        GetTrgtRotType2Neg(cam_pos, cam->vTarget, rot, 3);
        sceVu0RotMatrixX(wlm, wlm, rot[0]);
        sceVu0RotMatrixY(wlm, wlm, rot[1]);
        sceVu0TransMatrix(wlm, wlm, cam_pos);
        sceVu0ApplyMatrix(bpos2, wlm, ppp2);
    }

    length = sqrtf((bpos2[0] - bpos1[0]) * (bpos2[0] - bpos1[0])
                   + (bpos2[1] - bpos1[1]) * (bpos2[1] - bpos1[1])
                   + (bpos2[2] - bpos1[2]) * (bpos2[2] - bpos1[2]));
    r1l   = length / 6.0f;
    r2l   = (5.0f * length) / 6.0f;
    span1 = length / 10.0f;
    span2 = span1 + 50.0f;

    if (fl)
    {
        for (i = 0; i < nyoro_num; i++)
        {
            NEW_PERTICLE *np = &new_perticle[i];

            memcpy(np->npos, bpos1, sizeof(sceVu0FVECTOR));
            np->oposp = enedmg2_tail[i].opos;
            np->wmtxp = enedmg2_tail[i].wmtx;
            np->rot   = EffEneRand01() * 6.2831855f - 3.1415927f;
            np->rotp  = EffEneRand01() * 0.034906585f - 0.008726646f;
            np->n     = span1 * EffEneRand01() + span2;
            np->time  = 1;
            np->top   = 1;
            np->cnt   = 3.0f;
            np->num   = 1;
            np->xp    = EffEneRand01() * 0.3f + 0.9f;

            // The step SetEneDmgEffect2_Sub2 makes, run once at cnt 3.
            s = Vu0Sinf(0.05235988f);
            f = sqrtf(fabsf(1.0f - (1.0f - s) * (1.0f - s)));   // PC: the PS2 sqrt takes |x|
            np->x = f * r1l;
            np->y = Vu0Sinf(f * 1.5707964f) * np->n;

            wpos[0] = bpos1[0] + ((bpos2[0] - bpos1[0]) * np->x) / length;
            wpos[1] = bpos1[1] + ((bpos2[1] - bpos1[1]) * np->x) / length;
            wpos[2] = bpos1[2] + ((bpos2[2] - bpos1[2]) * np->x) / length;
            wpos[3] = 1.0f;

            opos1[0] = 0.0f;
            opos1[1] = np->y;
            opos1[2] = 0.0f;
            opos1[3] = 1.0f;

            rot_z = np->rot + (np->rotp * np->x * 135.0f) / (r1l + r2l);
            while (rot_z < -3.1415927f)
                rot_z += 6.2831855f;
            while (rot_z > 3.1415927f)
                rot_z -= 6.2831855f;

            sceVu0UnitMatrix(wlm);
            sceVu0RotMatrixZ(wlm, wlm, rot_z);
            sceVu0ApplyMatrix(opos1, wlm, opos1);

            GetTrgtRotType2Neg(bpos1, bpos2, rot, 3);
            sceVu0UnitMatrix(wlm);
            sceVu0RotMatrixX(wlm, wlm, rot[0]);
            sceVu0RotMatrixY(wlm, wlm, rot[1]);
            sceVu0TransMatrix(wlm, wlm, wpos);
            sceVu0ApplyMatrix(wpos, wlm, opos1);

            np->cnt += np->xp * enedmg2_sp;

            memcpy(np->oposp[0], np->npos, sizeof(sceVu0FVECTOR));
            memcpy(np->npos, wpos, sizeof(sceVu0FVECTOR));
        }
    }

    j = -1;
    k = -1;
    for (i = 0; i < nyoro_num; i++)
    {
        NEW_PERTICLE *np = &new_perticle[i];

        if ((float)np->time != 1.0f)    // an int compared in float, as in the ROM
            continue;

        if (np->num > 0)
        {
            k++;
            SetEneDmgEffect2_Sub2(np, bpos1, bpos2, rgb2[c][0], rgb2[c][1], rgb2[c][2],
                                  rgb2[c][4], rgb2[c][5], rgb2[c][6]);
        }

        if (np->cnt >= 180.0f)
        {
            j = i;
            continue;
        }

        de[0] = 0x161;                  // TEX1
        de[1] = 0x48;                   // ALPHA: additive
        de[2] = 0x10a000118ULL;         // ZBUF: no depth write
        de[3] = 0x5000d;                // TEST
        de[4] = 0;                      // CLAMP
        de[5] = 0x302a400000008004ULL;  // the strip's GIF tag

        Get2PosRot(cam_pos, cam->vTarget, &rot_x, &rot_y);
        sceVu0UnitMatrix(wlm);
        sceVu0RotMatrixX(wlm, wlm, rot_x);
        sceVu0RotMatrixY(wlm, wlm, rot_y);
        sceVu0TransMatrix(wlm, wlm, np->npos);

        f = 1.0f;
        if (!EffEnePlyrIsFinderView())
            f = 1.0f - np->cnt / 180.0f;

        Set3DPosTexure(72.0f, 72.0f, wlm, de, 0xe, rgb1[c][0], rgb1[c][1], rgb1[c][2],
                       (u_char)((int)(f * 16.0f) & 0xff));
        Set3DPosTexure(20.0f, 20.0f, wlm, de, 0x56, rgb1[c][4], rgb1[c][5], rgb1[c][6],
                       (u_char)((int)(f * 110.0f) & 0xff));
    }

    if (j != -1)
    {
        if (k != -1)
            enedmg_status = 1;
        if (k == -1)
        {
            enedmg_status = 2;
            enedmg2.enedmg2_flg = 0;
        }
    }

    eff_wrk.monochro_mode = (mono != 0);
}

// Arms the strand pass (the frame an overlay's quad 0 starts its bloom).
// FF3 0x0015d8c8.

void SetEneDmgEffect2(void)
{
    enedmg2.enedmg2_flg = 1;
}

// ---- work init / release ------------------------------------------------------------

// FF3's burst cell is the 0x50 ENEDMG_P_WRK (FF2 0xf50). FF3 0x0015d8d8.

static void EneDmgParticleWorkInit(void)
{
    SingleLinkListInit(&EneParticleList, sizeof(ENEDMG_P_WRK));
}

// FF3 0x0015d8f8.

static void EneDmgParticleWorkRelease(void)
{
    EneDmgParticleListCutAll(&EneParticleList);
}

// FF3 0x0015d918.

static void EffectEndParticleWorkInit(void)
{
    SingleLinkListInit(&EffectEndParticleList, sizeof(ENEDMG_P_WRK));
}

// FF3 0x0015d938.

static void EffectEndParticleWorkRelease(void)
{
    EneDmgParticleListCutAll(&EffectEndParticleList);
}

// FF3 0x0015d958.

static void EneDmgScreenWorkInit(void)
{
    memset(&ene_dmg_eff, 0, sizeof(ene_dmg_eff));
}

// FF3 0x0015d980.

static void EneHitEffectCtrlInit(void)
{
    memset(&EneHitEffectCtrl, 0, sizeof(EneHitEffectCtrl));
}

// ---- the camera hit's screen half ----------------------------------------------

// Starts the screen half of a camera hit at the ghost's point: label picks
// the blur / contrast preset and the flashes (EneHitEffectSubFuncReq); a
// label past 0x19 becomes 0. One controller, so a second hit restarts it.
// FF3 0x0015d9a8; FF2 EneHitEffectReq.

void EneHitEffectReq(int ene_no, float *mpos, int label)
{
    ENE_HIT_EFFECT_CTRL *pCtrl = &EneHitEffectCtrl;

    if ((u_int)label > ENE_HIT_EFFECT_NUM - 1)
        label = 0;

    pCtrl->EneMposP0[0] = mpos[0];
    pCtrl->EneMposP0[1] = mpos[1];
    pCtrl->EneMposP0[2] = mpos[2];
    pCtrl->EneMposP0[3] = mpos[3];
    pCtrl->EneWrkNo = ene_no;
    pCtrl->pBlurContrast = EffEneDmgLargeHitBlurParameterPtrGet(label);
    pCtrl->HitEffecType = label;
    pCtrl->Flow = 1;
    pCtrl->BlurAlpha = 0;
    pCtrl->Counter = 0;
    pCtrl->ContrastColor = 0;
    pCtrl->ContrastAlpha = 0;
    pCtrl->BlurScale = 0;
    pCtrl->BlurRot = 0;
}

// The hit's rumble, FF2's SetVibrate(0, 30, 1) / SetVibrate(1, 40, 255)
// inlined in FF3 against pad 0's motor work (0x329750 / 0x329754 /
// 0x329758), each behind the option's vibration switch. FF3 fetches the
// option work through the cached pointer sdata 0x372a04 (player.c's); the
// port asks GetOptionWrk each time, the same pointer.

static void EffEneHitVibrate(void)
{
    if (GetOptionWrk()->now.vibration != 0)
        pad_wrk[0].vib_small_time = 30;
    if (GetOptionWrk()->now.vibration != 0)
    {
        pad_wrk[0].vib_big_pow = 0xff;
        pad_wrk[0].vib_big_time = 40;
    }
    ReiPan_HapticCue(REIPAN_CUE_SHOT, 1.0f);    // PC
}

// The screen half per frame. Flow 1 waits SEC0 frames and raises the
// flashes on frame SEC0, 2 runs the blur / contrast ramp with the camera
// shake and the rumble at their frames, 3 resets; from Flow 2 on the blur
// (SetEffects 3, centred on the ghost's screen point) and the contrast
// (SetEffects 0xb) are requested every frame their preset switches on.
// FF3 differences: the two FF2 statics are out of line (0x2b0358 /
// 0x2b0538), the blur takes its alpha by value after a type 1 (FF2 a
// pointer), the contrast is id 0xb (FF2 0xd), the shake is
// IgEffectZeroCamFreqReq (FF2 ReqFreqCamera) and the rumble is inlined.
// FF3 0x0015da20; FF2 EneHitEffectMain.
// callcheck: fptodp, GetOptionWrk (the (double) arguments; the rumble, EffEneHitVibrate here, is inlined in FF3)

void EneHitEffectMain(void)
{
    ENE_HIT_EFFECT_CTRL *pCtrl = &EneHitEffectCtrl;
    ENE_DMG_BLUR_CONTRAST_PARAMETER *p;
    int NowTime;
    float tx;
    float ty;

    switch (pCtrl->Flow)
    {
    case 0:
        return;

    case 1:
        if (pCtrl->Counter == SEC0)
            EneHitEffectSubFuncReq(pCtrl->EneWrkNo, pCtrl->HitEffecType);
        pCtrl->Counter++;
        if (pCtrl->Counter >= SEC0 + 1)
        {
            pCtrl->Flow++;
            pCtrl->Counter = 0;
        }
        break;

    case 2:
        EneHitEffectBlurContrastCalc(pCtrl);
        pCtrl->Counter++;
        p = pCtrl->pBlurContrast;
        if (p->StartFrame + p->InTime + p->KeepTime + p->OutTime < pCtrl->Counter)
            pCtrl->Flow++;
        NowTime = pCtrl->Counter - p->StartFrame;
        if (p->CameraShakeOnFlg != 0 && NowTime == p->CameraShakeFrame)
            IgEffectZeroCamFreqReq();
        if (p->PadVibrateOnFlg != 0 && NowTime == p->PadVibrateFrame)
            EffEneHitVibrate();
        break;

    case 3:
        pCtrl->Flow = 0;
        pCtrl->Counter = 0;
        break;
    }

    if (pCtrl->Flow < 2)
        return;

    p = pCtrl->pBlurContrast;
    if (p->BlurOnFlg != 0)
    {
        WorldToScreenPos(pCtrl->EneMposP0, &tx, &ty);
        SetEffects(3, 1, 1, (int)pCtrl->BlurAlpha, pCtrl->BlurScale, pCtrl->BlurRot,
                   (double)tx, (double)ty);
    }
    if (p->ContrastOnFlg != 0)
        SetEffects(0xb, 1, pCtrl->ContrastColor, pCtrl->ContrastAlpha);
}

// The ramp over Counter (FF2's inlined static, MioPan's name): before
// StartFrame every output is 0; then in over InTime (a zero InTime is fully
// in), keep, out over OutTime (a zero OutTime is fully out), each half
// written only when its preset switch is on.
// FF3 0x002b0358 (out of line).

static void EneHitEffectBlurContrastCalc(ENE_HIT_EFFECT_CTRL *pCtrl)
{
    ENE_DMG_BLUR_CONTRAST_PARAMETER *p = pCtrl->pBlurContrast;
    int   NowTime;
    float Progress;

    if (pCtrl->Counter < p->StartFrame)
    {
        pCtrl->BlurAlpha = 0;
        pCtrl->ContrastColor = 0;
        pCtrl->ContrastAlpha = 0;
        pCtrl->BlurScale = 0;
        pCtrl->BlurRot = 0;
        return;
    }

    NowTime = pCtrl->Counter - p->StartFrame;
    Progress = 1.0f;                                    // rdata 0x36e078
    if (NowTime < p->InTime)
    {
        if (p->InTime != 0)
            Progress = (float)NowTime / (float)p->InTime;
    }
    else if (NowTime >= p->InTime + p->KeepTime)
    {
        if (p->OutTime == 0)
            Progress = 0.0f;
        else
            Progress = 1.0f - (float)(NowTime - (p->InTime + p->KeepTime)) / (float)p->OutTime;
    }

    if (p->BlurOnFlg != 0)
    {
        pCtrl->BlurScale = (int)((float)(p->MaxBlurScale - p->MinBlurScale) * Progress + (float)p->MinBlurScale);
        pCtrl->BlurRot = (int)((float)(p->MaxBlurRot - p->MinBlurRot) * Progress + (float)p->MinBlurRot);
        pCtrl->BlurAlpha = (u_char)(int)((float)(p->MaxBlurAlpha - p->MinBlurAlpha) * Progress
                                         + (float)p->MinBlurAlpha);
    }
    if (p->ContrastOnFlg != 0)
    {
        pCtrl->ContrastColor = (int)((float)(p->MaxContrastColor - p->MinContrastColor) * Progress
                                     + (float)p->MinContrastColor);
        pCtrl->ContrastAlpha = (int)((float)(p->MaxContrastAlpha - p->MinContrastAlpha) * Progress
                                     + (float)p->MinContrastAlpha);
    }
}

// The hit's flashes, one frame (FF2's inlined static, MioPan's name): both
// flash slots off, then labels 0 / 1 raise preset 0 / 1, label 2 the pair
// 2 + 3, and labels 3..0x19 the camera sub-functions (the three-grade ones
// with grade 0 plain, 1 shutter chance, 2 special). FF3 adds labels
// 0x14..0x19 (IgEffectSubFuncEx1Req / Ex2Req).
// FF3 0x002b0538 (out of line).

static void EneHitEffectSubFuncReq(int EneWrkNo, int HitEffectLabel)
{
    EneDmgLargeHitAllOff();

    switch (HitEffectLabel)
    {
    case 0x00: EneDmgLargeHitReq(0);                    break;
    case 0x01: EneDmgLargeHitReq(1);                    break;
    case 0x02: EneDmgLargeHitReq(2);
               EneDmgLargeHitReq(3);                    break;
    case 0x03: IgEffectSubFuncSlowReq(EneWrkNo);        break;
    case 0x04: IgEffectSubFuncZeroReq(EneWrkNo, 0);     break;
    case 0x05: IgEffectSubFuncZeroReq(EneWrkNo, 1);     break;
    case 0x06: IgEffectSubFuncZeroReq(EneWrkNo, 2);     break;
    case 0x07: IgEffectSubFuncKokuReq(EneWrkNo, 0);     break;
    case 0x08: IgEffectSubFuncKokuReq(EneWrkNo, 1);     break;
    case 0x09: IgEffectSubFuncKokuReq(EneWrkNo, 2);     break;
    case 0x0a: IgEffectSubFuncParalyzeReq(EneWrkNo);    break;
    case 0x0b: IgEffectSubFuncViewReq(EneWrkNo);        break;
    case 0x0c: IgEffectSubFuncMetsuReq(EneWrkNo, 0);    break;
    case 0x0d: IgEffectSubFuncMetsuReq(EneWrkNo, 1);    break;
    case 0x0e: IgEffectSubFuncMetsuReq(EneWrkNo, 2);    break;
    case 0x0f: IgEffectSubFuncRenReq(EneWrkNo, 0);      break;
    case 0x10: IgEffectSubFuncRenReq(EneWrkNo, 1);      break;
    case 0x11: IgEffectSubFuncRenReq(EneWrkNo, 2);      break;
    case 0x12: IgEffectSubFuncTsuiReq(EneWrkNo);        break;
    case 0x13: IgEffectSubFuncFuuReq(EneWrkNo);         break;
    case 0x14: IgEffectSubFuncEx1Req(EneWrkNo, 0);      break;
    case 0x15: IgEffectSubFuncEx1Req(EneWrkNo, 1);      break;
    case 0x16: IgEffectSubFuncEx1Req(EneWrkNo, 2);      break;
    case 0x17: IgEffectSubFuncEx2Req(EneWrkNo, 0);      break;
    case 0x18: IgEffectSubFuncEx2Req(EneWrkNo, 1);      break;
    case 0x19: IgEffectSubFuncEx2Req(EneWrkNo, 2);      break;
    }
}

// ---- the particle bursts --------------------------------------------------------------

// A camera hit's burst (FF2 EneDmgParticleEffectReq): Num particles and as
// many tails on the effect heap, the flight speed from the distance (8 below
// 20, 18 above 80, linear between, / 2500; FF2 0.8..1.8 over 500..2000).
// FF3 differences: the particle array is allocated too (FF2 inlined it), Num
// is clamped below at 0, and a failed tail allocation leaks the particle
// array (the ROM frees a0 = 0 there; kept). The ROM leaves cnt and
// EffectType uninitialised (stack garbage; cnt is the arming clock); the port
// zeroes the local. FF3 0x0015dcc8.
// callcheck: EFFECT_MALLOC, EFFECT_FREE (read as macros)

int EneDmgParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pReq)
{
    ENEDMG_P_WRK pw;
    float dist = pReq->DistPE;

    memset(&pw, 0, sizeof(pw));
    pw.Num = pReq->ParticleNum;
    if (pw.Num < 0)
        pw.Num = 0;
    if (pw.Num > ENEDMG_PARTICLE_MAX)
        pw.Num = ENEDMG_PARTICLE_MAX;

    pw.particle = (ENEDMG_PARTICLE_ONE *)EFFECT_MALLOC(pw.Num * 0x50);
    if (pw.particle == nullptr)
        return 0;
    pw.pTailWrk = (ENEDMG_P_TAIL_WRK *)EFFECT_MALLOC(pw.Num * 0x330);
    if (pw.pTailWrk == nullptr)
        return 0;                       // ROM: EFFECT_FREE(0), the particle array leaks

    memcpy(pw.StartPos, pReq->StartPos, sizeof(pw.StartPos));
    pw.pEndPos = pReq->pEndPos;
    if (dist < 20.0f)                   // rdata 0x36deac..0x36dec0
        pw.SpeedRate = 8.0f;
    else if (dist > 80.0f)
        pw.SpeedRate = 18.0f;
    else
        pw.SpeedRate = (dist - 20.0f) / 6.0f + 8.0f;
    pw.SpeedRate = pw.SpeedRate / 2500.0f;
    pw.SuctionFlg = pReq->SuctionFlg;
    pw.flow = 1;
    pw.Type = 0;

    if (SingleLinkListAddEnd(&EneParticleList, &pw) != nullptr)
        return 1;

    if (pw.particle != nullptr)
    {
        EFFECT_FREE(pw.particle);
        pw.particle = nullptr;
    }
    if (pw.pTailWrk != nullptr)
    {
        EFFECT_FREE(pw.pTailWrk);
        pw.pTailWrk = nullptr;
    }
    return 0;
}

// A ghost's end burst (FF2 EffectEndParticleEffectReq): no tails, speed
// 0.0068 (FF2 1.13), Type 1 in EffectType's colour. FF3 fixes FF2's bug: the
// cell goes onto EffectEndParticleList (FF2 added it to EneParticleList).
// cnt is uninitialised in the ROM, zeroed here. FF3 0x0015de48.
// callcheck: EFFECT_MALLOC, EFFECT_FREE (read as macros)

int EffectEndParticleEffectReq(const ENE_DMG_PARTICLE_REQ *pReq)
{
    ENEDMG_P_WRK pw;

    memset(&pw, 0, sizeof(pw));
    pw.Num = pReq->ParticleNum;
    if (pw.Num < 0)
        pw.Num = 0;
    if (pw.Num > ENEDMG_PARTICLE_MAX)
        pw.Num = ENEDMG_PARTICLE_MAX;

    pw.particle = (ENEDMG_PARTICLE_ONE *)EFFECT_MALLOC(pw.Num * 0x50);
    if (pw.particle == nullptr)
        return 0;

    pw.pTailWrk = nullptr;
    memcpy(pw.StartPos, pReq->StartPos, sizeof(pw.StartPos));
    pw.pEndPos = pReq->pEndPos;
    pw.EffectType = pReq->EffectType;
    pw.SpeedRate = 0.0068f;             // rdata 0x36dec4
    pw.Type = 1;
    pw.SuctionFlg = 0;
    pw.flow = 1;

    if (SingleLinkListAddEnd(&EffectEndParticleList, &pw) != nullptr)
        return 1;

    if (pw.particle != nullptr)
        EFFECT_FREE(pw.particle);
    return 0;
}

// Frees a burst's particle and tail arrays and unlinks its cell (FF2 only
// unlinked: its particles were inline). FF3 0x0015df18.
// callcheck: EFFECT_FREE (read as a macro)

void EneDmgParticleEffectCut(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell)
{
    ENEDMG_P_WRK *pw;

    if (pCell == nullptr)
        return;

    pw = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell);
    if (pw->particle != nullptr)
    {
        EFFECT_FREE(pw->particle);
        pw->particle = nullptr;
    }
    if (pw->pTailWrk != nullptr)
    {
        EFFECT_FREE(pw->pTailWrk);
        pw->pTailWrk = nullptr;
    }
    SingleLinkListRemove(pSLL, pCell);
}

// Every cell of a burst list, the next link taken before the cut.
// FF3 0x0015dfb0.

void EneDmgParticleListCutAll(SINGLE_LINK_LIST *pSLL)
{
    SLL_CELL *pCell = SingleLinkListBeginCell(pSLL);

    while (pCell != nullptr)
    {
        SLL_CELL *pNext = pCell->pNext;

        EneDmgParticleEffectCut(pSLL, pCell);
        pCell = pNext;
    }
}

// Retargets every damage burst (Type 0); the end bursts keep theirs. FF2's.
// FF3 0x0015e008.

void EneDmgParticleEndPosSet(float (*pEndPos)[4])
{
    SLL_CELL *pCell;

    for (pCell = SingleLinkListBeginCell(&EneParticleList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        ENEDMG_P_WRK *pw = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell);

        if (pw->Type == 0)
            pw->pEndPos = pEndPos;
    }
}

// Any damage particle work live (FF2's; IgEffectIsEndParticleSuck, which FF3
// never calls). The ROM tests the list's address before reading it.
// FF3 0x0015e7f0.

int IsActiveEneDmgParticle(void)
{
    return EneParticleList.RegCount != 0;
}

// ---- the damage sprite and the per-frame sweep ---------------------------------------

// The damage sprite's ramp (FF2's ENE_DMG_EFF clock), the damage bursts and
// the large-hit flashes, per frame. flow 0 / 8 idle, 1 the arming delay
// (frame SEC0 raises the large hits; at SEC0 + 5 a type 3 / 4 hit shakes and
// rumbles), 2 resolves each ramp's maximum from the distance and falls into
// 3..7: in over SEC1, hold SEC2 / SEC3, out over SEC4, settle over SEC5.
// From flow 2 on the sprite is requested as a blur (SetEffects 3) and a
// contrast (0xb) at the ghost's screen point.
// FF3 differences: FF3 has no EneDmgScreenEffectReq and nothing else writes
// flow, so the ramp idles at flow 0 (kept as the ROM has it); the ramp
// tables are constants (FF2's alp / scl / rot / ccol / calp: 33 / 118 / 75,
// 1000 / 1080 / 1010, 1800 / 1820 / 1804, 70 / 150 / 90 twice) with the
// distance band 120 (FF2 3000); the large hits are type 3 -> preset 1 and
// type 4 -> presets 2 + 3 (FF2 2 / >= 3), the shake and rumble belong to
// types 3 / 4 (FF2 > 2), and the sprite turns for every type but 3 / 4 (FF2
// < 2); the blur takes its alpha by value after a type 1, the contrast is
// id 0xb (FF2 0xd).
// FF3 0x0015e080; FF2 EneDmgMain.
// callcheck: fptodp, GetOptionWrk (the (double) arguments; the rumble, EffEneHitVibrate here, is inlined in FF3)

void EneDmgMain(void)
{
    ENE_DMG_EFF *de = &ene_dmg_eff;
    int big = (u_int)(de->DmgType - 3) < 2;     // the fatal grades 3 / 4
    SLL_CELL *pCell;
    float tx;
    float ty;

    switch (de->flow)
    {
    case 0:
        de->cnt = 0;
        goto rest;

    case 1:
        de->alp = 33;
        de->scl = 1000;
        de->cntalp = 70;
        de->cntcol = 70;
        de->rot = 1800;
        if (de->cnt == (u_int)SEC0)
        {
            if (de->DmgType == 3)
            {
                EneDmgLargeHitReq(1);
            }
            else if (de->DmgType == 4)
            {
                EneDmgLargeHitReq(2);
                EneDmgLargeHitReq(3);
            }
        }
        de->cnt++;
        if (de->cnt < (u_int)(SEC0 + 5))
            break;
        if (big)
        {
            IgEffectZeroCamFreqReq();
            EffEneHitVibrate();
        }
        de->flow++;
        break;

    case 2:
        de->flow++;
        de->almx = 118 - (int)((de->dist * 43.0f) / 120.0f);    // rdata 0x36dec8 / 0x36ded0
        de->ccmx = 150 - (int)((de->dist * 60.0f) / 120.0f);    // rdata 0x36ded4
        de->scmx = 1080 - (int)((de->dist * 70.0f) / 120.0f);   // rdata 0x36decc
        de->rtmx = 1820 - (int)((de->dist * 16.0f) / 120.0f);   // rdata 0x36ded8
        de->camx = de->ccmx;
        // fall through: the frame that resolves the maxima draws the first step
    case 3:
        de->alp = 33;
        de->scl = (int)((de->scmx - 1000) * de->cnt) / SEC1 + 1000;
        de->rot = 1800;
        if (!big)
            de->rot = (int)((de->rtmx - 1800) * de->cnt) / SEC1 + 1800;
        {
            int dcol = (int)((de->ccmx - 70) * de->cnt);
            int dalp = (int)((de->camx - 70) * de->cnt);

            de->cnt++;
            de->cntcol = dcol / SEC1 + 70;
            de->cntalp = dalp / SEC1 + 70;
        }
        if (de->cnt >= (u_int)SEC1)
        {
            de->cnt = 0;
            de->flow++;
        }
        break;

    case 4:
        de->scl = de->scmx;
        de->alp = (u_char)((int)((de->almx - 33) * de->cnt) / SEC2 + 33);
        de->rot = big ? 1800 : de->rtmx;
        de->cnt++;
        de->cntcol = de->ccmx;
        de->cntalp = de->camx;
        if (de->cnt >= (u_int)SEC2)
            goto hold_end;
        break;

    case 5:
        de->alp = (u_char)de->almx;
        de->scl = de->scmx;
        de->rot = big ? 1800 : de->rtmx;
        de->cnt++;
        de->cntcol = de->ccmx;
        de->cntalp = de->camx;
        if (de->cnt >= (u_int)SEC3)
            goto hold_end;
        break;

    hold_end:
        de->cntalp = de->camx;
        de->cntcol = de->ccmx;
        de->scl = de->scmx;
        de->cnt = 0;
        de->flow++;
        break;

    case 6:
        de->scl = de->scmx;
        de->alp = (u_char)(de->almx - (int)((de->almx - 33) * de->cnt) / SEC4);
        de->rot = big ? 1800 : de->rtmx;
        {
            int dcol = (int)((de->ccmx - 70) * de->cnt);
            int dalp = (int)((de->camx - 70) * de->cnt);

            de->cnt++;
            de->cntcol = de->ccmx - dcol / SEC4;
            de->cntalp = de->camx - dalp / SEC4;
        }
        if (de->cnt >= (u_int)SEC4)
        {
            de->cnt = 0;
            de->flow++;
        }
        break;

    case 7:
        de->alp = 33;
        de->scl = de->scmx - (int)((de->scmx - 1000) * de->cnt) / SEC5;
        if (big)
            de->rot = 1800;
        else
            de->rot = de->rtmx - (int)((de->rtmx - 1800) * de->cnt) / SEC5;
        de->cntalp = 70;
        de->cnt++;
        de->cntcol = 70;
        if (de->cnt >= (u_int)SEC5)
        {
            de->cnt = 0;
            de->flow++;
        }
        break;

    case 8:
        de->flow = 0;
    rest:
        de->cntalp = 70;
        de->rot = 1800;
        de->scl = 1000;
        de->alp = 33;
        de->cntcol = 70;
        break;
    }

    if (de->flow > 1)
    {
        WorldToScreenPos(de->MposP0, &tx, &ty);
        SetEffects(3, 1, 1, (int)de->alp, de->scl, de->rot, (double)tx, (double)ty);
        SetEffects(0xb, 1, de->cntcol, de->cntalp);
    }

    // PC: the ROM reads the next link after EneDmgParticle, which may have
    // freed the cell (harmless on the EE heap); the port takes it first.
    pCell = SingleLinkListBeginCell(&EneParticleList);
    while (pCell != nullptr)
    {
        SLL_CELL *pNext = SingleLinkListNextCell(pCell);

        EneDmgParticle(&EneParticleList, pCell);
        pCell = pNext;
    }

    EneDmgLargeHitCtrlMain();
}

// The effect-end bursts, as FF2. FF3 0x0015e780. PC: the ROM reads the next
// link after EneDmgParticle, which may have freed the cell (harmless on the
// EE heap); the port takes it first.

void EffectEndParticleMain(void)
{
    SLL_CELL *pCell = SingleLinkListBeginCell(&EffectEndParticleList);

    while (pCell != nullptr)
    {
        SLL_CELL *pNext = SingleLinkListNextCell(pCell);

        EneDmgParticle(&EffectEndParticleList, pCell);
        pCell = pNext;
    }
}

// ---- one burst ---------------------------------------------------------------------------

static void EneDmgParticleColorGet(int *Color, int Type, int EffectType);
static float EneDmgParticleSizeWGet(int Type);
static float EneDmgParticleSizeHGet(int Type);
static int  EneDmgParticleAnmCountGet(int Type);
static float EneDmgParticleAlphaGet(int Type);
static void EneDmgParticleOneInit(ENEDMG_PARTICLE_ONE *pOne, float *Center, float SpeedRate, int Type);
static int  EneDmgParticleOneUpdate(ENEDMG_PARTICLE_ONE *pOne);
static void EneDmgParticleOneDraw(ENEDMG_PARTICLE_ONE *pOne, float rot_x, float rot_y,
                                  int r, int g, int b, float AlphaRate, int Type);
static void EneDmgParticleSuctionInit(ENEDMG_P_WRK *pw, float *Start, float *End);
static int  EneDmgParticleSuction(ENEDMG_P_WRK *pw);
static int  EneDmgParticleSuctionOne(ENEDMG_PARTICLE_ONE *pOne, float *StartPos, float *SubVec,
                                     float Length, float LengthDiv6);
static void EneDmgParticleSuctionTailInit(ENEDMG_P_TAIL_WRK *pTail, ENEDMG_PARTICLE_ONE *pOne);
static int  EneDmgParticleSuctionTail(ENEDMG_P_TAIL_WRK *pTail, ENEDMG_PARTICLE_ONE *pOne);

// The burst's colour: an end burst (Type 1) by EffectType (above 4: 0), a
// camera hit the one blue (63, 96, 255). FF3's end colours differ from FF2's
// (rdata 0x352b60). FF3 0x0015e810.

static void EneDmgParticleColorGet(int *Color, int Type, int EffectType)
{
    static const int EffectEndCol[5][4] = {     // rdata 0x352b60
        { 255, 153, 217, 0 },
        {  74, 255,  74, 0 },
        { 100, 220, 255, 0 },
        { 224, 243,  22, 0 },
        { 255, 242, 100, 0 },
    };

    if (EffectType > 4)
        EffectType = 0;
    if (Type == 1)
    {
        Color[0] = EffectEndCol[EffectType][0];
        Color[1] = EffectEndCol[EffectType][1];
        Color[2] = EffectEndCol[EffectType][2];
        Color[3] = EffectEndCol[EffectType][3];
        return;
    }
    Color[0] = 63;
    Color[1] = 96;
    Color[2] = 255;
    Color[3] = 0;
}

// One burst's frame (FF2's): flow 1 a two-frame arming delay, 2 seeds the
// particles (and falls into the bloom), 3 / 4 the bloom over SpreadTime, 5
// the flight home. A bloom that ends without suction, or a flight that has
// landed, frees the burst.
// FF3 differences: SpreadTime 57 for an end burst, 15 for a suction burst,
// 40 for a plain one (FF2 54 / 15); the freed burst returns at once (FF2 drew
// it once more); the particles step and draw from flow 3 (FF2 drew from 2,
// which only the seeding frame reached).
// FF3 0x0015e878; FF2 EneDmgParticle.

void EneDmgParticle(SINGLE_LINK_LIST *pSLL, SLL_CELL *pCell)
{
    ENEDMG_P_WRK *pw = (ENEDMG_P_WRK *)SingleLinkListCellBodyPtr(pCell);
    int   SpreadTime;
    float *cam_dir;
    int   Color[4];
    float rot_x;
    float rot_y;
    float AlphaRate;
    int   i;

    if (pw->flow == -1)
        return;

    if (pw->Type == 1)
        SpreadTime = 57;
    else if (pw->SuctionFlg != 0)
        SpreadTime = 15;
    else
        SpreadTime = 40;

    cam_dir = gra3dcamGetDirection();
    EneDmgParticleColorGet(Color, pw->Type, pw->EffectType);

    switch (pw->flow)
    {
    case 1:
        pw->cnt++;
        if (pw->cnt >= 2)
        {
            pw->cnt = 0;
            pw->flow++;
        }
        break;

    case 2:
        for (i = 0; i < pw->Num; i++)
            EneDmgParticleOneInit(&pw->particle[i], pw->StartPos, pw->SpeedRate, pw->Type);
        pw->cnt = 0;
        pw->flow++;
        // fall through: the seeded particles step on the same frame
    case 3:
    case 4:
        for (i = 0; i < pw->Num; i++)
            EneDmgParticleOneUpdate(&pw->particle[i]);
        pw->cnt++;
        if (pw->cnt >= SpreadTime)
        {
            if (pw->SuctionFlg == 0)
            {
                EneDmgParticleEffectCut(pSLL, pCell);
                return;
            }
            EneDmgParticleSuctionInit(pw, pw->particle[0].rpos, *pw->pEndPos);
            pw->cnt = 0;
            pw->flow = 5;
        }
        break;

    case 5:
        if (eff_wrk.stop_flg != 0)
            break;
        if (EneDmgParticleSuction(pw) == 0)
        {
            EneDmgParticleEffectCut(pSLL, pCell);
            return;
        }
        break;
    }

    if (pw->flow < 3)
        return;

    for (i = 0; i < pw->Num; i++)
    {
        if (pw->particle[i].anm_count < 100)
            pw->particle[i].anm_count++;
    }

    AlphaRate = 1.0f;                   // rdata 0x36dedc
    if (pw->SuctionFlg == 0)
        AlphaRate = (float)(SpreadTime - pw->cnt) / (float)SpreadTime;

    Vector2Rot(cam_dir, &rot_x, &rot_y);
    for (i = 0; i < pw->Num; i++)
        EneDmgParticleOneDraw(&pw->particle[i], rot_x, rot_y, Color[0], Color[1], Color[2], AlphaRate, pw->Type);
}

// The particle sprite's size: FF2's / 25 for a camera hit (21.1 x 21.4),
// larger than that for an end burst (6.6 x 6.3 in FF2). FF2
// EneDmgParticleSizeWGet / SizeHGet.
// FF3 0x0015eb98

static float EneDmgParticleSizeWGet(int Type)
{
    return (Type == 1) ? 0.308f : 0.844f;       // rdata 0x36dee0 / 0x36dee4
}

// FF3 0x0015ebc0

static float EneDmgParticleSizeHGet(int Type)
{
    return (Type == 1) ? 0.3f : 0.856f;         // rdata 0x36dee8 / 0x36deec
}

// The sprite's first animation frame: 20 for an end burst, a camera hit's
// staggered over (-21, -7]. FF2's. FF3 0x0015ebe8.
// callcheck: rand (EffEneRand01)

static int EneDmgParticleAnmCountGet(int Type)
{
    if (Type == 1)
        return 20;
    return (int)(-(EffEneRand01() * 14.0f)) - 7;     // rdata 0x36df3c
}

// The sprite's alpha, both in 0..1 in FF3 (FF2 138 against 0.384: the end
// burst's overflowed). FF3 0x0015ec50.

static float EneDmgParticleAlphaGet(int Type)
{
    return (Type == 1) ? 0.541176438f : 0.384313703f;   // rdata 0x36df40 / 0x36df44 (138 / 255, 98 / 255)
}

// Seeds one particle at Center: a random angle, an outward speed of
// (200 .. 600) * SpeedRate (FF2 SpeedRate * 40 .. 80) braked by 0.82 .. 0.88
// a frame. FF3 0x0015ec78; FF2 EneDmgParticleOneInit.
// callcheck: rand (EffEneRand01)

static void EneDmgParticleOneInit(ENEDMG_PARTICLE_ONE *pOne, float *Center, float SpeedRate, int Type)
{
    pOne->rrad = 0.0f;
    pOne->rrot = EffEneRand01() * 6.28318548f - 3.14159274f;   // rdata 0x36df4c / 0x36df50
    pOne->racc = (EffEneRand01() * 400.0f + 200.0f) * SpeedRate;
    pOne->AlphaRate = 1.0f;
    pOne->rbrk = EffEneRand01() * 0.06f + 0.82f;
    pOne->anm_count = EneDmgParticleAnmCountGet(Type);
    pOne->ralp = EneDmgParticleAlphaGet(Type);
    memcpy(pOne->rpos, Center, sizeof(pOne->rpos));
}

// Steps the bloom; 0 once the speed has decayed to 0.004 (FF2 0.1).
// FF3 0x0015edc0.

static int EneDmgParticleOneUpdate(ENEDMG_PARTICLE_ONE *pOne)
{
    float acc = pOne->racc;

    if (acc <= 0.004f)                  // rdata 0x36df68
        return 0;
    pOne->rrad += acc;
    pOne->racc = acc * pOne->rbrk;
    return 1;
}

// One particle: four camera-facing sprites on one matrix (FF2's inline
// EneDmgParticleOneDraw): the core, the ring ("wakka", over its first 20
// frames) and two crossed bars ("bou", over the first 18), additive. Center
// records where it was drawn. A particle still at a negative anm_count draws
// at the start values.
// FF3 differences: sprites 0x58 / 0x5a / 0x42 (FF2 0x5e / 0x60 / 0x44)
// through Set3DPosTexure2 with mono 0; the alphas go through int.
// FF3 0x002afe30 (out of line).

static void EneDmgParticleOneDraw(ENEDMG_PARTICLE_ONE *pOne, float rot_x, float rot_y,
                                  int r, int g, int b, float AlphaRate, int Type)
{
    u_long de[6] = { 0x161, 0x48, 0x10a000118ULL, 0x5000d, 0, 0x302a400000008004ULL };
    float  SizeW = EneDmgParticleSizeWGet(Type);
    float  SizeH = EneDmgParticleSizeHGet(Type);
    float  zero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    sceVu0FMATRIX wlm;
    sceVu0FMATRIX RotMat;
    sceVu0FMATRIX wlm2;
    float  BouScaleX, BouScaleY, BouAlpha, BouRot;
    float  WakkaScale, WakkaAlpha;
    float  AlphaBase;

    if (!(pOne->ralp > 0.0f))
        return;

    AlphaBase = AlphaRate * 255.0f;                 // rdata 0x36def8
    BouScaleX = 0.60999995f;                        // rdata 0x36defc
    BouScaleY = 2.98999977f;                        // rdata 0x36df00
    BouAlpha = 0.0f;
    BouRot = 0.0f;
    WakkaScale = 0.0999999940f;                     // rdata 0x36def4
    WakkaAlpha = 0.0f;
    if (pOne->anm_count >= 0)
    {
        float n = (float)pOne->anm_count;

        if (pOne->anm_count < 18)
        {
            BouAlpha = (n * -0.429999977f) / 18.0f + 0.429999977f;
            BouScaleY = (n * -2.87999964f) / 18.0f + 2.98999977f;
            BouScaleX = (n * -0.49999994f) / 18.0f + 0.60999995f;
            BouRot = (n * 2.93215275f) / 18.0f + 0.0f;
        }
        else
        {
            BouScaleX = 0.11f;                      // rdata 0x36df1c
            BouScaleY = 0.11f;
            BouRot = 2.93215275f;                   // rdata 0x36df14
        }
        if (pOne->anm_count < 20)
        {
            WakkaScale = (n * 3.84999967f) / 20.0f + 0.0999999940f;
            WakkaAlpha = (n * -0.669999957f) / 20.0f + 0.669999957f;
        }
        else
        {
            WakkaScale = 3.94999981f;               // rdata 0x36df30
        }
    }

    zero[0] = pOne->rrad;
    sceVu0UnitMatrix(wlm);
    sceVu0TransMatrix(wlm, wlm, zero);
    sceVu0RotMatrixZ(wlm, wlm, pOne->rrot);
    sceVu0RotMatrixX(wlm, wlm, rot_x);
    sceVu0RotMatrixY(wlm, wlm, rot_y);
    sceVu0TransMatrix(wlm, wlm, pOne->rpos);
    memcpy(pOne->Center, wlm[3], sizeof(pOne->Center));

    Set3DPosTexure2(SizeW, SizeH, wlm, de, 0x58, (u_char)r, (u_char)g, (u_char)b,
                    (u_char)(int)(AlphaBase * pOne->ralp), 0);
    Set3DPosTexure2(SizeW * WakkaScale, SizeH * WakkaScale, wlm, de, 0x5a, (u_char)r, (u_char)g, (u_char)b,
                    (u_char)(int)(AlphaBase * pOne->ralp * WakkaAlpha), 0);

    sceVu0UnitMatrix(RotMat);
    sceVu0RotMatrixZ(RotMat, RotMat, BouRot);
    sceVu0MulMatrix(wlm2, wlm, RotMat);
    Set3DPosTexure2(SizeW * BouScaleX, SizeH * BouScaleY, wlm2, de, 0x42, (u_char)r, (u_char)g, (u_char)b,
                    (u_char)(int)(AlphaBase * pOne->ralp * BouAlpha), 0);

    sceVu0UnitMatrix(RotMat);
    sceVu0RotMatrixZ(RotMat, RotMat, BouRot + 1.57079637f);    // rdata 0x36df34
    sceVu0MulMatrix(wlm2, wlm, RotMat);
    Set3DPosTexure2(SizeW * BouScaleX, SizeH * BouScaleY, wlm2, de, 0x42, (u_char)r, (u_char)g, (u_char)b,
                    (u_char)(int)(AlphaBase * pOne->ralp * BouAlpha), 0);
}

// ---- the flight home ----------------------------------------------------------------------

// Arms every particle for the flight: the clock from 0.556 (FF2 13.9) at
// 6.0 .. 6.3 a step (FF2 3.8 .. 4.1), a spin of -0.5 .. 1.5 degrees, the
// bloom radius latched, Center placed and the tail seeded.
// FF3 0x0015ee08; FF2 EneDmgParticleSuctionInit.
// callcheck: rand (EffEneRand01)

static void EneDmgParticleSuctionInit(ENEDMG_P_WRK *pw, float *Start, float *End)
{
    float  zero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };    // rdata 0x36df6c
    float *cam_dir = gra3dcamGetDirection();
    sceVu0FMATRIX wlm;
    float  rot_x;
    float  rot_y;
    int    i;

    memcpy(pw->StartPos, Start, sizeof(pw->StartPos));
    memcpy(pw->EndPos, End, sizeof(pw->EndPos));
    Vector2Rot(cam_dir, &rot_x, &rot_y);

    for (i = 0; i < pw->Num; i++)
    {
        ENEDMG_PARTICLE_ONE *p = &pw->particle[i];

        p->cnt_f = 0.555999994f;                                    // rdata 0x36df74
        p->cnt_spd = (EffEneRand01() * 750.0f + 15000.0f) / 2500.0f; // rdata 0x36df78..0x36df80
        p->rrad_max = p->rrad;
        p->rot_spd = EffEneRand01() * 0.0349065848f - 0.00872664619f;
        zero[0] = p->rrad;
        sceVu0UnitMatrix(wlm);
        sceVu0TransMatrix(wlm, wlm, zero);
        sceVu0RotMatrixZ(wlm, wlm, p->rrot);
        sceVu0RotMatrixX(wlm, wlm, rot_x);
        sceVu0RotMatrixY(wlm, wlm, rot_y);
        sceVu0TransMatrix(wlm, wlm, p->rpos);
        memcpy(p->Center, wlm[3], sizeof(p->Center));
        EneDmgParticleSuctionTailInit(&pw->pTailWrk[i], p);
    }
}

// The flight's frame: re-reads the destination, steps every particle and
// its tail. Each particle that lands this frame counts in
// EneDmgParticleSuctionNum and charges the lens. Returns 1 while a tail is
// still out; on 0 the lens' charge sound ends.
// FF3 differences: the lens charge (LensAddCharge 9) per landing and
// LensSeEnd at the end (FF2 left the banking to CNEquipTrayWrk).
// FF3 0x0015f050; FF2 EneDmgParticleSuction.

static int EneDmgParticleSuction(ENEDMG_P_WRK *pw)
{
    float SubVec[4];
    float Length;
    float r1l;
    int   end_count = 0;
    int   ret = 0;
    int   i;

    memcpy(pw->EndPos, *pw->pEndPos, sizeof(pw->EndPos));
    {
        float dx = pw->EndPos[0] - pw->StartPos[0];
        float dy = pw->EndPos[1] - pw->StartPos[1];
        float dz = pw->EndPos[2] - pw->StartPos[2];

        Length = sqrtf(dz * dz + dy * dy + dx * dx);
    }
    r1l = Length / 6.0f;                // rdata 0x36df8c
    sceVu0SubVector(SubVec, pw->EndPos, pw->StartPos);

    EneDmgParticleSuctionNum = 0;
    if (pw->Num > 0)
    {
        for (i = 0; i < pw->Num; i++)
        {
            if (EneDmgParticleSuctionOne(&pw->particle[i], pw->StartPos, SubVec, Length, r1l) == 1)
            {
                EneDmgParticleSuctionNum++;
                GetLensWrk()->AddCharge(9, 0xff);   // 0xff: the current player's lens
            }
            if (EneDmgParticleSuctionTail(&pw->pTailWrk[i], &pw->particle[i]) != 0)
                end_count++;
        }
        ret = end_count < pw->Num;
    }
    if (!ret)
        GetLensWrk()->SeEnd();
    return ret;
}

// One particle's flight (FF2's): f2 = 1 - cos(cnt_f / 90 * pi / 2) eases
// it along the line, the radius follows sin((f2 + 1) * pi / 2), the spin
// scales with f2; the clock runs double below 15, single below 30, half
// below 90. Returns 1 the frame it lands, 2 when found landed, else 0.
// FF3 differences: Length is held at 0.0001 or more in the divisions;
// Vu0Sinf.
// FF3 0x0015f1f8; FF2 EneDmgParticleSuctionOne.

static int EneDmgParticleSuctionOne(ENEDMG_PARTICLE_ONE *pOne, float *StartPos, float *SubVec,
                                    float Length, float LengthDiv6)
{
    const float pi_2 = 1.57079637f;     // rdata 0x36df94
    float L = (Length >= 0.0001f) ? Length : 0.0001f;  // rdata 0x36df90
    float s;
    float f2;
    float PosRate;
    float spin;
    float rot;
    float cnt;
    int   ret = 0;

    s = Vu0Sinf((pOne->cnt_f * pi_2) / 90.0f);
    f2 = 1.0f - sqrtf(fabsf(1.0f - s * s));   // PC: the PS2 sqrt takes |x|
    PosRate = f2 * L;

    pOne->rrad = Vu0Sinf((f2 + 1.0f) * pi_2) * pOne->rrad_max;
    spin = pOne->rot_spd * f2 * LengthDiv6 * 135.0f;  // rdata 0x36dfa4
    rot = pOne->rrot + (spin / 5.0f) / L;
    pOne->rrot = rot;
    if (rot < -3.14159274f)
    {
        do
            rot += 6.28318548f;
        while (rot < -3.14159274f);
        pOne->rrot = rot;
    }
    rot = pOne->rrot;
    if (rot > 3.14159274f)
    {
        do
            rot -= 6.28318548f;
        while (rot > 3.14159274f);
        pOne->rrot = rot;
    }

    pOne->rpos[0] = StartPos[0] + (SubVec[0] * PosRate) / L;
    pOne->rpos[1] = StartPos[1] + (SubVec[1] * PosRate) / L;
    pOne->rpos[2] = StartPos[2] + (SubVec[2] * PosRate) / L;
    pOne->rpos[3] = 1.0f;

    cnt = pOne->cnt_f;
    if (cnt < 15.0f)
    {
        pOne->cnt_f = cnt + (pOne->cnt_spd + pOne->cnt_spd) * enedmg2_sp;
    }
    else if (cnt < 30.0f)
    {
        pOne->cnt_f = cnt + pOne->cnt_spd * enedmg2_sp;
    }
    else if (cnt < 90.0f)
    {
        pOne->cnt_f = cnt + pOne->cnt_spd * 0.5f * enedmg2_sp;
    }
    else
    {
        pOne->cnt_f = 90.0f;
        pOne->ralp = 0.0f;
        ret = 2;
    }

    if (pOne->cnt_f >= 90.0f)
    {
        pOne->cnt_f = 90.0f;
        pOne->ralp = 0.0f;
        if (ret == 0)
            ret = 1;
    }
    return ret;
}

// Seeds the tail: ten slots collapsed onto the particle with unit matrices.
// FF2's. FF3 0x0015f4f8.

static void EneDmgParticleSuctionTailInit(ENEDMG_P_TAIL_WRK *pTail, ENEDMG_PARTICLE_ONE *pOne)
{
    int i;

    pTail->Top = 1;
    pTail->NumMax = 10;
    pTail->Num = 1;
    for (i = 0; i < pTail->NumMax; i++)
    {
        sceVu0UnitMatrix(pTail->LwMatrix[i]);
        memcpy(pTail->OldPos[i], pOne->Center, sizeof(pTail->OldPos[i]));
    }
}

// Advances the tail and draws it (FF2 EneDmgParticleSuctionTail): the newest
// slot takes a camera-facing matrix at the particle, each slot gives three
// points 0.356 apart (FF2 8.9) and the ribbon goes out pale blue at alpha
// 25. Once the particle has landed the ring stops and Num winds down;
// returns 1 when it reaches 0 (or for a null argument). The heading from
// the previous slot is computed, wrapped and never used, as in FF2.
// FF3 differences: GetTrgtRotType2Neg for GetTrgtRot.
// FF3 0x0015f588.

static int EneDmgParticleSuctionTail(ENEDMG_P_TAIL_WRK *pTail, ENEDMG_PARTICLE_ONE *pOne)
{
    static const float wpos[3][4] = {               // the stack copy at sp+0x50
        {  0.356f, 0.0f, 0.0f, 1.0f },
        {  0.0f,   0.0f, 0.0f, 1.0f },
        { -0.356f, 0.0f, 0.0f, 1.0f },
    };
    GRA3DCAMERA  *cam = gra3dGetCamera();
    float        *cam_pos = gra3dcamGetPosition();
    sceVu0FVECTOR wwpos[30];
    sceVu0FMATRIX wlm;
    float         rot[4];
    float         rottt[4];
    int           tbl[12];
    int           i, n, num;

    if (pTail == nullptr || pOne == nullptr)
        return 1;

    n = pTail->Top;
    for (i = 0; i < pTail->NumMax; i++)
    {
        tbl[i] = n;
        n--;
        if (n < 0)
            n = pTail->NumMax - 1;
    }

    GetTrgtRotType2Neg(pTail->OldPos[tbl[1]], pOne->Center, rot, 3);   // dead, as in FF2
    while (rot[0] < -3.14159274f) rot[0] += 6.28318548f;              // rdata 0x36dfc0..0x36dfc8
    while (rot[0] >= 3.14159274f) rot[0] -= 6.28318548f;
    while (rot[1] < -3.14159274f) rot[1] += 6.28318548f;
    while (rot[1] >= 3.14159274f) rot[1] -= 6.28318548f;

    GetTrgtRotType2Neg(cam_pos, cam->vTarget, rottt, 3);
    sceVu0UnitMatrix(wlm);
    sceVu0RotMatrixY(wlm, wlm, rottt[1]);
    sceVu0RotMatrixX(wlm, wlm, rottt[0]);
    sceVu0TransMatrix(wlm, wlm, pOne->Center);
    sceVu0CopyMatrix(pTail->LwMatrix[tbl[0]], wlm);

    num = 0;
    for (i = 0; i < pTail->Num; i++)
    {
        sceVu0ApplyMatrix(wwpos[num * 3 + 0], pTail->LwMatrix[tbl[i]], (float *)wpos[0]);
        sceVu0ApplyMatrix(wwpos[num * 3 + 1], pTail->LwMatrix[tbl[i]], (float *)wpos[1]);
        sceVu0ApplyMatrix(wwpos[num * 3 + 2], pTail->LwMatrix[tbl[i]], (float *)wpos[2]);
        num++;
    }
    DrawNewPerticleSub(num, wwpos, 0x79, 0xae, 0xff, 0x79, 0xae, 0xff, 25);

    if (pOne->cnt_f < 90.0f)            // rdata 0x36dfcc
    {
        pTail->Top = (pTail->Top + 1) % pTail->NumMax;
        pTail->Num = (pTail->Num + 1 < pTail->NumMax) ? pTail->Num + 1 : pTail->NumMax;
        return 0;
    }
    pTail->Num = (pTail->Num - 1 < 0) ? 0 : pTail->Num - 1;
    return pTail->Num == 0;
}

// ---- the ribbon ---------------------------------------------------------------------------

// A trail as two gouraud strips (FF2's): pos is num groups of (left, centre,
// right); pass 0 draws (left, centre), pass 1 (centre, right), the centre in
// r1 g1 b1 with alpha (num - i - 1) * a / num (times i / num again outside a
// camera view, so it fades at both ends), the edges r2 g2 b2 at alpha 0. A
// triangle is kicked only when its three corners pass the guard band; eight
// REGLIST vertices fill a GIF tag. FF3 differences: the camera-view test.
// FF3 sizes the work arrays for 30 points but SetEneDmgEffect2_Sub2 passes
// up to 36 (12 groups); the port sizes them 36. FF3 0x0015f948.

void DrawNewPerticleSub(int num, sceVu0FVECTOR *pos, u_char r1, u_char g1, u_char b1,
                        u_char r2, u_char g2, u_char b2, u_char a)
{
    GRA3DCAMERA  *cam = gra3dGetCamera();
    sceVu0IVECTOR ivec[36];
    int           clip[36];
    u_long        xyzf[36];
    u_long        rgbaq1, rgbaq2, reg;
    u_long       *p;
    int           i, j, k, alp, cl, tagnum, cnt, ndpkt;

    if (num == 0)
        return;

    if (eff_wrk.monochro_mode != 0)
    {
        r2 = g2 = b2 = (u_char)(((u_int)r2 + g2 + b2) / 3);
        r1 = g1 = b1 = (u_char)(((u_int)r1 + g1 + b1) / 3);
    }
    rgbaq2 = (u_long)r2 | ((u_long)g2 << 8) | ((u_long)b2 << 16);

    sceVu0RotTransPersN(ivec, cam->matWorldScreen, pos, num * 3, 1);
    for (i = 0; i < num * 3; i++)
    {
        clip[i] = 1;
        if ((u_int)(ivec[i][0] - 0x80) > 0xff00)
            clip[i] = 0;
        if ((u_int)(ivec[i][1] - 0x80) > 0xff00)
            clip[i] = 0;
        if ((u_int)(ivec[i][2] - 1) > 0xfffffe)
            clip[i] = 0;
        xyzf[i] = (u_long)(u_int)ivec[i][0] | ((u_long)(u_int)ivec[i][1] << 16)
                | ((u_long)(u_int)ivec[i][2] << 32);
    }

    {
        GS_REG_SET regs[5];

        regs[0].data = 0x48;            regs[0].reg = SCE_GS_ALPHA_1;
        regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
        regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
        regs[3].data = 0x50003;         regs[3].reg = SCE_GS_TEST_1;
        regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
        SetGsRegsCached(regs, 5, 1);
    }

    DrawNewPerticleSubHost(num, pos, clip, r1, g1, b1, r2, g2, b2, a);

    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x2400400000008001ULL;       // 1 loop, PRIM, REGLIST (PRIM, NOP)
    p[1] = 0xf0;
    p[2] = 0x14c;                       // TRIANGLE_STRIP, IIP, ABE
    p[3] = 0;
    tagnum = 2;
    ndpkt = 3;
    reg = 0;
    cnt = 0;

    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < num; i++)
        {
            alp = ((num - 1 - i) * a) / num;
            if (!EffEnePlyrIsFinderView())
                alp = (i * alp) / num;

            k = j + i * 3;
            rgbaq1 = (u_long)r1 | ((u_long)g1 << 8) | ((u_long)b1 << 16) | ((u_long)alp << 24);

            p[ndpkt * 2 + 0] = (j == 0) ? rgbaq2 : rgbaq1;
            p[ndpkt * 2 + 1] = xyzf[k];
            ndpkt++;
            cl = (i == 0) ? 0 : clip[k - 3] + clip[k - 2] + clip[k];
            reg |= (u_long)(cl >= 3 ? 0x41 : 0xc1) << (cnt * 8);
            cnt++;

            p[ndpkt * 2 + 0] = (j == 0) ? rgbaq1 : rgbaq2;
            p[ndpkt * 2 + 1] = xyzf[k + 1];
            ndpkt++;
            cl = (i == 0) ? 0 : clip[k - 2] + clip[k] + clip[k + 1];
            reg |= (u_long)(cl >= 3 ? 0x41 : 0xc1) << (cnt * 8);
            cnt++;

            if (cnt >= 8)
            {
                p[tagnum * 2 + 0] = ((u_long)(cnt * 2) << 60) | 0x0400400000008001ULL;
                p[tagnum * 2 + 1] = reg;
                tagnum = ndpkt++;
                reg = 0;
                cnt = 0;
            }
        }
    }

    if (cnt > 0)
    {
        p[tagnum * 2 + 0] = ((u_long)(cnt * 2) << 60) | 0x0400400000008001ULL;
        p[tagnum * 2 + 1] = reg;
    }
    else
    {
        ndpkt--;
    }

    EndDmaDirectTrans((qword *)&p[ndpkt * 2]);
}

// PORT-ONLY. The ribbon as a triangle list (MioPan's): each strip rebuilt in
// the ROM's emission order, a triangle kept exactly when the REGLIST would
// kick it (its three corners inside the guard band). The centre carries the
// taper, the edges alpha 0, which is the whole look.

static void DrawNewPerticleSubHost(int num, sceVu0FVECTOR *pos, const int *clip, u_char rr1, u_char gg1,
                                   u_char bb1, u_char rr2, u_char gg2, u_char bb2, u_char a)
{
    static float  tri_pos[144][4];
    static u_char tri_col[144][4];
    float  sv[24][4];
    u_char sc[24][4];
    int    sk[24];
    int    nvtx = 0;
    int    i, j, e, t, c;

    for (j = 0; j < 2; j++)
    {
        int nv = 0;

        for (i = 0; i < num; i++)
        {
            int alpv = ((num - 1 - i) * a) / num;

            if (!EffEnePlyrIsFinderView())
                alpv = (i * alpv) / num;

            for (e = 0; e < 2; e++)
            {
                int kk = j + i * 3 + e;

                memcpy(sv[nv], pos[kk], sizeof(sv[nv]));
                if (e == j)             // the rgbaq2 side: no alpha
                {
                    sc[nv][0] = rr2; sc[nv][1] = gg2; sc[nv][2] = bb2; sc[nv][3] = 0;
                }
                else                    // the rgbaq1 side: the taper
                {
                    sc[nv][0] = rr1; sc[nv][1] = gg1; sc[nv][2] = bb1; sc[nv][3] = (u_char)alpv;
                }
                sk[nv] = clip[kk];
                nv++;
            }
        }

        for (t = 2; t < nv; t++)
        {
            if (sk[t - 2] + sk[t - 1] + sk[t] < 3 || nvtx + 3 > 144)
                continue;
            for (c = 0; c < 3; c++)
            {
                int s = t - 2 + c;

                memcpy(tri_pos[nvtx], sv[s], sizeof(tri_pos[nvtx]));
                memcpy(tri_col[nvtx], sc[s], sizeof(tri_col[nvtx]));
                nvtx++;
            }
        }
    }

    if (nvtx != 0)
        ReiPan_Graph3dDrawGouraudTriangles(tri_pos, &tri_col[0][0], nvtx);
}

// ---- the large-hit flash -----------------------------------------------------------------

// The flash is a lens over a still of the frame: slot 0 stores the shown
// picture (the half-size copy at GS 0x38e0, 320 x 224) at EE 0x11c1000 on
// its first frame and every CaptureInterval frames after, and each frame the
// still goes back to GS 0x2bc0 and is drawn through a camera-facing fan that
// samples it at the preset's size and is drawn grown, spun and fading.
#define ENE_LARGE_HIT_EE_STASH  0x11c1000       // the haze's stash too (effect_obj.c)

// The still as a texture: TBP 0x2bc0, TBW 5, PSMCT24, 512 x 256, TCC RGB,
// MODULATE (rdata 0x36e010; FF2 512 x 512).
#define ENE_LARGE_HIT_TEX0      0x2000000224116bc0ULL

#define ENE_LARGE_HIT_VTX_MAX   256             // FF3's stack arrays; presets use at most 148

// The 50 flash presets (FF2's 38 then two FF3 sets; .data 0x2f6b68..0x2f7e28,
// table 0x2f8648). CenterRgba, OutsideRgba, VertexNumW / H, Size, LastScale,
// AllFrame, MoveDist, Distance, RotVal, CaptureInterval / Number, ALPHA A B C
// D FIX. FF3 retuned FF2's alphas and sizes (and preset 0's mesh is 60 x 15).
static ENE_DMG_LARGE_HIT_PARAMETER LargeHitParameter[ENE_DMG_LARGE_HIT_PRESET_NUM] = {
    { { 222, 206, 255,  60 }, { 121, 188, 243,   0 }, 60, 15,  68, 120, 30,  16,  901, 3600, 2, 2, 0, 2, 0, 1, 0 }, //  0 SmallHit 0x2f6b68
    { { 188, 216, 255,  95 }, { 255, 201, 255,  48 }, 39, 15, 101, 472, 29, 259,  971, 3600, 2, 2, 0, 1, 0, 1, 0 }, //  1 LargeHitType00 0x2f6bc8
    { { 233, 227, 191,  64 }, { 233, 227, 191,   9 }, 17, 15,  95, 284, 15, 327,  975, 3600, 1, 1, 0, 2, 0, 1, 0 }, //  2 LargeHitSPAType00 0x2f6c28
    { { 241, 189, 113,   0 }, { 241, 189, 113,   0 }, 17, 15, 100, 167, 46,   0, 1000, 3600, 1, 2, 0, 2, 0, 1, 0 }, //  3 LargeHitSPBType00 0x2f6c88
    { { 100,  68, 146, 125 }, {  73,  75,  78,   0 }, 17, 15, 100, 176, 35,   0,  940, 3600, 1, 1, 0, 2, 0, 1, 0 }, //  4 SlowHitAType00 0x2f6ce8
    { {  73,  72,  73,  13 }, {  73,  72,  73,   0 }, 17, 15, 100, 169, 33, 948,  946, 3600, 1, 1, 0, 2, 0, 1, 0 }, //  5 SlowHitBType00 0x2f6d48
    { { 203, 154,  93,  28 }, { 203, 154,  93,   0 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, //  6 ZeroHitA 0x2f6da8
    { { 116, 125, 180,  89 }, { 116, 125, 180,  26 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, //  7 ZeroHitB 0x2f6e08
    { { 203, 154,  93,  30 }, { 203, 154,  93,  25 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, //  8 ZeroHitSCA 0x2f6e68
    { { 116, 125, 180,  80 }, { 116, 125, 180,  25 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, //  9 ZeroHitSCB 0x2f6ec8
    { { 203, 154,  93,  40 }, { 207, 154,  93,  30 }, 17, 15, 100, 961,  8,  13,  946, 3701, 1, 6, 0, 2, 0, 1, 0 }, // 10 ZeroHitSPA 0x2f6f28
    { { 116, 125, 180,  92 }, { 116, 125, 180,  29 }, 17, 15, 100, 740, 31, 625,  992, 3500, 1, 1, 0, 2, 0, 1, 0 }, // 11 ZeroHitSPB 0x2f6f88
    { { 145, 152, 111,  30 }, { 175, 175, 114,  11 }, 17, 15, 101,  30, 11,   0,  974, 3600, 0, 0, 0, 2, 0, 1, 0 }, // 12 KokuHitA 0x2f6fe8
    { { 156, 153, 107,  30 }, { 172, 161, 111,  35 }, 17, 15, 104, 308, 29,  62,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 13 KokuHitB 0x2f7048
    { { 145, 152, 111,  50 }, { 175, 175, 114,  11 }, 17, 15, 101,  30, 11,   0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 }, // 14 KokuHitSCA 0x2f70a8
    { { 156, 153, 107,  38 }, { 169, 161, 111,  40 }, 17, 15, 104, 308, 29,  62,  992, 3600, 2, 8, 0, 2, 0, 1, 0 }, // 15 KokuHitSCB 0x2f7108
    { { 145, 152, 111,  60 }, { 175, 175, 114,  11 }, 17, 15,  98, 209, 17, 320, 1001, 3651, 2, 2, 0, 2, 0, 1, 0 }, // 16 KokuHitSPA 0x2f7168
    { { 156, 153, 107,  40 }, { 169, 161, 111,  60 }, 17, 15, 104, 308, 29,  62,  992, 3550, 2, 8, 0, 2, 0, 1, 0 }, // 17 KokuHitSPB 0x2f71c8
    { { 100, 158, 126, 145 }, { 100, 158, 126,   0 }, 17, 15, 100, 100, 33,   0, 1000, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 18 ParalyzeHitA 0x2f7228
    { { 116, 163, 159, 255 }, { 116, 127, 159,   0 }, 17, 15, 100, 501, 35, 636,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 19 ParalyzeHitB 0x2f7288
    { { 255, 247, 175, 108 }, { 255, 233, 177, 255 }, 17, 15, 100, 105, 31,   0, 1001, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 20 ViewHitA 0x2f72e8
    { { 116, 127, 159,  39 }, { 116, 127, 159,  43 }, 17, 15, 100, 231,  1,   0,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 21 ViewHitB 0x2f7348
    { { 206, 101,  63,  81 }, { 196, 119, 114,  60 }, 17, 15, 101, 171,  4,  27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 }, // 22 MetsuHitA 0x2f73a8
    { { 234, 110, 107,  70 }, { 255, 114, 111,  50 }, 17, 15, 104, 308,  9,  62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 }, // 23 MetsuHitB 0x2f7408
    { { 206, 101,  63,  80 }, { 196, 119, 114,  60 }, 17, 15, 101, 171,  4,  27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 }, // 24 MetsuHitSCA 0x2f7468
    { { 234, 110, 107,  70 }, { 255, 114, 111,  80 }, 17, 15, 104, 308,  9,  62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 }, // 25 MetsuHitSCB 0x2f74c8
    { { 206, 101,  63, 200 }, { 196, 119, 114, 100 }, 17, 15, 101, 171,  4,  27,  974, 3654, 2, 2, 0, 2, 0, 1, 0 }, // 26 MetsuHitSPA 0x2f7528
    { { 234, 110, 107,  81 }, { 255, 114, 111, 101 }, 17, 15, 104, 308,  9,  62,  992, 3483, 5, 6, 0, 2, 0, 1, 0 }, // 27 MetsuHitSPB 0x2f7588
    { { 114, 152, 111,  32 }, { 126, 175, 114,   0 }, 17, 15, 101,  10,  8,   1,  985, 3600, 0, 0, 0, 2, 0, 1, 0 }, // 28 RenHitA 0x2f75e8
    { { 114, 153, 107, 194 }, { 103, 161, 111, 123 }, 17, 15, 104, 308,  8, 881,  992, 3600, 1, 0, 0, 2, 0, 1, 0 }, // 29 RenHitB 0x2f7648
    { { 114, 152, 111, 153 }, { 126, 175, 114,   0 }, 17, 15, 101,  10, 11,   0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 }, // 30 RenHitSCA 0x2f76a8
    { { 114, 153, 107, 113 }, { 103, 161, 111,   0 }, 17, 15, 104, 308, 10,  62,  992, 3600, 2, 3, 0, 2, 0, 1, 0 }, // 31 RenHitSCB 0x2f7708
    { { 114, 152, 111, 153 }, { 126, 175, 114,   0 }, 17, 15, 101,  10, 11,   0,  974, 3600, 2, 2, 0, 2, 0, 1, 0 }, // 32 RenHitSPA 0x2f7768
    { { 114, 153, 107, 113 }, { 103, 161, 111,   0 }, 17, 15, 104, 308, 10,  62,  992, 3600, 2, 3, 0, 2, 0, 1, 0 }, // 33 RenHitSPB 0x2f77c8
    { { 255, 247, 193,  94 }, { 255, 233, 204, 255 }, 17, 15, 180, 100, 83,   0, 1001, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 34 TsuiHitA 0x2f7828
    { { 116, 127, 159,  39 }, { 116, 127, 159,  43 }, 17, 15, 262, 312,  1,   0,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 35 TsuiHitB 0x2f7888
    { { 184, 255, 193,   0 }, { 188, 255, 182, 255 }, 17, 15,  98, 209, 17, 320, 1001, 3600, 2, 2, 0, 2, 0, 1, 0 }, // 36 FuuHitA 0x2f78e8
    { { 133, 226, 159,   0 }, { 144, 233, 159, 154 }, 17, 15, 100, 336, 10, 351,  992, 3600, 2, 2, 0, 2, 0, 1, 0 }, // 37 FuuHitB 0x2f7948
    { {  93, 154, 203,  12 }, {  93, 154, 203,   0 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, // 38 Ex1HitA 0x2f79a8
    { { 116, 111, 116, 100 }, { 180, 125, 116,   0 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 39 Ex1HitB 0x2f7a08
    { {  93, 154, 203,  40 }, {  93, 154, 203,  33 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, // 40 Ex1HitSCA 0x2f7a68
    { { 116, 100, 116, 100 }, { 180, 125, 116,   0 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 41 Ex1HitSCB 0x2f7ac8
    { {  93, 154, 203,  84 }, {  93, 154, 207,  84 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, // 42 Ex1HitSPA 0x2f7b28
    { { 116, 100, 116, 100 }, { 180, 125, 116,   0 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 43 Ex1HitSPB 0x2f7b88
    { { 152,  55, 223,  11 }, { 148,  52, 223, 100 }, 17, 15, 100, 961,  8,  13,  946, 3600, 1, 6, 0, 2, 0, 1, 0 }, // 44 Ex2HitA 0x2f7be8
    { { 172, 154, 156,  70 }, { 130, 128, 125,  60 }, 17, 15, 100, 740, 31, 625,  992, 3600, 1, 1, 0, 2, 0, 1, 0 }, // 45 Ex2HitB 0x2f7c48
    { { 152,  55, 223,  11 }, { 148,  52, 223, 120 }, 17, 15, 100, 961,  8,  13,  946, 3303, 1, 6, 0, 2, 0, 1, 0 }, // 46 Ex2HitSCA 0x2f7ca8
    { { 172, 154, 156,  80 }, { 130, 128, 125,  70 }, 17, 15, 100, 740, 31, 625,  992, 3906, 1, 1, 0, 2, 0, 1, 0 }, // 47 Ex2HitSCB 0x2f7d08
    { { 152,  55, 223,  11 }, { 148,  52, 223, 144 }, 17, 15, 100, 961,  8,  13,  946, 3312, 1, 6, 0, 2, 0, 1, 0 }, // 48 Ex2HitSPA 0x2f7d68
    { { 172, 154, 156,  91 }, { 130, 128, 125,  87 }, 17, 15, 100, 740, 31, 625,  992, 3906, 1, 1, 0, 2, 0, 1, 0 }, // 49 Ex2HitSPB 0x2f7dc8
};

// The 26 blur / contrast presets, one per hit label: FF2's 20 (FF3 retunes
// LargeHit_Blur's shake / rumble frames and LargeHitSP_Blur's alpha), then
// six copies of ZeroHit_Blur for the FF3 labels 0x14..0x19 (.data
// 0x2f7e28.., table 0x2f8710). StartFrame, In / Keep / OutTime, Min / Max
// BlurScale, BlurAlpha, BlurRot, BlurOnFlg, Min / Max ContrastColor,
// ContrastAlpha, ContrastOnFlg, CameraShakeOnFlg / Frame, PadVibrateOnFlg /
// Frame.
static ENE_DMG_BLUR_CONTRAST_PARAMETER LargeHitBlurParameter[ENE_DMG_BLUR_PRESET_NUM] = {
    { 0,  2, 12,  9, 1000, 1080,  0,  64, 1800, 1800, 0,  0, 80,  0,  80, 0, 0,  0, 0,  0 }, //  0 SmallHit_Blur 0x2f7e28
    { 5,  0, 24,  0, 1000, 1207,  0,  15, 1800, 1800, 1,  0, 11,  0,  16, 1, 1,  4, 1,  3 }, //  1 LargeHit_Blur 0x2f7e78
    { 0,  0, 12,  0, 1000, 1283,  0,   0, 1800, 1800, 1, 30, 48, 27,  37, 1, 1, 11, 1, 10 }, //  2 LargeHitSP_Blur 0x2f7ec8
    { 0, 27, 27, 78, 1000, 1080,  0,  64, 1800, 1800, 0,  0, 80,  0,  80, 0, 0,  4, 0,  4 }, //  3 SlowHit_Blur 0x2f7f18
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, //  4 ZeroHit_Blur 0x2f7f68
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, //  5 ZeroHitSC_Blur 0x2f7fb8
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, //  6 ZeroHitSP_Blur 0x2f8008
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 0, 42, 80, 74, 140, 0, 1,  7, 1,  7 }, //  7 KokuHit_Blur 0x2f8058
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, //  8 KokuHitSC_Blur 0x2f80a8
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, //  9 KokuHitSP_Blur 0x2f80f8
    { 0,  2,  8,  8, 1000, 1392, 75, 129, 1800, 1820, 1,  0, 80,  0,  80, 1, 0,  5, 1,  5 }, // 10 ParalyzeHit_Blur 0x2f8148
    { 7, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 0,  6, 0,  6 }, // 11 ViewHit_Blur 0x2f8198
    { 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 12 MetsuHit_Blur 0x2f81e8
    { 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 13 MetsuHitSC_Blur 0x2f8238
    { 0, 20, 20, 20, 1030, 1683, 95,  93, 1571, 2073, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 14 MetsuHitSP_Blur 0x2f8288
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 15 RenHit_Blur 0x2f82d8
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 16 RenHitSC_Blur 0x2f8328
    { 0, 20, 20, 20, 1030, 1683, 35,  21, 1800, 1800, 1, 42, 80, 74, 140, 1, 1,  7, 1,  7 }, // 17 RenHitSP_Blur 0x2f8378
    { 7,  5, 23, 23, 1000, 1194, 60, 127, 1800, 1800, 1, 49, 80, 63,  80, 1, 0,  6, 0,  6 }, // 18 TsuiHit_Blur 0x2f83c8
    { 0,  2, 48, 27,  910, 1405, 16,  92, 1783, 1808, 1, 32, 64, 45, 158, 1, 0,  6, 0,  6 }, // 19 FuuHit_Blur 0x2f8418
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 20 Ex1Hit_Blur 0x2f8468
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 21 Ex1HitSC_Blur 0x2f84b8
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 22 Ex1HitSP_Blur 0x2f8508
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 23 Ex2Hit_Blur 0x2f8558
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 24 Ex2HitSC_Blur 0x2f85a8
    { 6, 20, 20, 20, 1000, 1080,  0,  64, 1800, 1820, 0,  0, 80,  0,  80, 0, 1,  7, 1,  7 }, // 25 Ex2HitSP_Blur 0x2f85f8
};

static void EneDmgLargeHitEffectSmallInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSlow_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectSlow_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZero_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZero_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectZeroSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKoku_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKoku_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectKokuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectParalyze_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectParalyze_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectView_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectView_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsu_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsu_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectMetsuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRen_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRen_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectRenSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectTsui_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectTsui_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectFuu_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectFuu_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1SC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1SC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1SP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx1SP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2SC_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2SC_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2SP_AInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitEffectEx2SP_BInit(ENE_DMG_LARGE_HIT *pLargeHit);
static void EneDmgLargeHitInit(ENE_DMG_LARGE_HIT *pLargeHit, ENE_DMG_LARGE_HIT_PARAMETER *pLHParam);
static void EneDmgLargeHitEffectDisp(ENE_DMG_LARGE_HIT *pLargeHit, float DrawScale, float RotZ);
static void EneDmgLargeHitMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center, float Width, float Height,
                                     u_int VertexNumW, u_int VertexNumH);

// FF3 0x0015fe00.

static void EneDmgLargeHitCtrlInit(void)
{
    int i;

    for (i = 0; i < 2; i++)
        EneDmgLargeHitCtrl.Status[i] = 0;
}

// Both slots per frame (FF2's). Slot 0 stores the still on its first frame
// (the Delay frame and the next, as NowFrame waits) and every
// CaptureInterval frames while CaptureNumber lasts. A running slot past its
// delay ramps on t = NowFrame / AllFrame: the hub drifts MoveDist back, the
// drawn scale goes 1 -> LastScale, both alphas fade to 0, the spin goes to
// RotVal (wrapped to -pi..pi); it ends at AllFrame.
// FF3 differences: the still is stored at EE 0x11c1000 from GS 0x38e0
// (LocalCopyLtoBAdrs; FF2 LocalCopyLtoB of the shown frame then
// EffImageHalf32).
// FF3 0x0015fe18; FF2 EneDmgLargeHitCtrlMain.

static void EneDmgLargeHitCtrlMain(void)
{
    ENE_DMG_LARGE_HIT *pLargeHit;
    float t;
    float Rot;
    int i;

    if (EneDmgLargeHitCtrl.Status[0] != 0)
    {
        pLargeHit = &EneDmgLargeHitCtrl.Work[0];
        if (pLargeHit->NowFrame == 0)
        {
            LocalCopyLtoBAdrs(1, ENE_LARGE_HIT_EE_STASH, 0x38e0);
        }
        else if (pLargeHit->CaptureNumber != 0 && pLargeHit->CaptureInterval != 0)
        {
            if (pLargeHit->NowFrame % pLargeHit->CaptureInterval == 0)
            {
                LocalCopyLtoBAdrs(1, ENE_LARGE_HIT_EE_STASH, 0x38e0);
                pLargeHit->CaptureNumber--;
            }
        }
    }

    for (i = 0; i < 2; i++)
    {
        if (EneDmgLargeHitCtrl.Status[i] == 0)
            continue;

        pLargeHit = &EneDmgLargeHitCtrl.Work[i];
        if (pLargeHit->Delay != 0)
        {
            pLargeHit->Delay--;
            continue;
        }

        t = (float)pLargeHit->NowFrame / (float)pLargeHit->AllFrame;
        pLargeHit->CenterPos[2] = -pLargeHit->MoveDist * t;
        pLargeHit->CenterRgba[3] = (u_char)(int)((float)pLargeHit->InitCenterAlpha * (1.0f - t));
        pLargeHit->OutsideRgba[3] = (u_char)(int)((float)pLargeHit->InitOutsideAlpha * (1.0f - t));
        for (Rot = pLargeHit->RotVal * t; Rot > 3.1415927f; Rot -= 6.2831855f)     // rdata 0x36dfec / 0x36dff0
            ;
        for (; Rot < -3.1415927f; Rot += 6.2831855f)                               // rdata 0x36dff4
            ;
        EneDmgLargeHitEffectDisp(pLargeHit, (pLargeHit->LastScale - 1.0f) * t + 1.0f, Rot);

        pLargeHit->NowFrame++;
        if (pLargeHit->NowFrame >= pLargeHit->AllFrame)
            EneDmgLargeHitCtrl.Status[i] = 0;
    }
}

// Arms the first free slot with preset 0..49 and holds it one frame (Delay
// 1), so the still taken on that frame is without the flash; both slots busy
// drops the request. An unknown preset still claims the slot (the ROM's).
// FF3 0x00160108; FF2 EneDmgLargeHitReq (38 presets).

void EneDmgLargeHitReq(int preset)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (EneDmgLargeHitCtrl.Status[i] != 0)
            continue;

        EneDmgLargeHitCtrl.Status[i] = 1;
        switch (preset)
        {
        case  0: EneDmgLargeHitEffectSmallInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  1: EneDmgLargeHitEffectInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  2: EneDmgLargeHitEffectSP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  3: EneDmgLargeHitEffectSP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  4: EneDmgLargeHitEffectSlow_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  5: EneDmgLargeHitEffectSlow_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  6: EneDmgLargeHitEffectZero_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  7: EneDmgLargeHitEffectZero_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  8: EneDmgLargeHitEffectZeroSC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case  9: EneDmgLargeHitEffectZeroSC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 10: EneDmgLargeHitEffectZeroSP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 11: EneDmgLargeHitEffectZeroSP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 12: EneDmgLargeHitEffectKoku_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 13: EneDmgLargeHitEffectKoku_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 14: EneDmgLargeHitEffectKokuSC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 15: EneDmgLargeHitEffectKokuSC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 16: EneDmgLargeHitEffectKokuSP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 17: EneDmgLargeHitEffectKokuSP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 18: EneDmgLargeHitEffectParalyze_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 19: EneDmgLargeHitEffectParalyze_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 20: EneDmgLargeHitEffectView_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 21: EneDmgLargeHitEffectView_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 22: EneDmgLargeHitEffectMetsu_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 23: EneDmgLargeHitEffectMetsu_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 24: EneDmgLargeHitEffectMetsuSC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 25: EneDmgLargeHitEffectMetsuSC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 26: EneDmgLargeHitEffectMetsuSP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 27: EneDmgLargeHitEffectMetsuSP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 28: EneDmgLargeHitEffectRen_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 29: EneDmgLargeHitEffectRen_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 30: EneDmgLargeHitEffectRenSC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 31: EneDmgLargeHitEffectRenSC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 32: EneDmgLargeHitEffectRenSP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 33: EneDmgLargeHitEffectRenSP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 34: EneDmgLargeHitEffectTsui_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 35: EneDmgLargeHitEffectTsui_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 36: EneDmgLargeHitEffectFuu_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 37: EneDmgLargeHitEffectFuu_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 38: EneDmgLargeHitEffectEx1_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 39: EneDmgLargeHitEffectEx1_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 40: EneDmgLargeHitEffectEx1SC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 41: EneDmgLargeHitEffectEx1SC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 42: EneDmgLargeHitEffectEx1SP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 43: EneDmgLargeHitEffectEx1SP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 44: EneDmgLargeHitEffectEx2_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 45: EneDmgLargeHitEffectEx2_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 46: EneDmgLargeHitEffectEx2SC_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 47: EneDmgLargeHitEffectEx2SC_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 48: EneDmgLargeHitEffectEx2SP_AInit(&EneDmgLargeHitCtrl.Work[i]); break;
        case 49: EneDmgLargeHitEffectEx2SP_BInit(&EneDmgLargeHitCtrl.Work[i]); break;
        }
        EneDmgLargeHitCtrl.Work[i].Delay = 1;
        return;
    }
}

// The same body as EneDmgLargeHitCtrlInit, as in FF2. FF3 0x001604d0.

static void EneDmgLargeHitAllOff(void)
{
    int i;

    for (i = 0; i < 2; i++)
        EneDmgLargeHitCtrl.Status[i] = 0;
}

// Unpacks a preset into a slot (FF2's): the colours as bytes and their
// alphas kept for the fade, Size / LastScale / 100, MoveDist / Distance
// / 250 (FF2 10), RotVal - 3600 degrees in radians, the five ALPHA fields
// packed into the register, the hub at the origin.
// FF3 0x001604e8; FF2 EneDmgLargeHitInit.

static void EneDmgLargeHitInit(ENE_DMG_LARGE_HIT *pLargeHit, ENE_DMG_LARGE_HIT_PARAMETER *pLHParam)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        pLargeHit->CenterRgba[i] = (u_char)pLHParam->CenterRgba[i];
        pLargeHit->OutsideRgba[i] = (u_char)pLHParam->OutsideRgba[i];
    }
    pLargeHit->VertexNumW = pLHParam->VertexNumW;
    pLargeHit->VertexNumH = pLHParam->VertexNumH;
    pLargeHit->Scale = (float)pLHParam->Size / 100.0f;             // rdata 0x36dff8
    pLargeHit->NowFrame = 0;
    pLargeHit->LastScale = (float)pLHParam->LastScale / 100.0f;
    pLargeHit->AllFrame = pLHParam->AllFrame;
    pLargeHit->MoveDist = (float)pLHParam->MoveDist / 250.0f;      // rdata 0x36dffc
    pLargeHit->InitCenterAlpha = pLHParam->CenterRgba[3];
    pLargeHit->InitOutsideAlpha = pLHParam->OutsideRgba[3];
    pLargeHit->Distance = (float)pLHParam->Distance / 250.0f;
    pLargeHit->CaptureInterval = pLHParam->CaptureInterval;
    pLargeHit->CaptureNumber = pLHParam->CaptureNumber;
    pLargeHit->AlphaBlend = (u_long)(int64_t)pLHParam->AlphaBlendA
                          | ((u_long)(int64_t)pLHParam->AlphaBlendFIX << 32)
                          | ((u_long)(int64_t)pLHParam->AlphaBlendC << 4)
                          | ((u_long)(int64_t)pLHParam->AlphaBlendB << 2)
                          | ((u_long)(int64_t)pLHParam->AlphaBlendD << 6);
    pLargeHit->CenterPos[3] = 1.0f;                                 // rdata 0x36e000
    pLargeHit->CenterPos[0] = 0.0f;
    pLargeHit->CenterPos[1] = 0.0f;
    pLargeHit->CenterPos[2] = 0.0f;
    pLargeHit->RotVal = ((float)(pLHParam->RotVal - 3600) * 3.1415927f) / 180.0f;   // rdata 0x36e004 / 0x36e008
}

// The 50 preset helpers, in FF3's address order (FF2's too: Paralyze and
// View sit after Ren). Each arms one slot from its preset.

// FF3 0x00160670
static void EneDmgLargeHitEffectSmallInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(0));
}

// FF3 0x001606a0
static void EneDmgLargeHitEffectInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(1));
}

// FF3 0x001606d0
static void EneDmgLargeHitEffectSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(2));
}

// FF3 0x00160700
static void EneDmgLargeHitEffectSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(3));
}

// FF3 0x00160730
static void EneDmgLargeHitEffectSlow_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(4));
}

// FF3 0x00160760
static void EneDmgLargeHitEffectSlow_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(5));
}

// FF3 0x00160790
static void EneDmgLargeHitEffectZero_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(6));
}

// FF3 0x001607c0
static void EneDmgLargeHitEffectZero_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(7));
}

// FF3 0x001607f0
static void EneDmgLargeHitEffectZeroSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(8));
}

// FF3 0x00160820
static void EneDmgLargeHitEffectZeroSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(9));
}

// FF3 0x00160850
static void EneDmgLargeHitEffectZeroSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(10));
}

// FF3 0x00160880
static void EneDmgLargeHitEffectZeroSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(11));
}

// FF3 0x001608b0
static void EneDmgLargeHitEffectKoku_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(12));
}

// FF3 0x001608e0
static void EneDmgLargeHitEffectKoku_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(13));
}

// FF3 0x00160910
static void EneDmgLargeHitEffectKokuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(14));
}

// FF3 0x00160940
static void EneDmgLargeHitEffectKokuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(15));
}

// FF3 0x00160970
static void EneDmgLargeHitEffectKokuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(16));
}

// FF3 0x001609a0
static void EneDmgLargeHitEffectKokuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(17));
}

// FF3 0x001609d0
static void EneDmgLargeHitEffectMetsu_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(22));
}

// FF3 0x00160a00
static void EneDmgLargeHitEffectMetsu_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(23));
}

// FF3 0x00160a30
static void EneDmgLargeHitEffectMetsuSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(24));
}

// FF3 0x00160a60
static void EneDmgLargeHitEffectMetsuSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(25));
}

// FF3 0x00160a90
static void EneDmgLargeHitEffectMetsuSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(26));
}

// FF3 0x00160ac0
static void EneDmgLargeHitEffectMetsuSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(27));
}

// FF3 0x00160af0
static void EneDmgLargeHitEffectRen_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(28));
}

// FF3 0x00160b20
static void EneDmgLargeHitEffectRen_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(29));
}

// FF3 0x00160b50
static void EneDmgLargeHitEffectRenSC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(30));
}

// FF3 0x00160b80
static void EneDmgLargeHitEffectRenSC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(31));
}

// FF3 0x00160bb0
static void EneDmgLargeHitEffectRenSP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(32));
}

// FF3 0x00160be0
static void EneDmgLargeHitEffectRenSP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(33));
}

// FF3 0x00160c10
static void EneDmgLargeHitEffectParalyze_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(18));
}

// FF3 0x00160c40
static void EneDmgLargeHitEffectParalyze_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(19));
}

// FF3 0x00160c70
static void EneDmgLargeHitEffectView_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(20));
}

// FF3 0x00160ca0
static void EneDmgLargeHitEffectView_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(21));
}

// FF3 0x00160cd0
static void EneDmgLargeHitEffectTsui_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(34));
}

// FF3 0x00160d00
static void EneDmgLargeHitEffectTsui_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(35));
}

// FF3 0x00160d30
static void EneDmgLargeHitEffectFuu_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(36));
}

// FF3 0x00160d60
static void EneDmgLargeHitEffectFuu_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(37));
}

// FF3 0x00160d90
static void EneDmgLargeHitEffectEx1_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(38));
}

// FF3 0x00160dc0
static void EneDmgLargeHitEffectEx1_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(39));
}

// FF3 0x00160df0
static void EneDmgLargeHitEffectEx1SC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(40));
}

// FF3 0x00160e20
static void EneDmgLargeHitEffectEx1SC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(41));
}

// FF3 0x00160e50
static void EneDmgLargeHitEffectEx1SP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(42));
}

// FF3 0x00160e80
static void EneDmgLargeHitEffectEx1SP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(43));
}

// FF3 0x00160eb0
static void EneDmgLargeHitEffectEx2_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(44));
}

// FF3 0x00160ee0
static void EneDmgLargeHitEffectEx2_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(45));
}

// FF3 0x00160f10
static void EneDmgLargeHitEffectEx2SC_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(46));
}

// FF3 0x00160f40
static void EneDmgLargeHitEffectEx2SC_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(47));
}

// FF3 0x00160f70
static void EneDmgLargeHitEffectEx2SP_AInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(48));
}

// FF3 0x00160fa0
static void EneDmgLargeHitEffectEx2SP_BInit(ENE_DMG_LARGE_HIT *pLargeHit)
{
    EneDmgLargeHitInit(pLargeHit, EffEneDmgLargeHitParameterPtrGet(49));
}


// CalcStqXYZ (FF2 effect_obj.h; VU0 code inlined in FF3's Disp): the
// vertex's frame position (x / y scaled by stqparam[0][2]) minus the GS
// offset, clamped to the 640 x 448 frame, scaled into the texture page with
// Q, so the fan samples the still under itself. PC: the matrix and the
// parameters FF3 leaves in vf4..vf10 are arguments.

static void EneDmgLargeHitCalcStq(float *stq, const float *vt, sceVu0FMATRIX slm, float stqparam[3][4])
{
    float v[3], s[4], q;
    int c;

    v[0] = vt[0] * stqparam[0][2];
    v[1] = vt[1] * stqparam[0][2];
    v[2] = vt[2];
    for (c = 0; c < 4; c++)
        s[c] = slm[0][c] * v[0] + slm[1][c] * v[1] + slm[2][c] * v[2] + slm[3][c];
    q = 1.0f / s[3];
    s[0] = s[0] * q - stqparam[0][0];
    s[1] = s[1] * q - stqparam[0][1];
    if (s[0] < stqparam[1][0]) s[0] = stqparam[1][0];
    if (s[0] > stqparam[1][1]) s[0] = stqparam[1][1];
    if (s[1] < stqparam[1][2]) s[1] = stqparam[1][2];
    if (s[1] > stqparam[1][3]) s[1] = stqparam[1][3];
    stq[0] = s[0] * (stqparam[2][0] * q) * stqparam[2][3];
    stq[1] = s[1] * (stqparam[2][1] * q) * stqparam[2][3];
    stq[2] = stqparam[2][2] * q * stqparam[2][3];
    stq[3] = s[3];                                      // sqc2 keeps the w (not read)
}

// One flash (FF2's): the still back to GS 0x2bc0, a billboard Distance
// before the camera turned to its heading, the rim built at Scale (x 3.2 /
// 2.24, the frame's aspect) and its STQ taken through the unspun matrix, so
// the sample stays put while the fan is drawn grown by DrawScale and spun by
// RotZ over it. ALPHA from the preset, bilinear, clamped, no depth test or
// write.
// FF3 differences: the still comes from EE 0x11c1000 (LocalCopyBtoLAdrs;
// FF2 LocalCopyBtoL of the back buffer), the billboard is unit size (FF2
// 25) with Distance / 250, the draw state goes through SetGsRegsCached (FF2
// SetDrawEnv) and the page is 512 x 256 (FF2 512 x 512).
// FF3 0x00160fd0; FF2 EneDmgLargeHitEffectDisp.
// callcheck: EneDmgLargeHitCalcStq (FF3's inlined VU0 code)

static void EneDmgLargeHitEffectDisp(ENE_DMG_LARGE_HIT *pLargeHit, float DrawScale, float RotZ)
{
    static sceVu0FVECTOR VtxBuf[ENE_LARGE_HIT_VTX_MAX];     // PC: off the stack
    static sceVu0FVECTOR StqBuf[ENE_LARGE_HIT_VTX_MAX];
    GRA3DCAMERA  *pCam;
    float        *pDir;
    float        *pCamPos;
    sceVu0FMATRIX LocalWorldMat;
    sceVu0FMATRIX LocalScreenMat;
    sceVu0FMATRIX RotScaleMat;
    sceVu0FVECTOR Pos;
    float         stqparam[3][4];
    GS_REG_SET    regs[5];
    float         RotX;
    float         RotY;
    int           VertexNum;
    int           i;

    pCam = gra3dGetCamera();
    pDir = gra3dcamGetDirection();
    pCamPos = gra3dcamGetPosition();
    LocalCopyBtoLAdrs(1, ENE_LARGE_HIT_EE_STASH, 0x2bc0);

    sceVu0ScaleVector(Pos, pDir, pLargeHit->Distance);
    sceVu0AddVector(Pos, Pos, pCamPos);

    // The hub, then the rim of a VertexNumW x VertexNumH rectangle with its
    // first vertex repeated to close the fan.
    VertexNum = (int)(pLargeHit->VertexNumW + pLargeHit->VertexNumH) * 2 - 2;

    Vector2Rot(pDir, &RotX, &RotY);
    sceVu0UnitMatrix(LocalWorldMat);
    LocalWorldMat[0][0] = 1.0f;                         // rdata 0x36e018 (FF2 25)
    LocalWorldMat[1][1] = 1.0f;
    LocalWorldMat[2][2] = 1.0f;
    sceVu0RotMatrixX(LocalWorldMat, LocalWorldMat, RotX);
    sceVu0RotMatrixY(LocalWorldMat, LocalWorldMat, RotY);
    sceVu0TransMatrix(LocalWorldMat, LocalWorldMat, Pos);
    sceVu0MulMatrix(LocalScreenMat, pCam->matWorldScreen, LocalWorldMat);

    // rdata 0x36e01c..0x36e034: the GS offset, the vertex scale, the 640 x
    // 448 clamp, the 1 / 1024 x 1 / 512 page scale and the 1 / 16 Q scale.
    // PC: the clamp is the window (the view bounds), as in effect_obj.c's
    // PartsDeformSetStqParam: the still holds the whole window, so the rim
    // past the original frame samples the scene there, not its edge column.
    ReiPan_RendererGetViewBounds(&stqparam[1][0], &stqparam[1][2],
                                 &stqparam[1][1], &stqparam[1][3]);
    stqparam[1][1] -= 1.0f;                             // 0, 639, 0, 447 in the ROM
    stqparam[1][3] -= 1.0f;
    stqparam[0][0] = 1727.5f;
    stqparam[0][1] = 1823.5f;
    stqparam[0][2] = 1.0f;
    stqparam[0][3] = 0.0f;                              // not written by the ROM, not read
    stqparam[2][0] = 0.0009765625f;
    stqparam[2][1] = 0.001953125f;
    stqparam[2][2] = 1.0f;
    stqparam[2][3] = 0.0625f;

    EneDmgLargeHitMakeVertex(VtxBuf, pLargeHit->CenterPos, pLargeHit->Scale * 3.2f,
                             pLargeHit->Scale * 2.24f, pLargeHit->VertexNumW,
                             pLargeHit->VertexNumH);                    // rdata 0x36e038 / 0x36e03c
    for (i = 0; i < VertexNum; i++)
        EneDmgLargeHitCalcStq(StqBuf[i], VtxBuf[i], LocalScreenMat, stqparam);

    sceVu0UnitMatrix(RotScaleMat);
    RotScaleMat[0][0] = DrawScale;
    RotScaleMat[1][1] = DrawScale;
    RotScaleMat[2][2] = DrawScale;
    sceVu0RotMatrixZ(RotScaleMat, RotScaleMat, RotZ);
    sceVu0MulMatrix(LocalWorldMat, LocalWorldMat, RotScaleMat);

    regs[0].data = pLargeHit->AlphaBlend;   regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;                   regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 5;                       regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x3000d;                 regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;          regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    EneDmgLargeHitMakePacket(VtxBuf, VertexNum, LocalWorldMat, pLargeHit->CenterRgba,
                             pLargeHit->OutsideRgba, StqBuf, ENE_LARGE_HIT_TEX0);
}

// The fan's vertices (FF2's): pVtxBuf[0] the hub, then the rectangle's rim
// from its top-left corner, VertexNumW - 1 steps along each horizontal edge
// and VertexNumH - 1 down the right one; the last edge takes VertexNumH
// steps, landing on the first rim vertex to close the fan.
// FF3 0x00161340; FF2 EneDmgLargeHitMakeVertex.

static void EneDmgLargeHitMakeVertex(sceVu0FVECTOR *pVtxBuf, float *Center, float Width, float Height,
                                     u_int VertexNumW, u_int VertexNumH)
{
    float CalcX;
    float CalcY;
    int   VertexCount;
    u_int i;

    pVtxBuf[0][0] = Center[0];
    pVtxBuf[0][1] = Center[1];
    pVtxBuf[0][2] = Center[2];
    pVtxBuf[0][3] = Center[3];
    VertexCount = 1;

    CalcX = -Width * 0.5f;                              // rdata 0x36e040
    CalcY = -Height * 0.5f;
    for (i = 0; i < VertexNumW - 1; i++)
    {
        pVtxBuf[VertexCount][0] = CalcX;
        pVtxBuf[VertexCount][1] = CalcY;
        pVtxBuf[VertexCount][2] = 0.0f;
        pVtxBuf[VertexCount][3] = 1.0f;                 // rdata 0x36e044
        CalcX += Width / (float)(VertexNumW - 1);
        VertexCount++;
    }

    CalcX = Width * 0.5f;
    CalcY = -Height * 0.5f;
    for (i = 0; i < VertexNumH - 1; i++)
    {
        pVtxBuf[VertexCount][0] = CalcX;
        pVtxBuf[VertexCount][1] = CalcY;
        pVtxBuf[VertexCount][2] = 0.0f;
        pVtxBuf[VertexCount][3] = 1.0f;
        CalcY += Height / (float)(VertexNumH - 1);
        VertexCount++;
    }

    CalcX = Width * 0.5f;
    CalcY = Height * 0.5f;
    for (i = 0; i < VertexNumW - 1; i++)
    {
        pVtxBuf[VertexCount][0] = CalcX;
        pVtxBuf[VertexCount][1] = CalcY;
        pVtxBuf[VertexCount][2] = 0.0f;
        pVtxBuf[VertexCount][3] = 1.0f;
        CalcX -= Width / (float)(VertexNumW - 1);
        VertexCount++;
    }

    CalcX = -Width * 0.5f;
    CalcY = Height * 0.5f;
    for (i = 0; i < VertexNumH; i++)
    {
        pVtxBuf[VertexCount][0] = CalcX;
        pVtxBuf[VertexCount][1] = CalcY;
        pVtxBuf[VertexCount][2] = 0.0f;
        pVtxBuf[VertexCount][3] = 1.0f;
        CalcY -= Height / (float)(VertexNumH - 1);
        VertexCount++;
    }
}

// The fan as a GIF packet (FF2's): TEXFLUSH, TEX0, PRIM 0x5d (fan, gouraud,
// textured, blended), then one REGLIST ST / RGBAQ / XYZF per vertex, the hub
// in the centre colour and the rim in the outside colour. The first two
// vertices never kick; after that a triangle is dropped (XYZF3) only when
// the hub, the previous and this vertex all left the guard band.
// FF3 differences: the REGLIST tag has PRE clear (0x3400000000008001; FF2
// 0x3400400000008001). FF3's haze (effect_obj.c) draws with it too.
// FF3 0x00161548; FF2 EneDmgLargeHitMakePacket.
// PC: nothing walks the DIRECT chain, so the same fan goes to the renderer
// as a triangle list (MioPan's bridge): triangle i is (hub, i - 1, i), kept
// on the ROM's test; the window coordinates lose the XYOFFSET (1728, 1824)
// and ST is divided by Q.
// callcheck: sceVu0MulMatrix (FF3's inlined VU0 products)

void EneDmgLargeHitMakePacket(sceVu0FVECTOR *pVertexBuf, int VertexNum, sceVu0FMATRIX LocalWorldMat,
                              u_char *pCenterRgba, u_char *pOutsideRgba, sceVu0FVECTOR *pStq,
                              u_long tex0)
{
    static int           clip[ENE_LARGE_HIT_VTX_MAX];           // PC: off the stack
    static sceVu0IVECTOR VertexBuf_I[ENE_LARGE_HIT_VTX_MAX];
    GRA3DCAMERA  *pCam;
    sceVu0FMATRIX matLocalScreen;
    sceVu0FMATRIX matLocalClip;
    u_long       *p;
    u_char       *col;
    u_int         q;
    int           i;

    pCam = gra3dGetCamera();
    sceVu0MulMatrix(matLocalScreen, pCam->matWorldScreen, LocalWorldMat);
    sceVu0MulMatrix(matLocalClip, pCam->matWorldClipPolygon, LocalWorldMat);
    for (i = 0; i < VertexNum; i++)
        clip[i] = CalcPartsDeformXYZ(VertexBuf_I[i], pVertexBuf[i], matLocalScreen, matLocalClip) & 0x3f;

    p = (u_long *)StartDmaDirectTrans();
    p[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);     // 0x1000000000008002
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = tex0;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x2400400000008001ULL;       // PACKED, 2 regs: PRIM, NOP
    p[7] = 0xf0;
    p[8] = 0x5d;
    p[9] = 0;
    p += 10;

    for (i = 0; i < VertexNum; i++)
    {
        if (i < 2 || (clip[0] != 0 && clip[i - 1] != 0 && clip[i] != 0))
            p[1] = 0xc12;               // ST RGBAQ XYZF3
        else
            p[1] = 0x412;               // ST RGBAQ XYZF2
        p[0] = 0x3400000000008001ULL;   // REGLIST, 3 regs
        memcpy(&p[2], &pStq[i][0], 8);
        col = (i == 0) ? pCenterRgba : pOutsideRgba;
        memcpy(&q, &pStq[i][2], sizeof(q));
        p[3] = (u_long)col[0] | ((u_long)q << 32) | ((u_long)col[2] << 16) | ((u_long)col[1] << 8)
             | ((u_long)col[3] << 24);
        p[5] = 0;
        p[4] = (u_long)(int64_t)VertexBuf_I[i][0] | ((u_long)(int64_t)VertexBuf_I[i][1] << 16)
             | ((u_long)(int64_t)VertexBuf_I[i][2] << 32);
        p += 6;
    }
    EndDmaDirectTrans((qword *)p);

    {
        static float  xy[ENE_LARGE_HIT_VTX_MAX * 3 * 2];
        static float  uv[ENE_LARGE_HIT_VTX_MAX * 3 * 2];
        static u_char rgba[ENE_LARGE_HIT_VTX_MAX * 3 * 4];
        int n = 0;
        int c;

        for (i = 2; i < VertexNum; i++)
        {
            int corner[3] = { 0, i - 1, i };

            if (clip[0] != 0 && clip[i - 1] != 0 && clip[i] != 0)
                continue;
            for (c = 0; c < 3; c++)
            {
                int   v = corner[c];
                float qq = pStq[v][2];

                col = (v == 0) ? pCenterRgba : pOutsideRgba;
                if (qq == 0.0f)
                    qq = 1.0f;
                xy[n * 2 + 0] = (float)VertexBuf_I[v][0] / 16.0f - 1728.0f;
                xy[n * 2 + 1] = (float)VertexBuf_I[v][1] / 16.0f - 1824.0f;
                uv[n * 2 + 0] = pStq[v][0] / qq;
                uv[n * 2 + 1] = pStq[v][1] / qq;
                rgba[n * 4 + 0] = col[0];
                rgba[n * 4 + 1] = col[1];
                rgba[n * 4 + 2] = col[2];
                rgba[n * 4 + 3] = col[3];
                n++;
            }
        }
        if (n != 0)
            ReiPan_RendererDrawTexturedTriangles2D((const sceGsTex0 *)&tex0, xy, uv, rgba, nullptr, n, 1, 1, 0);
    }
}

// FF3 0x00161838; FF2 EffEneDmgLargeHitParameterPtrGet (a pointer table).

ENE_DMG_LARGE_HIT_PARAMETER *EffEneDmgLargeHitParameterPtrGet(int label)
{
    return &LargeHitParameter[label];
}

// FF3 0x00161850; FF2 EffEneDmgLargeHitBlurParameterPtrGet (a pointer table).

ENE_DMG_BLUR_CONTRAST_PARAMETER *EffEneDmgLargeHitBlurParameterPtrGet(int label)
{
    return &LargeHitBlurParameter[label];
}

// ---- the enemy aura ------------------------------------------------------------------------

// The aura is a GS frame trick: EneAuraFrameBegin points FRAME_1 at a work
// frame (FBP 0x15e, after the Z buffer) and clears it, so the ghost model
// draws there with FBA_1 marking its pixels; EneAuraFrameEnd (optionally
// mixing the screen in) puts FRAME_1 back on the display buffer; EneAuraDraw
// copies the work frame over the screen once per layer, each shifted by its
// own wobble and blended by its own ALPHA.
// PC: the packets are built as FF3 builds them, and the host does the same
// thing with a renderer work frame (ReiPan_EffHostWorkBegin): the model is
// drawn into it, depth-tested against the room; the screen mix goes into it
// too; and each layer is a quad sampling it (EneAuraDrawLayers). Its alpha
// is the ghost mask FBA made on the GS: the frame opens transparent and the
// model's own alpha lands only where it draws. On a frame where no work
// frame can be opened (the slot's first), the old path: the model draws
// straight to the screen, the 2D bridges are muted, and no layer is drawn.

#define ENE_AURA_FBP    0x15e           // the work frame (TBP 0x2bc0)

static int ene_aura_host;               // PC: this frame's model went into a host work frame

// FF3 0x00161868.

void EneAuraFrameBegin(void)
{
    static const GS_REG_SET clear_regs[3] = {
        { 0x44, SCE_GS_ALPHA_1 },
        { 0x10a000118ULL, SCE_GS_ZBUF_1 },  // ZMSK
        { 0x30003, SCE_GS_TEST_1 },
    };
    static const GS_REG_SET model_regs[2] = {
        { 0xa000118, SCE_GS_ZBUF_1 },
        { 0x5101b, SCE_GS_TEST_1 },         // ATE, the model's Z test
    };
    u_long *p;
    u_int  *q;

    SetGsRegsCached(clear_regs, 3, 1);
    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000008002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0xa0000 | ENE_AURA_FBP;          // FBW 10, PSMCT32
    p[3] = SCE_GS_FRAME_1;
    p[4] = 0x720000006c00ULL;               // XYOFFSET 1728, 1824
    p[5] = SCE_GS_XYOFFSET_1;
    p[6] = 0x3003400000008001ULL;           // SPRITE: RGBAQ, XYZ2, XYZ2
    p[7] = 0x441;
    q = (u_int *)&p[8];
    q[0] = 0;      q[1] = 0;      q[2] = 0;  q[3] = 0;      // black, alpha 0
    q[4] = 0x6c00; q[5] = 0x7200; q[6] = 0;  q[7] = 0;      // 0, 0
    q[8] = 0x9400; q[9] = 0x8e00; q[10] = 0; q[11] = 0;     // 640, 448
    EndDmaDirectTrans((qword *)&p[14]);

    SetGsRegsCached(model_regs, 2, 1);
    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000008001ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 1;
    p[3] = SCE_GS_FBA_1;
    EndDmaDirectTrans((qword *)&p[4]);

    // PC: the work frame, 640 x 448 at TBP 0x2bc0 like the GS's, a picture
    // of the window.
    ene_aura_host = ReiPan_EffHostWorkBegin(ENE_AURA_FBP * 32, 640, 448, 0);
    if (ene_aura_host == 0)
        ReiPan_EffHostRedirectBegin(ENE_AURA_FBP * 32);
}

// With fx->mix: the other display buffer drawn into the work frame as a
// 640 x 448 sprite at fx->mix_alpha ((Cs - Cd) * FIX + Cd). The caller
// loads four more arguments (0x7f, 1, 128.0, 3.0) the ROM never reads.
// FF3 0x00161df8.

static void EneAuraScreenMix(ENE_AURA_DAT *fx)
{
    static const SPRT_DAT2 dat = {          // rdata 0x352d48
        0, 0.0f, 0.0f, 640.0f, 448.0f, 640.0f, 448.0f, -1.5f, -1.5f, 0xa0, 0x80
    };
    DISP_SPRT2 ds;
    u_char     a = fx->mix_alpha;

    if (fx->mix == 0)
        return;

    CopySprDToSpr2(&ds, &dat);
    ds.b = 0x80;
    ds.r = 0x80;
    ds.g = 0x80;
    ds.alpreg = ((u_long)a << 32) | 100;
    ds.test = 0x3c003;
    ds.tex0 = (u_long)(int64_t)(int)(((GetSysWrk()->count + 1) & 1) * 0x1180) | 0x28128000u
            | 0x2000000240000000ULL;        // rdata 0x36e070: TH, CLD
    ds.zbuf = 0x10a000118ULL;
    ds.alp = a;
    EffDispSprD2Direct(&ds);
}

// FF3 0x001619e8.

void EneAuraFrameEnd(ENE_AURA_DAT *fx)
{
    static const GS_REG_SET regs[2] = {     // rdata 0x352d28
        { 0xa000118, SCE_GS_ZBUF_1 },
        { 0x5000d, SCE_GS_TEST_1 },
    };
    u_long *p;

    EneAuraScreenMix(fx);
    SetGsRegsCached(regs, 2, 1);
    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000008002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = (u_long)(int64_t)(int)(((GetSysWrk()->count & 1) * 0x1180) >> 5) | 0x10a0000;
    p[3] = SCE_GS_FRAME_1;
    p[4] = 0;
    p[5] = SCE_GS_FBA_1;
    EndDmaDirectTrans((qword *)&p[6]);

    // PC
    if (ene_aura_host != 0)
        ReiPan_EffHostWorkEnd();
    else
        ReiPan_EffHostRedirectEnd();
}

// ---- the random timers (FF3-only, unreferenced) -------------------------------------

// Once obj's counter has run out: the counter and its period to 4, the
// second timer to 60, the third to 80..99 (GetRandValIByF(0) is always 0,
// but the ROM still draws the random numbers), and the pair a / b to
// -0.2 / 0.13 (mode 0) or 0.13 / -0.2 (mode 1; other modes leave them).
// 1 when re-armed. Nothing in FF3 calls it.
// FF3 0x00161af0

int EffEneTimerRearm(EFF_ENE_TIMER_OWNER *obj, int mode)
{
    EFF_ENE_TIMER *tm = &obj->tm;
    float lo = -0.2f;                   // rdata 0x36e050
    float hi = 0.13f;                   // rdata 0x36e054
    int   t;

    if (tm->cnt > 0)
        return 0;

    t = GetRandValIByF(0) + 4;
    tm->cnt = t;
    tm->time0 = t;
    tm->time1 = GetRandValIByF(0) + 60;
    tm->time3 = GetRandValIByF(20) + 80;
    if (mode == 0)
    {
        tm->b = hi;
        tm->a = lo;
    }
    else if (mode == 1)
    {
        tm->b = lo;
        tm->a = hi;
    }
    return 1;
}

static void EneAuraDrawLayers(ENE_AURA_DAT *fx, float *xoff, float *yoff, u_char alpha);

// The four layers' offsets: sin(freq * (time + phase) * pi / 180) * amp,
// phases 0 / 120 / 240 per axis and layer, some about 1 pixel.
// FF3 0x00161bb8.

void EneAuraDraw(ENE_AURA_DAT *fx, u_char alpha, float time)
{
    const float     pi = 3.14159274f;       // rdata 0x36e058
    const float     deg = 180.0f;           // rdata 0x36e05c
    ENE_AURA_LAYER *l = fx->layer;
    float           t120 = time + 120.0f;   // rdata 0x36e064
    float           t240 = time + 240.0f;   // rdata 0x36e068
    float           x[4];
    float           y[4];

    x[0] = Vu0Sinf((l[0].freq_x * time * pi) / deg) * l[0].amp_x;
    y[0] = Vu0Sinf((l[0].freq_y * time * pi) / deg) * l[0].amp_y;
    x[1] = Vu0Sinf((l[1].freq_x * t120 * pi) / deg) * l[1].amp_x + 1.0f;
    y[1] = Vu0Sinf((l[1].freq_y * time * pi) / deg) * l[1].amp_y + 1.0f;
    x[2] = Vu0Sinf((l[2].freq_x * t240 * pi) / deg) * l[2].amp_x + 1.0f;
    y[2] = Vu0Sinf((l[2].freq_y * t120 * pi) / deg) * l[2].amp_y;
    x[3] = Vu0Sinf((l[3].freq_x * time * pi) / deg) * l[3].amp_x;
    y[3] = Vu0Sinf((l[3].freq_y * t240 * pi) / deg) * l[3].amp_y + 1.0f;
    EneAuraDrawLayers(fx, x, y, alpha);
}

// The work frame as a texture (TEX0 TBP 0x2bc0, 1024 x 512, CT32 with its
// alpha), then per layer its ALPHA and a full-screen TRISTRIP of ten
// 64-pixel columns, shifted by (xoff, yoff), RGBA (gray, gray, gray,
// alpha * layer alpha). The first column pair carries ADC (no kick).
// FF3 0x002b0770 (out of line).
// callcheck: __fixunssfsi (the (u_int) casts)

static void EneAuraDrawLayers(ENE_AURA_DAT *fx, float *xoff, float *yoff, u_char alpha)
{
    static const GS_REG_SET regs[3] = {
        { 0x161, SCE_GS_TEX1_1 },
        { 0x3000d, SCE_GS_TEST_1 },
        { 0x10a000118ULL, SCE_GS_ZBUF_1 },
    };
    u_int   xs[4][11];
    u_int   ys[4][2];
    u_int   us[11];
    u_int   v0 = 8;
    u_int   v1 = 0x1bf8;                    // 448 * 16 - 8
    float   fa = (float)(int)alpha;
    u_long *p;
    u_int  *q;
    u_int   i, j;

    for (i = 0; i < 4; i++)
    {
        float x = xoff[i] + 2048.0f;        // rdata 0x36e090..0x36e0a0
        float y;

        for (j = 0; j < 11; j++)
            xs[i][j] = (u_int)(((x - 320.0f) + (float)j * 64.0f) * 16.0f);
        y = yoff[i] + 2048.0f;
        ys[i][0] = (u_int)((y - 224.0f) * 16.0f);
        ys[i][1] = (u_int)((y + 224.0f) * 16.0f);
    }
    for (j = 0; j < 11; j++)
        us[j] = j << 10;
    us[0] += 8;
    us[10] -= 8;

    SetGsRegsCached(regs, 3, 1);
    p = (u_long *)StartDmaDirectTrans();
    p[0] = 0x1000000000008002ULL;
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = 0x200000066802abc0ULL;           // rdata 0x36e080
    p[5] = SCE_GS_TEX0_1;
    EndDmaDirectTrans((qword *)&p[6]);

    for (i = 0; i < fx->num; i++)
    {
        ENE_AURA_LAYER *l = &fx->layer[i];

        SetGsRegCached(((u_long)(u_int)l->alpha_fix << 32) | (u_int)l->alpha_a
                       | ((u_long)(u_int)l->alpha_c << 4) | ((u_long)(u_int)l->alpha_b << 2)
                       | ((u_long)(u_int)l->alpha_d << 6), SCE_GS_ALPHA_1, 1);
        p = (u_long *)StartDmaDirectTrans();
        p[0] = 0x1020400000008001ULL;
        p[1] = 1;                           // RGBAQ
        q = (u_int *)&p[2];
        q[0] = (u_int)l->gray;
        q[1] = (u_int)l->gray;
        q[2] = (u_int)l->gray;
        q[3] = (u_int)(int)(fa * l->alpha);
        p[4] = 0x40aa40000000800bULL;       // rdata 0x36e088: TRISTRIP TME ABE FST, 11 x {UV, XYZ2, UV, XYZ2}
        p[5] = 0x4343;
        q = (u_int *)&p[6];
        for (j = 0; j < 11; j++)
        {
            u_int adc = (j == 0) ? 0x8000 : 0;

            q[0]  = us[j];    q[1]  = v0;       q[2]  = 0;       q[3]  = 0;
            q[4]  = xs[i][j]; q[5]  = ys[i][0]; q[6]  = 0xfff5f; q[7]  = adc;
            q[8]  = us[j];    q[9]  = v1;       q[10] = 0;       q[11] = 0;
            q[12] = xs[i][j]; q[13] = ys[i][1]; q[14] = 0xfff5f; q[15] = adc;
            q += 16;
        }
        EndDmaDirectTrans((qword *)q);

        // PC: the strip as one quad over the 640 x 448 frame, moved by the
        // layer's wobble, sampling the work frame half a texel in from each
        // edge (us / v0 / v1). The work frame is a capture slot holding the
        // window, so the renderer grows the quad to it like any frame copy.
        // Only after a host work frame: the old path has nothing to sample.
        if (ene_aura_host != 0)
        {
            static const u_long tex1 = 0x161;
            const u_long tex0 = 0x200000066802abc0ULL;
            float xy[8];
            float uv[8] = { 0.5f, 0.5f, 639.5f, 0.5f, 0.5f, 447.5f, 639.5f, 447.5f };

            xy[0] = xoff[i];          xy[1] = yoff[i];
            xy[2] = xoff[i] + 640.0f; xy[3] = yoff[i];
            xy[4] = xoff[i];          xy[5] = yoff[i] + 448.0f;
            xy[6] = xoff[i] + 640.0f; xy[7] = yoff[i] + 448.0f;
            ReiPan_RendererDrawTexturedQuad((const sceGsTex0 *)&tex0, (const sceGsTex1 *)&tex1,
                                            xy, uv, (u_char)l->gray, (u_char)l->gray,
                                            (u_char)l->gray, (u_char)(int)(fa * l->alpha));
        }
    }
}

// ---- the finder sparks (FF3-only, unreferenced) --------------------------------------

// The manager singleton (EffectFinderCreate's).
static CEffectFinderMgr *effect_finder;         // sbss 0x372f34

// FF3 0x00161f38

CEffectFinderMgr::CEffectFinderMgr()
{
    int i;

    for (i = 0; i < EFFECT_FINDER_NUM; i++)
        slot[i] = nullptr;
}

// FF3 0x00161f60 (EffectFinderMgrDtorBase) is the base-object twin with the same body.
// FF3 0x00161f78

CEffectFinderMgr::~CEffectFinderMgr()
{
    DeleteAll();
}

// Steps every effect and drops the ones that report their end.
// FF3 0x00161f90

void CEffectFinderMgr::Update()
{
    int i;

    for (i = 0; i < EFFECT_FINDER_NUM; i++)
    {
        if (slot[i] == nullptr)
            continue;

        slot[i]->Update();
        if (slot[i]->IsEnd())
        {
            delete slot[i];             // FF3: virtual deleting dtor (vtable +0x04)
            slot[i] = nullptr;
        }
    }
}

// FF3 0x00162020

void CEffectFinderMgr::Draw()
{
    int i;

    for (i = 0; i < EFFECT_FINDER_NUM; i++)
    {
        if (slot[i] != nullptr)
            slot[i]->Draw();
    }
}

// FF3 0x00162078

void CEffectFinderMgr::Main()
{
    Update();
    Draw();
}

// The first free slot gets a spark (kind 0) or a bare base effect (kind 1;
// other kinds leave it free). 1 when a slot was free, 0 when all 20 are in use.
// FF3 0x001620a0
// callcheck: operator_new, EffectFinderSparkCtor (new CEffectFinderSpark; the base is built inline)

int CEffectFinderMgr::Req(float x, float y, float z, int kind)
{
    int i;

    for (i = 0; i < EFFECT_FINDER_NUM; i++)
    {
        if (slot[i] != nullptr)
            continue;

        if (kind == 0)
            slot[i] = new CEffectFinderSpark(x, y, z);
        else if (kind == 1)
            slot[i] = new CEffectFinder(x, y, z);
        return 1;
    }
    return 0;
}

// Deletes every effect but leaves the slots pointing at them, as the ROM
// does: only the destructor's use is safe (EffectFinderDeleteAll's would
// leave dangling pointers; nothing calls it).
// FF3 0x00162180

void CEffectFinderMgr::DeleteAll()
{
    int i;

    for (i = 0; i < EFFECT_FINDER_NUM; i++)
    {
        if (slot[i] != nullptr)
            delete slot[i];             // FF3: virtual deleting dtor (vtable +0x04)
    }
}

// FF3 0x001621d8
// callcheck: operator_new, EffectFinderMgrCtor (new CEffectFinderMgr)

void EffectFinderCreate(void)
{
    effect_finder = new CEffectFinderMgr;
}

// FF3 0x00162210
// callcheck: EffectFinderMgrDtor, operator_delete (delete effect_finder)

void EffectFinderRelease(void)
{
    if (effect_finder != nullptr)
        delete effect_finder;
    effect_finder = nullptr;
}

// No check for the singleton, as in the ROM.
// FF3 0x00162250

void EffectFinderMain(void)
{
    effect_finder->Main();
}

// FF3 0x00162268

int EffectFinderReq(float x, float y, float z, int kind)
{
    if (effect_finder == nullptr)
        return 0;

    return effect_finder->Req(x, y, z, kind);
}

// FF3 0x001622a0

void EffectFinderDeleteAll(void)
{
    if (effect_finder != nullptr)
        effect_finder->DeleteAll();
}

// The particle list's bounds-checked read (inline in the ROM).

static EFF_FINDER_PTCL *EffectFinderPtclGet(EFF_FINDER_PTCL_LIST *list, int i)
{
    if (i < 0 || i >= list->num)
        return nullptr;

    return list->ptcl[i];
}

// One particle: effdat[sprite] centred on (x, y), scaled and rotated about
// it, at the particle's colour and alpha, blended by its ALPHA table entry.
// FF3 0x002b0ed0 (out of line).

static void EffectFinderPtclDraw(EFF_FINDER_PTCL *p)
{
    static const u_long alpha_tbl[4] = { 0x44, 0x48, 0x42, 0x84 };     // rdata 0x354ae0
    const SPRT_DAT *spr;
    DISP_SPRT ds;
    float x, y;

    if (p->sprite < 0 || p->alpha <= 0)
        return;

    spr = &effdat[p->sprite];
    x = p->x - (float)spr->w * 0.5f;    // rdata 0x36e0b4
    y = p->y - (float)spr->h * 0.5f;
    CopySprDToSpr(&ds, spr);
    ds.alpha = (u_char)p->alpha;
    ds.x = x;
    ds.y = y;
    ds.scw = p->scw;
    ds.sch = p->sch;
    ds.rot = p->rot;
    ds.crx = p->x;
    ds.cry = p->y;
    ds.csx = p->x;
    ds.csy = p->y;
    ds.r = p->r;
    ds.g = p->g;
    ds.b = p->b;
    ds.tex1 = 0x161;
    ds.alphar = alpha_tbl[p->blend];
    DispSprD(&ds);
}

// FF3 0x002b0ba8 (EffectFinderDtorDelete) is the deleting one (adds operator delete).
// FF3 0x002b0b98

CEffectFinder::~CEffectFinder()
{
}

// Each particle moves by its velocity (no decay).
// FF3 0x002b0bc8

void CEffectFinder::Update()
{
    int i;

    if (list == nullptr)
        return;

    for (i = 0; i < list->num; i++)
    {
        EFF_FINDER_PTCL *p = list->ptcl[i];

        p->x += p->vx;
        p->y += p->vy;
        p->rot += p->vrot;
    }
}

// FF3 0x002b0c40

void CEffectFinder::Draw()
{
    int i;

    if (list == nullptr)
        return;

    for (i = 0; i < list->num; i++)
        EffectFinderPtclDraw(list->ptcl[i]);
}

// FF3 0x002b0ca8

int CEffectFinder::IsEnd()
{
    return 1;
}

// 20..29 particles of effdat[0x54] at (x, y), alpha 0x80, flying out at
// (-5..5, -5..5) a frame. The third random number lands in scw (-0.5..0.5:
// the sprite flips and shrinks to nothing) while sch stays 1 and vrot 0;
// z is stored but never used.
// FF3 0x002b0cb0 (out of line).
// callcheck: operator_new, operator_new_array (the list, its pointer array and the particles)

CEffectFinderSpark::CEffectFinderSpark(float x, float y, float z) : CEffectFinder(x, y, z)
{
    EFF_FINDER_PTCL_LIST *l;
    int i;

    num = GetRandValIByF(10) + 20;
    l = new EFF_FINDER_PTCL_LIST;
    l->num = num;
    l->ptcl = new EFF_FINDER_PTCL *[l->num];
    for (i = 0; i < l->num; i++)
    {
        EFF_FINDER_PTCL *p = new EFF_FINDER_PTCL;

        p->x = 0.0f;
        p->y = 0.0f;
        p->rot = 0.0f;
        p->vx = 0.0f;
        p->vy = 0.0f;
        p->vrot = 0.0f;
        p->scw = 1.0f;                  // rdata 0x36e0a8
        p->sch = 1.0f;
        p->r = 0x80;
        p->g = 0x80;
        p->b = 0x80;
        p->alpha = 0x80;
        p->sprite = -1;
        p->blend = 0;
        l->ptcl[i] = p;
    }
    list = l;

    for (i = 0; i < num; i++)
    {
        EFF_FINDER_PTCL *p = EffectFinderPtclGet(list, i);

        if (p == nullptr)
            continue;

        p->x = this->x;
        p->y = this->y;
        p->vx = (GetRandValF(1.0f) - 0.5f) * 10.0f;    // rdata 0x36e0ac / 0x36e0b0
        p->vy = (GetRandValF(1.0f) - 0.5f) * 10.0f;
        p->sprite = 0x54;
        p->alpha = 0x80;
        p->scw = GetRandValF(1.0f) - 0.5f;
    }
}

// FF3 0x002b10a8 (EffectFinderSparkDtorDelete) is the deleting one (adds operator delete).
// FF3 0x002b0ff0
// callcheck: operator_delete, operator_delete_array (the particles, the pointer array and the list)

CEffectFinderSpark::~CEffectFinderSpark()
{
    int i;

    if (list == nullptr)
        return;

    for (i = 0; i < list->num; i++)
    {
        if (list->ptcl[i] != nullptr)
            delete list->ptcl[i];
    }
    if (list->ptcl != nullptr)
        delete[] list->ptcl;
    delete list;
}

// Each particle moves, then its velocity, scale and alpha shrink by 0.9.
// FF3 0x002b1160

void CEffectFinderSpark::Update()
{
    const float k = 0.9f;               // rdata 0x36e0b8
    int i;

    for (i = 0; i < num; i++)
    {
        EFF_FINDER_PTCL *p = EffectFinderPtclGet(list, i);

        if (p == nullptr)
            continue;

        p->rot += p->vrot;
        p->x += p->vx;
        p->y += p->vy;
        p->vx *= k;
        p->vy *= k;
        p->scw *= k;
        p->sch *= k;
        p->alpha = (short)(int)((float)p->alpha * k);
    }
}

// Over once no particle has alpha left.
// FF3 0x002b1238

int CEffectFinderSpark::IsEnd()
{
    int i;

    for (i = 0; i < num; i++)
    {
        EFF_FINDER_PTCL *p = EffectFinderPtclGet(list, i);

        if (p != nullptr && p->alpha > 0)
            return 0;
    }
    return 1;
}
