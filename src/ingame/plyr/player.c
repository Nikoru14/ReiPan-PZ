// ingame/plyr/player.c -- the player characters (FF2 player.c's slot; FF3
// 0x231628..0x2394a8, pinned 2026-10-14: the inferred start 0x2295c8 held
// menu / film code).
//
// Reconstructed so far: init, PlayerMain and its per-frame helpers (enemy
// checks, heartbeat rumble, timed status, map camera watch), the damage and
// hold reactions (PlyrDamageCtrl, escape windows, the flash), the mode 0 /
// camera / door / mode 11 handlers, the current-player accessors, HP, modes,
// the flash count, SetPlyrAnime and its anime tables, and the player SE. FF3
// keeps three player works (plyr_wrk[3], one per playable character) where
// FF2 had one; most code works on the current one through ingame_wrk.plyr_wrk.
// The remaining functions are stubs until reconstructed; see
// tools/reverse/stage.py.
#include "system/mc/dat/save_data.h"

#include <math.h>                       // sqrtf, fabsf
#include <stdlib.h>                     // abs
#include <string.h>                     // memset, memcpy

#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "common/reipan_stub.h"
#include "ingame/plyr/player.h"

#include "ingame/plyr/plyr_gauge.h"       // PlyrGaugeAdd, PlyrGaugeTimeStep
#include "ingame/camera/cam_rail.h"     // CamRailGetCutFlg, CamStanceInit, CamStanceGetTarget
#include "ingame/candle_ctrl.h"          // GetCandleControl, CandleControl::IsOut
#include "graphics/effect/effect.h"     // SetEffects, EffectModelKindIsSet, eff_wrk
#include "graphics/effect/effect_obj.h" // EffPlyrHazeReq
#include "graphics/effect/effect_oth.h" // CallNega2, CallDeform2, EffectMonochromeSet
#include "ingame/enemy/ene_main.h"     // ene_wrk (the enemy work), IsActEnemy, EneAttackReq, EneGetJointPos
#include "ingame/enemy/enemy_act.h"     // EneActSet, EneActReqById (ene_main.h: EneStepReset)
#include "graphics/graph3d/gra3d.h"     // gra3dcamGetPosition, gra3dcamGetTarget
#include "graphics/graph3d/gra3dMisc.h" // gra3dPrelight
#include "ingame/camera/event_camera.h" // EventCameraSetChgFlg, EventCameraGetChgFlg
#include "ingame/camera/map_camera.h"   // MapCamGetCurData, the player camera requests, MapCamGetEventCameraFlg
#include "ingame/enemy/enemy.h"         // EnemyReleaseAll, EneStreamFadeOut, CheckEneView
#include "ingame/enemy/fly_ctrl.h"      // FlyFadeAll
#include "ingame/tattoo_event.h"        // TattooEvEffectStop / IsStopped / Release
#include "ingame/event/prg/ev_open.h"   // SetOpenCondSwitch
#include "ingame/ingame.h"              // GameModel
#include "ingame/ingame_wrk.h"          // ingame_wrk
#include "ingame/map/MapFog.h"          // MapFogStartFogEv, MapFogEndFogEv
#include "ingame/map/MapLoad.h"         // MapLoadGetRoomNoNow, MapLoadGetBuffID, MapLoadGetAreaData, MapLoadCheckLoadNow
#include "ingame/map/MapCtrl.h"         // MapAreaHitCircle, MapAreaAtPos, MapAreaGetHeight
#include "ingame/map/MapRcb.h"          // RCB_OBJ, RcbGetMotObj, RcbUpdateMhNo
#include "ingame/map/map_hit_check.h"   // MapHitLineCheck
#include "ingame/photo/finder_photo.h"  // GetHudHpWrk, GetAlbumSaveDisp, GetPhotoModeWrk, PhotoModeTegataFadeOut
#include "ingame/photo/fndr_parts.h"    // HudHpSet, HudHpSetNow, LensGetFunc
#include "ingame/photo/photo.h"         // PeepStateIsLook, PeepLookCtrl
#include "ingame/plyr/plyr_mdl.h"       // PlyrFinderStartEnd, PlyrFinderEndEnd
#include "ingame/plyr/plyr_param.h"     // PlyrMdlGetHpMax, PlyrGetShotRange, PlyrGetFinderRadius
#include "ingame/plyr/plyr_sub.h"       // the model, ladder, crawl and move control (0x23e208..0x243898)
#include "ingame/story_door.h"  // MapAreaNoSet, MapDoorRoomMhGet, MapDoorModeGet
#include "ingame/map/MapSnap.h"   // MapSnapSetFloor
#include "ingame/map/MapDoor.h"  // door table, door motion pack
#include "ingame/enemy/random_ghost.h"  // RandGhostAppearReq
#include "ingame/sound/snd_ctrl.h"      // SndSePlay, SndSeFadeStop, SND_3D_SET
#include "main/gphase.h"                // GetGPhaseCtrl
#include "outgame/option.h"             // GetOptionWrk
#include "ingame/plyr/unit_ctl.h"        // GetStickRaw, GetLength2D, GetStickDir8, UnitGetTrgtRot, OutSightChk, GetMidPos
#include "reipan/os/reipan_rand.h"      // PC: ReiPan_Rand
#include "ingame/gameover/prg/gameover.h"   // GameOverReq
#include "sdk/libvu0.h"                 // sceVu0SubVector, sceVu0AddVector
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetFrameTimeScale, GetPALMode
#include "system/pad/pad.h"             // pad_wrk, pad_key, CheckPadNowPlaySe, PadGetAnalogRaw
#include "system/pad/pad_ctrl.h"        // CheckPadTrgPlaySe, PAD_KEY_*
#include "ingame/npc/npc_act.h"         // PlyrCameraPlyrPush
#include "ingame/story_menu.h"  // IsLockTblOn, LockTblInc/Dec
#include "graphics/graph2d/fade.h"
#include "ingame/item/prg/item.h"       // ItemGetCnt, ItemLost
#include "ingame/cat/cat_ctrl.h"        // CatViewCtrl, CatPlyrHitCheck
#include "graphics/effect/effect_sub.h" // Get2PDist3D, WorldToScreenPos
#include "reipan/gameplay/reipan_haptic_cues.h"     // PC: what the rumble is for
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@0011cea8 (map hits)

// unassigned@001cae98 (screen fades)

// unassigned@001dfbc8

// unassigned@0013e648

// unassigned@001ef370 (vector helpers)
void RotVectorY(float rot, float *dst, float *src);    // at 0x1ef498
int  GetRandValIByF(int range);         // at 0x1efba0: (int)GetRandValF(range)
void RotLimitChk(float *rot);           // at 0x1efbc8: wraps into -pi..pi


// unassigned@00226138 (furniture photo)

// ──────────────────────────────────────────────────────────────────────

PLYR_WRK plyr_wrk[PLYR_NUM];            // data 0x32f4a0

static u_char  plyr_ctrl_opt;           // sdata 0x371b08: option b11 last frame (PlyrCtrlOptCheck)
static u_short mvib_time1;              // sdata 0x371b0c: heartbeat pulse frames left (PlyrVibCheck)
static u_short mvib_time0;              // sdata 0x371b0a: frames to the next pulse
static u_char  mvib_degree;             // sbss 0x373810: pulse strength
static u_char  mvib_pow;                // sdata 0x371b2c: PlyrVibCtrlBig's power
static u_char  mash_dir = 0xff;         // sdata 0x371ae4: stick direction last counted (PlyrMashStep)
static u_char  plyr_equip_lv[PLYR_EQUIP_NUM];   // sbss 0x373820 (gp+0x330): PlyrGetEquipLv
static float   plyr_flash_wait;         // sbss 0x37381c (gp+0x32c): the flash fires when <= 0 (PlyrFlashIsReady)
static int     plyr_door_anime;         // sbss 0x37380c (gp+0x31c): the anime DoorMotionIsEnd waits for (PlyrDoorStart)
static OPTION_WRK *plyr_opt;            // sdata 0x372a04: GetOptionWrk, fetched once (PlyrNormalCtrl)

static const u_char plyr_flash_num_tbl[8] = { 4, 8, 12, 16 };   // sdata 0x371b90 (PlyrFlashNumByLv)

// The hold anime of each attacker kind (PLYR_WRK._147), data 0x32fe90.
const u_char plyr_hold_anime[3][16] =
{
    { 0x80, 0x8c, 0x6d, 0x86, 0x89, 0x83, 0x70, 0x72, 0x73, 0x76, 0x7a, 0x7d, 0x00, 0x00, 0x6d, 0x9f },
    { 0x81, 0x8d, 0x6e, 0x87, 0x8a, 0x84, 0x71, 0x72, 0x74, 0x77, 0x7b, 0x7e, 0x00, 0x00, 0x6e, 0xa0 },
    { 0x82, 0x8e, 0x6f, 0x88, 0x8b, 0x85, 0x82, 0x82, 0x75, 0x78, 0x7c, 0x7f, 0x00, 0x00, 0x6f, 0x82 },
};

