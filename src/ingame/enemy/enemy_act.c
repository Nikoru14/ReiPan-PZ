// ingame/enemy/enemy_act.c -- the ghost behaviour-script interpreter (FF3
// 0x18a338..0x1931e0; FF2 enemy_act.c, with EneActSet / EneBlinkSet from FF2
// enemy.c).
//
// A ghost's actions are byte-code scripts in the ALG pak (ENE_ACT01_OBJ, at
// EE 0x12ef000): a u16 table by enemy type leads to a row per ENE_CMN.kind,
// whose u16 entries are the actions. ENEALG_WRK (ENE_WRK +0xa0) is the
// cursor: EneActSet points comm_add at an action, EneAlgCtrl counts
// wait_time down by the enemy's step and, once it runs out, fetches opcodes
// and calls their handlers until one leaves a positive wait (255 ends the
// run). A handler that wants another frame leaves pos_no != 0 and is called
// again without a fetch. Operands are little-endian byte pairs, jumps are
// u16 offsets from comm_add_top. FF3 renumbered FF2's opcodes: the four
// ranges keep FF2's bounds (0x00..0x6f control, 0x70..0x9f move, 0xa0..0xdf
// branch, 0xe0..0xfe effect) but the tables are FF3's own (0x48 / 14 / 0x36 /
// 8 entries); the handlers are named after their FF3 slot. Frame counts in
// the scripts are 60 Hz, divided by GetFrameTimeScale (1.2 in PAL).
//
// The blink script beside it (bcomm_add, table of 7) takes the ENE_WRK: the
// alpha wobble and the branches on the fade flags.
//
// Reconstructed from FF3; MioPan's enemy_act.c gave the shape.
#include "common/reipan_stub.h"

#include "ingame/enemy/enemy_act.h"
#include "common/vu0math.h"                     // Vu0Sinf, RotVectorZXY, RotLimitChk
#include "graphics/effect/effect_obj.h"         // EffLanternCut
#include "graphics/effect/effect_oth.h"         // CallNega2
#include "ingame/tattoo_event.h"                 // TattooEvStart / Resume / IsLate / IsIdle, TattooScrEffectRestore
#include "graphics/effect/effect_sub.h"         // Get2PLength
#include "graphics/graph2d/fade.h"              // FadeInReq, FadeOutReq
#include "ingame/candle_ctrl.h"                 // GetCandleControl, CandleControl::IsOut
#include "ingame/map/MapCtrl.h"                 // MapAreaHitLineCheck
#include "graphics/game_anime_model.h"          // GameAnimeModel::GetAnimeLength
#include "ingame/char_model.h"                  // CharModel::GetMotionYaw, GetRootMove
#include "ingame/ingame.h"                      // SndSePlayStd
#include "ingame/enemy/enemy.h"                 // EneMotScrAttrGet
#include "ingame/photo/finder_photo.h"          // GetAlbumSaveDisp
#include "ingame/plyr/player.h"                 // PLYR_WRK
#include "ingame/plyr/plyr_gauge.h"             // PlyrGaugeDmgRate
#include "reipan/reipan_memory.h"               // PC: ReiPan_GetHostPointer
#include "reipan/os/reipan_rand.h"              // PC: ReiPan_Rand, REIPAN_RAND_MAXF
#include "system/os/system.h"                   // GetFrameTimeScale
#include "graphics/graph3d/gra3d.h"             // gra3dcamGetPosition
#include "ingame/camera/map_camera.h"           // MapCamSetFinCamera, ReqFinderInOverRap
#include "ingame/enemy/fly_ctrl.h"              // EneFlyActByKind, EraseEneFlyWork
#include "ingame/enemy/random_ghost.h"          // RandGhostInfoCopy
#include "ingame/ingame_wrk.h"                  // ingame_wrk.plyr_wrk
#include "ingame/plyr/plyr_mdl.h"               // PlyrFinderModeCancel
#include "ingame/plyr/plyr_sub.h"               // PlyrTurnFlagClear
#include "ingame/plyr/unit_ctl.h"               // UnitGetTrgtRot
#include "ingame/plyr/plyr_shot.h"           // FinderViewTurnToEne
#include "ingame/sound/snd_ctrl.h"              // SndSePlay
#include "ingame/story_menu.h"                  // LockTblInc / Dec
#include "sdk/libvu0.h"                         // sceVu0AddVector

#include <math.h>                               // fabs, fabsf, sqrtf
#include <string.h>                             // memset, memcpy


typedef void (*ENE_JOB_FUNC)(ENEALG_WRK *alg);
typedef void (*ENE_BJOB_FUNC)(ENE_WRK *ene);

// The ALG pak's EE address (Boot_Load loads ENE_ACT01_OBJ 0xac3 there).
#define ENE_ALG_PAK_ADDR 0x12ef000

// ──────────────────────────────────────────────────────────────────────
// The handlers, by table slot (FF3 0x18a910..0x1931dc).
// ──────────────────────────────────────────────────────────────────────

void BJobL00(ENE_WRK *ene);
void BJobL01(ENE_WRK *ene);
void BJobL02(ENE_WRK *ene);
void BJobL03(ENE_WRK *ene);
void BJobL04(ENE_WRK *ene);
void BJobL05(ENE_WRK *ene);
void BJobL06(ENE_WRK *ene);

void EJobC00(ENEALG_WRK *alg);
void EJobC01(ENEALG_WRK *alg);
void EJobC02(ENEALG_WRK *alg);
void EJobC03(ENEALG_WRK *alg);
void EJobC04(ENEALG_WRK *alg);
void EJobC05(ENEALG_WRK *alg);
void EJobC06(ENEALG_WRK *alg);
void EJobC07(ENEALG_WRK *alg);
void EJobC08(ENEALG_WRK *alg);
void EJobC09(ENEALG_WRK *alg);
void EJobC0A(ENEALG_WRK *alg);
void EJobC0B(ENEALG_WRK *alg);
void EJobC0C(ENEALG_WRK *alg);
void EJobC0D(ENEALG_WRK *alg);
void EJobC0E(ENEALG_WRK *alg);
void EJobC0F(ENEALG_WRK *alg);
void EJobC10(ENEALG_WRK *alg);
void EJobC11(ENEALG_WRK *alg);
void EJobC12(ENEALG_WRK *alg);
void EJobC13(ENEALG_WRK *alg);
void EJobC14(ENEALG_WRK *alg);
void EJobC15(ENEALG_WRK *alg);
void EJobC16(ENEALG_WRK *alg);
void EJobC17(ENEALG_WRK *alg);
void EJobC18(ENEALG_WRK *alg);
void EJobC19(ENEALG_WRK *alg);
void EJobC1A(ENEALG_WRK *alg);
void EJobC1B(ENEALG_WRK *alg);
void EJobC1C(ENEALG_WRK *alg);
void EJobC1D(ENEALG_WRK *alg);
void EJobC1E(ENEALG_WRK *alg);
void EJobC1F(ENEALG_WRK *alg);
void EJobC20(ENEALG_WRK *alg);
void EJobC21(ENEALG_WRK *alg);
void EJobC22(ENEALG_WRK *alg);
void EJobC23(ENEALG_WRK *alg);
void EJobC24(ENEALG_WRK *alg);
void EJobC25(ENEALG_WRK *alg);
void EJobC26(ENEALG_WRK *alg);
void EJobC27(ENEALG_WRK *alg);
void EJobC28(ENEALG_WRK *alg);
void EJobC29(ENEALG_WRK *alg);
void EJobC2A(ENEALG_WRK *alg);
void EJobC2B(ENEALG_WRK *alg);
void EJobC2C(ENEALG_WRK *alg);
void EJobC2D(ENEALG_WRK *alg);
void EJobC2E(ENEALG_WRK *alg);
void EJobC2F(ENEALG_WRK *alg);
void EJobC30(ENEALG_WRK *alg);
void EJobC31(ENEALG_WRK *alg);
void EJobC32(ENEALG_WRK *alg);
void EJobC33(ENEALG_WRK *alg);
void EJobC34(ENEALG_WRK *alg);
void EJobC35(ENEALG_WRK *alg);
void EJobC36(ENEALG_WRK *alg);
void EJobC37(ENEALG_WRK *alg);
void EJobC38(ENEALG_WRK *alg);
void EJobC39(ENEALG_WRK *alg);
void EJobC3A(ENEALG_WRK *alg);
void EJobC3B(ENEALG_WRK *alg);
void EJobC3C(ENEALG_WRK *alg);
void EJobC3D(ENEALG_WRK *alg);
void EJobC3E(ENEALG_WRK *alg);
void EJobC3F(ENEALG_WRK *alg);
void EJobC40(ENEALG_WRK *alg);
void EJobC41(ENEALG_WRK *alg);
void EJobC42(ENEALG_WRK *alg);
void EJobC43(ENEALG_WRK *alg);
void EJobC44(ENEALG_WRK *alg);
void EJobC45(ENEALG_WRK *alg);
void EJobC46(ENEALG_WRK *alg);
void EJobC47(ENEALG_WRK *alg);
void EJobM00(ENEALG_WRK *alg);
void EJobM01(ENEALG_WRK *alg);
void EJobM02(ENEALG_WRK *alg);
void EJobM03(ENEALG_WRK *alg);
void EJobM04(ENEALG_WRK *alg);
void EJobM05(ENEALG_WRK *alg);
void EJobM06(ENEALG_WRK *alg);
void EJobM07(ENEALG_WRK *alg);
void EJobM08(ENEALG_WRK *alg);
void EJobM09(ENEALG_WRK *alg);
void EJobM0A(ENEALG_WRK *alg);
void EJobM0B(ENEALG_WRK *alg);
void EJobM0C(ENEALG_WRK *alg);
void EJobM0D(ENEALG_WRK *alg);
void EJobB00(ENEALG_WRK *alg);
void EJobB01(ENEALG_WRK *alg);
void EJobB02(ENEALG_WRK *alg);
void EJobB03(ENEALG_WRK *alg);
void EJobB04(ENEALG_WRK *alg);
void EJobB05(ENEALG_WRK *alg);
void EJobB06(ENEALG_WRK *alg);
void EJobB07(ENEALG_WRK *alg);
void EJobB08(ENEALG_WRK *alg);
void EJobB09(ENEALG_WRK *alg);
void EJobB0A(ENEALG_WRK *alg);
void EJobB0B(ENEALG_WRK *alg);
void EJobB0C(ENEALG_WRK *alg);
void EJobB0D(ENEALG_WRK *alg);
void EJobB0E(ENEALG_WRK *alg);
void EJobB0F(ENEALG_WRK *alg);
void EJobB10(ENEALG_WRK *alg);
void EJobB11(ENEALG_WRK *alg);
void EJobB12(ENEALG_WRK *alg);
void EJobB13(ENEALG_WRK *alg);
void EJobB14(ENEALG_WRK *alg);
void EJobB15(ENEALG_WRK *alg);
void EJobB16(ENEALG_WRK *alg);
void EJobB17(ENEALG_WRK *alg);
void EJobB18(ENEALG_WRK *alg);
void EJobB19(ENEALG_WRK *alg);
void EJobB1A(ENEALG_WRK *alg);
void EJobB1B(ENEALG_WRK *alg);
void EJobB1C(ENEALG_WRK *alg);
void EJobB1D(ENEALG_WRK *alg);
void EJobB1E(ENEALG_WRK *alg);
void EJobB1F(ENEALG_WRK *alg);
void EJobB20(ENEALG_WRK *alg);
void EJobB21(ENEALG_WRK *alg);
void EJobB22(ENEALG_WRK *alg);
void EJobB23(ENEALG_WRK *alg);
void EJobB24(ENEALG_WRK *alg);
void EJobB25(ENEALG_WRK *alg);
void EJobB26(ENEALG_WRK *alg);
void EJobB27(ENEALG_WRK *alg);
void EJobB28(ENEALG_WRK *alg);
void EJobB29(ENEALG_WRK *alg);
void EJobB2A(ENEALG_WRK *alg);
void EJobB2B(ENEALG_WRK *alg);
void EJobB2C(ENEALG_WRK *alg);
void EJobB2D(ENEALG_WRK *alg);
void EJobB2E(ENEALG_WRK *alg);
void EJobB2F(ENEALG_WRK *alg);
void EJobB30(ENEALG_WRK *alg);
void EJobB31(ENEALG_WRK *alg);
void EJobB32(ENEALG_WRK *alg);
void EJobB33(ENEALG_WRK *alg);
void EJobB34(ENEALG_WRK *alg);
void EJobE00(ENEALG_WRK *alg);
void EJobE01(ENEALG_WRK *alg);
void EJobE02(ENEALG_WRK *alg);
void EJobE03(ENEALG_WRK *alg);
void EJobE04(ENEALG_WRK *alg);
void EJobE05(ENEALG_WRK *alg);
void EJobE06(ENEALG_WRK *alg);
void EJobE07(ENEALG_WRK *alg);

// ──────────────────────────────────────────────────────────────────────
// The jump tables (rodata 0x3013a0..0x301610).
// ──────────────────────────────────────────────────────────────────────

// the blink script (data 0x3013a0; the eighth word is 0)
static ENE_BJOB_FUNC const ene_bjob_tbl[] =
{
    BJobL00, BJobL01, BJobL02, BJobL03,
    BJobL04, BJobL05, BJobL06, nullptr,
};

// control: opcodes 0x00..0x47 (data 0x3013c0)
static ENE_JOB_FUNC const ene_job_cont_tbl[] =
{
    EJobC00, EJobC01, EJobC02, EJobC03,
    EJobC04, EJobC05, EJobC06, EJobC07,
    EJobC08, EJobC09, EJobC0A, EJobC0B,
    EJobC0C, EJobC0D, EJobC0E, EJobC0F,
    EJobC10, EJobC11, EJobC12, EJobC13,
    EJobC14, EJobC15, EJobC16, EJobC17,
    EJobC18, EJobC19, EJobC1A, EJobC1B,
    EJobC1C, EJobC1D, EJobC1E, EJobC1F,
    EJobC20, EJobC21, EJobC22, EJobC23,
    EJobC24, EJobC25, EJobC26, EJobC27,
    EJobC28, EJobC29, EJobC2A, EJobC2B,
    EJobC2C, EJobC2D, EJobC2E, EJobC2F,
    EJobC30, EJobC31, EJobC32, EJobC33,
    EJobC34, EJobC35, EJobC36, EJobC37,
    EJobC38, EJobC39, EJobC3A, EJobC3B,
    EJobC3C, EJobC3D, EJobC3E, EJobC3F,
    EJobC40, EJobC41, EJobC42, EJobC43,
    EJobC44, EJobC45, EJobC46, EJobC47,
};

// move: opcodes 0x70..0x7d (data 0x3014e0)
static ENE_JOB_FUNC const ene_job_move_tbl[] =
{
    EJobM00, EJobM01, EJobM02, EJobM03,
    EJobM04, EJobM05, EJobM06, EJobM07,
    EJobM08, EJobM09, EJobM0A, EJobM0B,
    EJobM0C, EJobM0D,
};

// branch: opcodes 0xa0..0xd5 (data 0x301518)
static ENE_JOB_FUNC const ene_job_brnch_tbl[] =
{
    EJobB00, EJobB01, EJobB02, EJobB03,
    EJobB04, EJobB05, EJobB06, EJobB07,
    EJobB08, EJobB09, EJobB0A, EJobB0B,
    EJobB0C, EJobB0D, EJobB0E, EJobB0F,
    EJobB10, EJobB11, EJobB12, EJobB13,
    EJobB14, EJobB15, EJobB16, EJobB17,
    EJobB18, EJobB19, EJobB1A, EJobB1B,
    EJobB1C, EJobB1D, EJobB1E, EJobB1F,
    EJobB20, EJobB21, EJobB22, EJobB23,
    EJobB24, EJobB25, EJobB26, EJobB27,
    EJobB28, EJobB29, EJobB2A, EJobB2B,
    EJobB2C, EJobB2D, EJobB2E, EJobB2F,
    EJobB30, EJobB31, EJobB32, EJobB33,
    EJobB34, nullptr,
};

// effect: opcodes 0xe0..0xe7 (data 0x3015f0)
static ENE_JOB_FUNC const ene_job_eff_tbl[] =
{
    EJobE00, EJobE01, EJobE02, EJobE03,
    EJobE04, EJobE05, EJobE06, EJobE07,
};

#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

