/* ==========================================================================
 *  ingame/plyr/unit_ctl.c
 *
 *  Angle / point helpers, stick readers and the video-mode select (see
 *  unit_ctl.h). FF3 0x25e9e0..0x25fce0. The first part is FF2 unit_ctl.c
 *  (MioPan zero_rom/zero2np/src/ingame/plyr/unit_ctl.c) with FF3's pi
 *  (0x40490fdb, where FF2 had the truncated 0x40490fda); GetTrgtRotFromPlyr,
 *  GetRndSP and ReqEneStop are gone from it. The video-mode select replaces
 *  FF2 outgame/framerate.c: FF3 draws it from E_SETTINGS_PHF's item hooks.
 *
 *  FF3 reads GetOptionWrk / GetSysWrk / GetGPhaseCtrl
 *  through gp-relative cached pointers; nothing depends on the cache, so
 *  calls go to the getters directly. FF3 0x0025fcc0 (GPhaseCtrlTerm on the
 *  controller singleton) is the compiler's atexit thunk and is not written.
 * ======================================================================== */
#include "ingame/plyr/unit_ctl.h"

#include <math.h>                           // sqrtf, fabsf

#include "common/cmn_fade_layer.h"          // GetCmnFadeLayer, CmnFadeLayer*
#include "common/vu0math.h"                 // g3dAtan2f, RotLimitChk, GetRandValF, Vu0Sinf / Cosf
#include "graphics/graph3d/gra3d.h"         // gra3dcamGetPosition / Target
#include "graphics/motion/anicode.h"        // B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState
#include "ingame/menu/menu_bg.h"            // B2dHookPrintMsg, B2dHookPrintNumber, B2dDrawSpriteDirectLinear
#include "ingame/menu/menu_csr.h"
#include "main/gphase.h"
#include "outgame/option.h"                 // GetOptionWrk
#include "sdk/libvu0.h"                     // sceVu0SubVector
#include "system/b2d/B2dCtl.h"              // B2D_HOOK_ARGS
#include "system/b2d/BpCtl.h"
#include "system/eeiop/cd_file_dat.h"       // E_SETTINGS_PHF
#include "system/os/system.h"               // GetSysWrk, SYS_WRK::GetLanguage, GetPALMode, ChangeVideoMode
#include "system/pad/pad.h"                 // pad_wrk
#include "system/pad/pad_ctrl.h"
#include "graphics/effect/effect_sub.h" // WorldToScreenPos
#include "reipan/gameplay/reipan_mouse_actions.h"   // PC: the mouse wheel changes film

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json,
// or inferred only). Move each block to its real header when that run is
// defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────

#define UNIT_CTL_PI     3.14159274f         // sdata 0x372764 (0x40490fdb)

// ══════════════════════════════════════════════════════════════════════
// Angles and points
// ══════════════════════════════════════════════════════════════════════

// The floor-plane (XZ) distance as FF3 inlines it (FF2 GetDistV): vsub.xyz,
// vmul.xyz, vaddz.x, vsqrt (of the magnitude).
static float UnitDistXZ(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];

    return sqrtf(fabsf(dx * dx + dz * dz));
}

// FF3 0x0025e9e0; FF2 GetTrgtRot (unit_ctl.c).
void UnitGetTrgtRot(float *p0, float *p1, float *rot, u_char id)
{
    sceVu0FVECTOR dist;

    rot[0] = 0.0f;
    rot[1] = 0.0f;
    rot[2] = 0.0f;

    sceVu0SubVector(dist, p1, p0);

    if ((id & 1) != 0)
    {
        dist[3] = UnitDistXZ(p0, p1);
        rot[0]  = g3dAtan2f(-dist[1], dist[3]);
        RotLimitChk(&rot[0]);
    }

    if ((id & 2) != 0)
    {
        rot[1] = g3dAtan2f(dist[0], dist[2]);
        RotLimitChk(&rot[1]);
    }
}

// FF3 0x0025eab8; FF2 GetTrgtRotY.
float GetTrgtRotY(float *p0, float *p1)
{
    return g3dAtan2f(p1[0] - p0[0], p1[2] - p0[2]);
}