// 3D distance of two positions (the VU0 vsub / vmul / vaddbc / vsqrt sequence
// FF3 inlines, xyz only).
static float PlyrDistV(const float *p0, const float *p1)
{
    float dx = p0[0] - p1[0];
    float dy = p0[1] - p1[1];
    float dz = p0[2] - p1[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ──────────────────────────────────────────────────────────────────────
// Restores the current player after a load: the model pointers in the work
// came from the save, the SE and some flags are dropped, and the HUD shows
// the loaded HP at once.
// FF3 0x002316d8 (FF3-only).

void PlyrLoadRestore(void)
{
    PLYR_WRK *plyr;
    int       i;

    PlyrSetCurrent(PlyrGetCurNo());
    plyr = PlyrGetCurWrk();

    plyr->mdl = nullptr;
    plyr->mdl_fx = nullptr;
    for (i = 0; i < 4; i++)
        plyr->acs_mdl[i] = nullptr;
    plyr->danger_se = 0;
    plyr->flags2 &= ~(u_long)0x3fbff;
    plyr->anime_no = 0xff;
    plyr->flags3 &= ~(u_long)0x3fbff;

    HudHpSetNow(GetHudHpWrk(), (float)PlyrGetHp(plyr) / (float)PlyrGetHpMax(plyr));
}

// ──────────────────────────────────────────────────────────────────────
// The player's frame: skipped while phase 0x22 is next, under lock 2 (unless
// in mode 7) and without a model; otherwise the per-frame checks, then one
// handler chosen by the hold flags (0x1c00000), the puzzle flags (0x1c000000)
// or the mode, then the trailing updates.
// FF3 0x00231928 (FF3-only; FF2's PlayerMainCmn plays this role).

int PlayerMain(void)
{
    PLYR_WRK *plyr;
    u_long    flags;

    if (GetGPhaseCtrl()->next == 0x22)
        return ingame_wrk.plyr_wrk->_234 != 0;

    if (IsLockTblOn(2) && ingame_wrk.plyr_wrk->mode != PLYR_MODE_7)
        return ingame_wrk.plyr_wrk->_234 != 0;

    if (ingame_wrk.plyr_wrk->_2e4 == 0)
        return ingame_wrk.plyr_wrk->_234 != 0;

    PlyrCtrlOptCheck();
    PlyrAnimeEndCheck();
    PlyrEneCheck();
    PlyrStatusCtrl();
    PlyrMapObjFlagCtrl();
    PlyrMapCamCheck();

    plyr = ingame_wrk.plyr_wrk;
    flags = plyr->flags;
    if ((flags & PLYR_FLG_HOLD) != 0)
    {
        if ((u_char)(plyr->mode - 1) < 4)
        {
            if (plyr->dmg_type != 4)
                plyr->flags = flags & ~(u_long)PLYR_FLG_HOLD;
            PlyrDamageCtrl();
        }

        flags = ingame_wrk.plyr_wrk->flags;
        if ((flags & PLYR_FLG_400000) != 0)
            PlyrFinderStartEnd();
        else if ((flags & PLYR_FLG_800000) != 0)
            PlyrFinderCtrl();
        else if ((flags & PLYR_FLG_1000000) != 0)
            PlyrFinderEndEnd();
    }
    else if ((flags & PLYR_FLG_PZL) == 0)
    {
        switch (plyr->mode)
        {
        default:
            PlyrNormalCtrl();
            break;
        case 1:
        case 2:
        case 3:
        case 4:
            PlyrDamageCtrl();
            break;
        case 5:
            PlyrMode5Ctrl();
            break;
        case 6:
            PlyrDoorCtrl();
            break;
        case 7:
            PlyrDeathCtrl();
            break;
        case 8:
            PlyrMode8Ctrl();
            break;
        case 9:
            break;
        case 10:
            PlyrMode10Ctrl(ingame_wrk.plyr_wrk);
            break;
        }
    }
    else
    {
        if ((u_char)(plyr->mode - 1) < 4)
            PlyrDamageCtrl();

        plyr = ingame_wrk.plyr_wrk;
        if (plyr->mode == PLYR_MODE_7)
        {
            plyr->flags &= ~(u_long)PLYR_FLG_HOLD;
            PlyrDeathCtrl();
        }
        else
        {
            flags = plyr->flags;
            if ((flags & PLYR_FLG_4000000) != 0)
                PlyrCrawlStart();
            else if ((flags & PLYR_FLG_8000000) != 0)
                PlyrCrawlCtrl();
            else if ((flags & PLYR_FLG_10000000) != 0)
                PlyrCrawlEnd();
        }
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) == 0)
        PlyrSetB260Mode2();
    PlyrActionCtrl();
    ingame_wrk.plyr_wrk->mdl_fx->Main(ingame_wrk.plyr_wrk->mdl, PlyrGaugeTimeStep(GetAlbumSaveDisp()));
    ingame_wrk.plyr_wrk->mdl_fx->GhostCtrl();
    PlyrAnimeCtrl();
    PlyrMoveUpdate();
    PlyrMdlMain();

    return ingame_wrk.plyr_wrk->_234 != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Draw-skip bits for IngameDrawSub: 0xbe while +0x233 is 11..15 (unless
// _147 is 13).
// FF3 0x002323b8 (FF3-only).

u_int PlyrGetDrawSkipFlags(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (plyr->_147 == 13)
        return 0;
    if ((u_char)(plyr->death_step - 11) > 4)
        return 0;
    return 0xbe;
}

// FF3 0x002323f0 (FF3-only).

int PlyrIsMode233Over6(void)
{
    return ingame_wrk.plyr_wrk->death_step > 6;
}

// ──────────────────────────────────────────────────────────────────────
// The enemy checks of a frame, and the touch cool-down outside the damage
// modes.
// FF3 0x00232408 (FF3-only; FF2 PlayerMainCmn calls the same checks).

void PlyrEneCheck(void)
{
    PLYR_WRK *plyr;

    PlyrBattleCheck();
    NearEneInfo(ingame_wrk.plyr_wrk);
    PlyrEneTouchCheck();
    PlyrVibCheck();

    plyr = ingame_wrk.plyr_wrk;
    if ((u_char)(plyr->mode - 1) > 3)
        plyr->ene_wait = (plyr->ene_wait == 0) ? 0 : plyr->ene_wait - 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002326a8 (FF3-only).

void PlyrSetB260Mode2(void)
{
    ingame_wrk.plyr_wrk->_260 = 2;
}

// ──────────────────────────────────────────────────────────────────────
// The timed status (+0x116) and its per-frame effect, two more timers on
// the album-save step, the low-HP SE, then this frame's flags, status and
// HP become the "old" copies.
// FF3 0x002326c0 (FF3-only).

void PlyrStatusCtrl(void)
{
    PLYR_WRK *plyr;
    float     step;
    float     t;

    step = PlyrGaugeTimeStep(GetAlbumSaveDisp());
    plyr = ingame_wrk.plyr_wrk;

    if (plyr->status_time > 0.0f)
    {
        t = plyr->status_time - 1.0f;
        if (t <= 0.0f)
            t = 0.0f;
        plyr->status_time = t;
    }

    switch (plyr->status)
    {
    case 1:                             // shaking off: full rumble, mashing shortens it
        VibrateRequest2(0, 0xff);
        ReiPan_HapticCue(REIPAN_CUE_STRICKEN, 1.0f);    // PC
        if (plyr->status_time > 0.0f && PlyrMashStep())
        {
            t = plyr->status_time - 1.0f;
            if (t <= 0.0f)
                t = 0.0f;
            plyr->status_time = t;
        }
        break;
    case 2:                             // fog event while it lasts
        if (plyr->status_old != 2)
            MapFogStartFogEv(75.0f, 80.0f, 4, 4, 4, 0x32, 0xff);
        break;
    case 4:                             // held: ends with the enemy
        if (ene_wrk[plyr->ene_no].state == 3)
        {
            if (ene_wrk[plyr->ene_no].hp == 0)
                plyr->status_time = 0.0f;
        }
        else
            plyr->status_time = 0.0f;
        break;
    case 5:
        if (plyr->status_time <= 0.0f)
            GetPhotoModeWrk()->TegataFadeOut(10);
        break;
    }

    if (plyr->status_time <= 0.0f)
        plyr->status = 0;
    if (plyr->status_old != plyr->status && plyr->status_old == 2)
        MapFogEndFogEv(0x1e);

    if (plyr->_254 > 0.0f)
    {
        t = plyr->_254 - step;
        plyr->_254 = (t > 0.0f) ? t : 0.0f;
    }
    if (plyr->_338 > 0.0f)
    {
        t = plyr->_338 - step;
        plyr->_338 = (t > 0.0f) ? t : 0.0f;
    }

    PlyrDangerSeCtrl();

    plyr = ingame_wrk.plyr_wrk;
    plyr->status_old = plyr->status;
    plyr->flags_old = plyr->flags;
    plyr->_0e4 = plyr->_0e0;
    plyr->hp_old = plyr->hp;
}

// ──────────────────────────────────────────────────────────────────────
// The ROM's inlined random range: (float)rand() / RAND_MAX * range. PC:
// ReiPan_Rand is the ROM's 31-bit rand(); the host's is 15-bit.
static int PlyrRandRange(float range)
{
    return (int)((float)ReiPan_Rand() / REIPAN_RAND_MAXF * range + 0.0f);
}

// The damage reactions, PlayerMain's modes 1..4, by PLYR_WRK.dmg_type:
//  - 1, 7, 8, 9: a hit -- the damage at once, then mode 4 until its anime
//    is over;
//  - 6: a hit that holds for dmg_time, then the enemy's hit_dmg;
//  - 3, 4, 5, 10, 11: a ghost's hold. Mode 1 sets up the two escape
//    windows (PlyrHoldEscapeCheck), mode 2 waits for them and the grab anime:
//    with item 8 and the right presses the player breaks free (the flash or a
//    shake-off), else the hold starts (mode 3): damage ticks every dmg_time
//    and mashing wears the enemy's hold_pow down; at 0 the player is let go
//    (mode 4). The holding enemy moves with the player (hold_move).
// When the enemy is no longer attacking (state != 3) the hit ends early.
// FF3 0x00232c00 (FF3-only).
// callcheck: rand (ReiPan_Rand, through PlyrRandRange)

void PlyrDamageCtrl(void)
{
    PLYR_GAUGE *disp;
    PLYR_WRK        *plyr;
    ENE_WRK    *ene;
    u_short          type;
    int              mash;

    GetHudHpWrk();
    disp = GetAlbumSaveDisp();
    plyr = ingame_wrk.plyr_wrk;
    ene = &ene_wrk[plyr->ene_no];
    type = (u_short)plyr->dmg_type;

    if (type != 8 && type != 1 && ene->state != 3)
    {
        if (plyr->mode == PLYR_MODE_NORMAL || plyr->mode > PLYR_MODE_DMG4)
            PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
        else if (plyr->mode < PLYR_MODE_DMG4)
        {
            switch (type)
            {
            case 3:
            case 4:
            case 5:
            case 10:
            case 11:
                PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
                break;
            }
        }
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_10000;
        SetEffects(6, 1, 3);
        EndPlyrApproachCameraCtrl();
    }

    switch (ingame_wrk.plyr_wrk->mode)
    {
    case PLYR_MODE_DMG1:
        // window 0 opens 14..20 frames in for 4..10 frames; window 1 at once for 30
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;
        plyr->esc_time0 = 0.0f;
        plyr->esc_push0 = 0;
        plyr->esc_start0 = (u_short)(PlyrRandRange(6.0f) + 14);
        plyr->esc_len0 = (u_short)(PlyrRandRange(6.0f) + 4);
        plyr->esc_start0 = (u_short)(int)((float)plyr->esc_start0 / GetFrameTimeScale());
        plyr->esc_len0 = (u_short)(int)((float)plyr->esc_len0 / GetFrameTimeScale());
        plyr->esc_push1 = 0;
        plyr->esc_time1 = 0.0f;
        plyr->esc_start1 = (u_short)PlyrRandRange(0.0f);
        plyr->esc_len1 = (u_short)(PlyrRandRange(0.0f) + 30);
        plyr->esc_start1 = (u_short)(int)((float)plyr->esc_start1 / GetFrameTimeScale());
        plyr->esc_len1 = (u_short)(int)((float)plyr->esc_len1 / GetFrameTimeScale());
        PlyrSetMode(PLYR_MODE_DMG2, nullptr);
        // fall through

    case PLYR_MODE_DMG2:
        switch ((u_short)ingame_wrk.plyr_wrk->dmg_type)
        {
        case 1:
        case 7:
        case 8:
        case 9:
            PlyrDamage(ingame_wrk.plyr_wrk, ingame_wrk.plyr_wrk->dmg);
            ingame_wrk.plyr_wrk->dmg = 0;
            PlyrSetMode(PLYR_MODE_DMG4, nullptr);
            CallNega2(0, 10, 5);
            PlyrVibCtrl(15);
            PlyrSePlay(ingame_wrk.plyr_wrk, 1, 100, 0x1000);
            return;

        case 6:
            PlyrDamage(ingame_wrk.plyr_wrk, ingame_wrk.plyr_wrk->dmg);
            ingame_wrk.plyr_wrk->dmg = 0;
            PlyrSetMode(PLYR_MODE_DMG3, nullptr);
            CallNega2(0, 10, 5);
            ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, 0x3c, 0x1e, 0x80, 1);
            PlyrVibCtrl(15);
            PlyrSePlay(ingame_wrk.plyr_wrk, 1, 100, 0x1000);
            return;

        case 3:
        case 4:
        case 5:
        case 10:
            if (PlyrHoldEscapeCheck() == 0
                || CheckPlyrAnimeEnd(plyr_hold_anime[PLYR_HOLD_GRAB][ingame_wrk.plyr_wrk->_147]) == 0)
                break;

            if (*ItemGetCnt(8) != 0 && GetPhotoModeWrk()->GetMode() == 0
                && ((GetLensWrk()->GetFunc(2) != 0 && plyr->esc_push0 == 1) || plyr->esc_push1 != 0))
            {
                ingame_wrk.plyr_wrk->flags |= PLYR_FLG_20000;
                ingame_wrk.plyr_wrk->flags |= PLYR_FLG_10000;
                ingame_wrk.plyr_wrk->dmg = 0;
                ene->hold_pow = 0.0f;
                PlyrSePlay(ingame_wrk.plyr_wrk, 0, 100, 0x1000);
                if (PlyrGetCurNo() == 0 && plyr->esc_push0 != 1)
                    PlyrFlashRelease();
                else
                    PlyrHoldShakeOff();
                break;
            }

            EneStepReset(ene, 7);
            if (ingame_wrk.plyr_wrk->dmg_type != 5)
            {
                PlyrDamage(ingame_wrk.plyr_wrk, ingame_wrk.plyr_wrk->dmg);
                ingame_wrk.plyr_wrk->dmg = 0;
            }
            if (ingame_wrk.plyr_wrk->dmg_type == 4)
            {
                ingame_wrk.plyr_wrk->ene_wait = 0;
                ingame_wrk.plyr_wrk->status = 4;
                ingame_wrk.plyr_wrk->status_time = ene->hold_pow / GetFrameTimeScale();
            }
            PlyrSetMode(PLYR_MODE_DMG3, nullptr);
            ingame_wrk.plyr_wrk->flags |= PLYR_FLG_20000;
            SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_LOOP][ingame_wrk.plyr_wrk->_147], 0, 0xff, 0xff, 1, 0.0f);
            PlyrSePlay(ingame_wrk.plyr_wrk, 2, 100, 0x1000);
            PhotoModeSePlay(5, 100, 0x1000);
            break;

        case 11:
            EneStepReset(ene, 7);
            PlyrSetMode(PLYR_MODE_DMG3, nullptr);
            ingame_wrk.plyr_wrk->flags |= PLYR_FLG_20000;
            PlyrSePlay(ingame_wrk.plyr_wrk, 2, 100, 0x1000);
            PhotoModeSePlay(5, 100, 0x1000);
            break;

        default:
            return;
        }
        break;

    case PLYR_MODE_DMG3:
        switch ((u_short)ingame_wrk.plyr_wrk->dmg_type)
        {
        case 1:
        case 7:
        case 8:
        case 9:
            PlyrSetMode(PLYR_MODE_DMG4, nullptr);
            return;

        case 3:
        case 11:
            plyr = ingame_wrk.plyr_wrk;
            if (plyr->dmg_time >= 0.0f)
            {
                plyr->dmg_time -= PlyrGaugeTimeStep(disp);
                if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
                {
                    CallNega2(0, 15, 8);
                    ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, (int)ene->hold_pow, 0x1e, 0x80, 1);
                }
            }
            if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
            {
                PlyrDamage(ingame_wrk.plyr_wrk, (int)((float)ene->dat->atk * PlyrGaugeDmgRate(disp)));
                mash = PlyrMashStep();
                if ((ene->flags & 0x4000) != 0 && ene->hold_pow > 0.0f)
                {
                    if (mash != 0 && (int)ene->hold_pow > 22)
                        ene->hold_pow -= PlyrGaugeDmgRate(disp) * 3.0f;
                    VibrateRequest1(0, 1);
                    ReiPan_HapticCue(REIPAN_CUE_HELD, 1.0f);    // PC
                }
                if ((int)ene->hold_pow > 20)
                    CallDeform2(0, 0, (int)ene->hold_pow, 7, 0xd);
                if (ene->hold_pow <= 0.0f)
                {
                    SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_FREE][ingame_wrk.plyr_wrk->_147], 1, 0xff, 0xff, 0, 0.0f);
                    PlyrSetMode(PLYR_MODE_DMG4, nullptr);
                }
            }
            break;

        case 4:
            PlyrFinderStartCheck();
            plyr = ingame_wrk.plyr_wrk;
            if (plyr->dmg_time >= 0.0f)
            {
                plyr->dmg_time -= PlyrGaugeTimeStep(disp);
                if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
                    ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, (int)ene->hold_pow, 0x1e, 0x80, 1);
            }
            if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
            {
                PlyrDamage(ingame_wrk.plyr_wrk, (int)((float)ene->dat->atk * PlyrGaugeDmgRate(disp)));
                if ((ene->flags & 0x4000) != 0 && ene->hold_pow > 0.0f)
                {
                    ene->hold_pow = ingame_wrk.plyr_wrk->status_time;
                    VibrateRequest1(0, 1);
                    ReiPan_HapticCue(REIPAN_CUE_HELD, 1.0f);    // PC
                }
                if ((int)ene->hold_pow > 20)
                    CallDeform2(0, 0, (int)ene->hold_pow, 7, 0xd);
                if (ene->hold_pow <= 0.0f)
                {
                    SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_FREE][ingame_wrk.plyr_wrk->_147], 1, 0xff, 0xff, 0, 0.0f);
                    PlyrFinderModeCancel();
                    PlyrSetMode(PLYR_MODE_DMG4, nullptr);
                }
            }
            if (PlyrFlashInput())
            {
                PlyrFlashRelease();
                ingame_wrk.plyr_wrk->status_time = 0.0f;
                ene->hold_pow = 0.0f;
            }
            break;

        case 5:
            plyr = ingame_wrk.plyr_wrk;
            if (plyr->dmg_time >= 0.0f)
                plyr->dmg_time -= PlyrGaugeTimeStep(disp);
            if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
            {
                PlyrSetMode(PLYR_MODE_DMG4, nullptr);
                CallNega2(0, 10, 5);
                ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, 0x3c, 0x1e, 0x80, 1);
                PlyrDamage(ingame_wrk.plyr_wrk, ingame_wrk.plyr_wrk->dmg);
                PlyrVibCtrl(15);
                PlyrSePlay(ingame_wrk.plyr_wrk, 1, 100, 0x1000);
                ingame_wrk.plyr_wrk->dmg = 0;
                break;
            }
            mash = PlyrMashStep();
            if ((ene->flags & 0x4000) != 0 && ene->hold_pow > 0.0f && mash != 0)
                ene->hold_pow -= PlyrGaugeDmgRate(disp) * 3.0f;
            if (ene->hold_pow <= 0.0f)
            {
                SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_FREE][ingame_wrk.plyr_wrk->_147], 1, 0xff, 0xff, 0, 0.0f);
                PlyrSetMode(PLYR_MODE_DMG4, nullptr);
            }
            if (PlyrFlashInput())
            {
                PlyrFlashRelease();
                ene->hold_pow = 0.0f;
            }
            break;

        case 6:
            plyr = ingame_wrk.plyr_wrk;
            if (plyr->dmg_time >= 0.0f)
                plyr->dmg_time -= PlyrGaugeTimeStep(disp);
            if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
            {
                PlyrSetMode(PLYR_MODE_DMG4, nullptr);
                CallNega2(0, 10, 5);
                ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, 0x3c, 0x1e, 0x80, 1);
                PlyrDamage(ingame_wrk.plyr_wrk, ene->hit_dmg);
                ingame_wrk.plyr_wrk->dmg = 0;
                PlyrVibCtrl(15);
                PlyrSePlay(ingame_wrk.plyr_wrk, 1, 100, 0x1000);
            }
            break;

        case 10:
            plyr = ingame_wrk.plyr_wrk;
            if (plyr->dmg_time >= 0.0f)
            {
                plyr->dmg_time -= PlyrGaugeTimeStep(disp);
                if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
                {
                    CallNega2(0, 15, 8);
                    ingame_wrk.plyr_wrk->mdl_fx->Req(0x3c, 0x3c, 0x1e, 0x80, 1);
                }
            }
            if (ingame_wrk.plyr_wrk->dmg_time <= 0.0f)
            {
                PlyrDamage(ingame_wrk.plyr_wrk, (int)((float)ene->dat->atk * PlyrGaugeDmgRate(disp)));
                if (CheckPlyrAnimeEnd(plyr_hold_anime[PLYR_HOLD_LOOP][ingame_wrk.plyr_wrk->_147]))
                {
                    SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_FREE][ingame_wrk.plyr_wrk->_147], 1, 0xff, 0xff, 0, 0.0f);
                    PlyrSetMode(PLYR_MODE_DMG4, nullptr);
                    ene->hold_pow = 0.0f;
                }
                else
                {
                    VibrateRequest1(0, 1);
                    ReiPan_HapticCue(REIPAN_CUE_HELD, 1.0f);    // PC
                }
            }
            break;

        default:
            return;
        }
        break;

    case PLYR_MODE_DMG4:
        // back to mode 0 once the reaction's anime is over
        switch ((u_short)ingame_wrk.plyr_wrk->dmg_type)
        {
        case 1:
        case 8:
        case 9:
            if ((u_char)(ingame_wrk.plyr_wrk->anime_no - 0x6b) < 2
                && (ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
                return;
            break;
        case 3:
        case 4:
        case 10:
        case 11:
            if (CheckPlyrAnimeEnd(plyr_hold_anime[PLYR_HOLD_FREE][ingame_wrk.plyr_wrk->_147]) == 0)
                return;
            break;
        case 5:
            if (CheckPlyrAnimeEnd(plyr_hold_anime[PLYR_HOLD_LOOP][ingame_wrk.plyr_wrk->_147]) == 0)
                return;
            break;
        case 6:
            if (CheckPlyrAnimeEnd(plyr_hold_anime[PLYR_HOLD_LOOP][ingame_wrk.plyr_wrk->_147]) == 0
                && (ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
                return;
            break;
        case 7:
            if (CheckPlyrAnimeEnd(0x79) == 0)
                return;
            break;
        default:
            return;
        }
        PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
        EndPlyrApproachCameraCtrl();
        SetEffects(6, 1, 3);
        return;

    default:
        return;
    }

    // modes 2 and 3: the holding enemy moves with the player
    ene->pos[0] += plyr->hold_move[0];
    ene->pos[1] += plyr->hold_move[1];
    ene->pos[2] += plyr->hold_move[2];
    ene->pos[3] += plyr->hold_move[3];
}

// ──────────────────────────────────────────────────────────────────────
// HP. FF3 0x00233ae0 / 0x00233b28 / 0x00233bf0 / 0x00233bf8 (FF3-only).

// Sets hp, clamped to 0..hp_max; returns the HUD rate hp / hp_max.
// FF3 0x00233ae0

float PlyrSetHp(PLYR_WRK *plyr, int hp)
{
    int max = plyr->hp_max;

    if (hp < 0)
        hp = 0;
    else if (hp > max)
        hp = max;
    plyr->hp = hp;
    return (float)hp / (float)max;
}

// FF3 0x00233b28

int PlyrGetHp(PLYR_WRK *plyr)
{
    return plyr->hp;
}

// FF3 0x00233bf0

int PlyrGetHpMax(PLYR_WRK *plyr)
{
    return plyr->hp_max;
}

// Heals n below hp_max; returns whether there was anything to heal.
// FF3 0x00233bf8

int PlyrAddHp(PLYR_WRK *plyr, int n)
{
    int heal = PlyrGetHp(plyr) < PlyrGetHpMax(plyr);

    if (heal)
    {
        HudHpSet(GetHudHpWrk(), PlyrSetHp(plyr, PlyrGetHp(plyr) + n));
        PlyrDangerSeCtrl();
    }
    return heal;
}

// ──────────────────────────────────────────────────────────────────────
// Takes dmg (doubled while the album-view control's flag is set). At 0 HP
// one of item 7 refills the HP (SE 0x1a9b0007, played twice); without one
// the player dies (PlyrDeathStart: 4 for character 13, 0x11 in a puzzle hold,
// else 1).
// FF3 0x00233c88 (FF3-only).

void PlyrDamage(PLYR_WRK *plyr, int dmg)
{
    float rate = 1.0f;
    float hud;
    int   hp;
    int   loss;
    int   type;

    if (GetCandleControl()->IsOut())
        rate = 2.0f;

    hp = PlyrGetHp(plyr);
    loss = (int)((float)dmg * rate);
    PlyrGaugeAdd((float)dmg * rate * 0.07f, GetAlbumSaveDisp());

    if (loss < hp)
        hud = PlyrSetHp(plyr, hp - loss);
    else if (*ItemGetCnt(7) == 0)
    {
        type = 4;
        if (ingame_wrk.plyr_wrk->_147 != 13)
            type = ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_PZL) != 0) ? 0x11 : 1;
        hud = 0.0f;
        PlyrDeathStart(type);
    }
    else
    {
        ItemLost(7, 1);
        hud = 1.0f;
        PlyrAddHp(plyr, PlyrGetHpMax(plyr) - hp);
        SndSePlay(0x1a9b0007, 0x7f, 0x1000, 0, nullptr, 0);
        SndSePlay(0x1a9b0007, 0x7f, 0x1000, 0, nullptr, 0);
    }

    HudHpSet(GetHudHpWrk(), hud);
}

// ──────────────────────────────────────────────────────────────────────
// Close ordinary enemies that touch the player: within reach (2 + 2, or the
// kinds 0x60..0x62 measured from a point 16 in front of them with reach
// 12 + 2, kinds 0x63/0x64/0x66 with reach 6 + 2 when their point 0x1b is
// within 40 in height) and with height ranges overlapping, each gets
// EneAttackReq(ene, 9, 0xc). Nothing while in mode 7, under locks 8 / 9 or
// during the touch cool-down.
// FF3 0x00233e70 (FF3-only).