// The per-slot work some handlers keep across frames (bss 0x3df2c0..).
static u_short ene_job_jump[ENE_WRK_MAX];       // FF3 bss 0x3df2c0: EJobC02's target offset
static float   ene_fade_out_time[ENE_WRK_MAX];  // FF3 bss 0x3df2e0: EJobC0E's length in frames
static float   ene_fade_out_alpha[ENE_WRK_MAX]; // FF3 bss 0x3df320: alpha_a at its start
static u_char  ene_fade_out_held[ENE_WRK_MAX];  // FF3 bss 0x3df360: 1: flags 0x400000 was already set
static float   ene_fade_in_time[ENE_WRK_MAX];   // FF3 bss 0x3df370: EJobC0F's length in frames
static float   ene_fade_in_alpha[ENE_WRK_MAX];  // FF3 bss 0x3df3b0: the alpha_a it ends on
static float   ene_fade_in_start[ENE_WRK_MAX];  // FF3 bss 0x3df3f0: alpha_a at its start
static u_char  ene_fade_in_held[ENE_WRK_MAX];   // FF3 bss 0x3df430: 1: flags 0x400000 was already set
static float   ene_fade_time[ENE_WRK_MAX];      // FF3 bss 0x3df440: EJobC11's length (loop[0] at its start)
static float   ene_fade_alpha[ENE_WRK_MAX];     // FF3 bss 0x3df480: alpha_a at its start
static float   ene_rate_out_time[ENE_WRK_MAX];  // FF3 bss 0x3df4c0: EJobC32's length in frames
static float   ene_rate_in_time[ENE_WRK_MAX];   // FF3 bss 0x3df500: EJobC33's length in frames
static u_char  ene_warp_alpha[ENE_WRK_MAX];     // FF3 bss 0x3df540: alpha_a while EJobM03 hides the enemy
static sceVu0FVECTOR ene_goal_pos[ENE_WRK_MAX]; // FF3 bss 0x3df550: EJobM0B's goal
static int     ene_circle_cnt;                  // FF3 sbss 0x372fdc (gp -0x514): EJobM0C's ease counter, one for all enemies
static u_short ene_hold_jump_end[ENE_WRK_MAX];  // FF3 bss 0x3df650: EJobB0E's jump once the hold has run its course
static u_short ene_hold_jump_free[ENE_WRK_MAX]; // FF3 bss 0x3df670: ... and once the player is free of it
static float   ene_hold_time[ENE_WRK_MAX];      // FF3 bss 0x3df690: frames in EJobB0E's step 1
static sceVu0FVECTOR ene_hold_rot[ENE_WRK_MAX]; // FF3 bss 0x3df6d0: [1] the facing the held player is turned to, [3] the player's facing before

// ──────────────────────────────────────────────────────────────────────
// Operand fetch. FF3 open-codes these at every handler (lo + hi * 0x100,
// a byte at a time: the stream is not aligned); MioPan's helpers stand in.
// ──────────────────────────────────────────────────────────────────────

static inline u_char EaGetU8(ENEALG_WRK *alg)
{
    return *alg->comm_add++;
}

static inline u_short EaGetU16(ENEALG_WRK *alg)
{
    u_short v = (u_short)(alg->comm_add[0] + alg->comm_add[1] * 0x100);

    alg->comm_add += 2;
    return v;
}

static inline void EaJumpTo(ENEALG_WRK *alg, u_short ofs)
{
    alg->comm_add = alg->comm_add_top + ofs;
}

static inline u_char EaBGetU8(ENE_WRK *ene)
{
    return *ene->alg.bcomm_add++;
}

static inline u_short EaBGetU16(ENE_WRK *ene)
{
    u_short v = (u_short)(ene->alg.bcomm_add[0] + ene->alg.bcomm_add[1] * 0x100);

    ene->alg.bcomm_add += 2;
    return v;
}

static inline u_short EaPakU16(const u_char *p)
{
    return (u_short)(p[0] + p[1] * 0x100);
}

// The enemy a cursor belongs to.
static inline ENE_WRK *EaEne(ENEALG_WRK *alg)
{
    return &ene_wrk[alg->idx];
}

// The usual handler end: next opcode in the same frame.
static inline void EaNext(ENEALG_WRK *alg)
{
    alg->pos_no    = 0;
    alg->wait_time = 0.0f;
}

static inline void EaBNext(ENE_WRK *ene)
{
    ene->bpos_no         = 0;
    ene->alg.bwait_time  = 0.0f;
}

// rand() / RAND_MAX as FF3 open-codes it (PC: ReiPan_Rand, the ROM's 31-bit
// range).
static inline float EaRand01(void)
{
    return (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
}

// The inline VU0 length FF3 measures enemies with: x and z only (vaddz.x
// adds the squares of x and z, y is dropped).
static inline float EaDistXZ(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dz * dz);
}