// FF3 0x0025eae8; FF2 RotRngChk. rot is reused as the scratch angle (it
// lives in a stack slot because RotLimitChk takes its address).
int RotRngChk(float *vp, float *tp, float rot, float rng)
{
    rot = GetTrgtRotY(vp, tp) - rot;
    RotLimitChk(&rot);
    rot = fabsf(rot);

    return rot <= rng;
}

// FF3 0x0025eb40; FF2 OutSightChk. The distance test is written as the ROM
// branches (!(d <= dist)), so a NaN distance counts as out of sight.
int OutSightChk(float *tp, float *vp, float rot, float sight, float dist)
{
    if (dist != 0.0f && !(UnitDistXZ(tp, vp) <= dist))
        return 1;

    // sight is the full cone width.
    return RotRngChk(vp, tp, rot, sight * 0.5f) == 0;      /* sdata 0x372758 */
}

// FF3 0x0025ebc8; new in FF3 (in FF2 GetRndSP's place).
void GetMidPos(float *out, float *a, float *b)
{
    out[0] = (a[0] + b[0]) * 0.5f;                          /* sdata 0x37275c */
    out[1] = (a[1] + b[1]) * 0.5f;
    out[3] = 1.0f;                                          /* sdata 0x372760 */
    out[2] = (a[2] + b[2]) * 0.5f;
}

// FF3 0x0025ec28; FF2 ConvertRot2Dir. Each block: lift the heading by pi so
// it is not negative, add half a sector, divide by the sector, take it
// modulo the count and turn by half a turn so 0 is straight ahead. (The
// float -> int conversion truncates, as the EE's cvt.w.s does.)
u_char ConvertRot2Dir(float rot, u_char id)
{
    u_char dir;

    switch (id)
    {
    case 0:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 4.0f) / (UNIT_CTL_PI / 2.0f)) % 4);
        dir = (u_char)(dir + 2);
        if (dir >= 4)
            dir = (u_char)(dir - 4);
        return dir;

    case 1:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 8.0f) / (UNIT_CTL_PI / 4.0f)) % 8);
        dir = (u_char)(dir + 4);
        if (dir >= 8)
            dir = (u_char)(dir - 8);
        return dir;

    case 2:
        dir = (u_char)((int)((rot + UNIT_CTL_PI + UNIT_CTL_PI / 2.0f) / UNIT_CTL_PI) % 2);
        dir = (u_char)(dir + 1);
        if (dir >= 2)
            dir = (u_char)(dir - 2);
        return dir;

    default:
        return rot < 0.0f ? 1 : 0;
    }
}

// FF3 0x0025edc0 (no Ghidra function before 2026-10-14: it follows
// ConvertRot2Dir's last block). No callers in the retail ELF.
void GetRndRingPos(float *out, float *base, float max, float min)
{
    float r = GetRandValF(max - min) + min;
    float a = GetRandValF(UNIT_CTL_PI * 2.0f) - UNIT_CTL_PI; /* sdata 0x372774, 0x372778 */

    out[0] = base[0] + r * Vu0Sinf(a);
    out[1] = base[1];
    out[2] = base[2] + r * Vu0Cosf(a);
    out[3] = base[3];
}

// FF3 0x0025ee70 (mula / madd, then VU0 vsqrt).
float GetLength2D(float x, float y)
{
    return sqrtf(fabsf(x * x + y * y));
}