void PlyrEneTouchCheck(void)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    ENE_WRK *ene;
    sceVu0FVECTOR in;
    sceVu0FVECTOR out;
    sceVu0FVECTOR pt;
    float         dist;
    float         reach;
    float         ene_lo;               // the enemy's height range: mpos[1]..pos[1]
    float         ene_hi;
    float         plyr_top;             // the player's point 2 (PlyrGetJointPos)
    float         plyr_y;
    int           i;

    if (plyr->mode == PLYR_MODE_7 || IsLockTblOn(8) || IsLockTblOn(9) || plyr->ene_wait != 0)
        return;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ene = &ene_wrk[i];
        if (ene->state != 3 || ene->hp == 0 || (ene->flags & 0x100000) != 0)
            continue;
        if (ene->alpha == 0 || (ene->flags2 & 0x80) == 0 || ene->type >= 2)
            continue;

        dist = PlyrDistV(ingame_wrk.plyr_wrk->pos, ene->pos);
        ene_hi = ene->pos[1];
        plyr_y = ingame_wrk.plyr_wrk->pos[1];
        ene_lo = ene->mpos[1];
        in[0] = 5.0f;
        in[1] = 0.0f;
        in[2] = 0.0f;
        in[3] = 1.0f;
        PlyrGetJointPos(2, out, in);
        plyr_top = out[1];

        reach = 2.0f;
        switch (ene->cmn->kind)
        {
        case 0x60:
        case 0x61:
        case 0x62:
            pt[0] = 0.0f;
            pt[1] = 0.0f;
            pt[2] = 16.0f;
            pt[3] = 0.0f;
            RotVectorY(ene->rot[1], pt, pt);
            pt[0] += ene->pos[0];
            pt[1] += ene->pos[1];
            pt[2] += ene->pos[2];
            pt[3] += ene->pos[3];
            dist = PlyrDistV(ingame_wrk.plyr_wrk->pos, pt);
            reach = 12.0f;
            break;
        case 0x63:
        case 0x64:
        case 0x66:
            EneGetJointPos(ene, 0x1b, pt, 0);
            if (fabsf(ingame_wrk.plyr_wrk->pos[1] - pt[1]) > 40.0f)
                return;                 // FF3 ends the whole scan here
            reach = 6.0f;
            break;
        }

        if (dist < reach + 2.0f
            && ((ene_lo < plyr_top && plyr_top <= ene_hi)
                || (ene_lo < plyr_y && plyr_y <= ene_hi)
                || (plyr_top < ene_lo && ene_hi <= plyr_y)))
            EneAttackReq(ene, 9, 0xc);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The nearest ordinary enemy (type < 2) and the nearest type-2 one among
// the active, living, targetable slots; last frame's distances are kept.
// FF3 0x00234188; FF2 NearEneInfo (FF3 tracks type 2 separately).

void NearEneInfo(PLYR_WRK *plyr)
{
    ENE_WRK *ene;
    float         dist0 = 99999.0f;
    float         dist2 = 99999.0f;
    float         d;
    u_char        no0 = 0xff;
    u_char        no2 = 0xff;
    u_char        i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ene = &ene_wrk[i];
        if (ene->state != 3 || ene->hp == 0 || (ene->flags & 0x100000) != 0)
            continue;

        if (ene->type < 2)
        {
            d = PlyrDistV(plyr->pos, ene->pos);
            if (d < dist0)
            {
                dist0 = d;
                no0 = i;
            }
        }
        else if (ene->type == 2)
        {
            d = PlyrDistV(plyr->pos, ene->pos);
            if (d < dist2)
            {
                dist2 = d;
                no2 = i;
            }
        }
    }

    plyr->near2_dist_old = plyr->near2_dist;
    plyr->near2_dist = dist2;
    plyr->near_no = no0;
    plyr->near_dist_old = plyr->near_dist;
    plyr->near_dist = dist0;
    plyr->near2_no = no2;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00234558 (FF3-only: FF2 had one player work).

PLYR_WRK *GetPlyrWrk(s_char no)
{
    return &plyr_wrk[no];
}

// FF3 0x00234580 (FF3-only).

PLYR_WRK *PlyrGetCurWrk(void)
{
    return ingame_wrk.plyr_wrk;
}

// FF3 0x00234590; FF2 GetPlyrRoomID's slot (FF3 returns the current player number).

u_char PlyrGetCurNo(void)
{
    return ingame_wrk.b00;
}

// ──────────────────────────────────────────────────────────────────────
// Makes player no (0..2) current; the other two lose their low-HP SE.
// FF3 0x002345a0 (FF3-only).