// The same with y: vadday.x + vmaddz.x with vf1 = (1, 1, 1, -1).
static inline float EaDist3D(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// GetCandleControl through the file's cache (FF3: sdata gp-0xb90, the inline
// getter's copy; random_ghost.c keeps its own).
static CandleControl *EaCandle(void)
{
    static CandleControl *candle;

    if (candle == nullptr)
        candle = GetCandleControl();
    return candle;
}

// ══════════════════════════════════════════════════════════════════════
// Starting a script
// ══════════════════════════════════════════════════════════════════════

// Action act_no of the enemy's row: the pak's u16 table by type gives the
// row of each ENE_CMN.kind, whose u16 entries are the actions. Wait 1, the
// call stack emptied, the move and turn vectors cleared, flags 0x518000
// (the per-action states) dropped.
// FF3 0x0018a338 (FF2 enemy.c EneActSet)

void EneActSet(ENE_WRK *ene, u_char act_no)
{
    u_char       *top = (u_char *)ReiPan_GetHostPointer(ENE_ALG_PAK_ADDR);
    const u_char *p;
    int           i;

    ene->alg.comm_add_top = top;
    ene->act_no = act_no;
    p = top + EaPakU16(top + ene->type * 2) + ene->cmn->kind * 2;
    p = top + EaPakU16(p) + act_no * 2;
    ene->alg.pos_no = 0;
    ene->alg.wait_time = 1.0f;
    ene->alg.comm_add = top + EaPakU16(p);
    for (i = 0; i < 4; i++)
    {
        ene->_370[i] = 0.0f;
        ene->turn[i] = 0.0f;
    }
    ene->alg.stack_p = ene->alg.stack_b;
    ene->flags &= ~(u_long)0x518000;
}

// The blink script at the first action of the enemy's row, wait 1.
// FF3 0x0018a468 (FF2 enemy.c EneBlinkSet)

void EneBlinkSet(ENE_WRK *ene)
{
    u_char       *top = (u_char *)ReiPan_GetHostPointer(ENE_ALG_PAK_ADDR);
    const u_char *p;

    ene->alg.bcomm_add_top = top;
    p = top + EaPakU16(top + ene->type * 2) + ene->cmn->kind * 2;
    ene->bpos_no = 0;
    ene->alg.bwait_time = 1.0f;
    ene->alg.bcomm_add = top + EaPakU16(top + EaPakU16(p));
}

// ══════════════════════════════════════════════════════════════════════
// The two machines
// ══════════════════════════════════════════════════════════════════════

// One frame of the action script: wait_time counts down by the step; once
// it is out, opcodes run until one leaves a positive wait or 255 (the end
// of the action: the run stops with wait 0).
// FF3 0x0018a520 (FF2 EneAlgCtrl)

void EneAlgCtrl(ENE_WRK *ene)
{
    ENEALG_WRK *alg = &ene->alg;
    ENE_JOB_FUNC func;
    u_char       job;

    if (alg->wait_time <= 0.0f)
        return;
    alg->wait_time -= ene->step;
    if (0.0f < alg->wait_time)
        return;

    for (;;)
    {
        if (alg->pos_no == 0)
        {
            alg->cur = alg->comm_add;
            alg->job_no = *alg->comm_add++;
        }
        job = alg->job_no;

        // PC: FF3 checks only the opcode ranges; an opcode past a table's end
        // would call the next table's entries (and a null slot address 0).
        // The scripts use none; here they do nothing.
        func = nullptr;
        if (job < 0x70)
            func = (job < COUNTOF(ene_job_cont_tbl)) ? ene_job_cont_tbl[job] : nullptr;
        else if ((u_char)(job - 0x70) < 0x30)
            func = ((u_char)(job - 0x70) < COUNTOF(ene_job_move_tbl)) ? ene_job_move_tbl[job - 0x70] : nullptr;
        else if ((u_char)(job - 0xa0) < 0x40)
            func = ((u_char)(job - 0xa0) < COUNTOF(ene_job_brnch_tbl)) ? ene_job_brnch_tbl[job - 0xa0] : nullptr;
        else if ((u_char)(job - 0xe0) < 0x1f)
            func = ((u_char)(job - 0xe0) < COUNTOF(ene_job_eff_tbl)) ? ene_job_eff_tbl[job - 0xe0] : nullptr;
        if (func != nullptr)
            func(alg);

        if (alg->wait_time == 255.0f)
        {
            alg->pos_no = 0;
            alg->wait_time = 0.0f;
            return;
        }
        if (0.0f < alg->wait_time)
            return;
    }
}

// One frame of the blink script: the same machine on bcomm_add, one table
// and no range check.
// FF3 0x0018a6b0 (FF2 EneBlinkCtrl)

void EneBlinkCtrl(ENE_WRK *ene)
{
    ENEALG_WRK   *alg = &ene->alg;
    ENE_BJOB_FUNC func;

    if (alg->bwait_time <= 0.0f)
        return;
    alg->bwait_time -= ene->step;
    if (0.0f < alg->bwait_time)
        return;

    for (;;)
    {
        if (ene->bpos_no == 0)
            ene->bjob_no = *alg->bcomm_add++;
        // PC: an opcode past the table (or its null eighth word) does nothing.
        func = (ene->bjob_no < COUNTOF(ene_bjob_tbl)) ? ene_bjob_tbl[ene->bjob_no] : nullptr;
        if (func != nullptr)
            func(ene);

        if (alg->bwait_time == 255.0f)
        {
            ene->bpos_no = 0;
            alg->bwait_time = 0.0f;
            return;
        }
        if (0.0f < alg->bwait_time)
            return;
    }
}

// ══════════════════════════════════════════════════════════════════════
// Requests from outside
// ══════════════════════════════════════════════════════════════════════

// An event (EvSetObjAlgorithm) or the player's death (PlyrDeathCtrl) asks an
// enemy for an action: act becomes its request (act_req), 0xff starts
// action 6 at once. 0 when the enemy is there but does not act; 1 otherwise,
// also when there is no such enemy.
// FF3 0x0018a790

int EneActReqById(u_char type, short id, u_char act)
{
    int no = SearchEneWrkNo(type, id);

    if (no >= 0)
    {
        ENE_WRK *ene = &ene_wrk[no];

        if (ene->state != ENE_STATE_ACT)
            return 0;
        if (act == 0xff)
            EneActSet(ene, 6);
        else
            ene->act_req = act;
    }
    return 1;
}

// Per frame (EneRule): an ended action (flags 0x80000, EJobC03) starts the
// idle action again -- 2 for the bound enemies (types 0 / 1), 1 for type 2.
// While attacking (0x4000): with no hold strength left the attack ends;
// during a hold (atk_type 3, 10 or 11) between the player's damage ticks
// the strength wears down by the player's gauge rate.
// FF3 0x0018a808

void EneActRule(ENE_WRK *ene)
{
    u_long fl = ene->flags;

    if ((fl & 0x80000) != 0)
    {
        u_char act = 2;

        fl &= ~(u_long)0x80000;
        ene->flags = fl;
        if (ene->type > 1)
            act = (ene->type == 2) ? 1 : 0xff;
        if (act != 0xff)
        {
            EneActSet(ene, act);
            fl = ene->flags;
        }
    }

    if ((fl & 0x4000) == 0)
        return;
    if (ene->hold_pow <= 0.0f)
    {
        ene->atk_type = 0;
        ene->flags = fl & ~(u_long)0x4000;
        return;
    }
    if ((ene->atk_type == 3 || ene->atk_type == 10 || ene->atk_type == 11) && ene->target->dmg_time <= 0.0f)
        ene->hold_pow -= PlyrGaugeDmgRate(GetAlbumSaveDisp());
}

// FF3 0x0018a900: empty.

void EneDamageHitHook(ENE_WRK *ene)
{
}

// FF3 0x0018a908: always 0 -- the end-of-action handler asks it whether a
// hit action carries on.

static int EneActEndHook(ENE_WRK *ene)
{
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// The blink script (table 0x3013a0)
// ══════════════════════════════════════════════════════════════════════

// 0x00  wait <u8 frames>; 0 waits a frame and stays (pos 2). Not scaled for
// PAL.
// FF3 0x0018a910

void BJobL00(ENE_WRK *ene)
{
    u_char t;

    switch (ene->bpos_no)
    {
    case 0:
        t = EaBGetU8(ene);
        if (t == 0)
        {
            ene->bpos_no = 2;
            ene->alg.bwait_time = 1.0f;
        }
        else
        {
            ene->bpos_no++;
            ene->alg.bwait_time = (float)t;
        }
        break;
    case 1:
        EaBNext(ene);
        break;
    case 2:
        ene->alg.bwait_time = 1.0f;
        break;
    }
}

// 0x01  jump <u16 offset>.
// FF3 0x0018a9a0

void BJobL01(ENE_WRK *ene)
{
    u_short ofs = EaBGetU16(ene);

    EaBNext(ene);
    ene->alg.bcomm_add = ene->alg.bcomm_add_top + ofs;
}

// 0x02  skips a 4-byte operand.
// FF3 0x0018a9e0

void BJobL02(ENE_WRK *ene)
{
    EaBNext(ene);
    ene->alg.bcomm_add += 4;
}

// 0x03  jump <u16 offset> while flags 0x400000 (a script fade, EJobC0E /
// EJobC0F) is set.
// FF3 0x0018aa00

void BJobL03(ENE_WRK *ene)
{
    u_short ofs = EaBGetU16(ene);

    if ((ene->flags & 0x400000) != 0)
        ene->alg.bcomm_add = ene->alg.bcomm_add_top + ofs;
    EaBNext(ene);
}

// 0x04  jump <u16 offset> while flags 0x800000 (EJobC0D) is set.
// FF3 0x0018aa50

void BJobL04(ENE_WRK *ene)
{
    u_short ofs = EaBGetU16(ene);

    if ((ene->flags & 0x800000) != 0)
        ene->alg.bcomm_add = ene->alg.bcomm_add_top + ofs;
    EaBNext(ene);
}

// 0x05  one step of the alpha wobble: alpha_b = _581 + sin(_584 degrees) *
// _582, the phase advancing by _583 times the step. With the motion
// script's attr 2 the wobble stops at its peak (the phase does not pass 90).
// FF3 0x0018aaa0

void BJobL05(ENE_WRK *ene)
{
    float  s = Vu0Sinf(((float)(u_short)ene->_584 * 3.1415927f) / 180.0f);
    u_int  ph = ((u_short)ene->_584 + ((int)((float)ene->_583 * ene->step) & 0xff)) % 360;

    ene->alpha_b = (u_char)((s_char)(int)(s * (float)ene->_582) + ene->_581);
    if ((EneMotScrAttrGet(ene, ene->_554, ene->_555) & 2) != 0 && ph > 90 && (u_short)ene->_584 <= 90)
    {
        EaBNext(ene);
        return;
    }
    ene->_584 = (short)ph;
    EaBNext(ene);
}

// 0x06  no operation.
// FF3 0x0018abb0

void BJobL06(ENE_WRK *ene)
{
    EaBNext(ene);
}

// ══════════════════════════════════════════════════════════════════════
// Control (table 0x3013c0, opcodes 0x00..0x47)
// ══════════════════════════════════════════════════════════════════════

// 0x00  jump <u16 offset>.
// FF3 0x0018abc0

void EJobC00(ENEALG_WRK *alg)
{
    u_short ofs = EaGetU16(alg);

    EaNext(alg);
    EaJumpTo(alg, ofs);
}

// 0x01  wait <u8 frames>, scaled for PAL and at least 1. (FF3 still tests
// for 0 after the clamp: pos 2, wait 1 for ever -- dead code.)
// FF3 0x0018abf8

void EJobC01(ENEALG_WRK *alg)
{
    u_int t;

    switch (alg->pos_no)
    {
    case 0:
        t = (u_int)(int)((float)EaGetU8(alg) / GetFrameTimeScale()) & 0xff;
        if (t == 0)
            t = 1;
        alg->pos_no++;
        alg->wait_time = (float)t;
        break;
    case 1:
        EaNext(alg);
        break;
    case 2:
        alg->wait_time = 1.0f;
        break;
    }
}

// 0x02  jump <u16 offset> after a frame.
// FF3 0x0018acd8

void EJobC02(ENEALG_WRK *alg)
{
    switch (alg->pos_no)
    {
    case 0:
        ene_job_jump[alg->idx] = EaGetU16(alg);
        alg->wait_time = 1.0f;
        alg->pos_no++;
        break;
    case 1:
        alg->pos_no = 0;
        EaJumpTo(alg, ene_job_jump[alg->idx]);
        alg->wait_time = 0.0f;
        break;
    case 2:
        alg->wait_time = 1.0f;
        break;
    }
}

// 0x03  end of the action: the per-action flags 0x519800 drop; unless a hit
// action (3) in a weak reaction carries on (EneActEndHook, never), act_no
// 0xff, flags 0x80008 (EneActRule restarts the idle action) and the run
// stops.
// FF3 0x0018ada8 (FF2 EJobC02)

void EJobC03(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      carry = 0;

    ene->flags &= ~(u_long)0x519800;
    if (ene->act_no == 3 && ene->dmg_react < 3)
        carry = EneActEndHook(ene);
    if (carry != 1)
    {
        ene->act_no = 0xff;
        ene->flags |= 0x80008;
        alg->pos_no = 0;
        alg->wait_time = 255.0f;
    }
}

// 0x04  anime <u8 anime> <u8 sub> <u16 start>: EneAnimeSet with a blend
// in; an auto enemy (type 2) with flags2 0x1000000 plays anime 1 instead.
// The start's high byte is dropped.
// FF3 0x0018ae98

void EJobC04(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    u_char   anime = EaGetU8(alg);
    u_char   sub   = EaGetU8(alg);
    u_char   start = (u_char)EaGetU16(alg);

    if (ene->type == 2 && (ene->flags2 & 0x1000000) != 0)
        anime = 1;
    EneAnimeSet(ene, anime, start, sub, 0, 0, 0, 0xff, 1);
    EaNext(alg);
}

// 0x05  loop[0] = <u16 frames>, scaled for PAL.
// FF3 0x0018af58

void EJobC05(ENEALG_WRK *alg)
{
    u_short t = EaGetU16(alg);

    EaNext(alg);
    alg->loop[0] = (float)t / GetFrameTimeScale();
}

// 0x06  loop[<u8 n>] = <u16 frames>, scaled for PAL.
// FF3 0x0018afc8

void EJobC06(ENEALG_WRK *alg)
{
    u_char  n = EaGetU8(alg);
    u_short t = EaGetU16(alg);

    EaNext(alg);
    alg->loop[n] = (float)t / GetFrameTimeScale();
}

// 0x07  the hit anime for the damage reaction: 2 / 5 the row's first, 3 its
// last, 4 one by the enemy data's byte +0xa5 (0: the first, 1: the second,
// 2: second or third half and half, else second / third / fourth a third
// each); other reactions play nothing. Rows (rodata 0x354c88): anime
// 10..14, or 22, 23, 23, 23, 24 for ENE_CMN.kind 0x24.
// FF3 0x0018b058

void EJobC07(ENEALG_WRK *alg)
{
    static const u_int dmg_anime[10] = { 10, 11, 12, 13, 14, 22, 23, 23, 23, 24 };
    ENE_WRK     *ene = EaEne(alg);
    const u_int *row = (ene->cmn->kind == 0x24) ? &dmg_anime[5] : &dmg_anime[0];
    u_int        anime;
    int          r;
    u_char       b;

    switch (ene->dmg_react)
    {
    case 2:
    case 5:
        anime = row[0];
        break;
    case 3:
        anime = row[4];
        break;
    case 4:
        // callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
        r = (int)(((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 100.0f + 0.0f);
        b = ene->dat->_a5[0];
        if (b == 1)
            anime = row[1];
        else if (b == 0)
            anime = row[0];
        else if (b == 2)
            anime = (r < 0x32) ? row[1] : row[2];
        else if (r < 0x21)
            anime = row[1];
        else if (r < 0x42)
            anime = row[2];
        else
            anime = row[3];
        break;
    default:
        EaNext(alg);
        return;
    }
    ene->_557 = 1;
    EneAnimeSet(ene, (u_char)anime, 0, 0, 0, 0, 0, 0xff, 1);
    EaNext(alg);
}

// 0x08  <u8 on>: flags 0x800000000 (down: no vertical move) on / off.
// FF3 0x0018b238

void EJobC08(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x800000000;
    else
        ene->flags |= (u_long)0x800000000;
    EaNext(alg);
}

// 0x09  call <u16 offset>: the return address on the stack (no overflow
// check).
// FF3 0x0018b2a0

void EJobC09(ENEALG_WRK *alg)
{
    u_short ofs = EaGetU16(alg);

    *alg->stack_p++ = alg->comm_add;
    alg->pos_no = 0;
    EaJumpTo(alg, ofs);
    alg->wait_time = 0.0f;
}

// 0x0a  return; an empty stack goes on.
// FF3 0x0018b2f8

void EJobC0A(ENEALG_WRK *alg)
{
    if (alg->stack_b < alg->stack_p)
        alg->comm_add = *--alg->stack_p;
    EaNext(alg);
}

// 0x0b  attack <u8 kind> <u16 offset>: EneAttackReq(dmg_type 3, kind); a
// refused attack (0: the player is on the ladder) jumps.
// FF3 0x0018b328 (was MakeDitherPattern, a wrong FF2 match)

void EJobC0B(ENEALG_WRK *alg)
{
    u_char  kind = EaGetU8(alg);
    u_short ofs  = EaGetU16(alg);

    if (EneAttackReq(EaEne(alg), 3, kind) == 0)
        EaJumpTo(alg, ofs);
    EaNext(alg);
}

// 0x0c  attack: EneAttackReq(dmg_type 1, kind 0xc).
// FF3 0x0018b3b8

void EJobC0C(ENEALG_WRK *alg)
{
    EneAttackReq(EaEne(alg), 1, 0xc);
    EaNext(alg);
}

// 0x0d  <u8 on>: flags 0x800000 (the blink script's BJobL04 branch) on / off.
// FF3 0x0018b408

void EJobC0D(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x800000;
    else
        ene->flags |= 0x800000;
    EaNext(alg);
}

// 0x0e  fade out <u16 frames> (PAL-scaled; a frame per run, not by the
// step): alpha_a and the two deform rates go from their start to 0. flags
// 0x400000 stays set while it runs (left alone if it was set before).
// FF3 0x0018b468

void EJobC0E(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no  = alg->idx;
    float    t;

    if (alg->pos_no == 0)
    {
        t = (float)EaGetU16(alg) / GetFrameTimeScale();
        alg->loop[0] = t;
        ene_fade_out_time[no] = t;
        ene_fade_out_held[no] = 0;
        ene_fade_out_alpha[no] = (float)ene->alpha_a;
        if ((ene->flags & 0x400000) == 0)
            ene->flags |= 0x400000;
        else
            ene_fade_out_held[no] = 1;
        alg->pos_no++;
        alg->wait_time = 1.0f;
    }
    else if (alg->pos_no != 1)
        return;

    t = alg->loop[0] - 1.0f;
    if (0.0f < t)
    {
        alg->loop[0] = t;
        ene->alpha_a = (u_char)(int)((ene_fade_out_alpha[no] * t) / ene_fade_out_time[no]);
        ene->eff.rate[0] = (ene->eff.deform_b1[0] * alg->loop[0]) / ene_fade_out_time[no];
        ene->eff.rate[1] = (ene->eff.deform_b1[1] * alg->loop[0]) / ene_fade_out_time[no];
        alg->wait_time = 1.0f;
        return;
    }

    alg->loop[0] = 0.0f;
    ene->alpha_a = 0;
    ene->eff.rate[0] = 0.0f;
    ene->eff.rate[1] = 0.0f;
    if (ene_fade_out_held[no] != 1)
        ene->flags &= ~(u_long)0x400000;
    alg->wait_time = 0.0f;
    alg->pos_no = 0;
}

// 0x0f  fade in <u16 frames> <u16 alpha>: alpha_a from its value to alpha,
// the deform rates from 0 to their rows' values. An auto enemy (type 2) with
// flags2 0x4000000 takes one frame. flags 0x400000 as in EJobC0E.
// FF3 0x0018b690

void EJobC0F(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no  = alg->idx;
    float    t;

    if (alg->pos_no == 0)
    {
        int frames = EaGetU16(alg);

        ene_fade_in_alpha[no] = (float)EaGetU16(alg);
        ene_fade_in_start[no] = (float)ene->alpha_a;
        if (ene->type == 2 && (ene->flags2 & 0x4000000) != 0)
            frames = 1;
        alg->loop[0] = (float)frames / GetFrameTimeScale();
        ene_fade_in_time[no] = alg->loop[0];
        ene_fade_in_held[no] = 0;
        if ((ene->flags & 0x400000) == 0)
            ene->flags |= 0x400000;
        else
            ene_fade_in_held[no] = 1;
        alg->pos_no++;
        alg->wait_time = 1.0f;
    }
    else if (alg->pos_no != 1)
        return;

    t = alg->loop[0] - 1.0f;
    if (0.0f < t)
    {
        float len = ene_fade_in_time[no];

        alg->loop[0] = t;
        ene->alpha_a = (u_char)(int)((1.0f - t / len) * (ene_fade_in_alpha[no] - ene_fade_in_start[no])
                                     + ene_fade_in_start[no]);
        ene->eff.rate[0] = ene->eff.deform_b1[0] - (ene->eff.deform_b1[0] * alg->loop[0]) / len;
        ene->eff.rate[1] = ene->eff.deform_b1[1] - (ene->eff.deform_b1[1] * alg->loop[0]) / len;
        alg->wait_time = 1.0f;
        return;
    }

    alg->loop[0] = 0.0f;
    ene->eff.rate[0] = ene->eff.deform_b1[0];
    ene->eff.rate[1] = ene->eff.deform_b1[1];
    ene->alpha_a = (u_char)(int)ene_fade_in_alpha[no];
    if (ene_fade_in_held[no] != 1)
        ene->flags &= ~(u_long)0x400000;
    alg->wait_time = 0.0f;
    alg->pos_no = 0;
}

// 0x10  <u8 mode>: the scripted grab scenes. 1: back to normal (the
// screen effect restored for kinds 0x1f / 0x20, the step request cleared);
// 2: the other enemies' flags cleared and this one released; 3: a hold
// starts (the slow mode ends, lock 3, the others flagged); 4: the finder
// camera cuts to it -- 32 in front of it, turned towards the camera -- the
// viewfinder dropped, the player idle, the others flagged, lock 6; 5: the
// cut ends (finder overlap 5, locks 3 and 6 dropped) and it is released.
// FF3 0x0018b950

void EJobC10(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    u_char        mode = EaGetU8(alg);
    sceVu0FVECTOR rot;
    sceVu0FVECTOR ofs;
    sceVu0FVECTOR pos;

    switch (mode)
    {
    case 1:
        if (ene->type != 2 && (u_short)(ene->cmn->kind - 0x1f) < 2)
            TattooScrEffectRestore();
        ene->step_time = 0;
        ene->step_req  = 1.0f;
        break;
    case 2:
        EneFlagReq(0, 0);
        ene->state = ENE_STATE_RELEASE;
        break;
    case 3:
        if (ene->type != 2 && (u_short)(ene->cmn->kind - 0x1f) < 2)
            TattooScrEffectRestore();
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_100000) != 0)
            PlyrGaugeSlowEnd(GetAlbumSaveDisp());
        LockTblInc(3);
        PlyrTurnFlagClear();
        EneFlagReq(1, (u_char)(1 << (alg->idx & 0x1f)));
        break;
    case 4:
        ene->step_req  = 1.0f;
        ofs[0] = 0.0f;
        ofs[1] = 0.0f;
        ofs[2] = 32.0f;
        ofs[3] = 0.0f;
        ene->step_time = 0;
        UnitGetTrgtRot(ene->mpos, gra3dcamGetPosition(), rot, 3);
        RotVectorZXY(ofs, ofs, rot);
        sceVu0AddVector(pos, ene->mpos, ofs);
        MapCamSetFinCamera(pos, ene->mpos);
        PlyrFinderModeCancel();
        SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
        EneFlagReq(2, (u_char)(1 << (alg->idx & 0x1f)));
        LockTblInc(6);
        break;
    case 5:
        EneFlagReq(0, 0);
        ReqFinderInOverRap(5);
        LockTblDec(3);
        LockTblDec(6);
        ene->state = ENE_STATE_RELEASE;
        break;
    }
    EaNext(alg);
}

// 0x11  fade out over loop[0] (set before by EJobC05 / EJobC06; a frame per
// run): alpha_a from its value to 0; the deform rates 69 / 45 for the first
// half, then down to 0 with it.
// FF3 0x0018bbb8

void EJobC11(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no  = alg->idx;
    float    r;
    float    f;

    if (alg->pos_no == 0)
    {
        ene_fade_time[no]  = alg->loop[0];
        ene_fade_alpha[no] = (float)ene->alpha_a;
        alg->pos_no++;
        alg->wait_time = 1.0f;
        return;
    }
    if (alg->pos_no != 1)
        return;

    if (0.0f < alg->loop[0])
    {
        r = alg->loop[0] / ene_fade_time[no];
        f = 1.0f;
        ene->alpha_a = (u_char)(int)(ene_fade_alpha[no] * r);
        if (r < 0.5f)
            f = r + r;
        ene->eff.rate[1] = (float)(int)(f * 45.0f);
        ene->eff.rate[0] = (float)(int)(f * 69.0f);
        alg->wait_time = 1.0f;
        alg->loop[0] -= 1.0f;
        return;
    }
    ene->eff.rate[1] = 0.0f;
    ene->alpha_a     = 0;
    ene->eff.rate[0] = 0.0f;
    EaNext(alg);
}

// 0x12  loop[0] = the frames of the current anime left after <u16 frame>
// (0 past its end).
// FF3 0x0018bd38

void EJobC12(ENEALG_WRK *alg)
{
    u_int frame = EaGetU16(alg);
    u_int len   = (u_int)EneGetAnimeLength(EaEne(alg)) & 0xffff;

    if (frame < len)
        alg->loop[0] = (float)(int)(len - frame);
    else
        alg->loop[0] = 0.0f;
    EaNext(alg);
}

// 0x13  <u8 on>: flags 0x100000 (not a target) on / off.
// FF3 0x0018bdc8

void EJobC13(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x100000;
    else
        ene->flags |= 0x100000;
    EaNext(alg);
}

// 0x14  <u8 on>: flags 0x400000 (the script fade, BJobL03) on / off.
// FF3 0x0018be28

void EJobC14(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x400000;
    else
        ene->flags |= 0x400000;
    EaNext(alg);
}

// 0x15  alpha_a = <u8>.
// FF3 0x0018be88

void EJobC15(ENEALG_WRK *alg)
{
    EaEne(alg)->alpha_a = EaGetU8(alg);
    EaNext(alg);
}

// 0x16  alpha_b = <u8>.
// FF3 0x0018bec0

void EJobC16(ENEALG_WRK *alg)
{
    EaEne(alg)->alpha_b = EaGetU8(alg);
    EaNext(alg);
}

// 0x17  SE <u8 se> of the enemy's bank.
// FF3 0x0018bef8

void EJobC17(ENEALG_WRK *alg)
{
    u_char se = EaGetU8(alg);

    EneSePlay(EaEne(alg), se);
    EaNext(alg);
}

// 0x18  a fly <u8 kind> (EneFlyActByKind).
// FF3 0x0018bf50

void EJobC18(ENEALG_WRK *alg)
{
    u_char kind = EaGetU8(alg);

    EneFlyActByKind(EaEne(alg), kind);
    EaNext(alg);
}

// 0x19  its flies end.
// FF3 0x0018bfa8

void EJobC19(ENEALG_WRK *alg)
{
    EraseEneFlyWork(EaEne(alg));
    EaNext(alg);
}

// 0x1a  <u8 joint>: the point the camera aims at -- 1 joint 1, 2 joint
// 0x13, 3 joint 7, else mpos (joint 2).
// FF3 0x0018bff0

void EJobC1A(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    switch (EaGetU8(alg))
    {
    case 1:
        ene->tpos = ene->jpos1;
        break;
    case 2:
        ene->tpos = ene->jpos13;
        break;
    case 3:
        ene->tpos = ene->jpos7;
        break;
    default:
        ene->tpos = ene->mpos;
        break;
    }
    EaNext(alg);
}

// 0x1b  action <u8 act> (EneActSet: the script restarts there).
// FF3 0x0018c068

void EJobC1B(ENEALG_WRK *alg)
{
    u_char act = EaGetU8(alg);

    EneActSet(EaEne(alg), act);
    EaNext(alg);
}

// 0x1c  release (state 4).
// FF3 0x0018c0c0

void EJobC1C(ENEALG_WRK *alg)
{
    EaEne(alg)->state = ENE_STATE_RELEASE;
    EaNext(alg);
}

// 0x1d  loads the linked enemies that have no slot yet; a random ghost's
// links take its row and zone.
// FF3 0x0018c0f0

void EJobC1D(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];

            if (id >= 0 && SearchEneWrkNo(ene->type, id) < 0)
            {
                EneLoadStart(ene->type, id);
                if (ene->type == 1)
                    RandGhostInfoCopy(&EneGetWrk(1, id)->rg, &ene->rg);
            }
        }
    }
    EaNext(alg);
}