// FF3 0x0025ee98
int IsPosInView(float *pos)
{
    float *cam = gra3dcamGetPosition();
    float  rot = GetTrgtRotY(cam, gra3dcamGetTarget());
    float  sx, sy;

    WorldToScreenPos(pos, &sx, &sy);
    if (OutSightChk(pos, gra3dcamGetPosition(), rot, UNIT_CTL_PI, 300.0f) != 0)  /* sdata 0x37277c, 0x372780 */
        return 0;
    // Written as the ROM branches: a NaN coordinate is off screen.
    if (!(0.0f <= sx) || !(sx <= 640.0f) || !(0.0f <= sy) || !(sy <= 448.0f))   /* sdata 0x372784, 0x372788 */
        return 0;
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// Sticks
// ══════════════════════════════════════════════════════════════════════

// A d-pad pressure (0..0xff) as a stick half-axis: 2..48 -> 0..127.
static int UnitPressureToAxis(u_char press)
{
    int p = 2;

    if (press > 1)
    {
        p = press;
        if (p > 0x30)
            p = 0x30;
    }
    return (u_short)((p * 0x7f - 0xfe) / 0x2e);
}

// FF3 0x0025ef78 (Ghidra's TitleMenuMovePhase until 2026-10-14: a wrong FF2
// match). Jump table data 0x3634f0.
void GetStickRaw(u_char mode, u_char *x, u_char *y)
{
    OPTION_DAT *opt = &GetOptionWrk()->now;

    switch (mode)
    {
    case 0:
    case 1:
        if (opt->b12 == 0)
            *x = (u_char)((PadGetPressure(1, 0) >> 1) + (PadGetPressure(0, 0) >> 1) - 0x80);
        else
            *x = PadGetAnalogRaw(opt->b13 != 0 ? 0 : 2, 0);
        *y = 0;
        return;

    case 2:
        *x = PadGetAnalogRaw(2, 0);
        *y = PadGetAnalogRaw(3, 0);
        return;

    case 4:
        *x = PadGetAnalogRaw(0, 0);
        *y = PadGetAnalogRaw(1, 0);
        return;

    case 5:
        if (opt->b12 == 1)
        {
            // Left / right and up / down pressures (buttons 1 / 0, 2 / 3).
            int l = UnitPressureToAxis(PadGetPressure(1, 0));
            int r = UnitPressureToAxis(PadGetPressure(0, 0));
            *x = (u_char)(r - l - 0x80);
            int u = UnitPressureToAxis(PadGetPressure(2, 0));
            int d = UnitPressureToAxis(PadGetPressure(3, 0));
            *y = (u_char)(d - u - 0x80);
        }
        else if (opt->b13 == 0)
        {
            *x = PadGetAnalogRaw(2, 0);
            *y = PadGetAnalogRaw(3, 0);
        }
        else
        {
            *x = PadGetAnalogRaw(0, 0);
            *y = PadGetAnalogRaw(1, 0);
        }
        if (opt->b09 == 1)
            *y = (u_char)~*y;
        return;

    case 6:
        if (opt->b13 != 0)
        {
            *x = PadGetAnalogRaw(2, 0);
            *y = PadGetAnalogRaw(3, 0);
        }
        else
        {
            *x = PadGetAnalogRaw(0, 0);
            *y = PadGetAnalogRaw(1, 0);
        }
        return;

    default:
        return;
    }
}

// The stick option b13 picks entered 8-way direction dir this frame.
static int UnitStickTrg(const OPTION_DAT *opt, u_char dir)
{
    int axis = opt->b13 != 0 ? 1 : 0;

    if (PadGetStickByte(axis, 0, 0) != dir)
        return 0;
    return PadGetStickWord(axis, 0, 0) == 1;
}

// FF3 0x0025f268
int GetDirTrg(u_char dir)
{
    OPTION_DAT *opt = &GetOptionWrk()->now;
    u_short     mask;

    // PC: the mouse wheel changes film too (0 previous, 1 next).
    if ((dir == 0 || dir == 1) && ReiPan_MouseActionFilmTrg(dir))
        return 1;

    switch (dir)
    {
    case 0:
        if (opt->b12 != 0)
            return UnitStickTrg(opt, 6);
        mask = 0x8000;                      // d-pad left
        break;

    case 1:
        if (opt->b12 != 0)
            return UnitStickTrg(opt, 2);
        mask = 0x2000;                      // d-pad right
        break;

    case 3:
        if (PadGetStickByte(0, 0, 0) != 4)
            return 0;
        return PadGetStickWord(0, 0, 0) == 1;

    default:
        return 0;
    }
    return CheckPadTrgPlaySe(mask, -1, 0);
}

// FF3 0x0025f410. Only the x / z words of v reach GetTrgtRotY.
float GetStickRot(u_char mode)
{
    sceVu0FVECTOR org = { 0.0f, 0.0f, 0.0f, 1.0f };        /* sdata 0x37278c */
    sceVu0FVECTOR v   = { 0.0f, 0.0f, 0.0f, 0.0f };
    u_char        x, y;
    float         rot;

    GetStickRaw(mode, &x, &y);
    v[2] = (float)(int)(0xff - y) - 128.0f;                 /* sdata 0x372790 */
    v[0] = (float)x - 128.0f;

    rot = GET_STICK_ROT_NONE;
    if (50.0f <= GetLength2D(v[0], v[2]))                   /* sdata 0x372798 */
    {
        rot = GetTrgtRotY(org, v);
        RotLimitChk(&rot);
    }
    return rot;
}

// FF3 0x0025f4d8
u_char GetStickDir8(u_char mode)
{
    float rot = GetStickRot(mode);

    if (rot == GET_STICK_ROT_NONE)
        return 0xff;
    return ConvertRot2Dir(rot, 1);
}

// ══════════════════════════════════════════════════════════════════════
// Video-mode select
// ══════════════════════════════════════════════════════════════════════

#define FRAMERATE_CSR       0x12            // menu cursor: 480p, PAL 50 Hz, NTSC 60 Hz

enum
{
    FRAMERATE_SE_MOVE   = 0x1a9b0000,       // SYS_BASE_BD
    FRAMERATE_SE_CANCEL = 0x1a9b0001,
    FRAMERATE_SE_OK     = 0x1a9b0003,
};

// Video mode of each cursor item (data 0x3727c0).
static const u_char framerate_mode_tbl[3] = { VIDEO_MODE_DTV480P, VIDEO_MODE_PAL, VIDEO_MODE_NTSC };

static short framerate_mode;            // sbss 0x373a08: the mode to fall back to
static short framerate_timer;           // sbss 0x373a0a: frames left to confirm a new mode, 0 = none

static int FrameRateSelLayoutHandle(void)
{
    return BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + E_SETTINGS_PHF);
}