void PlyrSetCurrent(u_char no)
{
    int i;

    if (no >= PLYR_NUM)
        return;

    ingame_wrk.plyr_wrk = &plyr_wrk[no];
    ingame_wrk.b00 = no;
    for (i = 0; i < PLYR_NUM; i++)
    {
        if (i != no)
            PlyrDangerSeStop((s_char)i);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The battle bits: 0x20 while an ordinary enemy is active, 0x40 while a
// type-2 one is; FF3 adds a frame counter for the first and clears +0x348
// when neither is.
// FF3 0x00234860; FF2 PlyrBattleCheck.

void PlyrBattleCheck(void)
{
    int i;

    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_000020;
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_BATTLE2;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        if (IsActEnemy(i) != 0 && ene_wrk[i].type != 2)
            ingame_wrk.plyr_wrk->flags |= PLYR_FLG_000020;
        else if (IsActEnemy(i) != 0 && ene_wrk[i].type == 2)
            ingame_wrk.plyr_wrk->flags |= PLYR_FLG_BATTLE2;
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_000020) == 0)
    {
        ingame_wrk.plyr_wrk->battle_cnt = 0;
        ingame_wrk.plyr_wrk->_130 = 0;
    }
    else
        ingame_wrk.plyr_wrk->battle_cnt++;

    if ((ingame_wrk.plyr_wrk->flags & (PLYR_FLG_000020 | PLYR_FLG_BATTLE2)) == 0)
        ingame_wrk.plyr_wrk->_348 = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Sets the player's mode (plyr nullptr: the current player), keeping the
// previous one. Mode 7 is sticky: only a request for mode 0 leaves it.
// FF3 0x00234d28 (FF3-only).

void PlyrSetMode(u_char mode, PLYR_WRK *plyr)
{
    u_char old;

    if (plyr == nullptr)
        plyr = ingame_wrk.plyr_wrk;
    old = plyr->mode;

    if (old == PLYR_MODE_7)
    {
        if (mode != 0)
            return;
        plyr->mode = 0;
    }
    else
        plyr->mode = mode;
    plyr->mode_old = old;
}

// ──────────────────────────────────────────────────────────────────────
// Shake-off input: 1 while a d-pad button is down (option b11 == 1) or the
// left stick of a DualShock (id 0x79) is pushed past 0x31; 2 past 0x7e.
// FF3 0x00234d68 (FF3-only).

int PlyrGetMashInput(void)
{
    int    ret = 0;
    u_char x;
    u_char y;
    u_int  mag;

    if (GetOptionWrk()->now.b11 == 1
        && (CheckPadNowPlaySe(0x1000, -1, 0) || CheckPadNowPlaySe(0x4000, -1, 0)
            || CheckPadNowPlaySe(0x2000, -1, 0) || CheckPadNowPlaySe(0x8000, -1, 0)))
        ret = 1;

    if ((u_char)pad_wrk[0].id == 0x79)
    {
        GetStickRaw(2, &x, &y);
        x = ((x & 0x80) == 0) ? (u_char)(0x7f - x) : (u_char)(x + 0x80);
        y = ((y & 0x80) == 0) ? (u_char)(0x7f - y) : (u_char)(y + 0x80);
        mag = (u_int)(int)GetLength2D((float)x, (float)y) & 0xff;
        if (mag > 0x31)
            ret = 1;
        if (mag > 0x7e)
            ret = 2;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// One shake-off step: the stick pushed hard in a new direction.
// FF3 0x00235970 (FF3-only).

int PlyrMashStep(void)
{
    if (PlyrGetMashInput() == 2 && GetStickDir8(2) != mash_dir)
    {
        mash_dir = GetStickDir8(2);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Follows a change of the option's byte +0x11 (PlyrTurnFlagClear).
// FF3 0x00235cc0 (FF3-only).

void PlyrCtrlOptCheck(void)
{
    u_char opt = GetOptionWrk()->now.b11;

    if (plyr_ctrl_opt != opt)
        PlyrTurnFlagClear();
    plyr_ctrl_opt = opt;
}

// ──────────────────────────────────────────────────────────────────────
// Rumble requests for this frame, when the option allows vibration. FF3
// keeps them in player.c (FF2 had them with the pad code). The small motor
// only switches on; the second argument is unused in FF3.
// FF3 0x00235d10; FF2 VibrateRequest1.

void VibrateRequest1(int pad, int on)
{
    (void)on;
    if (GetOptionWrk()->now.vibration != 0)
        pad_wrk[pad].vib_small_time = 1;
}

// FF3 0x00235d68; FF2 VibrateRequest2.

void VibrateRequest2(int pad, u_char pow)
{
    if (GetOptionWrk()->now.vibration != 0)
    {
        pad_wrk[pad].vib_big_pow = pow;
        pad_wrk[pad].vib_big_time = 1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The heartbeat: the closer the nearest enemy, the shorter the gap between
// pulses and the harder each one. FF3 scales FF2's 300..3000 band to
// 12..120 (inside 1..160), drops FF2's SE and its "engaged" exception, and
// skips enemies with flags2 0x2000 (FF2 attr 0x80).
// FF3 0x00235dd0; from MioPan ingame/plyr/player.c PlyrVibCheck.

void PlyrVibCheck(void)
{
    ENE_WRK *ene;
    ENE_WRK *ewp = nullptr;
    float         dist = 0.0f;
    float         d;
    int           chk = 0;
    u_char        no = 0;
    u_char        i;
    u_int         degree;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ene = &ene_wrk[i];
        if ((ene->flags2 & 0x2000) != 0 || ene->state != 3)
            continue;
        if (ene->hp == 0 || (ene->flags & 0x104000) != 0)
            continue;

        d = Get2PDist3D(ingame_wrk.plyr_wrk->pos, ene->pos);
        if (dist == 0.0f || d < dist)
        {
            chk = 1;
            dist = d;
            ewp = ene;
            no = i;
        }
    }

    if (chk && dist >= 1.0f && dist <= 160.0f)
    {
        if (mvib_time1 == 0)
        {
            if (mvib_time0-- == 0)
            {
                d = 120.0f;
                if (dist <= 120.0f)
                    d = (dist >= 12.0f) ? dist : 12.0f;

                mvib_time1 = 0x12;
                degree = ~(u_int)(int)(((d - 12.0f) * 128.0f) / 108.0f);
                mvib_time0 = (u_short)(int)(((d - 12.0f) * 90.0f) / 108.0f);
                if (ene_wrk[no].type == 2)
                    mvib_degree = (u_char)((degree & 0xff) >> 1);
                else
                    mvib_degree = (u_char)degree;
            }
        }
        else if ((ewp->flags2 & 0x2000) == 0)
        {
            if (mvib_time1 == 0x12)     // PC: the beat's first frame
                ReiPan_HapticCue(REIPAN_CUE_HEARTBEAT, (float)mvib_degree / 255.0f);
            mvib_time1--;
            VibrateRequest2(0, mvib_degree);
        }
    }

    PlyrVibCtrl(0);
    PlyrVibCtrlBig(0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// time == 0 only runs a request in flight; a longer time raises it.
// FF3 0x00236068; from MioPan ingame/plyr/player.c PlyrVibCtrl.

void PlyrVibCtrl(u_char time)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (time != 0)                      // PC: every request is a ghost's blow
        ReiPan_HapticCue(REIPAN_CUE_HIT, 1.0f);

    if (time != 0 && plyr->vib_time_sm < time)
        plyr->vib_time_sm = time;

    if (plyr->vib_time_sm != 0)
    {
        VibrateRequest1(0, 1);
        plyr->vib_time_sm--;
    }
}

// FF3 0x002360d8; from MioPan ingame/plyr/player.c PlyrVibCtrlBig.

void PlyrVibCtrlBig(u_char pow, u_char time)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (time != 0 && plyr->vib_time_bg < time)
    {
        plyr->vib_time_bg = time;
        mvib_pow = pow;
    }

    if (plyr->vib_time_bg != 0)
    {
        VibrateRequest2(0, mvib_pow);
        plyr->vib_time_bg--;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The low-HP SE: SE 3 starts once while 0 < hp <= hp_max / 5, else stops.
// FF3 0x00236608 (FF3-only).

void PlyrDangerSeCtrl(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (plyr->hp >= 1 && plyr->hp <= plyr->hp_max / 5)
    {
        if (plyr->danger_se == 0)
            plyr->danger_se = PlyrSePlay(plyr, 3, 100, 0x1000);
    }
    else
        PlyrDangerSeStop((s_char)PlyrGetCurNo());
}

// FF3 0x002366d0 (FF3-only).

void PlyrDangerSeStop(s_char no)
{
    if (plyr_wrk[no].danger_se != 0)
    {
        SndSeFadeStop(plyr_wrk[no].danger_se, 1);
        plyr_wrk[no].danger_se = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Plays se from the current player's SE bank at plyr's position (not
// positioned while the camera is up); returns the play id.
// FF3 0x00236728 (FF3-only).

int PlyrSePlay(PLYR_WRK *plyr, u_int se, int vol, int pitch)
{
    SND_3D_SET    s3s;
    const PLYR_MDL_DAT *dat;

    memset(&s3s, 0, sizeof(s3s));
    s3s.pos = plyr->pos;
    dat = PlyrGetMdlDat(PlyrGetCurNo());  // FF3 also leaves 0 in a1 (unused)
    return SndSePlay(((u_int)(int)dat->se_bank << 16) | se, vol, pitch, 0,
                     (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) ? nullptr : &s3s, 0);
}

// Save block 28: the three player works. PC: the table's host size (mdl,
// acs_mdl, mdl_fx and ladder are pointers); FF3 returns 0x9f0. A load
// leaves stale pointers here, as on the EE: PlyrLoadRestore re-points
// ingame_wrk.plyr_wrk and clears the current player's model pointers.
// FF3 0x002367f0 (FF3-only).

int PlyrWrkSaveGet(void **addr)
{
    *addr = plyr_wrk;
    return sizeof(plyr_wrk);
}

// FF3 0x002373d8 (FF3-only).

u_short PlyrGetFlashNum(void)
{
    return (u_short)ingame_wrk.plyr_wrk->flash_num;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the current anime has ended or another one runs.
// FF3 0x00237868; FF2 CheckPlyrAnimeEnd (FF3 reads the end flag the model
// sets, PlyrAnimeEndCheck).

int CheckPlyrAnimeEnd(u_char anime_no)
{
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
        return 1;
    return anime_no != ingame_wrk.plyr_wrk->anime_no;
}

// ──────────────────────────────────────────────────────────────────────
// Raises PLYR_FLG_ANIME_END when the model's anime has ended and counts the
// frames since.
// FF3 0x002392a0 (FF3-only; the matcher had paired it with FF2
// PlayerFinderUnlock).

void PlyrAnimeEndCheck(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (static_cast<GameAnimeModel *>(plyr->mdl)->IsAnimeEnd() != 0)
    {
        ingame_wrk.plyr_wrk->flags |= PLYR_FLG_ANIME_END;
        plyr->anime_end_cnt++;
    }
    else
        plyr->anime_end_cnt = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Rest of PlayerMain's map camera watch: a change of the camera word (or
// its event bits) restarts camchg_time and ends the approach camera; in
// other modes the timer is zero.
// FF3 0x002355b8 (FF3-only).

void PlyrMapCamCheck(void)
{
    MAP_CAM_DATA *cam = MapCamGetCurData();
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    u_int         word;
    u_int         old;
    u_int         ev;
    float         t;

    if (cam == nullptr)
        return;

    ev = (u_int)EventCameraGetChgFlg() << 31;
    word = cam->word;
    ev |= (u_int)CamRailGetCutFlg() << 30;
    word |= ev | (u_int)MapCamGetEventCameraFlg() << 29;

    if (ingame_wrk.plyr_wrk->mode == 0)
    {
        old = ingame_wrk.plyr_wrk->camdata;
        if ((word & 0x3fffffff) == (old & 0x3fffffff)
            && ((word & 0x80000000) == 0 || (old & 0x80000000) != 0)
            && ((word & 0x40000000) == 0 || (old & 0x40000000) != 0))
        {
            t = plyr->camchg_time - 1.0f;
            if (t < 0.0f)
                t = 0.0f;
            plyr->camchg_time = t;
        }
        else
        {
            plyr->camchg_time = PlyrGetCamChgTime();
            ingame_wrk.plyr_wrk->flags2 |= 0x80000;
            plyr->_214 = PlyrGetStickAngle();
            EndPlyrApproachCameraCtrl();
        }
    }
    else
    {
        plyr->camchg_time = 0.0f;
        PlyrMapCamChgClear();
    }

    ingame_wrk.plyr_wrk->camdata = word;
    EventCameraSetChgFlg(0);                    // a tail call (j) in FF3: callcheck lists it as extra
}

// ──────────────────────────────────────────────────────────────────────
// Init. FF3 0x00231628 / 0x002317c0 / 0x002318a8 / 0x00236960 (FF3-only).

// Clears the three player works: no SE, no anime, _264 4.8; HP from the
// difficulty; player 0 current, the HUD full.
// FF3 0x00231628

void PlyrWrkInitAll(void)
{
    PLYR_WRK *plyr;
    int       i;

    for (i = 0; i < PLYR_NUM; i++)
    {
        plyr = GetPlyrWrk((s_char)i);
        memset(plyr, 0, sizeof(PLYR_WRK));
        plyr->_264 = 4.8f;
        plyr->danger_se = 0;
        plyr->anime_no = 0xff;
    }
    PlyrSetHpByDifficulty(0xff);
    PlyrSetCurrent(0);
    HudHpSetNow(GetHudHpWrk(), 1.0f);
    PlyrIdleTimeSet();
}

// PlyrWrkInitAll keeping each player's equipment bytes (+0x230..+0x232,
// +0x2e9, +0x2eb): a new game started from a clear save.
// FF3 0x002317c0

void PlyrWrkInitKeepEquip(void)
{
    u_char    equip[PLYR_NUM][3];
    u_char    equip_2e9[PLYR_NUM];
    u_char    equip_2eb[PLYR_NUM];
    PLYR_WRK *plyr;
    int       i;

    for (i = 0; i < PLYR_NUM; i++)
    {
        plyr = GetPlyrWrk((s_char)i);
        memcpy(equip[i], plyr->equip, 3);
        equip_2e9[i] = plyr->costume;
        equip_2eb[i] = plyr->acs;
    }

    PlyrWrkInitAll();

    for (i = 0; i < PLYR_NUM; i++)
    {
        plyr = GetPlyrWrk((s_char)i);
        memcpy(plyr->equip, equip[i], 3);
        plyr->costume = equip_2e9[i];
        plyr->acs = equip_2eb[i];
    }
}

// hp = hp_max = the table value of each player for the difficulty (0xff: the
// current one).
// FF3 0x002318a8

void PlyrSetHpByDifficulty(u_char diff)
{
    PLYR_WRK *plyr;
    int       i;
    int       hp;

    if (diff == 0xff)
        diff = (u_char)IngameWrkGetDifficulty(title_start_flg.mode);

    for (i = 0; i < PLYR_NUM; i++)
    {
        plyr = GetPlyrWrk((s_char)i);
        hp = PlyrMdlGetHpMax((u_char)i, diff);
        plyr->hp = hp;
        plyr->hp_max = hp;
    }
}

// FF3 0x00236960

void PlyrModeInit(void)
{
    memset(plyr_equip_lv, 0, PLYR_EQUIP_NUM);
    plyr_flash_wait = 0.0f;
    PlyrFlashNumInit();
}

// FF3 0x002317b0

void PlyrClear234(void)
{
    ingame_wrk.plyr_wrk->_234 = 0;
}

// FF3 0x00234540; FF2 GetPlyrAreaNo (FF3 asks the map loader).

int GetPlyrAreaNo(void)
{
    return MapLoadGetRoomNoNow();
}

// ──────────────────────────────────────────────────────────────────────
// The player dies: locks the menus, pause and the camera (0x11, 0x12, 0xf,
// 0x10) and enters mode 7 with the death kind in death_step.
// FF3 0x00231c70 (FF3-only).

void PlyrDeathStart(u_char kind)
{
    ingame_wrk.plyr_wrk->death_step = kind;
    LockTblInc(0x11);
    LockTblInc(0x12);
    LockTblInc(0xf);
    LockTblInc(0x10);
    ingame_wrk.plyr_wrk->flags &= ~(u_long)0x2000000;
    PlyrSetMode(PLYR_MODE_7, nullptr);
}

// ──────────────────────────────────────────────────────────────────────
// Mode 0: mode 11 first; outside the timed statuses 1 / 3 / 4 the control by
// +0xe0 (bit 0: a pending furniture photo; bit 1: nothing; else the control
// option b11 picks one of the two schemes), then PlyrFinderStartCheck unless status
// 1 / 3 or lock 3.
// FF3 0x00232478 (FF3-only).

void PlyrNormalCtrl(void)
{
    OPTION_WRK *opt;
    short       status;
    int         bits;

    if (plyr_opt == nullptr)
        plyr_opt = GetOptionWrk();
    opt = plyr_opt;

    if (PlyrMode11Ctrl())
        return;

    status = ingame_wrk.plyr_wrk->status;
    if (status != 1 && status != 3 && status != 4)
    {
        bits = ingame_wrk.plyr_wrk->_0e0;
        if ((bits & 1) != 0)
        {
            if (PeepStateIsLook())
                PeepLookCtrl();
        }
        else if ((bits & 2) == 0)
        {
            // PC: the over-the-shoulder camera walks the player relative to
            // itself whatever the control option says.
            if (opt->now.b11 != 0 && opt->now.b11 == 1 && !ReiPan_OverShoulderOwnsMovement())
                PlyrTankMoveCtrl();
            else
                PlyrMoveDirCtrl();
        }
    }

    status = ingame_wrk.plyr_wrk->status;
    if (status != 1 && status != 3 && IsLockTblOn(3) == 0)
        PlyrFinderStartCheck();
}

// Mode 11 (anime 0x25): holds (returns 1) until the anime is past frame 100
// and then ends or any input comes; leaving resets the control and mode 0.
// FF3 0x00232af8 (FF3-only).

int PlyrMode11Ctrl(void)
{
    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_11)
        return 0;

    if (ingame_wrk.plyr_wrk->anime_no == 0x25)
    {
        if (PlyrGetAnimeFrame() < 101)
            return 0;
        if (CheckPlyrAnimeEnd(0x25) == 0 && PlyrGetInputCount() == 0)
            return 1;
    }
    PlyrTurnFlagClear();
    PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
    return 0;
}

// Sticks pushed past 0x1d from centre plus the buttons held (0x40, 0x10,
// 0x100, 0x800, 0x80).
// FF3 0x00232978 (FF3-only).

int PlyrGetInputCount(void)
{
    int ax0 = PadGetAnalogRaw(0, 0);
    int ax1 = PadGetAnalogRaw(1, 0);
    int ax2 = PadGetAnalogRaw(2, 0);
    int ax3 = PadGetAnalogRaw(3, 0);
    int d0 = abs(ax0 - 0x80);
    int d1 = abs(ax1 - 0x80);
    int d2 = abs(ax2 - 0x80);
    int d3 = abs(ax3 - 0x80);
    s_char b0 = (s_char)CheckPadNowPlaySe(0x40, -1, 0);
    s_char b1 = (s_char)CheckPadNowPlaySe(0x10, -1, 0);
    s_char b2 = (s_char)CheckPadNowPlaySe(0x100, -1, 0);
    s_char b3 = (s_char)CheckPadNowPlaySe(0x800, -1, 0);
    s_char b4 = (s_char)CheckPadNowPlaySe(0x80, -1, 0);

    return (s_char)(b0 + (d0 > 0x1d) + (d1 > 0x1d) + b1 + b2 + b3 + b4 + (d2 > 0x1d) + (d3 > 0x1d));
}

// FF3 0x002393a0 (FF3-only).

int PlyrGetAnimeFrame(void)
{
    return (int)static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeTime();
}

// ──────────────────────────────────────────────────────────────────────
// Camera up (PLYR_FLG_800000): outside status 1 / 3 and lock 3, the enemy
// targeting, then the finder control by +0xe0 (bit 0: a pending furniture
// photo, bit 1: CatViewCtrl, else PlyrFinderMoveCtrl) and PlyrFinderEndCheck unless
// PlyrFinderShotCtrl says no; _260 counts down to 0.
// FF3 0x00232590 (FF3-only).

void PlyrFinderCtrl(void)
{
    int bits;
    int n;

    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xf;
    if (ingame_wrk.plyr_wrk->status == 1 || ingame_wrk.plyr_wrk->status == 3)
        return;
    if (IsLockTblOn(3))
        return;

    PlyrFinderEneCheck();

    bits = ingame_wrk.plyr_wrk->_0e0;
    if ((bits & 1) != 0)
    {
        if (PeepStateIsLook() && PlyrFinderShotCtrl() == 0)
        {
            PeepLookCtrl();
            PlyrFinderEndCheck();
        }
    }
    else if ((bits & 2) != 0)
    {
        if (PlyrFinderShotCtrl() == 0)
        {
            CatViewCtrl();
            PlyrFinderEndCheck();
        }
    }
    else if (PlyrFinderShotCtrl() == 0)
    {
        PlyrFinderMoveCtrl();
        PlyrFinderEndCheck();
    }

    n = (s_char)ingame_wrk.plyr_wrk->_260 - 1;
    if (n < 0)
        n = 0;
    ingame_wrk.plyr_wrk->_260 = (u_char)n;
}

// ──────────────────────────────────────────────────────────────────────
// Doors. FF3 0x00236380 / 0x002364d0 / 0x00236550 / 0x00234a88 (FF3-only).

// FF3 0x00236380

int DoorMotionIsEnd(void)
{
    return CheckPlyrAnimeEnd((u_char)plyr_door_anime);
}

// Goes through a door: the door's player anime for that side (DOOR_DAT, none
// or <= 0: nothing), mode 6, and DoorMotionIsEnd waits for it. Side 0 sets
// flag 8, _264 2.4 and the camera (0, 0, 0, 0); side 1 sets
// PLYR_FLG_DOOR_OPEN and the camera (0, 50, 128, 15). The first argument is
// the door's id (init_Story_Door_Open, FUN_001f2f70).
// FF3 0x00236398 (FF3-only).

void PlyrDoorStart(int door_id, int side)
{
    DOOR_DAT *door  = DoorDatGet(door_id);
    int       anime = -1;

    if (door != nullptr)
        anime = door->plyr_anime[side];
    if (anime <= 0)
        return;

    PlyrSetMode(PLYR_MODE_DOOR, nullptr);
    if (side == 0)
    {
        SetPlyrAnime((u_char)anime, 0, 0xff, 0xff, 0, 0.0f);
        ingame_wrk.plyr_wrk->flags |= 0x8;
        PlyrSet264(2.4f);
        PlyrSet280Req(0, 0.0f, 0.0f, 0.0f, 0.0f);
    }
    else if (side == 1)
    {
        SetPlyrAnime((u_char)anime, 0, 0xff, 0xff, 0, 0.0f);
        ingame_wrk.plyr_wrk->flags |= PLYR_FLG_DOOR_OPEN;
        PlyrSet280Req(0, 0.0f, 50.0f, 128.0f, 15.0f);
    }
    plyr_door_anime = anime;
}

// Leaves the door: flags 8 / 0x10 / anime-end off, mode 0, _264 back to 4.8.
// FF3 0x002364d0

int PlyrDoorEnd(void)
{
    ingame_wrk.plyr_wrk->flags &= ~(u_long)0x8;
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_DOOR_OPEN;
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_ANIME_END;
    PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
    RandGhostAppearReq(1);
    PlyrSet264(4.8f);
    return 1;
}

// Mode 6 (FF2 had GetPlyrDoorMoveFlg here; FF3's is a mode handler).
// FF3 0x00236550

int PlyrDoorCtrl(void)
{
    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_DOOR)
        return 0;

    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;
    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xf;
    if (DoorMotionIsEnd() && (ingame_wrk.plyr_wrk->flags & PLYR_FLG_DOOR_OPEN) != 0)
        return PlyrDoorEnd();
    return 0;
}

// FF3 0x00234a88

void PlyrSet264(float v)
{
    ingame_wrk.plyr_wrk->_264 = v;
}

// ──────────────────────────────────────────────────────────────────────
// Escaping a hold. FF3 0x00235728 / 0x00236d58..0x00237240 (FF3-only).

// Advances both escape windows by the timer step and counts the presses that
// land inside them: window 0 the escape buttons (player 0 also the flash),
// window 1 the flash (player 0 only). True once the windows are over.
// FF3 0x00235728

int PlyrHoldEscapeCheck(void)
{
    PLYR_GAUGE *disp = GetAlbumSaveDisp();
    PLYR_WRK        *plyr = ingame_wrk.plyr_wrk;
    u_short          t;

    plyr->esc_time0 += PlyrGaugeTimeStep(disp);
    plyr->esc_time1 += PlyrGaugeTimeStep(disp);

    if (PlyrGetCurNo() == 0)
    {
        t = (u_short)(int)plyr->esc_time0;
        if (t >= plyr->esc_start0 && t < (u_int)plyr->esc_start0 + plyr->esc_len0
            && (CheckPadTrgPlaySe(pad_key[PAD_KEY_06], -1, 0) || CheckPadTrgPlaySe(pad_key[PAD_KEY_07], -1, 0)
                || PlyrFlashInput()))
            plyr->esc_push0++;

        t = (u_short)(int)plyr->esc_time1;
        if (t >= plyr->esc_start1 && t < (u_int)plyr->esc_start1 + plyr->esc_len1 && PlyrFlashInput())
            plyr->esc_push1++;

        if ((u_short)(int)plyr->esc_time0 < (u_int)plyr->esc_start0 + plyr->esc_len0)
            return 0;
        return (u_int)plyr->esc_start1 + plyr->esc_len1 <= (u_short)(int)plyr->esc_time1;
    }

    t = (u_short)(int)plyr->esc_time0;
    if (t >= plyr->esc_start0 && t < (u_int)plyr->esc_start0 + plyr->esc_len0
        && (CheckPadTrgPlaySe(pad_key[PAD_KEY_06], -1, 0) || CheckPadTrgPlaySe(pad_key[PAD_KEY_07], -1, 0)))
        plyr->esc_push0++;
    return (u_int)plyr->esc_start0 + plyr->esc_len0 <= (u_short)(int)plyr->esc_time0;
}

// The flash's reactions: every live enemy in state 3 (flags not 0x110000,
// flags2 not 0x100, kind 1, 2 or 6) with flags2 0x4000 gets reaction 5,
// except the one holding the player.
// FF3 0x00236d58

static void PlyrFlashEneReact5(void)
{
    ENE_WRK *hold = &ene_wrk[ingame_wrk.plyr_wrk->ene_no];
    ENE_WRK *ene;
    int           i;

    for (i = 0, ene = ene_wrk; i < ENE_WRK_MAX; i++, ene++)
    {
        if (ene->state != 3 || ene->hp == 0 || (ene->flags & 0x110000) != 0 || (ene->flags2 & 0x100) != 0)
            continue;
        if ((u_char)(ene->act_no - 1) >= 2 && ene->act_no != 6)
            continue;
        if ((ene->flags2 & 0x4000) == 0)
            continue;
        if ((u_char)(ingame_wrk.plyr_wrk->mode - 1) < 4 && ene == hold)
            continue;
        EneActSet(ene, 5);
    }
}

// FF3 0x00236e78

static void PlyrFlashEneSet528(void)
{
    ENE_WRK *ene;
    int           i;

    for (i = 0, ene = ene_wrk; i < ENE_WRK_MAX; i++, ene++)
    {
        if (ene->state == 3 && ene->hp != 0 && (ene->flags2 & 0x8000) != 0)
            ene->_528 = 1000;
    }
}

// FF3 0x00236ec8

static void PlyrFlashEneReact4(void)
{
    ENE_WRK *ene;
    int           i;

    for (i = 0, ene = ene_wrk; i < ENE_WRK_MAX; i++, ene++)
    {
        if (ene->state != 3 || ene->hp == 0 || (ene->flags & 0x110000) != 0 || (ene->flags2 & 0x100) != 0)
            continue;
        if ((u_char)(ene->act_no - 1) >= 2 && ene->act_no != 6)
            continue;
        if ((ene->flags2 & 0x10000) != 0)
            EneActSet(ene, 4);
    }
}

// The flash breaks a hold (or fires at close ghosts): SE 5, the enemies
// react, one flash used, no enemy touch for 30 frames; outside the camera
// view mode 8 with the recoil anime (0x95 when flags2 0x2000000, else 0x98).
// FF3 0x00236fa0

void PlyrFlashRelease(void)
{
    int     cam = (GetPhotoModeWrk()->GetMode() != 0) ? 2 : 1;
    u_short wait;

    PlyrSePlay(ingame_wrk.plyr_wrk, 5, 100, 0x1000);
    PlyrTurnFlagClear();
    EffPlyrHazeReq();
    PlyrFlashEneReact5();
    PlyrFlashEneSet528();
    PlyrFlashEneReact4();
    FlyFadeAll();
    EndPlyrApproachCameraCtrl();
    PlyrFlashNumSub(1);

    wait = (u_short)(int)(30.0f / GetFrameTimeScale());
    if ((u_short)ingame_wrk.plyr_wrk->ene_wait < wait)
        ingame_wrk.plyr_wrk->ene_wait = (short)wait;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        return;

    PlyrSetMode(PLYR_MODE_8, nullptr);
    PlyrSet280Req(cam, 128.0f, 38.0f, 0.0f, 4.0f);
    if ((ingame_wrk.plyr_wrk->flags2 & 0x2000000) != 0)
        SetPlyrAnime(0x95, 0, 0xff, 0xff, 0, 0.0f);
    else
        SetPlyrAnime(0x98, 2, 0xff, 0xff, 0, 0.0f);
}

// The hold broken without a flash: SE 0x1a9b000e, anime 0x82, mode 8.
// FF3 0x00237130

void PlyrHoldShakeOff(void)
{
    int cam = (GetPhotoModeWrk()->GetMode() != 0) ? 2 : 1;

    SndSePlayStd(0x1a9b000e);
    PlyrTurnFlagClear();
    EffPlyrHazeReq();
    EndPlyrApproachCameraCtrl();
    PlyrSet280Req(cam, 128.0f, 30.0f, 0.0f, 10.0f);
    SetPlyrAnime(0x82, 1, 0xff, 0xff, 0, 0.0f);
    PlyrSetMode(PLYR_MODE_8, nullptr);
}

// ──────────────────────────────────────────────────────────────────────
// The flash. FF3 0x002371e8..0x002374b8, 0x00237840 (FF3-only).

// FF3 0x002371e8

int PlyrFlashAble(void)
{
    if (PlyrGetCurNo() != 0)
        return 0;
    if (PlyrGetEquipLv(0) == 0)
        return 0;
    return GetPhotoModeWrk()->IsFinderReady() != 0;
}

// The flash button with a flash left, lock 0xe off, effect 0x1b not running
// and the flash recoil (0x98 / 0x95) not playing.
// FF3 0x00237240

int PlyrFlashInput(void)
{
    if (PlyrFlashAble() == 0)
        return 0;
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_18], -1, 0) == 0)
        return 0;
    if (PlyrGetFlashNum() == 0)
        return 0;
    if (IsLockTblOn(0xe) != 0)
        return 0;
    if (EffectModelKindIsSet(0x1b) != 0)
        return 0;
    return ingame_wrk.plyr_wrk->anime_no != 0x98 && ingame_wrk.plyr_wrk->anime_no != 0x95;
}

// FF3 0x00237360 (the matcher had named it PlayerChangeMode).

void PlyrFlashNumInit(void)
{
    PLYR_WRK *plyr = GetPlyrWrk(0);

    plyr->flash_num = PlyrFlashNumByLv(PlyrGetEquipLv(1));
}

// FF3 0x002373b8

void PlyrFlashNumSub(u_short n)
{
    ingame_wrk.plyr_wrk->flash_num -= n;
}

// FF3 0x002373e8

u_char PlyrFlashNumByLv(u_char lv)
{
    if (lv == 0xff)
        lv = PlyrGetEquipLv(1);
    return plyr_flash_num_tbl[lv];
}

// FF3 0x002374a0

int PlyrFlashIsReady(void)
{
    return plyr_flash_wait <= 0.0f;
}

// FF3 0x00237840

u_char PlyrGetEquipLv(u_char no)
{
    return plyr_equip_lv[no];
}

// Save block 56, also kept over a retry (ingame/mission/ExtraMissionShelt.c).
// FF3 0x00237858

int PlyrEquipLvSaveGet(void **addr)
{
    *addr = plyr_equip_lv;
    return sizeof(plyr_equip_lv);
}

// ──────────────────────────────────────────────────────────────────────
// The player's anime. FF3 0x00237898..0x002381c8, 0x00239270, 0x00239300.
// The three tables were checked against the ROM for every (anime, old) pair
// by running the EE code (2026-10-14).

// Blend frames from the old anime to the new one; 0 with the camera up.
// FF3 0x00237898 (FF3-only).

static u_char PlyrAnimeBlendFrames(u_char anime, u_char old)
{
    u_char frames = 5;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        return 0;

    switch (old)
    {
    case 0x00:
        if (anime == 0x05 || anime == 0x14 || (u_char)(anime - 0x21) < 4)
            frames = 0;
        break;
    case 0x04:
    case 0x2a: case 0x30: case 0x33: case 0x36: case 0x39: case 0x3c:
        if (anime == 0x00)
            frames = 10;
        break;
    case 0x05:
    case 0x14:
        if ((u_char)(anime - 0x06) < 2 || (u_char)(anime - 0x15) < 2)
            frames = 0;
        else if ((u_char)(anime - 0x90) < 3)
            frames = 10;
        break;
    case 0x06:
        if ((u_char)(anime - 0x07) < 2)
            frames = 0;
        else if ((u_char)(anime - 0x1d) < 2)
            frames = 3;
        else if ((u_char)(anime - 0x90) < 3)
            frames = 10;
        break;
    case 0x07:
    case 0x08:
    case 0x16:
    case 0x17:
        if ((u_char)(anime - 0x1d) < 2)
            frames = 3;
        else if ((u_char)(anime - 0x90) < 3)
            frames = 10;
        else
            frames = 0;
        break;
    case 0x09:
        if (anime == 0x0b)
            frames = 0;
        break;
    case 0x0b:
        if ((u_char)(anime - 0x0c) < 3)
            frames = 0;
        break;
    case 0x0c:
        if ((u_char)(anime - 0x0d) < 2)
            frames = 0;
        else if (anime == 0x06 || anime == 0x11 || anime == 0x92)
            frames = 10;
        break;
    case 0x0d:
    case 0x0e:
        if (anime == 0x09)
            frames = 0;
        else if (anime == 0x00 || anime == 0x05 || (u_char)(anime - 0x0f) < 2 || (u_char)(anime - 0x90) < 2)
            frames = 10;
        break;
    case 0x0f:
        if (anime == 0x10)
            frames = 0;
        break;
    case 0x10:
        if ((u_char)(anime - 0x11) < 3)
            frames = 0;
        break;
    case 0x11:
        if ((u_char)(anime - 0x12) < 2)
            frames = 0;
        else if (anime == 0x06 || anime == 0x0c || anime == 0x92)
            frames = 10;
        break;
    case 0x12:
    case 0x13:
        if (anime == 0x0f)
            frames = 0;
        else if (anime == 0x00 || anime == 0x05 || anime == 0x09 || anime == 0x0b || (u_char)(anime - 0x90) < 2)
            frames = 10;
        break;
    case 0x15:
        if ((u_char)(anime - 0x16) < 2)
            frames = 0;
        else if ((u_char)(anime - 0x1f) < 2)
            frames = 3;
        else if ((u_char)(anime - 0x90) < 3)
            frames = 10;
        break;
    case 0x1d: case 0x1e: case 0x1f: case 0x20:
    case 0x21: case 0x22: case 0x23: case 0x24:
    case 0x5c:
        frames = 0;
        break;
    case 0x26: case 0x29: case 0x2c: case 0x2f:
    case 0x32: case 0x35: case 0x38: case 0x3b:
        switch (anime)
        {
        case 0x27: case 0x2a: case 0x2d: case 0x30:
        case 0x33: case 0x36: case 0x39: case 0x3c:
            frames = 0;
            break;
        }
        break;
    case 0x27:
    case 0x2d:
        switch (anime)
        {
        case 0x00:
            frames = 10;
            break;
        case 0x28: case 0x2b: case 0x2e: case 0x31:
        case 0x34: case 0x37: case 0x3a: case 0x3d:
            frames = 8;
            break;
        }
        break;
    case 0x3e: case 0x40: case 0x42: case 0x44: case 0x46:
    case 0x48: case 0x4a: case 0x4c: case 0x4e: case 0x50:
    case 0x52: case 0x54: case 0x56: case 0x58: case 0x5a:
        if (anime == 0x00 || anime == 0x04)
            frames = 0;
        break;
    case 0x5e:
        if ((u_char)(anime - 0x60) < 2 || (u_char)(anime - 0x63) < 2)
            frames = 6;
        break;
    case 0x60:
        if (anime == 0x60)
            frames = 0;
        else if (anime == 0x5e || anime == 0x63)
            frames = 6;
        break;
    case 0x63:
        if (anime == 0x63)
            frames = 0;
        else if (anime == 0x5e || anime == 0x60)
            frames = 6;
        break;
    case 0x65:
    case 0x67:
        if (anime == 0x66 || anime == 0x68)
            frames = 0;
        break;
    case 0x66:
    case 0x68:
        if (anime == 0x8a || anime == 0x8d)
            frames = 0;
        break;
    case 0x6e: case 0x6f: case 0x71: case 0x74: case 0x75:
    case 0x77: case 0x78: case 0x7b: case 0x7c: case 0x7e:
    case 0x7f: case 0x81: case 0x82: case 0x84: case 0x85:
    case 0x87: case 0x88: case 0x8a: case 0x8b: case 0x8d:
    case 0x8e:
        if (anime == 0x00 || anime == 0x09 || anime == 0x0f || anime == 0x18 || anime == 0x90)
            frames = 0;
        break;
    case 0x90:
        if (anime == 0x91)
            frames = 0;
        break;
    case 0x91:
        if ((u_char)(anime - 0x92) < 3)
            frames = 0;
        break;
    case 0x92:
        if ((u_char)(anime - 0x93) < 2)
            frames = 0;
        else if (anime == 0x06 || anime == 0x0c || anime == 0x11)
            frames = 10;
        break;
    case 0x93:
    case 0x94:
        if (anime == 0x90)
            frames = 0;
        else if (anime == 0x00 || anime == 0x05 || anime == 0x09 || anime == 0x0b || (u_char)(anime - 0x0f) < 2)
            frames = 10;
        break;
    }

    if (anime == 0x5c || anime == 0x6a)
        frames = 0;
    return frames;
}

// The step phase a walk-type anime starts at: changes between the walk
// anime keep the model's step ((vtable +0x60) + 1) % 4.
// FF3 0x00237d58 (FF3-only).

static u_char PlyrAnimeStartPhase(u_char anime, u_char old)
{
    switch (old)
    {
    case 0x00: case 0x01: case 0x02: case 0x03:
        if (anime == 0x06 || anime == 0x15)
            return 1;
        break;
    case 0x1e: case 0x20: case 0x28: case 0x2b: case 0x34: case 0x37:
        if (anime == 0x06 || anime == 0x15)
            return 2;
        break;
    case 0x06: case 0x15:
        switch (anime)
        {
        case 0x06: case 0x15: case 0x27: case 0x2a: case 0x33: case 0x36:
            return (u_char)((static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep() + 1) % 4);
        }
        break;
    case 0x27: case 0x2a: case 0x2d: case 0x30:
        switch (anime)
        {
        case 0x33: case 0x36: case 0x39: case 0x3c:
            return (u_char)((static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep() + 1) % 4);
        }
        break;
    case 0x33: case 0x36: case 0x39: case 0x3c:
        switch (anime)
        {
        case 0x06: case 0x15: case 0x27: case 0x2a: case 0x2d: case 0x30:
            return (u_char)((static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep() + 1) % 4);
        }
        break;
    }
    return 0;
}

// PLYR_WRK.anime_attr of a change (0, 1 or 2; role not traced yet).
// FF3 0x00237ec0 (FF3-only; FF3 also sets 0 for old 0x27.. into 0x15 /
// 0x28.., a no-op left out).

static u_char PlyrAnimeAttr(u_char anime, u_char old)
{
    u_char attr = 0;

    switch (old)
    {
    case 0x06:
        if (anime == 0x15)
            attr = 1;
        break;
    case 0x15:
        if (anime == 0x92)
            attr = 1;
        break;
    case 0x25: case 0x28: case 0x2b: case 0x2e: case 0x31: case 0x34:
    case 0x37: case 0x3a: case 0x3d: case 0x3f: case 0x41: case 0x43:
    case 0x45: case 0x47: case 0x49: case 0x4b: case 0x4d: case 0x4f:
    case 0x51: case 0x53: case 0x55: case 0x57: case 0x59: case 0x5b:
        attr = 1;
        break;
    case 0x3e: case 0x40: case 0x42: case 0x44: case 0x46:
    case 0x48: case 0x4a: case 0x4c: case 0x4e:
        switch (anime)
        {
        case 0x3f: case 0x41: case 0x43: case 0x45: case 0x47:
        case 0x49: case 0x4b: case 0x4d: case 0x4f:
            attr = 2;
            break;
        }
        break;
    case 0x5e: case 0x5f: case 0x60: case 0x62: case 0x63:
        switch (anime)
        {
        case 0x5e: case 0x60: case 0x61: case 0x63: case 0x64:
            attr = 2;
            break;
        }
        break;
    case 0x6a: case 0x6b: case 0x6c:
        if (anime == 0x00)
            attr = 2;
        break;
    case 0x6f: case 0x71: case 0x75: case 0x78: case 0x7c:
    case 0x7f: case 0x82: case 0x88: case 0x8b: case 0x8e:
        attr = 2;
        break;
    case 0x8a:
        if (anime == 0x8b)
            attr = 2;
        break;
    }

    if (anime == 0x65 || anime == 0x67 || anime == 0xa1)
        attr = 2;
    switch (anime)
    {
    case 0x26: case 0x29: case 0x2c: case 0x2f:
    case 0x32: case 0x35: case 0x38: case 0x3b:
        attr = 1;
        break;
    }
    return attr;
}

// Starts anime_no on the player model (ids above 0xb0 ignored; the same
// anime again only for 0x5c, 0x60, 0x63, 0x6b, 0x6c). blend / phase / attr
// 0xff: from the tables above. Non-idle anime restart the idle timer.
// FF3 0x00238020; FF2 SetPlyrAnime (FF3 drives the model directly).

void SetPlyrAnime(u_char anime_no, u_char blend, u_char phase, u_char attr, u_char arg, float start)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (plyr->_2e4 == 0 || anime_no > 0xb0)
        return;
    if (anime_no != 0x60 && anime_no != 0x63 && anime_no != 0x5c && anime_no != 0x6b && anime_no != 0x6c
        && plyr->anime_no == anime_no)
        return;

    if (blend == 0xff)
        blend = PlyrAnimeBlendFrames(anime_no, plyr->anime_no);
    if (phase == 0xff)
        phase = PlyrAnimeStartPhase(anime_no, plyr->anime_no);
    if (attr == 0xff)
        attr = PlyrAnimeAttr(anime_no, plyr->anime_no);
    plyr->anime_attr = attr;

    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_ANIME_END;
    // FF3 calls the model's vtable +0x40 (CharPlayerModel: 0x1450e0)
    static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->StartAnime(anime_no, blend, phase, (plyr->anime_no != 0xff) ? arg : 0, start, 1.0f);
    plyr->anime_no = anime_no;

    if (PlyrAnimeIsIdle(anime_no) == 0)
    {
        PlyrIdleTimeSet();
        ingame_wrk.plyr_wrk->flags &= ~(u_long)0x80000;
    }
}

// FF3 0x00239270 (the matcher had named it PlyrOutsideCheck).

int PlyrAnimeIsIdle(u_char anime_no)
{
    if (anime_no == 0)
        return 0;
    return anime_no < 4 || anime_no == 10;
}

// The idle timer: 90..210 frames (75..175 in PAL).
// FF3 0x00239300 (FF3-only).
// callcheck: rand (ReiPan_Rand)

void PlyrIdleTimeSet(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       lo = GetPALMode() ? 75 : 90;
    int       hi = GetPALMode() ? 175 : 210;

    plyr->idle_time = (short)(int)((float)lo + (float)(hi - lo) * ((float)ReiPan_Rand() / REIPAN_RAND_MAXF));
}


// ──────────────────────────────────────────────────────────────────────
// The frame's anime (0x2381c8..0x239268, FF3-only). PlyrAnimeCtrl starts
// at 0xff (none) and lets four choosers fill it in turn: event poses from
// flags2 0x3f800, moves from flags2 0x3ff, the walk-step follow-ups, and the
// stance / idle anime.

// flags2 bits -> event pose, data 0x360c08 (8-byte mask, 8-byte anime;
// 0xffffffff ends it). FF3 copies it to the stack first.
struct PLYR_ANIME_EV { u_long mask; u_long anime; };
static_assert(sizeof(PLYR_ANIME_EV) == 0x10, "PLYR_ANIME_EV is 0x10 bytes (EE 64-bit words)");
static const PLYR_ANIME_EV plyr_anime_ev_tbl[] =
{
    { 0x14800, 0x21 }, { 0x24800, 0x22 }, { 0x18800, 0x23 }, { 0x28800, 0x24 },
    { 0x1a000, 0x1f }, { 0x2a000, 0x20 }, { 0x19000, 0x1d }, { 0x29000, 0x1e },
    { 0xffffffff, 0 },
};

// flags2 bits -> move anime, data 0x360fa8: 'start' from a standing anime,
// 'cont' when the current anime already moves (PlyrAnimeMoveNo).
struct PLYR_ANIME_MOVE { u_long mask; u_long start; u_long cont; };
static_assert(sizeof(PLYR_ANIME_MOVE) == 0x18, "PLYR_ANIME_MOVE is 0x18 bytes (EE 64-bit words)");
static const PLYR_ANIME_MOVE plyr_anime_move_tbl[] =
{
    { 0x400002, 0x0c, 0x0b }, { 0x800002, 0x11, 0x10 }, { 0x2000002, 0x92, 0x91 },
    { 0x000001, 0x15, 0x14 }, { 0x000002, 0x06, 0x05 },
    { 0xffffffff, 0, 0 },
};

// The event pose for flags2, -1: none (or flags2 0x3c00000).
// FF3 0x002381c8 (FF3-only; also called by PlyrTurnStart).

int PlyrAnimeEventNo(void)
{
    u_long f2 = ingame_wrk.plyr_wrk->flags2;
    int    i;

    if ((ingame_wrk.plyr_wrk->flags2 & 0x3c00000) != 0)
        return -1;
    for (i = 0; plyr_anime_ev_tbl[i].mask != 0xffffffff; i++)
    {
        if ((f2 & plyr_anime_ev_tbl[i].mask) == plyr_anime_ev_tbl[i].mask)
            return (int)plyr_anime_ev_tbl[i].anime;
    }
    return -1;
}

// The move anime for flags2, -1: none.
// FF3 0x002389b0 (FF3-only).

static int PlyrAnimeMoveNo(void)
{
    u_long f2 = ingame_wrk.plyr_wrk->flags2;
    int    i;

    for (i = 0; plyr_anime_move_tbl[i].mask != 0xffffffff; i++)
    {
        if ((f2 & plyr_anime_move_tbl[i].mask) == plyr_anime_move_tbl[i].mask)
            break;
    }
    if (plyr_anime_move_tbl[i].mask == 0xffffffff)
        return -1;

    // Already moving (0, 7..9, 0xd..0xf, 0x12, 0x13, 0x16, 0x17, 0x90,
    // 0x93, 0x94): the continuing anime.
    switch (ingame_wrk.plyr_wrk->anime_no)
    {
    case 0x00: case 0x07: case 0x08: case 0x09: case 0x0d: case 0x0e: case 0x0f:
    case 0x12: case 0x13: case 0x16: case 0x17: case 0x90: case 0x93: case 0x94:
        return (int)plyr_anime_move_tbl[i].cont;
    default:
        return (int)plyr_anime_move_tbl[i].start;
    }
}

// FF3 0x00238b20

static void PlyrAnimeSelEvent(u_char *anime)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       no;

    if ((plyr->flags2 & 0x3f800) == 0 || (plyr->flags2 & 0x400) != 0)
        return;
    no = PlyrAnimeEventNo();
    if (no >= 0)
        *anime = (u_char)no;
}

// Keeps a one-shot anime (5, 7, 8, 0xb, 0xd, 0xe, 0x10, 0x12..0x14, 0x16,
// 0x17, 0x91, 0x93, 0x94) to its end, else the move anime.
// FF3 0x00238b98

static void PlyrAnimeSelMove(u_char *anime)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       no;

    if (*anime != 0xff)
        return;

    switch (plyr->anime_no)
    {
    case 0x05: case 0x07: case 0x08: case 0x0b: case 0x0d: case 0x0e: case 0x10:
    case 0x12: case 0x13: case 0x14: case 0x16: case 0x17: case 0x91: case 0x93: case 0x94:
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
        {
            *anime = plyr->anime_no;
            return;
        }
        break;
    default:
        break;
    }

    if ((ingame_wrk.plyr_wrk->flags2 & 0x3ff) == 0)
        return;
    no = PlyrAnimeMoveNo();
    if (no >= 0)
        *anime = (u_char)no;
}

// The stop after a move, by the walk step the model is at (0..3).
// FF3 0x00238cd0

static void PlyrAnimeSelStep(u_char *anime)
{
    u_char step;

    if (*anime != 0xff)
        return;
    // FF3 calls the model's vtable +0x60 (CharPlayerModel: 0x146640)
    step = (u_char)static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();

    switch (ingame_wrk.plyr_wrk->anime_no)
    {
    case 0x05: case 0x1d: case 0x21: case 0x22: case 0x23: case 0x24:
        *anime = 0x07;
        break;
    case 0x06:
        if (step == 0 || step == 3)
            *anime = 0x07;
        else if (step < 3)
            *anime = 0x08;
        break;
    case 0x0b:
        *anime = 0x0d;
        break;
    case 0x0c:
        if (step == 0 || step == 3)
            *anime = 0x0d;
        else if (step < 3)
            *anime = 0x0e;
        break;
    case 0x10:
        *anime = 0x12;
        break;
    case 0x11:
        if (step == 0 || step == 3)
            *anime = 0x12;
        else if (step < 3)
            *anime = 0x13;
        break;
    case 0x14: case 0x1f:
        *anime = 0x16;
        break;
    case 0x15:
        if (step == 0 || step == 3)
            *anime = 0x16;
        else if (step < 3)
            *anime = 0x17;
        break;
    case 0x1e:
        *anime = 0x08;
        break;
    case 0x20:
        *anime = 0x17;
        break;
    case 0x91:
        *anime = 0x93;
        break;
    case 0x92:
        if (step == 0 || step == 3)
            *anime = 0x93;
        else if (step < 3)
            *anime = 0x94;
        break;
    default:
        break;
    }
}

// 0, 1 or 2 with chances a, b, c percent (0 for the rest).
// FF3 0x00238e90

static int PlyrRandPick3(int a, int b, int c)
{
    int r = (s_char)GetRandValIByF(100);

    if (r < a)
        return 0;
    if (r < a + b)
        return 1;
    return (r < a + b + c) ? 2 : 0;
}

// The stance (flags2 0x400000: 9, 0x800000: 0xf, 0x1000000: 0x5e,
// 0x2000000: 0x90, else 0), the low-HP stance 4, and after idle_time an
// idle variant (base + 1 .. base + n; player 2 standing: 1..3 by 15/45/40%).
// FF3 0x00238f08
// callcheck: rand (ReiPan_Rand)

static void PlyrAnimeSelIdle(u_char *anime)
{
    PLYR_WRK *plyr;
    u_long    f2;
    u_char    base;
    int       var;

    if (*anime != 0xff)
        return;

    f2 = ingame_wrk.plyr_wrk->flags2;
    if ((f2 & 0x400000) != 0)
    {
        base = 0x09;
        var  = 1;
    }
    else if ((f2 & 0x800000) != 0)
    {
        base = 0x0f;
        var  = 0;
    }
    else if ((f2 & 0x1000000) != 0)
    {
        base = 0x5e;
        var  = 0;
    }
    else if ((f2 & 0x2000000) != 0)
    {
        base = 0x90;
        var  = 0;
    }
    else
    {
        base = 0x00;
        var  = 3;
    }
    *anime = base;

    plyr = ingame_wrk.plyr_wrk;
    if (plyr->hp > 0 && plyr->hp <= plyr->hp_max / 5
        && (plyr->flags2 & 0x3c00000) == 0 && (plyr->flags & PLYR_FLG_PZL) == 0)
    {
        *anime = 0x04;
        return;
    }

    if (plyr->status != 0 || var == 0)
        return;

    if (IsLockTblOn(3) || (ingame_wrk.plyr_wrk->flags & PLYR_FLG_LOOK) != 0)
    {
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_IDLE_VAR;
        return;
    }

    plyr = ingame_wrk.plyr_wrk;
    if ((plyr->flags & PLYR_FLG_IDLE_VAR) != 0)
    {
        if ((plyr->flags & PLYR_FLG_ANIME_END) != 0)
            plyr->flags &= ~(u_long)PLYR_FLG_IDLE_VAR;
        else
            *anime = plyr->anime_no;
        return;
    }

    if (--plyr->idle_time != 0)
        return;
    ingame_wrk.plyr_wrk->flags |= PLYR_FLG_IDLE_VAR;

    if (PlyrGetCurNo() == 2 && base == 0)
    {
        *anime = (u_char)(PlyrRandPick3(15, 45, 40) + 1);
        return;
    }
    *anime = (u_char)(base + (int)((float)var * ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) + 0.0f) + 1);
}

// Per frame (PlayerMain): with the puzzle code not driving the anime (or in
// mode 8 / a hold) and the player free in mode 0, the four choosers pick the
// anime; then the model's walk step is kept.
// FF3 0x00239158 (FF3-only).

void PlyrAnimeCtrl(void)
{
    PLYR_WRK *plyr  = ingame_wrk.plyr_wrk;
    u_char    anime = 0xff;

    PlyrAnimeEndCheck();
    if (PlyrMapDirAnimeCtrl() == 0 || ingame_wrk.plyr_wrk->mode == PLYR_MODE_8
        || (ingame_wrk.plyr_wrk->flags & PLYR_FLG_HOLD) != 0)
    {
        PLYR_WRK *cur = ingame_wrk.plyr_wrk;

        if ((cur->flags & 0x14000000) == 0 && cur->mode == PLYR_MODE_NORMAL
            && (cur->flags & PLYR_FLG_HOLD) == 0 && (cur->_0e0 & 7) == 0)
        {
            PlyrAnimeSelEvent(&anime);
            PlyrAnimeSelMove(&anime);
            PlyrAnimeSelStep(&anime);
            PlyrAnimeSelIdle(&anime);
            SetPlyrAnime(anime, 0xff, 0xff, 0xff, 1, 0.0f);
        }
    }
    // FF3 calls the model's vtable +0x60 (CharPlayerModel: 0x146640)
    plyr->anime_step = (u_char)static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();
}

// ──────────────────────────────────────────────────────────────────────
// The death scene, mode 7 (FF3-only). PlyrDeathStart enters it at step 1
// (held to death), 4 (a hit) or 0x11; the steps fade, move the camera and
// play the death anime, then either the game over (GameOverReq, step 0xf)
// or, with lock 0x18, a revival at full HP (step 0x10).

static int plyr_death_cnt;              // sdata 0x3719ec (gp-0x1b04): frames in the step

// Effects hidden while the death camera runs (lock 0xb).
// FF3 0x00231ce0

static void PlyrDeathHideEffects(void)
{
    LockTblInc(0xb);
    eff_wrk.disp_flg = 0;
}

// FF3 0x00231d08

static void PlyrDeathShowEffects(void)
{
    eff_wrk.disp_flg = 1;
    LockTblDec(0xb);
}

// While the ghost holds the dying player it moves with the hold (xyzw).
static void PlyrDeathEneFollow(ENE_WRK *ene)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    ene->pos[0] += plyr->hold_move[0];
    ene->pos[1] += plyr->hold_move[1];
    ene->pos[2] += plyr->hold_move[2];
    ene->pos[3] += plyr->hold_move[3];
}

// FF3 0x00231d30

int PlyrDeathCtrl(void)
{
    sceVu0FVECTOR at;
    PLYR_WRK     *plyr;
    ENE_WRK *ene;

    switch (ingame_wrk.plyr_wrk->death_step)
    {
    default:
        break;

    case 1:                             // held: the camera closes in on the two
        plyr = ingame_wrk.plyr_wrk;
        GetMidPos(at, plyr->pos, ene_wrk[plyr->ene_no].pos);
        ReqPlyrApproachCameraCtrl(24.0f, 28.0f, at);
        plyr_death_cnt = 0;
        ingame_wrk.plyr_wrk->death_step = 2;
        break;

    case 2:
        if (plyr_death_cnt + 1 < 20)
        {
            plyr_death_cnt++;
            break;
        }
        plyr_death_cnt = 0;
        FadeOutReq(0x80, 0x80, 0x80, 25);
        ingame_wrk.plyr_wrk->death_step = 3;
        break;

    case 3:
        if (plyr_death_cnt + 1 < 25)
        {
            plyr_death_cnt++;
            break;
        }
        plyr_death_cnt = 0;
        PlyrDeathHideEffects();
        EndPlyrApproachCameraCtrl();
        plyr = ingame_wrk.plyr_wrk;
        ReqPlyrDeadCameraCtrl(8.0f, 20.0f, 40.0f, plyr->pos, plyr->rot);
        ingame_wrk.plyr_wrk->death_step = 0xb;
        break;

    case 4:                             // a fatal hit
        LockTblInc(3);
        FadeOutReq(0x80, 0x80, 0x80, 10);
        plyr_death_cnt = 0;
        ingame_wrk.plyr_wrk->death_step = 6;
        break;

    case 6:
        if (plyr_death_cnt + 1 < 10)
        {
            plyr_death_cnt++;
            break;
        }
        plyr_death_cnt = 0;
        ingame_wrk.plyr_wrk->death_step = 7;
        break;

    case 7:
        if (++plyr_death_cnt < 20)
            break;
        ingame_wrk.plyr_wrk->hp = 0;
        EneActReqById(0, ene_wrk[ingame_wrk.plyr_wrk->ene_no].id, 1);
        SetPlyrAnime(0x99, 0, 0xff, 0xff, 1, 0.0f);
        plyr = ingame_wrk.plyr_wrk;
        GetMidPos(at, plyr->pos, ene_wrk[plyr->ene_no].pos);
        ReqPlyrDamageCameraCtrl(18.0f, 45.0f, 45.0f, at, &ene_wrk[ingame_wrk.plyr_wrk->ene_no]);
        FadeInReq(0x80, 0x80, 0x80, 20);
        plyr_death_cnt = 0;
        ingame_wrk.plyr_wrk->death_step = 8;
        break;

    case 8:
        ene = &ene_wrk[ingame_wrk.plyr_wrk->ene_no];
        if (++plyr_death_cnt >= 170)
        {
            plyr_death_cnt = 0;
            FadeOutReq(0, 0, 0, 20);
            ingame_wrk.plyr_wrk->death_step = 9;
        }
        PlyrDeathEneFollow(ene);
        break;

    case 9:
        ene = &ene_wrk[ingame_wrk.plyr_wrk->ene_no];
        if (++plyr_death_cnt >= 20)
        {
            EneStreamFadeOut(3);
            TattooEvEffectStop();
            plyr_death_cnt = 20;
            ingame_wrk.plyr_wrk->death_step = 10;
        }
        PlyrDeathEneFollow(ene);
        break;

    case 10:
        if (TattooEvEffectIsStopped() == 0)
            break;
        TattooEvEffectRelease();
        EnemyReleaseAll();
        static_cast<CharPlayerModel *>(ingame_wrk.plyr_wrk->mdl)->ClearDreamLight();
        EffectMonochromeSet(0, 0);
        gra3dPrelight(0);
        PlyrDeathHideEffects();
        ingame_wrk.plyr_wrk->death_step = 0xe;
        break;

    case 0xb:                           // the player falls
        if (++plyr_death_cnt < 20)
            break;
        ingame_wrk.plyr_wrk->hp = 0;
        SetPlyrAnime(0x8f, 0, 0xff, 0xff, 1, 0.0f);
        PlyrSePlay(ingame_wrk.plyr_wrk, 4, 100, 0x1000);
        plyr_death_cnt = 0;
        FadeInReq(0x80, 0x80, 0x80, 20);
        ingame_wrk.plyr_wrk->death_step = 0xc;
        break;

    case 0xc:
        ingame_wrk.plyr_wrk->death_step = 0xd;
        // fall through
    case 0xd:
        if (++plyr_death_cnt < 60)
            break;
        FadeOutReq(0, 0, 0, 20);
        plyr_death_cnt = 0;
        ingame_wrk.plyr_wrk->death_step = 0xe;
        break;

    case 0xe:
        if (plyr_death_cnt + 1 < 40)
        {
            plyr_death_cnt++;
            break;
        }
        plyr_death_cnt = 0;
        EndPlyrApproachCameraCtrl();
        if (IsLockTblOn(0x18) == 0)
        {
            SetOpenCondSwitch(1);
            PlyrDeathShowEffects();
            GameOverReq();
            ingame_wrk.plyr_wrk->death_step = 0xf;
        }
        else
        {
            PlyrHpRestore();
            SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
            ingame_wrk.plyr_wrk->death_step = 0x10;
        }
        LockTblDec(0x11);
        LockTblDec(0x12);
        LockTblDec(0xf);
        LockTblDec(0x10);
        return 1;

    case 0x10:                          // revived
        ingame_wrk.plyr_wrk->flags |= PLYR_FLG_REVIVED;
        PlyrDeathShowEffects();
        ingame_wrk.plyr_wrk->death_step = 0;
        break;

    case 0x11:
        plyr_death_cnt = 0;
        FadeOutReq(0x80, 0x80, 0x80, 25);
        ingame_wrk.plyr_wrk->death_step = 0x12;
        break;

    case 0x12:
        if (plyr_death_cnt + 1 < 25)
        {
            plyr_death_cnt++;
            break;
        }
        plyr_death_cnt = 0;
        PlyrDeathHideEffects();
        plyr = ingame_wrk.plyr_wrk;
        ReqPlyrDeadCameraCtrl(8.0f, 20.0f, 40.0f, plyr->pos, plyr->rot);
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
        ingame_wrk.plyr_wrk->death_step = 0xb;
        break;
    }
    return 0;
}

// FF3 0x00232368 (FF3-only).

int PlyrDeathIsRevived(void)
{
    if (IsLockTblOn(0x18) == 0)
        return 0;
    return (ingame_wrk.plyr_wrk->flags & PLYR_FLG_REVIVED) != 0;
}

// FF3 0x00232ac0 (FF3-only).

void PlyrMode11Start(void)
{
    PlyrSetMode(PLYR_MODE_11, nullptr);
    SetPlyrAnime(0x25, 5, 0xff, 0xff, 1, 0.0f);
}

// ene_wait from frames at 60 fps, scaled to the frame rate. FF3 passes
// &plyr->flags (+0xd0) and writes +0x54 of it (ene_wait at +0x124).
// FF3 0x00232ba8 (FF3-only).

void PlyrSetEneWait(PLYR_WRK *plyr, u_short frames)
{
    if (plyr != nullptr)
        plyr->ene_wait = (short)(int)((float)frames / GetFrameTimeScale());
}

// ──────────────────────────────────────────────────────────────────────
// HP (FF3-only).

// FF3 0x00233a78

void PlyrHitDamage(void)
{
    (void)GetHudHpWrk();                // FF3 calls it and drops the result
    PlyrDamage(ingame_wrk.plyr_wrk, ingame_wrk.plyr_wrk->dmg);
    ingame_wrk.plyr_wrk->dmg = 0;
    CallNega2(0, 10, 5);
    PlyrVibCtrl(0xf);
    PlyrSePlay(ingame_wrk.plyr_wrk, 1, 100, 0x1000);
}

// FF3 0x00233b30

void PlyrHpRestore(void)
{
    HudHpSetNow(GetHudHpWrk(), 1.0f);
    PlyrClear108();
    ingame_wrk.plyr_wrk->hp = ingame_wrk.plyr_wrk->hp_max;
    PlyrDangerSeCtrl();
}

// FF3 0x00233b78

void PlyrHpRestoreAll(void)
{
    int i;

    HudHpSetNow(GetHudHpWrk(), 1.0f);
    for (i = 0; i < PLYR_NUM; i++)
    {
        PLYR_WRK *plyr = GetPlyrWrk((s_char)i);

        plyr->_108 = 0;
        plyr->hp   = plyr->hp_max;
        PlyrDangerSeStop((s_char)i);
    }
}

// FF3 0x00233e30

void PlyrClear108(void)
{
    ingame_wrk.plyr_wrk->_108 = 0;
}

// FF3 0x00233e40

float PlyrGetHpRate(void)
{
    return (float)ingame_wrk.plyr_wrk->hp / (float)ingame_wrk.plyr_wrk->hp_max;
}

// ──────────────────────────────────────────────────────────────────────
// The viewfinder and the enemies (FF3 0x2342f0..0x235548).

// The viewfinder frame, 520 x 328 (sdata 0x371ac0 as one word; FF2
// photo_frame_tbl[0]; only FrameInsideChk reads it).
static const u_short plyr_frame_size[2] = { 520, 328 };

// Each frame with the camera up: per enemy slot, flag 0x200 when its target
// point is inside the frame (screen point kept), 0x400 dmin the frame centre
// (fdr_dist), 0x40 when it can be targeted (close or flags2 0x20), 0x80 when
// it can be shot at its distance.
// FF3 0x002342f0 (FF3-only).

void PlyrFinderEneCheck(void)
{
    const u_long keep = 0xfffff7fffffff92full;  // sdata 0x371a68: clears 0x6d0 and bit 43
    int          i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];
        PLYR_WRK     *plyr;
        u_long        fl;
        float         dist, x, y, d;

        fl = ene->flags & keep;
        ene->flags = fl;
        if (ene->state != 3 || ene->hp == 0 || (fl & 0x100000) != 0)
            continue;

        dist = ene->dist;
        if (OutSightChk(ene->tpos, gra3dcamGetPosition(), ingame_wrk.plyr_wrk->rot[1], 2.0943952f, 200.0f) != 0)
            continue;
        if (FrameInsideChk(ene->tpos, &x, &y) == 0)
            continue;

        ene->flags |= 0x200;
        ene->scr_x = x;
        ene->scr_y = y;
        plyr = ingame_wrk.plyr_wrk;
        d = GetLength2D(x - plyr->finder_x, y - plyr->finder_y);
        if (d <= PlyrGetFinderRadius(0xff, 0xff))
        {
            ene->fdr_dist = d;
            ene->flags |= 0x400;
        }

        fl = ene->flags;
        if ((fl & 0x10000) == 0)
        {
            int close = ene->blink.result[0] != 0 || dist <= 12.0f;

            if ((close && (s_char)ingame_wrk.plyr_wrk->_260 <= 0) || (ene->flags2 & 0x20) != 0)
            {
                fl |= 0x40;
                ene->flags = fl;
            }
        }

        if ((fl & 0x400) == 0 && !(dist <= 20.4f && (fl & 0x200) != 0 && ene->type != 2))
            continue;
        if ((fl & 0x40) == 0 || ene->alpha == 0)
            continue;

        if (ene->type == 2)
        {
            if (dist <= 200.0f)
                ene->flags = fl | 0x80;
        }
        else if (dist <= PlyrGetShotRange(0xff, 0xff))
        {
            ene->flags |= 0x80;
        }
    }
}

// tv to the screen point (tx, ty); 1 when it is inside the viewfinder frame
// around the finder point.
// FF3 0x00234f78; FF2 FrameInsideChk (FF3 keeps the finder point as floats).

int FrameInsideChk(float *tv, float *tx, float *ty)
{
    float     w = (float)plyr_frame_size[0] * 0.5f;
    float     h = (float)plyr_frame_size[1] * 0.5f;
    PLYR_WRK *plyr;

    WorldToScreenPos(tv, tx, ty);
    plyr = ingame_wrk.plyr_wrk;
    return plyr->finder_x - w <= *tx && *tx <= plyr->finder_x + w
        && plyr->finder_y - h <= *ty && *ty <= plyr->finder_y + h;
}

// 1 when tpos is in the view (fov / range from the camera) and inside the
// frame; dist: its screen distance from the frame centre.
// FF3 0x00234ec0 (FF3-only).

int PlyrFinderTargetCheck(float fov, float range, float *tpos, float *dist)
{
    float x = 0.0f, y = 0.0f;           // FF3 reads them unset when the view check fails
    int   in = 0;

    if (OutSightChk(tpos, gra3dcamGetPosition(), ingame_wrk.plyr_wrk->rot[1], fov, range) == 0
        && FrameInsideChk(tpos, &x, &y) != 0)
        in = 1;
    *dist = GetLength2D(ingame_wrk.plyr_wrk->finder_x - x, ingame_wrk.plyr_wrk->finder_y - y);
    return in;
}

// FF3 0x002346d8 (FF3-only; no direct callers).

u_char PlyrEneViewCheck(float dist, float fov)
{
    int view = 0;
    int close = 0;
    int i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if (ene->state != 3 || ene->hp == 0)
            continue;
        view += CheckEneView(ingame_wrk.plyr_wrk->rot[1], 9999.0f, fov, ingame_wrk.plyr_wrk->pos, ene->pos);
        close += ene->dist <= dist;
    }
    return (u_char)((view != 0) | (close != 0 ? 2 : 0));
}