// The four bytes of a flag word operand, low first.
static inline u_int EaGetU32(ENEALG_WRK *alg)
{
    const u_char *p = alg->comm_add;

    alg->comm_add += 4;
    return (u_int)p[0] + (u_int)p[3] * 0x1000000 + (u_int)p[2] * 0x10000 + (u_int)p[1] * 0x100;
}

// 0x1e  flags2 &= ~<u32>.
// FF3 0x0018c1c8

void EJobC1E(ENEALG_WRK *alg)
{
    u_int m = EaGetU32(alg);

    EaEne(alg)->flags2 &= ~m;
    EaNext(alg);
}

// 0x1f  flags2 |= <u32>.
// FF3 0x0018c230

void EJobC1F(ENEALG_WRK *alg)
{
    u_int m = EaGetU32(alg);

    EaEne(alg)->flags2 |= m;
    EaNext(alg);
}

// EJobC20's step: of the enemy's acting links, fewer than max on action
// act and another left -> a random other starts act. 1 when one did.
// FF3 0x0018c298

static int EneLinkActSpread(ENE_WRK *ene, u_char max, u_char act)
{
    u_short other[8];
    u_int   n_other = 0;
    u_char  n_on    = 0;
    int     i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];
            int   no;

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no < 0 || ene_wrk[no].state != ENE_STATE_ACT)
                continue;
            if (ene_wrk[no].act_no == act)
                n_on++;
            else
                other[n_other++] = (u_short)no;
        }
    }
    if (n_on < max && n_other != 0)
    {
        EneActSet(&ene_wrk[other[GetRandValIByF((int)n_other)]], act);
        return 1;
    }
    return 0;
}

// 0x20  <s8 n> <s8 act>: up to n acting links (n on act at most) start act.
// FF3 0x0018c3e0

void EJobC20(ENEALG_WRK *alg)
{
    s_char n   = (s_char)EaGetU8(alg);
    s_char act = (s_char)EaGetU8(alg);
    s_char i;

    for (i = 0; i < n; i++)
    {
        if (EneLinkActSpread(EaEne(alg), (u_char)n, (u_char)act) == 0)
            break;
    }
    EaNext(alg);
}

// 0x21  flags2 &= ~<u32> on the linked enemies that have a slot.
// FF3 0x0018c4a0

void EJobC21(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_int    m   = EaGetU32(alg);
    int      i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];
            int   no;

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no >= 0)
                ene_wrk[no].flags2 &= ~m;
        }
    }
    EaNext(alg);
}

// 0x22  flags2 |= <u32> on the linked enemies that have a slot.
// FF3 0x0018c5a0

void EJobC22(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_int    m   = EaGetU32(alg);
    int      i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];
            int   no;

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no >= 0)
                ene_wrk[no].flags2 |= m;
        }
    }
    EaNext(alg);
}

// 0x23  <u8 who> <u8 n> <u8 v>: var[n] = v -- who 0xff itself, 0xfe every
// acting link with hp, 0..7 that link (when acting with hp).
// FF3 0x0018c6a0

void EJobC23(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_char   who = EaGetU8(alg);
    u_char   n   = EaGetU8(alg);
    u_char   v   = EaGetU8(alg);
    int      i;

    if (who == 0xff)
        ene->alg.var[n] = v;
    else if (who == 0xfe)
    {
        if (ene->type != 2)
        {
            for (i = 0; i < 8; i++)
            {
                short id = ene->dat->link[i];
                int   no;

                if (id < 0)
                    continue;
                no = SearchEneWrkNo(ene->type, id);
                if (no >= 0 && ene_wrk[no].state == ENE_STATE_ACT && ene_wrk[no].hp != 0)
                    ene_wrk[no].alg.var[n] = v;
            }
        }
    }
    else if (who < 8 && ene->type != 2)
    {
        short id = ene->dat->link[who];

        if (id >= 0)
        {
            int no = SearchEneWrkNo(ene->type, id);

            if (no >= 0 && ene_wrk[no].state == ENE_STATE_ACT && ene_wrk[no].hp != 0)
                ene_wrk[no].alg.var[n] = v;
        }
    }
    EaNext(alg);
}

// EJobC24's step: as EneLinkActSpread for var[n] = v (links in action 4
// do not count).
// FF3 0x0018c850

static int EneLinkVarSpread(ENE_WRK *ene, u_char max, u_char n, u_char v)
{
    u_short other[8];
    u_int   n_other = 0;
    u_char  n_on    = 0;
    int     i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];
            int   no;

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no < 0 || ene_wrk[no].state != ENE_STATE_ACT || ene_wrk[no].act_no == 4)
                continue;
            if (ene_wrk[no].alg.var[n] == v)
                n_on++;
            else
                other[n_other++] = (u_short)no;
        }
    }
    if (n_on < max && n_other != 0)
    {
        ene_wrk[other[GetRandValIByF((int)n_other)]].alg.var[n] = v;
        return 1;
    }
    return 0;
}

// 0x24  <u8 max> <s8 cnt> <u8 n> <u8 v>: up to cnt acting links (max at v
// at most) get var[n] = v.
// FF3 0x0018c9c0

void EJobC24(ENEALG_WRK *alg)
{
    u_char max = EaGetU8(alg);
    s_char cnt = (s_char)EaGetU8(alg);
    u_char n   = EaGetU8(alg);
    u_char v   = EaGetU8(alg);
    s_char i;

    for (i = 0; i < cnt; i++)
    {
        if (EneLinkVarSpread(EaEne(alg), max, n, v) == 0)
            break;
    }
    EaNext(alg);
}

// 0x25  release (state 4; as EJobC1C).
// FF3 0x0018caa8

void EJobC25(ENEALG_WRK *alg)
{
    EaEne(alg)->state = ENE_STATE_RELEASE;
    EaNext(alg);
}

// 0x26  <u8 on>: flags 0x10000 on / off.
// FF3 0x0018cad8

void EJobC26(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x10000;
    else
        ene->flags |= 0x10000;
    EaNext(alg);
}

// 0x27  <u16 frame>: an auto enemy's loop[0] = its act_len left after
// frame, PAL-scaled (0 past it; act_len < 0 leaves it).
// FF3 0x0018cb38 (PC: an enemy without adat -- the ROM reads EE address
// 0x58 -- leaves loop[0] too)

void EJobC27(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    int      frame = EaGetU16(alg);

    if (ene->adat != nullptr && ene->adat->act_len >= 0)
    {
        int left = ene->adat->act_len - frame;

        if (left < 1)
            alg->loop[0] = 0.0f;
        else
            alg->loop[0] = (float)left / GetFrameTimeScale();
    }
    EaNext(alg);
}

// 0x28  <u8 status> <s16 frames>: the player's timed status (5 also fades
// the tegata in).
// FF3 0x0018cbd8

void EJobC28(ENEALG_WRK *alg)
{
    u_char    status = EaGetU8(alg);
    short     frames = (short)EaGetU16(alg);
    PLYR_WRK *plyr   = ingame_wrk.plyr_wrk;

    plyr->status      = status;
    plyr->status_time = (float)frames / GetFrameTimeScale();
    if (status == 5)
        GetPhotoModeWrk()->TegataFadeIn(1);
    EaNext(alg);
}

// 0x29  attack <u8 dmg_type> <u8 kind> <u16 offset>: a refused attack
// jumps.
// FF3 0x0018cc98

void EJobC29(ENEALG_WRK *alg)
{
    u_char  type = EaGetU8(alg);
    u_char  kind = EaGetU8(alg);
    u_short ofs  = EaGetU16(alg);

    if (EneAttackReq(EaEne(alg), type, kind) == 0)
        EaJumpTo(alg, ofs);
    EaNext(alg);
}

// 0x2a  cnt[<u8 n>] = <u8>. (n is 0 or 1: 2 would write idx.)
// FF3 0x0018cd30

void EJobC2A(ENEALG_WRK *alg)
{
    u_char n = EaGetU8(alg);

    alg->cnt[n] = EaGetU8(alg);
    EaNext(alg);
}

// 0x2b  cnt[<u8 n>] += <u8>.
// FF3 0x0018cd68

void EJobC2B(ENEALG_WRK *alg)
{
    u_char n = EaGetU8(alg);

    alg->cnt[n] = (u_char)(alg->cnt[n] + EaGetU8(alg));
    EaNext(alg);
}

// 0x2c  loop[<u8 n>] = <u16 base> + a random 0..<u16 rnd>, PAL-scaled.
// FF3 0x0018cda8

void EJobC2C(ENEALG_WRK *alg)
{
    u_char n    = EaGetU8(alg);
    u_int  base = EaGetU16(alg);
    int    rnd  = EaGetU16(alg);
    u_int  add  = 0;

    // callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
    if (rnd != 0)
        add = (u_int)(int)((float)rnd * ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) + 0.0f) & 0xffff;
    EaNext(alg);
    alg->loop[n] = (float)(base + add) / GetFrameTimeScale();
}

// 0x2d  anime <u8 anime> <u8 sub> <u16 start>, no blend (EJobC04 with
// blend 0); the start's high byte is dropped.
// FF3 0x0018cea8

void EJobC2D(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    u_char   anime = EaGetU8(alg);
    u_char   sub   = EaGetU8(alg);
    u_char   start = (u_char)EaGetU16(alg);

    if (ene->type == 2 && (ene->flags2 & 0x1000000) != 0)
        anime = 1;
    EneAnimeSet(ene, anime, start, sub, 0, 0, 0, 0xff, 0);
    EaNext(alg);
}

// 0x2e  EneMdlCtrlSet mode <u8>.
// FF3 0x0018cf60

void EJobC2E(ENEALG_WRK *alg)
{
    u_char mode = EaGetU8(alg);

    EneMdlCtrlSet(0.0f, 0.0f, EaEne(alg), mode);
    EaNext(alg);
}

// 0x2f  turn_pos = the player's position (EneTurnReq 0xfe turns to it).
// FF3 0x0018cfc8

void EJobC2F(ENEALG_WRK *alg)
{
    ENE_WRK  *ene  = EaEne(alg);
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    ene->turn_pos[0] = plyr->pos[0];
    ene->turn_pos[1] = plyr->pos[1];
    ene->turn_pos[2] = plyr->pos[2];
    ene->turn_pos[3] = plyr->pos[3];
    EaNext(alg);
}

// 0x30  _6f0 = <u16> frames (not PAL-scaled).
// FF3 0x0018d000

void EJobC30(ENEALG_WRK *alg)
{
    EaEne(alg)->_6f0 = (float)EaGetU16(alg);
    EaNext(alg);
}

// 0x31  keeps the facing (rot[1]) for EJobM08.
// FF3 0x0018d050

void EJobC31(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    EaNext(alg);
    ene->rot_keep = ene->rot[1];
}

// 0x32  <u16 frames> (PAL-scaled): the deform rates from their rows' values
// to 0, whole steps (a frame per run).
// FF3 0x0018d080

void EJobC32(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no  = alg->idx;
    float    t;

    if (alg->pos_no == 0)
    {
        t = (float)EaGetU16(alg) / GetFrameTimeScale();
        alg->loop[0] = t;
        ene_rate_out_time[no] = t;
        alg->wait_time = 1.0f;
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)
    {
        if (0.0f < alg->loop[0])
        {
            float len = ene_rate_out_time[no];

            t = alg->loop[0] - 1.0f;
            alg->loop[0] = t;
            ene->eff.rate[0] = (float)((int)((ene->eff.deform_b1[0] * t) / len) & 0xff);
            ene->eff.rate[1] = (float)((int)((ene->eff.deform_b1[1] * alg->loop[0]) / len) & 0xff);
            alg->wait_time = 1.0f;
        }
        else
        {
            alg->loop[0] = 0.0f;
            ene->eff.rate[1] = 0.0f;
            ene->eff.rate[0] = 0.0f;
            EaNext(alg);
        }
    }
}

// 0x33  <u16 frames> (PAL-scaled): the deform rates from 0 to their rows'
// values (a frame per run).
// FF3 0x0018d208

void EJobC33(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no  = alg->idx;
    float    t;

    if (alg->pos_no == 0)
    {
        t = (float)EaGetU16(alg) / GetFrameTimeScale();
        alg->loop[0] = t;
        ene_rate_in_time[no] = t;
        alg->wait_time = 1.0f;
        alg->pos_no++;
    }
    else if (alg->pos_no == 1)
    {
        if (0.0f < alg->loop[0])
        {
            float len = ene_rate_in_time[no];

            t = alg->loop[0] - 1.0f;
            alg->loop[0] = t;
            ene->eff.rate[0] = ene->eff.deform_b1[0] - (ene->eff.deform_b1[0] * t) / len;
            ene->eff.rate[1] = ene->eff.deform_b1[1] - (ene->eff.deform_b1[1] * alg->loop[0]) / len;
            alg->wait_time = 1.0f;
        }
        else
        {
            alg->loop[0] = 0.0f;
            ene->eff.rate[0] = ene->eff.deform_b1[0];
            ene->eff.rate[1] = ene->eff.deform_b1[1];
            EaNext(alg);
        }
    }
}

// 0x34  <u8 pct>: both deform bases = pct / 100 (0xff: 1, 0: 0).
// FF3 0x0018d378

void EJobC34(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_char   p   = EaGetU8(alg);
    float    b   = 1.0f;

    if (p == 0)
    {
        ene->eff.base[1] = 0.0f;
        ene->eff.base[0] = 0.0f;
    }
    else
    {
        if (p != 0xff)
            b = (float)p / 100.0f;
        ene->eff.base[1] = b;
        ene->eff.base[0] = b;
    }
    EaNext(alg);
}

// 0x35  hold_pow = 0 (the hold ends).
// FF3 0x0018d408

void EJobC35(ENEALG_WRK *alg)
{
    EaEne(alg)->hold_pow = 0.0f;
    EaNext(alg);
}

// 0x36  anime <u8 anime> <u8 sub> <u16 start> with attr 0 and a blend in.
// FF3 0x0018d430

void EJobC36(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    u_char   anime = EaGetU8(alg);
    u_char   sub   = EaGetU8(alg);
    u_char   start = (u_char)EaGetU16(alg);

    if (ene->type == 2 && (ene->flags2 & 0x1000000) != 0)
        anime = 1;
    EneAnimeSet(ene, anime, start, sub, 0, 0, 0, 0, 1);
    EaNext(alg);
}

// 0x37  <u8 on>: flags 0x4000000000 (its voice stream may play) on / off;
// the player's _348 = 2 / 0 with it.
// FF3 0x0018d4f0

void EJobC37(ENEALG_WRK *alg)
{
    ENE_WRK  *ene  = EaEne(alg);
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (EaGetU8(alg) == 0)
    {
        ene->flags &= ~(u_long)0x4000000000;
        plyr->_348 = 0;
    }
    else
    {
        ene->flags |= (u_long)0x4000000000;
        plyr->_348 = 2;
    }
    EaNext(alg);
}

// 0x38  <u8 se, u8 pct> x 4: one SE by percentage (EneSePlayRand).
// FF3 0x0018d578