// The cursor item of a video mode, 0 when it is not in the table.
static short FrameRateSelModeToPos(u_char mode)
{
    for (int i = 0; i < 3; i++)
    {
        if (framerate_mode_tbl[i] == mode)
            return (short)i;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Item hooks of E_SETTINGS_PHF (data 0x363520, copied to the stack before
// B2dSetHookList). Words: t1 the item rectangle (text) or x (sprite), t3
// colour, stack[2] alpha, stack[3] flags.
// ──────────────────────────────────────────────────────────────────────

// Item "setting_msg": message (0x84, 2), or (0x84, 3) while a new mode
// waits for its confirmation. FF3 0x0025fae8 (no Ghidra function before
// 2026-10-14: it follows one_Title_FrameRate_Sel).
static int FrameRateSelMsgDrawCallback(const B2D_HOOK_ARGS *args)
{
    B2dHookPrintMsg(0x84, framerate_timer != 0 ? 3 : 2, 0, (const int *)args->t1, (u_int)args->t3,
                    (int)args->stack[2], (u_int)args->stack[3]);
    SetDefaultGsDrawState();
    return 0;
}

// Item "timer": the seconds left to confirm, two digits.
// FF3 0x0025fb38
static int FrameRateSelTimerDrawCallback(const B2D_HOOK_ARGS *args)
{
    short timer = framerate_timer;

    if (timer != 0)
    {
        int sec = GetPALMode() ? timer / 25 : timer / 30;

        B2dHookPrintNumber(sec + 1, 2, 1, (const int *)args->t1, (u_int)args->t3,
                           (u_char)args->stack[2], (u_int)args->stack[3]);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Item "R csl": the cursor frame, 150 px right per item.
// FF3 0x0025fbe0 (no Ghidra function before 2026-10-14).
static int FrameRateSelCsrDrawCallback(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS moved = *args;

    moved.t1 += MenuCsrGetPos(FRAMERATE_CSR) * 150;
    B2dDrawSpriteDirectLinear(&moved);
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC framerate_hooks[] =
{
    { FrameRateSelMsgDrawCallback,   0,  0, "setting_msg" },
    { FrameRateSelTimerDrawCallback, 0,  0, "timer" },
    { FrameRateSelCsrDrawCallback,   0,  0, "R csl" },
    { nullptr,                      -1, -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Phase 6, FrameRateSel_Main: the layout and the fade layer around phase 18.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025f528
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (FrameRateSelLayoutHandle)
int init_FrameRateSel_Main(void)
{
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        pad_wrk[0].stick_dpad = 1;
        GetCmnFadeLayer()->Term();
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + E_SETTINGS_PHF, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
    {
        capCtl *fade;

        if (BpCtlIsDataReady(FrameRateSelLayoutHandle()) < 1)
            return 0;
        BpPlayLabel(FrameRateSelLayoutHandle(), "in");
        B2dSetHookList(FrameRateSelLayoutHandle(), framerate_hooks);
        fade = GetCmnFadeLayer();
        fade->base_alpha = 0x80;
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        GetCmnFadeLayer()->Show(0);
        return 1;
    }

    default:
        return 0;
    }
}

// FF3 0x0025f6f8
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (FrameRateSelLayoutHandle)
int end_FrameRateSel_Main(void)
{
    short state = GetGPhaseCtrl()->leaf->state_cnt;

    if (state == 0)
    {
        BpPlayLabel(FrameRateSelLayoutHandle(), "out");
        GetCmnFadeLayer()->Term();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    else if (state != 1)
    {
        return 0;
    }

    if (!BpCtlIsLoadEnd(GetSysWrk()->GetLanguage() + E_SETTINGS_PHF))
        return 0;
    BpCtlRelease(FrameRateSelLayoutHandle());
    pad_wrk[0].stick_dpad = 0;
    return 1;
}

// FF3 0x0025f820
int one_FrameRateSel_Main(void)
{
    return 1;
}

// FF3 0x0025f828
void pre_FrameRateSel_Main(void)
{
}

// FF3 0x0025f830
void after_FrameRateSel_Main(void)
{
    BpCtlMain();
}

// ──────────────────────────────────────────────────────────────────────
// Phase 18, Title_FrameRate_Sel: moving the cursor switches the video mode
// at once; OK within 15 s keeps it, cancel or the timeout goes back to the
// previous one. OK or cancel with no change pending returns to Title_Mode.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025f848; FF2 init_Title_FrameRate_Sel (outgame/title.c) only locked
// the title movie.
int init_Title_FrameRate_Sel(void)
{
    s_char mode;

    MenuCsrInit(FRAMERATE_CSR, 3, 1, 0);
    mode            = (s_char)GetSysWrk()->video_mode;
    framerate_timer = 0;
    framerate_mode  = mode;
    MenuCsrSetPos(FRAMERATE_CSR, FrameRateSelModeToPos((u_char)mode));
    return 1;
}

// FF3 0x0025f8e8
int end_Title_FrameRate_Sel(void)
{
    MenuCsrRelease(FRAMERATE_CSR);
    return 1;
}

// FF3 0x0025f908
int one_Title_FrameRate_Sel(void)
{
    if (framerate_timer == 0)
    {
        MenuCsrUpdate(FRAMERATE_CSR, 0);
        if (MenuCsrIsMovedSe(FRAMERATE_CSR, FRAMERATE_SE_MOVE))
        {
            ChangeVideoMode(framerate_mode_tbl[MenuCsrGetPos(FRAMERATE_CSR)]);
            // 15 s at 25 / 30 frames a second.
            framerate_timer = GetPALMode() ? 375 : 450;
        }
        else
        {
            // Both are checked (both SEs may play).
            int ok     = CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], FRAMERATE_SE_OK, 0);
            int cancel = CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], FRAMERATE_SE_CANCEL, 0);

            if (ok | cancel)
            {
                GetGPhaseCtrl()->SetNext(5, 0);   // Title_Mode
                return 1;
            }
        }
    }
    else
    {
        framerate_timer--;
        if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], FRAMERATE_SE_OK, 0))
        {
            // Confirmed: the new mode is the one to fall back to.
            framerate_mode  = (s_char)GetSysWrk()->video_mode;
            framerate_timer = 0;
        }
        else if (framerate_timer <= 0
                 || CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], FRAMERATE_SE_CANCEL, 0))
        {
            ChangeVideoMode((u_char)framerate_mode);
            MenuCsrSetPos(FRAMERATE_CSR, FrameRateSelModeToPos((u_char)framerate_mode));
            framerate_timer = 0;
        }
    }

    GetCmnFadeLayer()->Update();
    return 0;
}

// FF3 0x0025fcb8: empty, no references.
void FrameRateSelDummy(void)
{
}