// FF3 0x00234620 (FF3-only).

int PlyrIsHeldBy(ENE_WRK *ene)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (plyr->status != 4 || !(plyr->status_time > 0.0f))
        return 0;
    if (ene == nullptr)
        return 1;
    return plyr->ene_no == ene->alg.idx;
}

// The hit camera by attacker kind: three camera values, the point (between
// the two, or the player for kinds 9 and 12) and dmg_time.
// FF3 0x00234ac8 (FF3-only).

void PlyrDamageCameraReq(ENE_WRK *ene)
{
    sceVu0FVECTOR mid;
    float        *at = mid;
    float         a, b, c, t;

    GetMidPos(mid, ingame_wrk.plyr_wrk->pos, ene->pos);
    switch (ingame_wrk.plyr_wrk->_147)
    {
    case 0: case 3: case 5: case 15: a = 25.0f; b = 40.0f; c = 28.0f; t = 0.0f;   break;
    case 1: case 4: case 10:         a = 12.8f; b = 40.0f; c = 32.0f; t = 0.0f;   break;
    case 2: case 14:                 a = 20.0f; b = 50.0f; c = 40.0f; t = 135.0f; break;
    case 6:                          a = 20.0f; b = 50.0f; c = 50.0f; t = 80.0f;  break;
    case 7:                          a = 20.0f; b = 50.0f; c = 50.0f; t = 54.0f;  break;
    case 8:                          a = 25.0f; b = 50.0f; c = 40.0f; t = 0.0f;   break;
    case 9:                          a = 28.0f; b = 28.0f; c = 28.0f; t = 60.0f;
        at = ingame_wrk.plyr_wrk->pos;
        break;
    case 11:                         a = 16.0f; b = 60.0f; c = 40.0f; t = 0.0f;   break;
    case 12:                         a = 18.0f; b = 45.0f; c = 45.0f; t = 0.0f;
        at = ingame_wrk.plyr_wrk->pos;
        break;
    default:                            // 13, 16..
        return;
    }
    ingame_wrk.plyr_wrk->dmg_time = t;
    ReqPlyrDamageCameraCtrl(a, b, c, at, ene);
}