void EJobC38(ENEALG_WRK *alg)
{
    u_char b[8];
    int    i;

    for (i = 0; i < 8; i++)
        b[i] = EaGetU8(alg);
    EneSePlayRand(EaEne(alg), b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    EaNext(alg);
}

// 0x39  _1d0 = <u16> / 100.
// FF3 0x0018d630

void EJobC39(ENEALG_WRK *alg)
{
    EaEne(alg)->_1d0 = (float)EaGetU16(alg) / 100.0f;
    EaNext(alg);
}

// 0x3a  <u8 on>: flags 0x8000000000 on / off.
// FF3 0x0018d698

void EJobC3A(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (EaGetU8(alg) == 0)
        ene->flags &= ~(u_long)0x8000000000;
    else
        ene->flags |= (u_long)0x8000000000;
    EaNext(alg);
}

// 0x3b  <u8 voice>: the hit voice by the damage reaction -- 2: voice 0 and
// photo SE 1; 3: voice 1 or 2 at random, 4: the other one than last time,
// both with photo SE 0xc; else none (-1). A non-zero operand plays the
// voice (EneSePlay).
// FF3 0x0018d700

void EJobC3B(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    u_char   voice = EaGetU8(alg);
    int      se;
    float    r;

    switch (ene->dmg_react)
    {
    case 2:
        ene->_589 = 0;
        se = 1;
        break;
    case 3:
        // callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
        r = (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
        ene->_589 = (u_char)(((int)(r + r + 0.0f) & 1) + 1);
        se = 0xc;
        break;
    case 4:
        ene->_589 = (u_char)(2 - ((s_char)ene->_589 != 1));
        se = 0xc;
        break;
    default:
        ene->_589 = 0xff;
        se = -1;
        break;
    }
    if (se >= 0)
        PhotoModeSePlay(se, 100, 0x1000);
    if (voice != 0 && (s_char)ene->_589 >= 0)
        EneSePlay(ene, (u_int)(int)(s_char)ene->_589);
    EaNext(alg);
}

// 0x3c  flags 0x400000000 off.
// FF3 0x0018d848

void EJobC3C(ENEALG_WRK *alg)
{
    EaEne(alg)->flags &= ~(u_long)0x400000000;
    EaNext(alg);
}

// 0x3d  the player's _348 = <u8>.
// FF3 0x0018d888

void EJobC3D(ENEALG_WRK *alg)
{
    ingame_wrk.plyr_wrk->_348 = EaGetU8(alg);
    EaNext(alg);
}

// 0x3e  <u8 pct> <u8 alpha>: a step down -- the deform rates by pct % of
// deform_b1[0] (both: the ROM reads [0] twice), alpha_a by pct % of alpha;
// none below 0.
// FF3 0x0018d8b0

void EJobC3E(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_char   p   = EaGetU8(alg);
    u_char   a   = EaGetU8(alg);
    float    r;
    int      v;

    r = ene->eff.rate[0] - ((float)p * ene->eff.deform_b1[0]) / 100.0f;
    v = (int)ene->alpha_a - (int)(((u_int)p * a) / 100);
    if (v < 0)
        v = 0;
    ene->alpha_a = (u_char)v;
    if (r <= 0.0f)
        r = 0.0f;
    ene->eff.rate[0] = r;
    r = ene->eff.rate[1] - ((float)p * ene->eff.deform_b1[0]) / 100.0f;
    if (r <= 0.0f)
        r = 0.0f;
    ene->eff.rate[1] = r;
    EaNext(alg);
}

// 0x3f  <u8 pct> <u8 alpha>: a step up -- the deform rates by pct % of
// their rows' values up to them, alpha_a by pct % of alpha up to alpha.
// FF3 0x0018d9a0

void EJobC3F(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_char   p   = EaGetU8(alg);
    u_char   a   = EaGetU8(alg);
    float    top = ene->eff.deform_b1[0];
    float    r   = ene->eff.rate[0] + ((float)p * top) / 100.0f;
    u_int    v   = ene->alpha_a + ((u_int)p * a) / 100;

    if (a <= v)
        v = a;
    ene->alpha_a = (u_char)v;
    if (r < top)
        top = r;
    ene->eff.rate[0] = top;
    top = ene->eff.deform_b1[1];
    r = ene->eff.rate[1] + ((float)p * top) / 100.0f;
    if (top <= r)
        r = top;
    ene->eff.rate[1] = r;
    EaNext(alg);
}

// 0x40  <s16 mul> <s16 add> <s16 lo> <s16 hi>: loop[0] = (mul * a random
// lo..hi + add) frames, PAL-scaled.
// FF3 0x0018da88

void EJobC40(ENEALG_WRK *alg)
{
    int   mul = (short)EaGetU16(alg);
    int   add = (short)EaGetU16(alg);
    int   lo  = (short)EaGetU16(alg);
    int   hi  = (short)EaGetU16(alg);
    float r;

    // callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
    r = (float)(hi - lo) * ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) + 0.0f;
    alg->wait_time = 0.0f;
    alg->pos_no    = 0;
    alg->loop[0]   = (float)(mul * ((int)r + lo) + add) / GetFrameTimeScale();
}

// 0x41  the finder view turns to this enemy over a frame.
// FF3 0x0018dbd0

void EJobC41(ENEALG_WRK *alg)
{
    FinderViewTurnToEne(1.0f, EaEne(alg));
    EaNext(alg);
}

// 0x42  <u16 offset>: at a route node with an action (flag 0x1000) the
// node's action -- its facing, its anime, loop[1] = its seconds (x 30, 25
// in PAL, PAL-scaled; 0 jumps to offset at once) and its SE at the body.
// FF3 0x0018dc20

void EJobC42(ENEALG_WRK *alg)
{
    ENE_WRK     *ene = EaEne(alg);
    u_int        cur = EneAlgGetCur(ene);
    u_short      ofs = EaGetU16(alg);
    ENE_ALG_ACT *act;

    if ((cur & 0x1000) == 0)
    {
        EaNext(alg);
        return;
    }
    act = EneAlgActGet((int)(cur & 0xff));
    ene->rot[1] = act->rot;
    EneAnimeSet(ene, (u_char)act->anime, 0, 0, 0, 0, 0, 0xff, 1);
    if (act->wait == 0)
        EaJumpTo(alg, ofs);
    else
    {
        float fts = GetFrameTimeScale();
        u_int f   = (GetPALMode() == 0) ? (u_int)act->wait * 30 : (u_int)act->wait * 25;

        alg->loop[1] = (float)f / fts;
    }
    if (act->se > 0)
    {
        void *s3s[3];

        memset(s3s, 0, sizeof(s3s));
        s3s[0] = ene->pos;
        ene->se_id    = SndSePlay((u_int)act->se, 100, 0x1000, 5, s3s, 0);
        ene->se_anime = (u_int)act->anime;
    }
    EaNext(alg);
}

// 0x43  the route's next node (EneAlgNext).
// FF3 0x0018ddd0

void EJobC43(ENEALG_WRK *alg)
{
    EneAlgNext(EaEne(alg));
    EaNext(alg);
}

// 0x44  <u8 n>: var[n] = a random 0..1, or 0..2 when another acting enemy
// with hp of model 0x18 / 0x1f / 0x20 has var[n] != 2.
// FF3 0x0018de18

void EJobC44(ENEALG_WRK *alg)
{
    ENE_WRK *self   = EaEne(alg);
    u_char   n      = EaGetU8(alg);
    s_char   others = 0;
    s_char   free   = 0;
    float    r;
    int      i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *e = &ene_wrk[i];
        short    id;

        if (e->type == 2 || e->state != ENE_STATE_ACT || e->hp == 0)
            continue;
        id = e->cmn->id;
        if (id != 0x18 && id != 0x1f && id != 0x20)
            continue;
        if (e == self)
            continue;
        others++;
        if (e->alg.var[n] != 2)
            free++;
    }
    // callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
    r = (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
    if (others != 0 && free != 0)
        r = r * 3.0f;
    else
        r = r + r;
    EaNext(alg);
    alg->var[n] = (u_char)(int)(r + 0.0f);
}

// 0x45  <u8 cut> <u8 anime> <u8 sub> <u16 start>: the anime on every acting
// link (blended in unless cut).
// FF3 0x0018dfd8

void EJobC45(ENEALG_WRK *alg)
{
    ENE_WRK *ene   = EaEne(alg);
    u_char   cut   = EaGetU8(alg);
    u_char   anime = EaGetU8(alg);
    u_char   sub   = EaGetU8(alg);
    u_char   start = (u_char)EaGetU16(alg);
    int      i;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];
            int   no;

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no >= 0 && ene_wrk[no].state == ENE_STATE_ACT)
                EneAnimeSet(&ene_wrk[no], anime, start, sub, 0, 0, 0, 0xff, (cut == 0) ? 1 : 0);
        }
    }
    EaNext(alg);
}

// 0x46  the viewfinder drops (PlyrFinderModeCancel).
// FF3 0x0018e140

void EJobC46(ENEALG_WRK *alg)
{
    PlyrFinderModeCancel();
    EaNext(alg);
}

// 0x47  alpha_fdr = <u8> & 0x7f.
// FF3 0x0018e170

void EJobC47(ENEALG_WRK *alg)
{
    EaEne(alg)->alpha_fdr = EaGetU8(alg) & 0x7f;
    EaNext(alg);
}

// ══════════════════════════════════════════════════════════════════════
// Move (table 0x3014e0, opcodes 0x70..0x7d)
// ══════════════════════════════════════════════════════════════════════

// 0x70  turn <u8 mode>: EneTurnReq.
// FF3 0x0018e1b0

void EJobM00(ENEALG_WRK *alg)
{
    u_char mode = EaGetU8(alg);

    EneTurnReq(EaEne(alg), mode);
    EaNext(alg);
}

// 0x71  <s16> <s16 yaw> <s16>: a one-frame turn, rot_add[1] = yaw in tenths
// of a degree; the other two operands are read and dropped.
// FF3 0x0018e208

void EJobM01(ENEALG_WRK *alg)
{
    short v[3];
    int   i;

    for (i = 0; i < 3; i++)
        v[i] = (short)EaGetU16(alg);
    EaEne(alg)->rot_add[1] = ((float)v[1] * 3.1415927f) / 1800.0f;
    EaNext(alg);
}

// 0x72  face <u8 kind>: 0 the player, 1 away from the player (not wrapped),
// 2 / 3 the player give or take up to 30 / 80 degrees at random, 4 / 5 roll
// 0 / 180 degrees (rot[2]), 6 past the player by 70..79 degrees on the side
// the enemy turns towards.
// FF3 0x0018e2a8

void EJobM02(ENEALG_WRK *alg)
{
    ENE_WRK  *ene  = EaEne(alg);
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    kind = EaGetU8(alg);
    float     y;
    float     d;
    int       r;

    // callcheck: rand (EaRand01)
    switch (kind)
    {
    case 0:
        ene->rot[1] = GetTrgtRotY(ene->pos, plyr->pos);
        break;
    case 1:
        ene->rot[1] = GetTrgtRotY(ene->pos, plyr->pos) + 3.1415927f;
        break;
    case 2:
        ene->rot[1] = GetTrgtRotY(ene->pos, plyr->pos);
        r = (int)(EaRand01() * 60.0f + 0.0f) - 30;
        ene->rot[1] = ene->rot[1] + ((float)r * 3.1415927f) / 180.0f;
        RotLimitChk(&ene->rot[1]);
        break;
    case 3:
        ene->rot[1] = GetTrgtRotY(ene->pos, plyr->pos);
        r = (int)(EaRand01() * 160.0f + 0.0f) - 80;
        ene->rot[1] = ene->rot[1] + ((float)r * 3.1415927f) / 180.0f;
        RotLimitChk(&ene->rot[1]);
        break;
    case 4:
        ene->rot[2] = 0.0f;
        RotLimitChk(&ene->rot[2]);
        break;
    case 5:
        ene->rot[2] = 3.1415927f;
        RotLimitChk(&ene->rot[2]);
        break;
    case 6:
        y = GetTrgtRotY(ene->pos, plyr->pos);
        d = ((float)((int)(EaRand01() * 10.0f + 0.0f) + 70) * 3.1415927f) / 180.0f;
        if (0.0f < y - ene->rot[1])
            y += d;
        else
            y -= d;
        ene->rot[1] = y;
        RotLimitChk(&ene->rot[1]);
        break;
    }
    EaNext(alg);
}

// 0x73  warp <u8 kind> <u16 lo> <u16 hi> <u8 hide_lo> <u8 hide_hi>: the
// enemy jumps to a new place, dist = lo..hi (random between) times 0.2:
//   0, 8  dist in front of the player (by its facing), at the enemy's height;
//   1     dist behind the player;
//   2     dist from the player in a random direction (10-degree steps);
//   3     the same around the enemy itself;
//   4..6  10 / 20 / 40 to the enemy's right or left (along its rot),
//         whichever is nearer the player (XZ), at the player's height;
//   7     dist towards the player, 15 degrees off to a random side, and it
//         turns to the player (EneTurnReq 0);
//   9     dist sideways, 90 degrees off the line from the player;
//   10    as 2, at the player's height;
//   else  no move.
// Then an SE (kind 8 its own). With hide_lo != 0 it stays hidden (alpha_a 0,
// flags 0x100000: not a target) for hide_lo..hide_hi frames (random
// between, not PAL-scaled); the second step brings it back.
// FF3 0x0018e548

void EJobM03(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR v;
    sceVu0FVECTOR rot  = { 0.0f, 0.0f, 0.0f, 0.0f };
    sceVu0FVECTOR tmp;
    sceVu0FVECTOR a;
    float         s;
    float         r;
    u_char        kind, hide_lo, hide_hi;
    u_short       lo, hi;
    u_int         t;

    if (alg->pos_no != 0)
    {
        if (alg->pos_no != 1)
            return;
        ene->flags &= ~(u_long)0x100000;
        ene->alpha_a = ene_warp_alpha[alg->idx];
        EaNext(alg);
        return;
    }

    kind    = EaGetU8(alg);
    lo      = EaGetU16(alg);
    hi      = EaGetU16(alg);
    hide_lo = EaGetU8(alg);
    hide_hi = EaGetU8(alg);

    // callcheck: rand (EaRand01)
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[3] = 0.0f;
    if (hi == lo)
        v[2] = (float)hi * 0.2f;
    else
        v[2] = (float)((u_int)(int)((float)lo + (float)(hi - lo) * EaRand01()) & 0xffff) * 0.2f;

    // the direction (4..7 and 9 move the enemy here already)
    switch (kind)
    {
    case 0:
    case 8:
        rot[1] = plyr->rot[1];
        break;
    case 1:
        rot[1] = plyr->rot[1] + 3.1415927f;
        break;
    case 2:
    case 3:
    case 10:
        rot[1] = (float)(int)(EaRand01() * 36.0f + 0.0f) * 0.17453292f - 3.1415927f;
        break;
    case 4:
    case 5:
    case 6:
        s = (kind == 4) ? 0.5f : (kind == 5) ? 1.0f : 2.0f;
        v[0] = s * 20.0f;
        v[1] = v[2] = v[3] = 0.0f;
        RotVectorZXY(v, v, ene->rot);
        sceVu0AddVector(v, ene->pos, v);
        v[1] = plyr->pos[1];
        memcpy(a, v, sizeof(a));
        v[0] = s * -20.0f;
        v[1] = v[2] = v[3] = 0.0f;
        RotVectorZXY(v, v, ene->rot);
        sceVu0AddVector(v, ene->pos, v);
        v[1] = plyr->pos[1];
        if (EaDistXZ(v, plyr->pos) < EaDistXZ(a, plyr->pos))
            memcpy(ene->pos, v, sizeof(v));
        else
            memcpy(ene->pos, a, sizeof(a));
        break;
    case 7:
    case 9:
        if (kind == 7)
            UnitGetTrgtRot(ene->pos, plyr->pos, tmp, 2);
        else
            UnitGetTrgtRot(plyr->pos, ene->pos, tmp, 2);
        r = EaRand01();
        s = (kind == 7) ? 0.2617994f : 1.5707964f;
        if ((int)(r + r + 0.0f) > 0)
            tmp[1] += s;
        else
            tmp[1] -= s;
        RotLimitChk(&tmp[1]);
        rot[1] = tmp[1];
        RotVectorZXY(v, v, rot);
        sceVu0AddVector(v, ene->pos, v);
        v[1] = ene->pos[1];
        memcpy(ene->pos, v, sizeof(v));
        if (kind == 7)
            EneTurnReq(ene, 0);
        break;
    }

    // the place
    switch (kind)
    {
    case 0:
    case 1:
    case 2:
    case 8:
        RotLimitChk(&rot[1]);
        RotVectorZXY(v, v, rot);
        sceVu0AddVector(v, plyr->pos, v);
        v[1] = ene->pos[1];
        memcpy(ene->pos, v, sizeof(v));
        break;
    case 3:
        RotLimitChk(&rot[1]);
        RotVectorZXY(v, v, rot);
        sceVu0AddVector(v, ene->pos, v);
        v[1] = ene->pos[1];
        memcpy(ene->pos, v, sizeof(v));
        break;
    case 10:
        RotLimitChk(&rot[1]);
        RotVectorZXY(v, v, rot);
        sceVu0AddVector(v, plyr->pos, v);
        v[1] = plyr->pos[1];
        memcpy(ene->pos, v, sizeof(v));
        break;
    }

    SndSePlayStd((kind == 8) ? 0x1ac90000 : 0x1a9d0006);
    if (hide_lo == 0)
    {
        EaNext(alg);
        return;
    }
    ene_warp_alpha[alg->idx] = ene->alpha_a;
    ene->flags |= 0x100000;
    ene->alpha_a = 0;
    if (hide_lo == hide_hi)
        t = hide_lo;
    else
        t = (u_int)(int)((float)hide_lo + (float)(hide_hi - hide_lo) * EaRand01()) & 0xff;
    alg->pos_no++;
    alg->wait_time = (float)t;
}