// 1 when tpos is straight ahead, falling to 0 at deg off: ahead of the
// player (PlyrGetJointRotY(0x1b) as its facing), of the camera with the
// camera up (the player's facing), or of the camera toward CamStanceGetTarget's
// point in mode 10.
// FF3 0x00235050 (FF3-only).

float PlyrViewRate(float deg, float *tpos)
{
    sceVu0FVECTOR from;
    float         rot[3], rot2[3];
    float         lim = (deg * 3.1415927f) / 180.0f;
    float         a;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_MODE10) != 0)
    {
        UnitGetTrgtRot(gra3dcamGetPosition(), CamStanceGetTarget(), rot2, 2);
        memcpy(from, gra3dcamGetPosition(), sizeof(from));
        UnitGetTrgtRot(from, tpos, rot, 2);
        rot[1] -= rot2[1];
    }
    else if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
    {
        memcpy(from, gra3dcamGetPosition(), sizeof(from));
        UnitGetTrgtRot(from, tpos, rot, 2);
        rot[1] -= ingame_wrk.plyr_wrk->rot[1];
    }
    else
    {
        memcpy(from, ingame_wrk.plyr_wrk->pos, sizeof(from));
        UnitGetTrgtRot(from, tpos, rot, 2);
        rot[1] -= PlyrGetJointRotY(0x1b);
    }
    RotLimitChk(&rot[1]);
    a = fabsf(rot[1]);
    if (a <= lim)
        return (lim - a) / lim;
    return 0.0f;
}

// The same from the camera: against the camera's own direction, or the
// player's facing with the camera up.
// FF3 0x002351e0 (FF3-only).

float PlyrCamViewRate(float deg, float *tpos)
{
    sceVu0FVECTOR from;
    float         rot[3], rot2[3];
    float         lim = (deg * 3.1415927f) / 180.0f;
    float         a;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) == 0)
    {
        UnitGetTrgtRot(gra3dcamGetPosition(), tpos, rot, 2);
        UnitGetTrgtRot(gra3dcamGetPosition(), gra3dcamGetTarget(), rot2, 2);
    }
    else
    {
        memcpy(from, gra3dcamGetPosition(), sizeof(from));
        UnitGetTrgtRot(from, tpos, rot, 2);
        rot2[1] = ingame_wrk.plyr_wrk->rot[1];
    }
    rot[1] -= rot2[1];
    RotLimitChk(&rot[1]);
    a = fabsf(rot[1]);
    if (a <= lim)
        return (lim - a) / lim;
    return 0.0f;
}

// FF3 0x00235318 (FF3-only).

float PlyrDistRate(float dmin, float dmax, float *tpos)
{
    float d = PlyrDistV(ingame_wrk.plyr_wrk->pos, tpos);

    if (d <= dmin)
        return 1.0f;
    if (d < dmax)
        return 1.0f - (d - dmin) / (dmax - dmin);
    return 0.0f;
}

// FF3 0x002353a0 (FF3-only).

float PlyrCamDistRate(float dmin, float dmax, float *tpos)
{
    float d = PlyrDistV(gra3dcamGetPosition(), tpos);

    if (d <= dmin)
        return 1.0f;
    if (dmax <= d)
        return 0.0f;
    return 1.0f - (d - dmin) / (dmax - dmin);
}

// FF3 0x00235458 (FF3-only; Ghidra had it inside 0x2353a0's body, function
// created 2026-10-14; called from 0x228a88).

int PlyrViewDistCheck(float dmin, float dmax, float deg, float *tpos)
{
    int r = 0;

    if (PlyrViewRate(deg, tpos) > 0.0f)
        r = 1;
    if (PlyrDistRate(dmin, dmax, tpos) > 0.0f)
        r |= 2;
    return r;
}

// FF3 0x002354e0 (FF3-only).

float PlyrViewDistRate(float dmin, float dmax, float deg, float *tpos)
{
    float v = PlyrViewRate(deg, tpos);

    return v * PlyrDistRate(dmin, dmax, tpos);
}

// FF3 0x00235548 (FF3-only; the same plus 0).