// 0x74  turn anime <u8 right> <u8 left> <u16 start>: the anime for the side
// the player is on (right while the yaw to the player is past the facing),
// wait 1; then the turn is spread over the anime: turn[1] = (yaw to the
// player - (rot[1] + the motion's root yaw)) / (its length + 5).
// FF3 0x0018ed20

void EJobM04(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR trgt;
    float         d;
    float         yaw;
    u_char        right, left, start;
    int           len;

    if (alg->pos_no == 0)
    {
        right = EaGetU8(alg);
        left  = EaGetU8(alg);
        start = (u_char)EaGetU16(alg);
        UnitGetTrgtRot(ene->pos, plyr->pos, trgt, 3);
        d = trgt[1] - ene->rot[1];
        RotLimitChk(&d);
        EneAnimeSet(ene, (0.0f < d) ? right : left, start, 0, 0, 0, 0, 0xff, 1);
        alg->pos_no++;
        alg->wait_time = 1.0f;
    }
    else if (alg->pos_no == 1)
    {
        yaw = static_cast<CharModel *>(ene->mdl)->GetMotionYaw();
        yaw = ene->rot[1] + yaw;
        RotLimitChk(&yaw);
        UnitGetTrgtRot(ene->pos, plyr->pos, trgt, 3);
        d = trgt[1] - yaw;
        RotLimitChk(&d);
        len = static_cast<GameAnimeModel *>(ene->mdl)->GetAnimeLength(ene->_554);
        ene->turn[1] = d / ((float)len + 5.0f);
        RotLimitChk(&ene->turn[1]);
        EaNext(alg);
    }
}

// 0x75  <s16 x> <s16 y> <s16 z>: onto an acting link (dat->link; the last
// one acting wins): pos = the link's pos + (x, y, z) * 0.2 turned by its rot,
// rot = the link's rot. Not for type 2.
// FF3 0x0018ef00

void EJobM05(ENEALG_WRK *alg)
{
    ENE_WRK      *ene = EaEne(alg);
    sceVu0FVECTOR v;
    short         x   = (short)EaGetU16(alg);
    short         y   = (short)EaGetU16(alg);
    short         z   = (short)EaGetU16(alg);
    int           i;
    int           no;

    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            short id = ene->dat->link[i];

            if (id < 0)
                continue;
            no = SearchEneWrkNo(ene->type, id);
            if (no < 0 || ene_wrk[no].state != ENE_STATE_ACT)
                continue;
            v[0] = (float)x * 0.2f;
            v[1] = (float)y * 0.2f;
            v[2] = (float)z * 0.2f;
            v[3] = 0.0f;
            RotVectorZXY(v, v, ene_wrk[no].rot);
            sceVu0AddVector(v, ene_wrk[no].pos, v);
            memcpy(ene->pos, v, sizeof(v));
            memcpy(ene->rot, ene_wrk[no].rot, sizeof(ene->rot));
        }
    }
    EaNext(alg);
}

// 0x76  along the route <u16 jump_a> <u16 jump_b>: aims at the current
// node (rot_trgt) and moves its height towards the node's in step with the
// motion's root move (unless flags2 8). Away from the node (the distance
// left after this step 7 or more) it turns at 4 degrees a frame (times the
// frame scale) and the script goes on; at the node, the node's flag 0x1000
// jumps to jump_b, 0x4000 to jump_a, else the route's next node becomes
// current. follow is cleared.
// FF3 0x0018f0e0

void EJobM06(ENEALG_WRK *alg)
{
    ENE_WRK      *ene    = EaEne(alg);
    sceVu0FVECTOR node;
    u_short       jump_a = EaGetU16(alg);
    u_short       jump_b = EaGetU16(alg);
    float         dist;
    float         mx;
    float         step;

    ene->follow = 0;
    memcpy(node, ene->route[1]->pos, sizeof(node));
    dist = EaDistXZ(ene->pos, node);
    mx   = -static_cast<CharModel *>(ene->mdl)->GetRootMove(0, ene->mot_attr);
    step = GetLength2D(mx, static_cast<CharModel *>(ene->mdl)->GetRootMove(2, ene->mot_attr));
    // PC: not with the ghost right above or below the node (dist 0): the PS2
    // saturates step / 0 to the largest float, IEEE makes it infinite and the
    // height NaN for good.
    if (ene->pos[1] != node[1] && (ene->flags2 & 8) == 0 && dist > 0.0f)
        ene->pos[1] = ene->pos[1] - (step / dist) * (ene->pos[1] - node[1]);
    UnitGetTrgtRot(ene->pos, node, ene->rot_trgt, 2);

    // callcheck: fptodp, fabs, dpcmp (the compare is done in double)
    if (!(fabs((double)(dist - step)) < 7.0))
    {
        ene->turn_spd = GetFrameTimeScale() * 0.69813168f * 0.1f;
        EaNext(alg);
        return;
    }
    if ((EneAlgGetCur(ene) & 0x1000) != 0)
        EaJumpTo(alg, jump_b);
    else if ((EneAlgGetCur(ene) & 0x4000) != 0)
        EaJumpTo(alg, jump_a);
    else
        EneAlgNext(ene);
    EaNext(alg);
}

// 0x77  <u8 put>: the route's node nearest the enemy becomes the current
// one (EneAlgNearest); put 1 also puts the enemy there.
// FF3 0x0018f300

void EJobM07(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    s_char   put = (s_char)EaGetU8(alg);

    EneAlgNearest(ene);
    if (put == 1)
        memcpy(ene->pos, ene->route[1]->pos, sizeof(ene->pos));
    EaNext(alg);
}

// 0x78  the facing EJobC31 kept comes back.
// FF3 0x0018f388

void EJobM08(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    ene->rot[1] = ene->rot_keep;
    EaNext(alg);
}

// 0x79  <s16 x> <s16 y> <s16 z> <s16 deg>: by the player: pos = the
// player's pos + (x, y, z) * 0.2 turned by the player's rot, rot = the
// player's rot with deg degrees added to the yaw.
// FF3 0x0018f3b8

void EJobM09(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR v;
    short         x    = (short)EaGetU16(alg);
    short         y    = (short)EaGetU16(alg);
    short         z    = (short)EaGetU16(alg);
    short         deg  = (short)EaGetU16(alg);

    v[0] = (float)x * 0.2f;
    v[1] = (float)y * 0.2f;
    v[2] = (float)z * 0.2f;
    v[3] = 0.0f;
    RotVectorZXY(v, v, plyr->rot);
    sceVu0AddVector(ene->pos, plyr->pos, v);
    memcpy(ene->rot, plyr->rot, sizeof(ene->rot));
    ene->rot[1] = ene->rot[1] + ((float)deg * 3.1415927f) / 180.0f;
    RotLimitChk(&ene->rot[1]);
    EaNext(alg);
}

// 0x7a  <s8 at_joint> <s16 dist>: dist * 0.2 from the player (at_joint !=
// 0: from its joint 2) at a random pitch of -10..89 and yaw of -180..179
// degrees, facing the player.
// FF3 0x0018f570

void EJobM0A(ENEALG_WRK *alg)
{
    ENE_WRK      *ene      = EaEne(alg);
    PLYR_WRK     *plyr     = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR v        = { 0.0f, 0.0f, 0.0f, 1.0f };
    sceVu0FVECTOR tgt;
    sceVu0FVECTOR w;
    sceVu0FMATRIX m;
    s_char        at_joint = (s_char)EaGetU8(alg);
    short         dist     = (short)EaGetU16(alg);
    float         pitch;
    float         yaw;

    if (at_joint == 0)
        memcpy(tgt, plyr->pos, sizeof(tgt));
    else
        PlyrGetJointPos(2, tgt, v);
    // callcheck: rand (EaRand01)
    pitch = ((float)((int)(EaRand01() * 100.0f + 0.0f) - 10) * 3.1415927f) / 180.0f;
    yaw   = ((float)((int)(EaRand01() * 360.0f + 0.0f) - 180) * 3.1415927f) / 180.0f;
    v[2] = (float)dist * 0.2f;
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, pitch);
    sceVu0RotMatrixY(m, m, yaw);
    sceVu0ApplyMatrix(w, m, v);
    sceVu0AddVector(ene->pos, tgt, w);
    UnitGetTrgtRot(ene->pos, tgt, ene->rot, 3);
    EaNext(alg);
}

// 0x7b  <s8 mode> <s8 step> <s16 dlo> <s16 dhi> <s16 alo> <s16 ahi>
// <u16 jump>. Mode 0 picks a goal (ene_goal_pos): dlo..dhi * 0.2 from the
// player, alo..ahi degrees to either side of the line from the enemy (both
// random between, the side at random). Other modes walk towards it (XZ),
// step * 0.2 a frame; once there the enemy stands on it and the script
// jumps.
// FF3 0x0018f7d8

void EJobM0B(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR v;
    sceVu0FVECTOR rot;
    sceVu0FMATRIX m;
    float        *goal = ene_goal_pos[alg->idx];
    s_char        mode = (s_char)EaGetU8(alg);
    s_char        step = (s_char)EaGetU8(alg);
    short         dlo  = (short)EaGetU16(alg);
    short         dhi  = (short)EaGetU16(alg);
    short         alo  = (short)EaGetU16(alg);
    short         ahi  = (short)EaGetU16(alg);
    u_short       jump = EaGetU16(alg);
    short         d;
    short         ang;
    float         yaw;
    float         lim;
    float         r;

    if (mode == 0)
    {
        v[0] = v[1] = v[2] = 0.0f;
        v[3] = 1.0f;
        // callcheck: rand (EaRand01)
        d = dhi;
        if (dhi != dlo)
            d = (short)(int)((float)dlo + (float)(dhi - dlo) * EaRand01());
        v[2] = (float)d * 0.2f;
        yaw = GetTrgtRotY(ene->pos, plyr->pos);
        ang = ahi;
        if (ahi != alo)
            ang = (short)(int)((float)alo + (float)(ahi - alo) * EaRand01());
        r = EaRand01();
        if ((int)(r + r + 0.0f) == 1)
            ang = (short)-ang;
        yaw = yaw + ((float)ang * 3.1415927f) / 180.0f;
        RotLimitChk(&yaw);
        sceVu0UnitMatrix(m);
        sceVu0RotMatrixY(m, m, yaw);
        sceVu0ApplyMatrix(goal, m, v);
        sceVu0AddVector(goal, plyr->pos, goal);
        EaNext(alg);
        return;
    }

    lim = (float)step * 0.2f;
    if (lim < EaDistXZ(ene->pos, goal))
    {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = lim;
        v[3] = 0.0f;
        UnitGetTrgtRot(ene->pos, goal, rot, 2);
        RotVectorZXY(v, v, rot);
        sceVu0AddVector(ene->pos, ene->pos, v);
    }
    else
    {
        memcpy(ene->pos, goal, sizeof(ene->pos));
        EaJumpTo(alg, jump);
    }
    EaNext(alg);
}

// 0x7c  circle <u8 ramp> <u8 on> <u8 dir> <u16 spd> <u16 time>: with on 1
// the enemy keeps its distance to the player (XZ) and goes round them by
// spd * the frame scale / the distance radians a frame (dir 0 one way, else
// the other). _370 is the step, its height part (towards ofs[1] over the
// current height) clamped to +-1; a tenth of it is added to pos. ramp 1 / 2
// eases the speed in / out over time frames (PAL-scaled) with the counter
// ene_circle_cnt, which every enemy shares; 0 keeps it full. on != 1 only
// sets the counter: 0, or the full time for ramp 2.
// FF3 0x0018fba8

void EJobM0C(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR v    = { 0.0f, 0.0f, 0.0f, 0.0f };
    sceVu0FVECTOR rot  = { 0.0f, 0.0f, 0.0f, 0.0f };
    u_char        ramp = EaGetU8(alg);
    u_char        on   = EaGetU8(alg);
    u_char        dir  = EaGetU8(alg);
    u_short       spd  = EaGetU16(alg);
    u_short       time = EaGetU16(alg);
    int           n;
    float         a;

    v[2] = Get2PLength(plyr->pos, ene->pos);
    n = (int)((u_int)(int)((float)time / GetFrameTimeScale()) & 0xffff);
    if (n == 0)
        n = 1;

    if (on != 1)
    {
        if (ramp == 1)
            ene_circle_cnt = 0;
        else
            ene_circle_cnt = (ramp == 2) ? n : 0;
        EaNext(alg);
        return;
    }
    if (v[2] == 0.0f)
    {
        EaNext(alg);
        return;
    }

    if (ramp == 0)
        a = (float)spd;
    else
        a = (float)(spd * ene_circle_cnt * ene_circle_cnt) / (float)(n * n);
    a = (a * GetFrameTimeScale()) / v[2];
    if (ramp == 1)
        ene_circle_cnt = (ene_circle_cnt < n) ? ene_circle_cnt + 1 : n;
    else if (ramp == 2)
        ene_circle_cnt = (0 < ene_circle_cnt) ? ene_circle_cnt - 1 : 0;
    else
        ene_circle_cnt = 0;

    UnitGetTrgtRot(plyr->pos, ene->pos, rot, 2);
    if (dir == 0)
        rot[1] -= a;
    else
        rot[1] += a;
    RotLimitChk(&rot[1]);
    RotVectorZXY(v, v, rot);
    sceVu0AddVector(v, v, plyr->pos);
    sceVu0SubVector(v, v, ene->pos);
    v[1] = 0.0f;
    memcpy(ene->_370, v, sizeof(v));
    sceVu0AddVector(v, v, ene->pos);
    v[1] = (v[1] + ene->ofs[1]) - ene->pos[1];
    if (1.0f < fabsf(v[1]))
        v[1] = (v[1] < 0.0f) ? -1.0f : 1.0f;
    ene->_370[1] = v[1];
    sceVu0ScaleVector(v, ene->_370, 0.1f);
    sceVu0AddVector(ene->pos, ene->pos, v);
    EaNext(alg);
}

// 0x7d  back onto the route's previous node (route[2]): all of it while
// the roll is under 90 degrees, else only x and z.
// FF3 0x0018fee8

void EJobM0D(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);

    if (fabsf(ene->rot[2]) < 1.5707964f)
        memcpy(ene->pos, ene->route[2]->pos, sizeof(ene->pos));
    else
    {
        ene->pos[0] = ene->route[2]->pos[0];
        ene->pos[2] = ene->route[2]->pos[2];
    }
    EaNext(alg);
}

// ══════════════════════════════════════════════════════════════════════
// Branch (table 0x301518, opcodes 0xa0..0xd4). Jumps are u16 offsets from
// comm_add_top; a branch not taken goes on with the next opcode.
// ══════════════════════════════════════════════════════════════════════

// 0xa0  <u16 deg> <u16 jump>: jump while the player is within deg degrees
// of the enemy's facing (RotRngChk).
// FF3 0x0018ff78