float PlyrViewDistRate2(float dmin, float dmax, float deg, float *tpos)
{
    float v = PlyrViewRate(deg, tpos);

    return v * PlyrDistRate(dmin, dmax, tpos) + 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// Small accessors (FF3-only).

// FF3 0x002346a0

int PlyrIsMode5Or2000000(void)
{
    return ingame_wrk.plyr_wrk->mode == PLYR_MODE_5 || (ingame_wrk.plyr_wrk->flags2 & 0x2000000) != 0;
}

// FF3 0x002347d0

int PlyrLineHitCheck(float *to)
{
    PLYR_WRK *plyr  = ingame_wrk.plyr_wrk;
    int       room  = MapLoadGetBuffID(GetPlyrAreaNo());
    short     floor = ingame_wrk.plyr_wrk->floor;

    return MapHitLineCheck(plyr->pos, to, room, floor, PlyrIsMode10(ingame_wrk.plyr_wrk));
}

// FF3 0x002349b8 (the matcher had named it FF2 InFinderMode).

int PlyrIsBattle(void)
{
    return (ingame_wrk.plyr_wrk->flags & PLYR_FLG_000020) != 0;
}

// FF3 0x002349f0

void PlyrSetPos(float *pos)
{
    sceVu0FVECTOR d;

    sceVu0SubVector(d, pos, ingame_wrk.plyr_wrk->pos);
    sceVu0AddVector(ingame_wrk.plyr_wrk->listen_pos, ingame_wrk.plyr_wrk->listen_pos, d);
    memcpy(ingame_wrk.plyr_wrk->pos, pos, sizeof(sceVu0FVECTOR));
    PlyrTurnFlagClear();
}

// FF3 0x00234a60

void PlyrSetRot(float *rot)
{
    memcpy(ingame_wrk.plyr_wrk->rot, rot, sizeof(sceVu0FVECTOR));
}

// FF3 0x00234a98 (the matcher had named it FF2 PlayerModeIsFinder).

int PlyrStatusIs(u_char status)
{
    return (u_short)ingame_wrk.plyr_wrk->status == status;
}

// FF3 0x00234ab8 (the matcher had named it FF2 InDamageState).

u_char PlyrGet348(void)
{
    return ingame_wrk.plyr_wrk->_348;
}

// FF3 0x00235c88

int PlyrHasMove320(void)
{
    if (ingame_wrk.plyr_wrk->look_pitch == 0.0f)
        return ingame_wrk.plyr_wrk->look_yaw != 0.0f;
    return 1;
}

// After the player moved: wall hits against the map, the area and floor
// under the new position, the map hit object; hold_move gets the frame's
// move. Not in mode 5 or while a room loads.
// FF3 0x00236158 (FF3-only).

void PlyrMapPosUpdate(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       area, a;

    if (plyr->mode == PLYR_MODE_5 || MapLoadCheckLoadNow() != 0)
        return;

    area = GetPlyrAreaNo();
    memcpy(plyr->hold_move, ingame_wrk.plyr_wrk->pos, sizeof(sceVu0FVECTOR));
    PlyrCameraPlyrPush(ingame_wrk.plyr_wrk->_264, area, ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->pos_old);
    CatPlyrHitCheck(ingame_wrk.plyr_wrk->_264, area, ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->pos_old);
    MapAreaHitCircle(ingame_wrk.plyr_wrk->_264, ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->pos_old, area,
                 ingame_wrk.plyr_wrk->floor, ingame_wrk.plyr_wrk->mh_no);
    a = MapAreaAtPos(ingame_wrk.plyr_wrk->floor, ingame_wrk.plyr_wrk->pos);
    if (a >= 0)
        area = a;
    MapAreaGetHeight(ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->pos, area, 1);
    sceVu0SubVector(plyr->hold_move, ingame_wrk.plyr_wrk->pos, plyr->hold_move);

    plyr = ingame_wrk.plyr_wrk;
    plyr->mh_no = RcbUpdateMhNo(MapLoadGetAreaData(area, 3), plyr->mh_no, plyr->pos, &plyr->floor);
    MapSnapSetFloor(ingame_wrk.plyr_wrk->floor);
    MapAreaNoSet(area);
}

// A map hit object of that kind where the player stands (map kinds 0, 1
// and 3 look it up by room).
// FF3 0x002362b0 (FF3-only).

int PlyrOnMapObj(u_char kind)
{
    RCB_OBJ *list[4];
    int     area = MapLoadGetRoomNoNow();
    int     no   = ingame_wrk.plyr_wrk->mh_no;
    int     k    = MapDoorModeGet();
    int     n, i, on = 0;

    if (k >= 0 && (k < 2 || k == 3))
        no = MapDoorRoomMhGet(MapLoadGetBuffID(area));
    n = RcbGetMotObj(MapLoadGetAreaData(area, 3), no, ingame_wrk.plyr_wrk->pos, list);
    for (i = 0; i < n; i++)
    {
        if (list[i]->prm[1] == kind)
            on = 1;
    }
    return on;
}

// FF3 0x002365e8

void PlyrSet338(void)
{
    ingame_wrk.plyr_wrk->_338 = 90.0f;
}

// ──────────────────────────────────────────────────────────────────────
// Mode 8 and mode 10 (FF3-only).

// Back to mode 0 with the camera up, or once the flash recoil (0x98, 0x82,
// 0x95) is over or another anime runs.
// FF3 0x002372d8

void PlyrMode8Ctrl(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    a    = plyr->anime_no;

    if ((plyr->flags & PLYR_FLG_800000) != 0 || (a != 0x98 && a != 0x82 && a != 0x95)
        || (ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
    {
        (void)GetHudHpWrk();            // FF3 calls it and drops the result
        PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
        ingame_wrk.plyr_wrk->status_time = 0.0f;
    }
}

// FF3 0x00236990

void PlyrMode10Ctrl(PLYR_WRK *plyr)
{
    float t = plyr->mode10_time - PlyrGaugeTimeStep(GetAlbumSaveDisp());

    plyr->mode10_time = (t > 0.0f) ? t : 0.0f;
    if (PlyrMode10AnimeEnd(plyr))
    {
        plyr->flags &= ~(u_long)PLYR_FLG_MODE10;
        PlyrSetMode(PLYR_MODE_NORMAL, plyr);
    }
}

// Player 2's stance: not while hurt, not on map objects 2..4, only on map
// kind 5. Anime 0xa1, 40 frames, the stick drives the d-pad.
// FF3 0x00236a10

int PlyrMode10Start(PLYR_WRK *plyr)
{
    if ((u_char)(ingame_wrk.plyr_wrk->mode - 1) < 4)
        return 0;
    if (PlyrOnMapObj(2) || PlyrOnMapObj(3) || PlyrOnMapObj(4))
        return 0;
    if (MapDoorModeGet() != 5)
        return 0;

    PlyrSetMode(PLYR_MODE_10, plyr);
    plyr->flags |= PLYR_FLG_MODE10;
    SetPlyrAnime(0xa1, 0xff, 0xff, 0xff, 1, 0.0f);
    PlyrTurnFlagClear();
    plyr->mode10_time = 40.0f;
    CamStanceInit(plyr->rot[1]);
    PlyrSet264(7.2f);
    PlyrSePlay(ingame_wrk.plyr_wrk, 5, 100, 0x1000);
    pad_wrk[0].stick_dpad = 1;
    return 1;
}

// Leaves the stance once its time is over (anime 0xa3).
// FF3 0x00236b40

void PlyrMode10End(PLYR_WRK *plyr)
{
    if (plyr->mode10_time > 0.0f || ingame_wrk.plyr_wrk->anime_no == 0xa3)
        return;

    pad_wrk[0].stick_dpad = 0;
    PlyrSet264(4.8f);
    PlyrSePlay(ingame_wrk.plyr_wrk, 5, 100, 0x1000);
    if ((plyr->flags & PLYR_FLG_MODE10) != 0)
    {
        ReqFinderInOverRap(8);
        SetPlyrAnime(0xa3, 0xff, 0xff, 0xff, 1, 0.0f);
    }
}

// FF3 0x00236c60

int PlyrMode10AnimeEnd(PLYR_WRK *plyr)
{
    if (plyr->anime_no != 0xa3)
        return 0;
    return (plyr->flags & PLYR_FLG_ANIME_END) != 0;
}

// FF3 0x00236c90

int PlyrIsMode10(PLYR_WRK *plyr)
{
    return plyr->mode == PLYR_MODE_10;
}

// Drops the stance without its end anime (EneAttackReq, the enemy touch):
// the stick stops driving the d-pad, _264 back to 4.8.
// FF3 0x00236c10 (FF3-only).

void PlyrMode10Cancel(PLYR_WRK *plyr)
{
    pad_wrk[0].stick_dpad = 0;
    PlyrSet264(4.8f);
    plyr->flags &= ~(u_long)PLYR_FLG_MODE10;
}

// The stance's start anime runs (CameraMain).
// FF3 0x00236ca0 (FF3-only).

int PlyrMode10IsStarting(PLYR_WRK *plyr)
{
    if (plyr->mode != PLYR_MODE_10)
        return 0;
    return plyr->anime_no == 0xa1;
}

// ──────────────────────────────────────────────────────────────────────
// The action button per character (FF3-only).

// FF3 0x002374c0

int PlyrCamAble(void)
{
    if (ingame_wrk.plyr_wrk->mode == PLYR_MODE_7 || PlyrGetCurNo() != 1)
        return 0;
    if (PlyrGetEquipLv(2) == 0)
        return 0;
    return GetPhotoModeWrk()->IsFinderReady() != 0;
}

// FF3 0x00237530

int PlyrCamInput(void)
{
    if (PlyrCamAble() == 0)
        return 0;
    return CheckPadTrgPlaySe(pad_key[PAD_KEY_18], -1, 0) != 0;
}

// Per frame (PlayerMain): outside statuses 1 / 3 / 4 and lock 3, the flash
// wait runs down; player 0 flashes (free in mode 0 or with the camera up),
// player 1 uses the camera, player 2 enters / leaves mode 10 (cancel leaves).
// FF3 0x00237570

void PlyrActionCtrl(void)
{
    PLYR_WRK *plyr;
    short     st = ingame_wrk.plyr_wrk->status;
    float     t;

    if (st == 1 || st == 3 || st == 4 || IsLockTblOn(3))
        return;

    t = plyr_flash_wait - 1.0f;
    plyr_flash_wait = (t < 0.0f) ? 0.0f : t;

    switch (PlyrGetCurNo())
    {
    case 0:
        if (PlyrFlashInput() == 0 || plyr_flash_wait > 0.0f)
            break;
        plyr = ingame_wrk.plyr_wrk;
        if ((plyr->mode == PLYR_MODE_NORMAL && (plyr->flags2 & 0x3f800) == 0 && (plyr->flags3 & 0x3f800) == 0
             && (plyr->flags & (PLYR_FLG_400000 | PLYR_FLG_1000000)) == 0)
            || (plyr->flags & PLYR_FLG_800000) != 0)
        {
            PlyrFlashRelease();
            plyr_flash_wait = 43.0f;
        }
        break;

    case 1:
        if (PlyrCamInput() == 0)
            break;
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_100000) == 0)
            PlyrGaugeSlowStart(GetAlbumSaveDisp());
        else
            PlyrGaugeSlowEnd(GetAlbumSaveDisp());
        break;

    case 2:
        if (CheckPadTrgPlaySe(pad_key[PAD_KEY_18], -1, 0) != 0)
        {
            plyr = ingame_wrk.plyr_wrk;
            if ((plyr->mode == PLYR_MODE_NORMAL || plyr->mode == PLYR_MODE_10)
                && (plyr->flags2 & 0x3f800) == 0 && (plyr->flags & PLYR_FLG_HOLD) == 0)
            {
                if (PlyrIsMode10(ingame_wrk.plyr_wrk) == 0)
                    PlyrMode10Start(ingame_wrk.plyr_wrk);
                else
                    PlyrMode10End(ingame_wrk.plyr_wrk);
            }
        }
        else if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) != 0 && PlyrIsMode10(ingame_wrk.plyr_wrk))
        {
            PlyrMode10End(ingame_wrk.plyr_wrk);
        }
        break;

    default:
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The head look, the equipment and the side-step anime (FF3-only).

static u_char *plyr_equip_save;         // sdata 0x371b48 (gp-0x19a8): PlyrEquipBackup's heap block

// Each equipment slot's highest level, sdata 0x371b88 (PlyrEquipLvUp).
static const u_char plyr_equip_lv_max[8] = { 1, 3, 1, 3, 0, 3, 1, 3 };

// Rates by the level of equipment 3 / 4, data 0x360070 / 0x360080.
static const float plyr_equip3_rate[4] = { 0.7f, 0.8f, 0.9f, 1.0f };
static const float plyr_equip4_rate[4] = { 1.0f, 0.95f, 0.9f, 0.85f };

// cur one step towards target, stopping on it.
static float PlyrLookStep(float cur, float target, float step)
{
    if (cur < target)
    {
        cur += step;
        return (cur < target) ? cur : target;
    }
    cur -= step;
    return (target < cur) ? cur : target;
}

// The right stick turns the head (CharPlayerModel::UpdateLight, the player model's frame):
// pitch from the stick's y, yaw from its x, at 90 deg for full tilt (pitch
// -20..40 deg, yaw -30..30), each moving 6 deg a frame towards it; in the
// dead zone (0x30..0xd0) 2 deg a frame back to 0. Not while hurt or held
// (modes 1..4), in modes 5 / 6, in mode 10's stance, in statuses 1 / 3 or
// under locks 2 / 3. FF3 also measures the camera's yaw against the
// player's facing there and throws the result away; kept for the calls.
// FF3 0x002359c8 (FF3-only).

void PlyrLookCtrl(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    x, y;
    float     target, step;
    float     rot[3];

    if ((u_char)(plyr->mode - 1) < 4 || plyr->mode == PLYR_MODE_DOOR || plyr->mode == PLYR_MODE_5)
        return;
    if (IsLockTblOn(2) || IsLockTblOn(3))
        return;
    plyr = ingame_wrk.plyr_wrk;
    if ((plyr->flags & PLYR_FLG_MODE10) != 0 || plyr->status == 1 || plyr->status == 3)
        return;

    step = 0.034906585f;
    GetStickRaw(4, &x, &y);
    if (ReiPan_OverShoulderOwnsMovement())
    {
        // PC: the right stick turns the over-the-shoulder camera; read it as
        // centred so the head eases back instead of looking along with it.
        x = 0x80;
        y = 0x80;
    }

    target = 0.0f;
    if ((u_char)(y - 0x30) > 0xa0)
    {
        target = -(((float)(y - 0x80) * 1.5707964f) / 127.0f);
        step   = 0.10471975f;
        if (target > 0.6981317f)
            target = 0.6981317f;
        else if (target < -0.34906584f)
            target = -0.34906584f;
    }
    ingame_wrk.plyr_wrk->look_pitch = PlyrLookStep(ingame_wrk.plyr_wrk->look_pitch, target, step);

    step   = 0.034906585f;
    target = 0.0f;
    if ((u_char)(x - 0x30) > 0xa0)
    {
        target = ((float)(x - 0x80) * 1.5707964f) / 127.0f;
        step   = 0.10471975f;
        if (target > 0.52359873f)
            target = 0.52359873f;
        else if (target < -0.52359873f)
            target = -0.52359873f;

        UnitGetTrgtRot(ingame_wrk.plyr_wrk->pos, gra3dcamGetPosition(), rot, 2);
        rot[1] -= ingame_wrk.plyr_wrk->rot[1];
        RotLimitChk(&rot[1]);
        (void)fabsf(rot[1]);
    }
    ingame_wrk.plyr_wrk->look_yaw = PlyrLookStep(ingame_wrk.plyr_wrk->look_yaw, target, step);
}

// Keeps the three works' equip[3] in a 9-byte block of the system heap over
// the story game's reload after an extra mission (CExtraMissionShelt::CarryStore).
// FF3 0x00236808 (FF3-only).

void PlyrEquipBackup(void)
{
    int i;

    plyr_equip_save = (u_char *)heapCtrlMalloc(GetSystemHeapWrkP(), 9, 0, 0, 0);
    for (i = 0; i < PLYR_NUM; i++)
        memcpy(plyr_equip_save + i * 3, plyr_wrk[i].equip, 3);
}

// FF3 0x00236890 (FF3-only).

void PlyrEquipRestore(void)
{
    int i;

    if (plyr_equip_save == nullptr)
        return;
    for (i = 0; i < PLYR_NUM; i++)
        memcpy(plyr_wrk[i].equip, plyr_equip_save + i * 3, 3);
    heapCtrlFree(GetSystemHeapWrkP(), plyr_equip_save, 0, 0, 0);
    plyr_equip_save = nullptr;
}

// The slow-mode gauge's fill rate (plyr_gauge.c PlyrGaugeAdd).
// FF3 0x00236cc8 (FF3-only).

float PlyrEquip3Rate(u_char lv)
{
    if (lv == 0xff)
        lv = PlyrGetEquipLv(3);
    return plyr_equip3_rate[lv];
}

// The slow-mode gauge's drain rate (plyr_gauge.c PlyrGaugeSub).
// FF3 0x00236d10 (FF3-only).

float PlyrEquip4Rate(u_char lv)
{
    if (lv == 0xff)
        lv = PlyrGetEquipLv(4);
    return plyr_equip4_rate[lv];
}

// The flash recoil anime runs: 0x98 / 0x95 for player 0, 0x82 while
// PLYR_FLG_10000 is set (roku_pzl.c FUN_00243090).
// FF3 0x00237428 (FF3-only).

int PlyrIsFlashRecoil(void)
{
    if (PlyrGetCurNo() == 0)
    {
        if (ingame_wrk.plyr_wrk->anime_no == 0x98)
            return 1;
        if (ingame_wrk.plyr_wrk->anime_no == 0x95)
            return 1;
    }
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_10000) == 0)
        return 0;
    return ingame_wrk.plyr_wrk->anime_no == 0x82;
}

// FF3 0x002377e8 (FF3-only; FUN_001a17c0).

void PlyrEquipObtain(u_char no)
{
    plyr_equip_lv[no] = 1;
}

// FF3 0x00237800 (FF3-only; no direct callers).

void PlyrEquipLvUp(u_char no)
{
    u_int lv = plyr_equip_lv[no] + 1u;

    plyr_equip_lv[no] = (u_char)((lv <= plyr_equip_lv_max[no]) ? lv : plyr_equip_lv_max[no]);
}

// flags2's direction bits -> side-step / turn anime, data 0x360c98: 'anime'
// normally, 'from_idle' when the current anime is 0 (the 0x80 rows start
// from the step-off anime instead).
struct PLYR_ANIME_DIR { u_long mask; u_long anime; u_long from_idle; };
static_assert(sizeof(PLYR_ANIME_DIR) == 0x18, "PLYR_ANIME_DIR is 0x18 bytes (EE 64-bit words)");
static const PLYR_ANIME_DIR plyr_anime_dir_tbl[] =
{
    { 0x051, 0x32, 0x32 }, { 0x091, 0x33, 0x32 }, { 0x111, 0x34, 0x34 },
    { 0x061, 0x35, 0x35 }, { 0x0a1, 0x36, 0x35 }, { 0x121, 0x37, 0x37 },
    { 0x052, 0x26, 0x26 }, { 0x092, 0x27, 0x26 }, { 0x112, 0x28, 0x28 },
    { 0x062, 0x29, 0x29 }, { 0x0a2, 0x2a, 0x29 }, { 0x122, 0x2b, 0x2b },
    { 0xffffffff, 0, 0 },
};

// A looping side-step anime that has reached its loop point (the model's
// anime word 0) goes on to its stop anime: 'mid' in the middle of the step
// (1..2), 'edge' at its ends; else it keeps running.
static int PlyrDirAnimeLoop(int mid_step, int mid, int edge)
{
    if (static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeFlag(0) == 0)
        return ingame_wrk.plyr_wrk->anime_no;
    return mid_step ? mid : edge;
}

// The side-step / turn anime for flags2's direction bits (0x10 / 0x20 /
// 0x40 / 0x80 / 0x100 with 1 or 2), following the walk step: start anime
// (0x26, 0x29, .., 0x3b) lead into their loops (+1), loops into their stops
// once the model reaches the loop point, and the start / stop anime
// (0x26..0x3d less the loops) are held to their end unless flags2 0x200.
// -1: none. Called by roku_pzl.c PlyrMapDirAnimeCtrl.
// FF3 0x002382c0 (FF3-only; checked against the ROM with tools/reverse/mipsemu.py).
// callcheck: GetAnimeFlag (GameAnimeModel::GetAnimeFlag, through PlyrDirAnimeLoop)

int PlyrDirAnimeNo(void)
{
    u_long f2  = ingame_wrk.plyr_wrk->flags2;
    int    res = -1;
    int    i;
    u_char step, cur;
    int    mid;

    for (i = 0; plyr_anime_dir_tbl[i].mask != 0xffffffff; i++)
    {
        if ((f2 & plyr_anime_dir_tbl[i].mask) == plyr_anime_dir_tbl[i].mask)
            break;
    }
    if (plyr_anime_dir_tbl[i].mask != 0xffffffff)
        res = (int)((ingame_wrk.plyr_wrk->anime_no == 0) ? plyr_anime_dir_tbl[i].from_idle : plyr_anime_dir_tbl[i].anime);

    // FF3 calls the model's vtable +0x60 (CharPlayerModel: 0x146640)
    step = (u_char)static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();
    mid  = (u_char)(step - 1) < 2;
    cur  = ingame_wrk.plyr_wrk->anime_no;

    if ((ingame_wrk.plyr_wrk->flags2 & 0xf) != 0)
    {
        // Still moving: a loop that would stop takes the stop of the step.
        switch (cur)
        {
        case 0x27: if (res == 0x28 && mid) res = 0x2e; break;
        case 0x2a: if (res == 0x2b && mid) res = 0x31; break;
        case 0x2d: if (res == 0x28 && !mid) res = 0x2e; break;
        case 0x30: if (res == 0x2b && !mid) res = 0x31; break;
        case 0x33: if (res == 0x34 && mid) res = 0x3a; break;
        case 0x36: if (res == 0x37 && mid) res = 0x3d; break;
        case 0x39: if (res == 0x34 && !mid) res = 0x3a; break;
        case 0x3c: if (res == 0x37 && !mid) res = 0x3d; break;
        default: break;
        }
    }
    else
    {
        switch (cur)
        {
        case 0x26: res = 0x27; break;
        case 0x29: res = 0x2a; break;
        case 0x2c: res = 0x2d; break;
        case 0x2f: res = 0x30; break;
        case 0x32: res = 0x33; break;
        case 0x35: res = 0x36; break;
        case 0x38: res = 0x39; break;
        case 0x3b: res = 0x3c; break;
        case 0x27: res = PlyrDirAnimeLoop(mid, 0x2e, 0x28); break;
        case 0x2a: res = PlyrDirAnimeLoop(mid, 0x31, 0x2b); break;
        case 0x2d: res = PlyrDirAnimeLoop(mid, 0x28, 0x2e); break;
        case 0x30: res = PlyrDirAnimeLoop(mid, 0x2b, 0x31); break;
        case 0x33: res = PlyrDirAnimeLoop(mid, 0x3a, 0x34); break;
        case 0x36: res = PlyrDirAnimeLoop(mid, 0x3d, 0x37); break;
        case 0x39: res = PlyrDirAnimeLoop(mid, 0x34, 0x3a); break;
        case 0x3c: res = PlyrDirAnimeLoop(mid, 0x37, 0x3d); break;
        default: break;
        }
    }

    // From a walk (5; 6 / 0x15 at step 0..1) the side-step starts at its
    // second start anime; a start anime that ended goes on to its loop.
    switch (cur)
    {
    case 0x06: case 0x15:
        if (step >= 2)
            break;
        // fall through
    case 0x05:
        if (res == 0x32)
            res = 0x38;
        else if (res == 0x26)
            res = 0x2c;
        else if (res == 0x29)
            res = 0x2f;
        else if (res == 0x35)
            res = 0x3b;
        break;
    case 0x2c: case 0x2f: case 0x38: case 0x3b:
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
            res = cur + 1;
        break;
    default:
        break;
    }

    // Keep a loop already running from its other start.
    switch (res)
    {
    case 0x27: if (cur == 0x2d) res = 0x2d; break;
    case 0x2a: if (cur == 0x30) res = 0x30; break;
    case 0x33: if (cur == 0x39) res = 0x39; break;
    case 0x36: if (cur == 0x3c) res = 0x3c; break;
    case 0x28: case 0x2b: case 0x34: case 0x37:
        if (mid)
        {
            if (cur == 0x34)
                res = 0x3a;
            if (cur == 0x37)
                res = 0x3d;
            if (cur == 0x28)
                res = 0x2e;
            if (cur == 0x2b)
                res = 0x31;
        }
        break;
    default:
        break;
    }

    if ((ingame_wrk.plyr_wrk->flags2 & 0x200) == 0)
    {
        switch (cur)
        {
        case 0x26: case 0x28: case 0x29: case 0x2b: case 0x2c: case 0x2e: case 0x2f: case 0x31:
        case 0x32: case 0x34: case 0x35: case 0x37: case 0x38: case 0x3a: case 0x3b: case 0x3d:
            if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
                res = cur;
            break;
        default:
            break;
        }
    }
    return res;
}

// The current anime's length in frames (GameAnimeModel::GetAnimeLength; no
// direct callers).
// FF3 0x002393d0 (FF3-only).

int PlyrAnimeInfo(void)
{
    return static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeLength(ingame_wrk.plyr_wrk->anime_no);
}

// The current anime turns the neck itself: 5, 7, 8, 0xb, 0xd, 0xe, 0x10,
// 0x12..0x14, 0x91, 0x93, 0x94 (plyr_sub.c PlyrTankMoveCtrl). FF2 took the
// anime as an argument and returned 1 outside 0x32..0x3b.
// FF3 0x002393f8; FF2 CheckPlayerNeckSW (FF3 lists the anime; checked against
// the ROM with tools/reverse/mipsemu.py).

int CheckPlayerNeckSW(void)
{
    switch (ingame_wrk.plyr_wrk->anime_no)
    {
    case 0x05: case 0x07: case 0x08: case 0x0b: case 0x0d: case 0x0e: case 0x10:
    case 0x12: case 0x13: case 0x14: case 0x91: case 0x93: case 0x94:
        return 1;
    default:
        return 0;
    }
}

// ---- stubs (tools/reverse/stubgen.py) ----