void EJobB00(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  deg  = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);

    if (RotRngChk(ene->pos, ingame_wrk.plyr_wrk->pos, ene->rot[1], ((float)deg / 180.0f) * 3.1415927f) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa1  <u16 deg> <u16 jump>: jump while the player is not within deg
// degrees of the enemy's facing.
// FF3 0x00190060

void EJobB01(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  deg  = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);

    if (RotRngChk(ene->pos, ingame_wrk.plyr_wrk->pos, ene->rot[1], ((float)deg / 180.0f) * 3.1415927f) == 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa2  <u8 pct> <u16 jump>: jump when hp is at most pct percent of hp_max.
// FF3 0x00190148

void EJobB02(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_char   pct  = EaGetU8(alg);
    u_short  jump = EaGetU16(alg);

    if (ene->hp <= (ene->hp_max * (int)pct) / 100)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa3  <u8 pct> <u16 jump>: jump when hp is at least pct percent of hp_max.
// FF3 0x001901d0

void EJobB03(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_char   pct  = EaGetU8(alg);
    u_short  jump = EaGetU16(alg);

    if ((ene->hp_max * (int)pct) / 100 <= ene->hp)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa4  <u16 dist> <u16 jump>: jump when the player is nearer (XZ) than
// dist * 0.2.
// FF3 0x00190258

void EJobB04(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  dist = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);

    if (EaDistXZ(ingame_wrk.plyr_wrk->pos, ene->pos) < (float)dist * 0.2f)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa5  <u16 dist> <u16 jump>: jump when the player is dist * 0.2 or
// farther (XZ).
// FF3 0x00190320

void EJobB05(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  dist = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);

    if ((float)dist * 0.2f <= EaDistXZ(ingame_wrk.plyr_wrk->pos, ene->pos))
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa6  <u16 jump>: jump (the same as 0xa8).
// FF3 0x001903e8

void EJobB06(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    EaNext(alg);
    EaJumpTo(alg, jump);
}

// 0xa7  <u16 jump>: jump once the anime script has set flags 0x20000000
// (EneAnimeFlagUpdate).
// FF3 0x00190420

void EJobB07(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((EaEne(alg)->flags & 0x20000000) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xa8  <u16 jump>: jump.
// FF3 0x00190488

void EJobB08(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    EaNext(alg);
    EaJumpTo(alg, jump);
}

// 0xa9  <u16 jump>: jump once the hold strength is used up.
// FF3 0x001904c0

void EJobB09(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (EaEne(alg)->hold_pow <= 0.0f)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xaa  <u16 jump>: loop[0] counts down by 1 a pass; jump once it is out.
// FF3 0x00190528

void EJobB0A(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (alg->loop[0] <= 0.0f)
        EaJumpTo(alg, jump);
    else
        alg->loop[0] -= 1.0f;
    EaNext(alg);
}

// 0xab  <u8 n> <u16 jump>: the same on loop[n].
// FF3 0x00190590

void EJobB0B(ENEALG_WRK *alg)
{
    u_char  n    = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    if (alg->loop[n] <= 0.0f)
        EaJumpTo(alg, jump);
    else
        alg->loop[n] -= 1.0f;
    EaNext(alg);
}

// 0xac  <u16 j0> <u16 j1> <u16 j2> <u16 j3> <u16>: by the last photo hit's
// reaction (dmg_react): 0 / 1 jump to j0, 2 to j1, 3 / 4 to j3, 5 to j2;
// anything else goes on. The fifth operand is not used.
// FF3 0x00190610

void EJobB0C(ENEALG_WRK *alg)
{
    u_short j[5];
    int     i;

    for (i = 0; i < 5; i++)
        j[i] = EaGetU16(alg);
    switch (EaEne(alg)->dmg_react)
    {
    case 0:
    case 1:
        EaJumpTo(alg, j[0]);
        break;
    case 2:
        EaJumpTo(alg, j[1]);
        break;
    case 3:
    case 4:
        EaJumpTo(alg, j[3]);
        break;
    case 5:
        EaJumpTo(alg, j[2]);
        break;
    }
    EaNext(alg);
}

// 0xad  <u8 mode> <u16 jump> <u16>: the attack hit check. While the motion
// script's attack windows are open (flags 0x200000000: op 0x10's, then
// 0x100000000: op 0xd's), jump when the player is in reach: within _6a8
// (XZ) of the enemy, within _6ac radians of its facing and within 25 in
// height (plus ofs[1]). The 0x10 window closes on the hit; the 0xd one does
// not hit while the player's ene_wait runs. mode picks the points: 0 joint 1
// to the player's joint 1 against the enemy's facing, 1 the same against
// joint 3's angle about y, 2 the middle of joints 0x13 and 7 (3 out along x)
// to the player's pos. The last operand is not used.
// FF3 0x00190720

void EJobB0D(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    u_char        mode = EaGetU8(alg);
    u_short       jump = EaGetU16(alg);
    // PC: FF3 leaves the points uninitialised for modes past 2 (the scripts
    // use none).
    sceVu0FVECTOR epos = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR ppos = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR ofs, a, b, head;
    float         base = 0.0f;
    float         ang;
    u_long        fl;

    EaGetU16(alg);
    switch (mode)
    {
    case 0:
        EneGetJointPos(ene, 1, epos, nullptr);
        PlyrGetJointPos(1, ppos, nullptr);
        base = ene->rot[1];
        break;
    case 1:
        EneGetJointPos(ene, 1, epos, nullptr);
        PlyrGetJointPos(1, ppos, nullptr);
        base = EneGetJointRot(ene, 3, 1);
        break;
    case 2:
        ofs[0] = 3.0f;
        ofs[1] = 0.0f;
        ofs[2] = 0.0f;
        ofs[3] = 1.0f;
        EneGetJointPos(ene, 0x13, a, ofs);
        EneGetJointPos(ene, 7, b, ofs);
        GetMidPos(epos, a, b);
        memcpy(ppos, plyr->pos, sizeof(ppos));
        base = ene->rot[1];
        break;
    }
    ang = GetTrgtRotY(epos, ppos);
    RotLimitChk(&ang);
    ang -= base;
    RotLimitChk(&ang);

    // FF3 also takes the player's joint 2 (3.8 out along x) here and never
    // reads it.
    ofs[0] = 3.8f;
    ofs[1] = 0.0f;
    ofs[2] = 0.0f;
    ofs[3] = 1.0f;
    PlyrGetJointPos(2, head, ofs);

    fl = ene->flags;
    if ((fl & (u_long)0x200000000) != 0)
    {
        if (EaDistXZ(ppos, epos) <= ene->_6a8 && fabsf(ang) <= ene->_6ac &&
            fabsf((ppos[1] - epos[1]) + ene->ofs[1]) <= 25.0f)
        {
            ene->flags &= ~(u_long)0x200000000;
            EaJumpTo(alg, jump);
            EaNext(alg);
            return;
        }
        fl = ene->flags;
    }
    if ((fl & (u_long)0x100000000) != 0 &&
        EaDistXZ(ppos, epos) <= ene->_6a8 && fabsf(ang) <= ene->_6ac &&
        fabsf((ppos[1] - epos[1]) + ene->ofs[1]) <= 25.0f && plyr->ene_wait == 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xae  <u16 end> <u16 free>: the hold. The first pass turns the player to
// face the enemy (from pos + ofs; turned round in the _147 5 pose, the
// facing kept unless _147 is 8 or 9), puts the enemy at the player's height
// plus ofs[1] unless it is rolled past 90 degrees, and waits a frame. Then,
// while the enemy holds the player (ene_no): strength used up -> attacking
// (flags 0x4000), jump to end; step 1 waits one more frame, step 2 waits
// until the player breaks out (PLYR_FLG_20000): with PLYR_FLG_10000 SE 0 and
// jump to free, else attacking and jump to end. Held by another (or none):
// jump to free.
// FF3 0x00190ac8

void EJobB0E(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    u_char        idx  = alg->idx;
    sceVu0FVECTOR tmp;
    float         t;

    if (alg->pos_no == 0)
    {
        ene_hold_jump_end[idx]  = EaGetU16(alg);
        ene_hold_jump_free[idx] = EaGetU16(alg);
        sceVu0AddVector(tmp, plyr->pos, ene->ofs);
        UnitGetTrgtRot(tmp, ene->pos, ene_hold_rot[idx], 3);
        if (plyr->_147 == 5)
        {
            ene_hold_rot[idx][1] += 3.1415927f;
            RotLimitChk(&ene_hold_rot[idx][1]);
        }
        ene_hold_rot[idx][3] = plyr->rot[1];
        ene_hold_time[idx] = 0.0f;
        if ((u_char)(plyr->_147 - 8) > 1)
            plyr->rot[1] = ene_hold_rot[idx][1];
        if (fabsf(ene->rot[2]) < 1.5707964f)
            ene->pos[1] = plyr->pos[1] + ene->ofs[1];
        alg->pos_no++;
        alg->wait_time = 1.0f;
    }

    if (plyr->ene_no != idx)
    {
        alg->pos_no = 0;
        EaJumpTo(alg, ene_hold_jump_free[idx]);
        alg->wait_time = 0.0f;
        return;
    }

    if ((plyr->flags & PLYR_FLG_20000) == 0 || alg->pos_no != 2)
    {
        if (ene->hold_pow <= 0.0f)
        {
            ene->flags |= 0x4000;
            alg->wait_time = 0.0f;
            alg->pos_no = 0;
            EaJumpTo(alg, ene_hold_jump_end[idx]);
            return;
        }
        if (alg->pos_no == 1)
        {
            t = ene_hold_time[idx];
            if (1.0f <= t)
            {
                alg->wait_time = 1.0f;
                alg->pos_no = 2;
                return;
            }
            ene_hold_time[idx] = t + 1.0f;
            alg->wait_time = 1.0f;
            return;
        }
        if (alg->pos_no == 2)
            alg->wait_time = 1.0f;
        return;
    }

    if ((plyr->flags & PLYR_FLG_10000) == 0)
    {
        ene->flags |= 0x4000;
        EaJumpTo(alg, ene_hold_jump_end[idx]);
    }
    else
    {
        EneSePlay(ene, 0);
        EaJumpTo(alg, ene_hold_jump_free[idx]);
    }
    alg->pos_no = 0;
    alg->wait_time = 0.0f;
}

// 0xaf  <u16 jump>: jump once the hold is over: no strength left, or the
// player is held by another enemy (ene_no).
// FF3 0x00190e30

void EJobB0F(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (EaEne(alg)->hold_pow <= 0.0f || ingame_wrk.plyr_wrk->ene_no != alg->idx)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb0  <u8 pct> <u16 jump>: jump with a chance of pct in 99 (a random
// 1..99 at most pct).
// FF3 0x00190ea8

void EJobB10(ENEALG_WRK *alg)
{
    u_char  pct  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    // callcheck: rand (EaRand01)
    if ((int)(EaRand01() * 98.0f + 1.0f) <= (int)pct)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb1  <u8 pct> <u16 jump>: the other side of EJobB10: jump when the
// random 1..99 is pct or more.
// FF3 0x00190f78

void EJobB11(ENEALG_WRK *alg)
{
    u_char  pct  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    // callcheck: rand (EaRand01)
    if ((int)pct <= (int)(EaRand01() * 98.0f + 1.0f))
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb2  <u16 jump>: ends the finder camera cut (MapCamCutFinCamera) and
// jumps.
// FF3 0x00191048

void EJobB12(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    MapCamCutFinCamera();
    EaNext(alg);
    EaJumpTo(alg, jump);
}

// 0xb3  <u16 secs> <u16 jump>: jump once the battle has run longer than
// secs seconds (battle_cnt against 30 or, in PAL, 25 frames a second).
// FF3 0x001910b0

void EJobB13(ENEALG_WRK *alg)
{
    u_short secs = EaGetU16(alg);
    u_short jump = EaGetU16(alg);
    u_long  cnt  = ingame_wrk.plyr_wrk->battle_cnt;

    if (GetPALMode() == 0)
    {
        if ((u_long)secs * 30 < cnt)
            EaJumpTo(alg, jump);
    }
    else if ((u_long)secs * 25 < cnt)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb4  <u16 frames> <u16 jump>: jump once the player's _130 (cleared with
// battle_cnt) is past frames.
// FF3 0x00191190

void EJobB14(ENEALG_WRK *alg)
{
    u_short frames = EaGetU16(alg);
    u_short jump   = EaGetU16(alg);

    if ((u_long)frames < ingame_wrk.plyr_wrk->_130)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb5  <u16 deg> <u16 dist> <u16 jump>: jump when the player is within deg
// degrees of the enemy's facing and nearer (XZ) than dist * 0.2.
// FF3 0x00191200

void EJobB15(ENEALG_WRK *alg)
{
    ENE_WRK  *ene  = EaEne(alg);
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_short   deg  = EaGetU16(alg);
    u_short   dist = EaGetU16(alg);
    u_short   jump = EaGetU16(alg);
    float     rng  = ((float)deg / 180.0f) * 3.1415927f;

    RotLimitChk(&rng);
    if (RotRngChk(ene->pos, plyr->pos, ene->rot[1], rng) != 0 &&
        EaDistXZ(plyr->pos, ene->pos) < (float)dist * 0.2f)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// The linked enemies (dat->link, same type) EJobB16..B19 count: the slot of
// link i, -1 when there is none or it is not loaded.
static int EaLinkSlot(ENE_WRK *ene, int i)
{
    short id = ene->dat->link[i];

    if (id < 0)
        return -1;
    return SearchEneWrkNo(ene->type, id);
}

// 0xb6  <u16 n> <u16 jump>: jump when exactly n of the linked enemies are
// acting (state 3). A special enemy (type 2) has no links: n 0 jumps.
// FF3 0x00191380

void EJobB16(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  n    = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);
    u_int    cnt  = 0;
    int      i, no;

    // callcheck: SearchEneWrkNo (EaLinkSlot)
    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            no = EaLinkSlot(ene, i);
            if (no >= 0 && ene_wrk[no].state == ENE_STATE_ACT)
                cnt = (cnt + 1) & 0xff;
        }
    }
    if (n == cnt)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb7  <u8 who> <u8 n> <u16 val> <u16 jump>: jump when script variable
// var[n] is val: who 0xff the enemy's own, 0xfe any acting linked enemy's
// with hp left, 0..7 link who's (acting, hp left).
// FF3 0x001914c8

void EJobB17(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_char   who  = EaGetU8(alg);
    u_char   n    = EaGetU8(alg);
    u_short  val  = EaGetU16(alg);
    u_short  jump = EaGetU16(alg);
    ENE_WRK *lnk;
    int      i, no;

    // callcheck: SearchEneWrkNo (EaLinkSlot)
    if (who == 0xff)
    {
        if (alg->var[n] == val)
            EaJumpTo(alg, jump);
    }
    else if (who == 0xfe)
    {
        if (ene->type != 2)
        {
            for (i = 0; i < 8; i++)
            {
                no = EaLinkSlot(ene, i);
                if (no < 0)
                    continue;
                lnk = &ene_wrk[no];
                if (lnk->state == ENE_STATE_ACT && lnk->hp != 0 && lnk->alg.var[n] == val)
                    EaJumpTo(alg, jump);
            }
        }
    }
    else if (who < 8 && ene->type != 2)
    {
        no = EaLinkSlot(ene, who);
        if (no >= 0)
        {
            lnk = &ene_wrk[no];
            if (lnk->state == ENE_STATE_ACT && lnk->hp != 0 && lnk->alg.var[n] == val)
                EaJumpTo(alg, jump);
        }
    }
    EaNext(alg);
}

// 0xb8  <u8 act> <u8 n> <u16 jump>: jump when exactly n linked enemies run
// action act.
// FF3 0x001916d0

void EJobB18(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_char   act  = EaGetU8(alg);
    u_char   n    = EaGetU8(alg);
    u_short  jump = EaGetU16(alg);
    u_char   cnt  = 0;
    int      i, no;

    // callcheck: SearchEneWrkNo (EaLinkSlot)
    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            no = EaLinkSlot(ene, i);
            if (no >= 0 && ene_wrk[no].act_no == act)
                cnt++;
        }
    }
    if (cnt == n)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xb9  <u8 cnt> <u8 n> <u8 val> <u16 jump>: jump when exactly cnt acting
// linked enemies with hp left have var[n] == val.
// FF3 0x00191810

void EJobB19(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_char   want = EaGetU8(alg);
    u_char   n    = EaGetU8(alg);
    u_char   val  = EaGetU8(alg);
    u_short  jump = EaGetU16(alg);
    u_char   cnt  = 0;
    ENE_WRK *lnk;
    int      i, no;

    // callcheck: SearchEneWrkNo (EaLinkSlot)
    if (ene->type != 2)
    {
        for (i = 0; i < 8; i++)
        {
            no = EaLinkSlot(ene, i);
            if (no < 0)
                continue;
            lnk = &ene_wrk[no];
            if (lnk->state == ENE_STATE_ACT && lnk->hp != 0 && lnk->alg.var[n] == val)
                cnt++;
        }
    }
    if (cnt == want)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xba  <u8 side> <u16 jump>: which side of the enemy's facing its target
// (pos + ofs) is on: side 0 jumps when the angle to it is above the facing,
// 1 when it is at or below.
// FF3 0x00191988

void EJobB1A(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    u_char        side = EaGetU8(alg);
    u_short       jump = EaGetU16(alg);
    sceVu0FVECTOR tmp, rot;
    float         d;

    sceVu0AddVector(tmp, ene->target->pos, ene->ofs);
    UnitGetTrgtRot(ene->pos, tmp, rot, 3);
    d = rot[1] - ene->rot[1];
    RotLimitChk(&d);
    if (0.0f < d ? side == 0 : side == 1)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xbb  <u16 jump>: jump once flags 0x400000000 is set; waits a frame
// either way.
// FF3 0x00191aa0

void EJobB1B(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((EaEne(alg)->flags & (u_long)0x400000000) != 0)
        EaJumpTo(alg, jump);
    alg->pos_no = 0;
    alg->wait_time = 1.0f;
}

// 0xbc  <u16 jump>: an auto enemy's end of action: with no act_len (< 0)
// jump once the anime script sets flags 0x20000000, else jump once loop[0]
// (EJobC27's timer) is out, counting it down by 1 a pass.
// FF3 0x00191b18

void EJobB1C(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  jump = EaGetU16(alg);

    if (ene->adat->act_len < 0)
    {
        if ((ene->flags & 0x20000000) != 0)
            EaJumpTo(alg, jump);
    }
    else if (alg->loop[0] <= 0.0f)
        EaJumpTo(alg, jump);
    else
        alg->loop[0] -= 1.0f;
    EaNext(alg);
}

// 0xbd  <u16 jump>: a special enemy (type 2) with a near_dist: jump once the
// player is that near (XZ) and set its id flag 0x10.
// FF3 0x00191bc0

void EJobB1D(ENEALG_WRK *alg)
{
    ENE_WRK *ene  = EaEne(alg);
    u_short  jump = EaGetU16(alg);
    float    near_dist;

    if (ene->type == 2)
    {
        near_dist = ene->adat->near_dist * 0.2f;
        if (0.0f < near_dist && EaDistXZ(ingame_wrk.plyr_wrk->pos, ene->pos) <= near_dist)
        {
            EaJumpTo(alg, jump);
            EneIdFlgSet(ene->type, ene->id, 0x10);
        }
    }
    EaNext(alg);
}

// 0xbe  <u16 j1> <u16 j2>: the action an event asked for (act_req, then
// cleared): 1 and 0xf0 jump to j1, 2 to j2.
// FF3 0x00191cb0

void EJobB1E(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    u_char   req = ene->act_req;
    u_short  j1  = EaGetU16(alg);
    u_short  j2  = EaGetU16(alg);

    if (req == 1 || req == 0xf0)
        EaJumpTo(alg, j1);
    else if (req == 2)
        EaJumpTo(alg, j2);
    alg->pos_no = 0;
    ene->act_req = 0;
    alg->wait_time = 0.0f;
}

// 0xbf  <u16 jump>: jump (ending the approach camera) unless this enemy
// still holds the player (status 4 with time left).
// FF3 0x00191d58

void EJobB1F(ENEALG_WRK *alg)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_short   jump = EaGetU16(alg);

    if (plyr->status == 4 && 0.0f < plyr->status_time && plyr->ene_no == alg->idx)
    {
        EaNext(alg);
        return;
    }
    EaJumpTo(alg, jump);
    EndPlyrApproachCameraCtrl();
    EaNext(alg);
}

// 0xc0  <u8 cmp> <u8 n> <u8 val> <u16 jump>: on the byte counter cnt[n]:
// cmp 0 jumps when it is at most val, else when it is at least val.
// FF3 0x00191df0

void EJobB20(ENEALG_WRK *alg)
{
    u_char  cmp  = EaGetU8(alg);
    u_char  n    = EaGetU8(alg);
    u_char  val  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);
    int     stay;

    if (cmp == 0)
        stay = val < alg->cnt[n];
    else
        stay = alg->cnt[n] < val;
    if (!stay)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc1  <u16 dist> <u16 deg> <u16 jump>: jump when the enemy sees the
// player: its view cone (dist * 0.2 long, deg wide; EneViewPlyrCheck) holds
// the player and nothing is between joint 2 and the player's joint 2 (the
// player's own hit line, then the map's in the player's area). Also jumps
// while _6f0 (EJobC30's timer) runs.
// FF3 0x00191e78

void EJobB21(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    u_short       dist = EaGetU16(alg);
    u_short       deg  = EaGetU16(alg);
    u_short       jump = EaGetU16(alg);
    int           area = GetPlyrAreaNo();
    sceVu0FVECTOR ppos, epos;

    PlyrGetJointPos(2, ppos, nullptr);
    EneGetJointPos(ene, 2, epos, nullptr);
    if ((EneViewPlyrCheck((float)dist * 0.2f, (float)deg, ene) != 0 && PlyrLineHitCheck(epos) == 0 &&
         MapAreaHitLineCheck(epos, ppos, area) == 0) ||
        0.0f < ene->_6f0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc2  <u16 jump>: jump while the player's flags2 has any of 0xe.
// FF3 0x00191ff0

void EJobB22(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((ingame_wrk.plyr_wrk->flags2 & 0xe) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc3  <u16 jump>: jump while the player's flags2 has 1.
// FF3 0x00192040

void EJobB23(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((ingame_wrk.plyr_wrk->flags2 & 1) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc4  <u16 jump>: walk to turn_pos on the motion's root move: the height
// eases towards it by this step's share of the distance (unless flags2 8),
// the facing turns to it at 4 degrees a frame; jump once within 7 of it.
// Ends the follow (EneTurnReq's).
// FF3 0x00192098

void EJobB24(ENEALG_WRK *alg)
{
    ENE_WRK      *ene = EaEne(alg);
    CharModel    *mdl;
    u_short       jump;
    sceVu0FVECTOR goal;
    float         dist, step;

    ene->follow = 0;
    mdl  = static_cast<CharModel *>(ene->mdl);
    jump = EaGetU16(alg);
    memcpy(goal, ene->turn_pos, sizeof(goal));
    dist = EaDist3D(ene->pos, goal);
    step = -mdl->GetRootMove(0, ene->mot_attr);
    step = GetLength2D(step, mdl->GetRootMove(2, ene->mot_attr));
    // PC: not at the goal itself (dist 0; see the route step above).
    if (ene->pos[1] != goal[1] && (ene->flags2 & 8) == 0 && dist > 0.0f)
        ene->pos[1] -= (step / dist) * (ene->pos[1] - goal[1]);
    UnitGetTrgtRot(ene->pos, goal, ene->rot_trgt, 2);
    // callcheck: fptodp, fabs, dpcmp (the compare is done in double)
    if (7.0 <= fabs((double)(dist - step)))
        ene->turn_spd = GetFrameTimeScale() * 0.69813168f * 0.1f;
    else
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc5  <u8 cond> <u16 dist> <u16 jump>: jump when the player is nearer (in
// 3D) than dist * 0.2 and cond holds: 0 flags2 has any of 0xe, 1 flags2 has
// 1, 2 the player's _254 timer runs, 3 its _338 timer runs.
// FF3 0x00192268

void EJobB25(ENEALG_WRK *alg)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    float     d    = EaDist3D(plyr->pos, EaEne(alg)->pos);
    u_char    cond = EaGetU8(alg);
    u_short   dist = EaGetU16(alg);
    u_short   jump = EaGetU16(alg);
    int       ok;

    switch (cond)
    {
    case 0:  ok = (plyr->flags2 & 0xe) != 0; break;
    case 1:  ok = (plyr->flags2 & 1) != 0; break;
    case 2:  ok = 0.0f < plyr->_254; break;
    case 3:  ok = 0.0f < plyr->_338; break;
    default: ok = 0; break;
    }
    if (ok && d < (float)dist * 0.2f)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc6  <u8 n> <u16 jump>: jump when the hp "bars" left (EneHpRateCeil,
// ceil(hp / hp_max)) are at most n.
// FF3 0x001923d8

void EJobB26(ENEALG_WRK *alg)
{
    u_char  n    = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    if (EneHpRateCeil(EaEne(alg)) <= (int)n)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc7  <u8 n> <u16 jump>: ... at least n.
// FF3 0x00192470

void EJobB27(ENEALG_WRK *alg)
{
    u_char  n    = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    if ((int)n <= EneHpRateCeil(EaEne(alg)))
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// The percentage left of the current hp bar (hp above the full bars below
// it, over hp_max).
static int EaHpBarPct(ENE_WRK *ene)
{
    int rate = EneHpRateCeil(ene);

    return ((ene->hp - (rate - 1) * ene->hp_max) * 100) / ene->hp_max;
}

// 0xc8  <u8 pct> <u16 jump>: jump when the current bar is at most pct
// percent full.
// FF3 0x00192508

void EJobB28(ENEALG_WRK *alg)
{
    u_char  pct  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    // callcheck: EneHpRateCeil (EaHpBarPct)
    if (EaHpBarPct(EaEne(alg)) <= (int)pct)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xc9  <u8 pct> <u16 jump>: ... at least pct percent.
// FF3 0x001925e0

void EJobB29(ENEALG_WRK *alg)
{
    u_char  pct  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    // callcheck: EneHpRateCeil (EaHpBarPct)
    if ((int)pct <= EaHpBarPct(EaEne(alg)))
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xca  <u8 lit> <u16 jump>: on the candle: lit 0 jumps when it is out, else
// when it burns.
// FF3 0x001926b8

void EJobB2A(ENEALG_WRK *alg)
{
    u_char  lit  = EaGetU8(alg);
    u_short jump = EaGetU16(alg);
    int     out  = EaCandle()->IsOut();

    // callcheck: GetCandleControl (EaCandle)
    if (lit == 0 ? out != 0 : out == 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xcb  <u16 jump>: jump once flags 0x40000000 is set.
// FF3 0x00192780

void EJobB2B(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((EaEne(alg)->flags & 0x40000000) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xcc  <u8 arm> <u16 jump>: an arm sweep hits the player: the arm (0:
// joints 0x18 -> 0x13 out 4, else 0xc -> 7) crosses, seen from above (x,
// z), one of the two segments through the player's joint 2 (5.8 either side
// along z, 7 / -6 along x), and seen from the side (y, z) the segment from
// the player's joint 2 (7 out along x) down to pos; never while the player's
// ene_wait runs.
// FF3 0x001927e8

void EJobB2C(ENEALG_WRK *alg)
{
    ENE_WRK      *ene  = EaEne(alg);
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    u_char        arm  = EaGetU8(alg);
    u_short       jump = EaGetU16(alg);
    sceVu0FVECTOR ofs  = {7.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR p0, e0, e1, pz0, pz1, px0, px1;
    LINE_T        arm_xz, arm_yz, plyr_z, plyr_x, plyr_y;
    int           j;

    PlyrGetJointPos(2, p0, ofs);
    ofs[0] = 0.0f;
    if (arm == 0)
    {
        EneGetJointPos(ene, 0x18, e0, ofs);
        j = 0x13;
    }
    else
    {
        EneGetJointPos(ene, 0xc, e0, ofs);
        j = 7;
    }
    ofs[0] = 4.0f;
    EneGetJointPos(ene, j, e1, ofs);
    ofs[0] = 0.0f;
    ofs[2] = 5.8f;
    PlyrGetJointPos(2, pz0, ofs);
    ofs[2] = -5.8f;
    PlyrGetJointPos(2, pz1, ofs);
    ofs[0] = 7.0f;
    ofs[2] = 0.0f;
    PlyrGetJointPos(2, px0, ofs);
    ofs[0] = -6.0f;
    PlyrGetJointPos(2, px1, ofs);

    arm_xz.a.x = e0[0];  arm_xz.a.y = e0[2];  arm_xz.b.x = e1[0];  arm_xz.b.y = e1[2];
    arm_yz.a.x = e0[1];  arm_yz.a.y = e0[2];  arm_yz.b.x = e1[1];  arm_yz.b.y = e1[2];
    plyr_z.a.x = pz0[0]; plyr_z.a.y = pz0[2]; plyr_z.b.x = pz1[0]; plyr_z.b.y = pz1[2];
    plyr_x.a.x = px0[0]; plyr_x.a.y = px0[2]; plyr_x.b.x = px1[0]; plyr_x.b.y = px1[2];
    plyr_y.a.x = p0[1];  plyr_y.a.y = p0[2];  plyr_y.b.x = plyr->pos[1]; plyr_y.b.y = plyr->pos[2];

    if ((LineIntersect(&arm_xz, &plyr_z) != 0 || LineIntersect(&arm_xz, &plyr_x) != 0) &&
        LineIntersect(&arm_yz, &plyr_y) != 0 && plyr->ene_wait == 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xcd  <u16 jump>: jump while the player has the camera up
// (PLYR_FLG_800000).
// FF3 0x00192af8

void EJobB2D(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xce  <u8 status> <u16 jump>: jump when PlyrStatusIs(status).
// FF3 0x00192b50

void EJobB2E(ENEALG_WRK *alg)
{
    u_char  status = EaGetU8(alg);
    u_short jump   = EaGetU16(alg);

    if (PlyrStatusIs(status) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xcf  <u8 a> <u16 jump>: jump when alpha_a is a.
// FF3 0x00192bc8

void EJobB2F(ENEALG_WRK *alg)
{
    u_char  a    = EaGetU8(alg);
    u_short jump = EaGetU16(alg);

    if (EaEne(alg)->alpha_a == a)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xd0  <u16 deg> <u16 near> <u16 far> <u16 jump>: jump when the player is
// between near * 0.2 and far * 0.2 away (XZ) and within deg degrees of the
// enemy's facing.
// FF3 0x00192c30

void EJobB30(ENEALG_WRK *alg)
{
    ENE_WRK  *ene  = EaEne(alg);
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_short   deg  = EaGetU16(alg);
    u_short   dmin = EaGetU16(alg);
    u_short   dmax = EaGetU16(alg);
    u_short   jump = EaGetU16(alg);
    float     d    = EaDistXZ(plyr->pos, ene->pos);
    float     rng;

    if ((float)dmin * 0.2f <= d && d < (float)dmax * 0.2f)
    {
        rng = ((float)deg * 3.1415927f) / 180.0f;
        RotLimitChk(&rng);
        if (RotRngChk(ene->pos, plyr->pos, ene->rot[1], rng) != 0)
            EaJumpTo(alg, jump);
    }
    EaNext(alg);
}

// 0xd1  <u16 jump>: jump while the enemy is in the viewfinder frame (flags
// 0x200).
// FF3 0x00192dd8

void EJobB31(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if ((EaEne(alg)->flags & 0x200) != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xd2  <u16 jump>: jump when the player is in mode 7 (the death scene).
// FF3 0x00192e38

void EJobB32(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (ingame_wrk.plyr_wrk->mode == PLYR_MODE_7)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xd3  <u16 jump>: jump once the tattoo event is past its step 5.
// FF3 0x00192e88

void EJobB33(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (TattooEvIsLate() != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// 0xd4  <u16 jump>: jump while the tattoo event is idle.
// FF3 0x00192ef0

void EJobB34(ENEALG_WRK *alg)
{
    u_short jump = EaGetU16(alg);

    if (TattooEvIsIdle() != 0)
        EaJumpTo(alg, jump);
    EaNext(alg);
}

// ══════════════════════════════════════════════════════════════════════
// Effect (table 0x3015f0, opcodes 0xe0..0xe7)
// ══════════════════════════════════════════════════════════════════════

// 0xe0  <u8 in> <u8 keep> <u8 out>: the negative flash (CallNega2).
// FF3 0x00192f58

void EJobE00(ENEALG_WRK *alg)
{
    u_char in   = EaGetU8(alg);
    u_char keep = EaGetU8(alg);
    u_char out  = EaGetU8(alg);

    CallNega2(in, keep, out);
    EaNext(alg);
}

// 0xe1: puts out every lantern effect.
// FF3 0x00192fb0

void EJobE01(ENEALG_WRK *alg)
{
    EffLanternCut(-1);
    EaNext(alg);
}

// 0xe2  <u16 frames>: fades in from grey (0x80, 0x80, 0x80).
// FF3 0x00192fe0

void EJobE02(ENEALG_WRK *alg)
{
    u_short frames = EaGetU16(alg);

    FadeInReq(0x80, 0x80, 0x80, frames);
    EaNext(alg);
}

// 0xe3  <u16 frames>: fades out to grey.
// FF3 0x00193038

void EJobE03(ENEALG_WRK *alg)
{
    u_short frames = EaGetU16(alg);

    FadeOutReq(0x80, 0x80, 0x80, frames);
    EaNext(alg);
}

// 0xe4: starts the tattoo event on the enemy's first linked enemy.
// FF3 0x00193090

void EJobE04(ENEALG_WRK *alg)
{
    ENE_WRK *ene = EaEne(alg);
    int      no;

    if (ene->dat->link[0] >= 0)
    {
        no = SearchEneWrkNo(ene->type, ene->dat->link[0]);
        if (no >= 0)
            TattooEvStart(&ene_wrk[no]);
    }
    EaNext(alg);
}

// 0xe5: lets the tattoo event go on from its step 6.
// FF3 0x00193108

void EJobE05(ENEALG_WRK *alg)
{
    TattooEvResume();
    EaNext(alg);
}

// 0xe6: the object effects (kinds 0x1f / 0x20) at rate 1.
// FF3 0x00193138

void EJobE06(ENEALG_WRK *alg)
{
    EneObjEffSetRate(1.0f, EaEne(alg));
    EaNext(alg);
}

// 0xe7: ... and at 0.
// FF3 0x00193188

void EJobE07(ENEALG_WRK *alg)
{
    EneObjEffSetRate(0.0f, EaEne(alg));
    EaNext(alg);
}
