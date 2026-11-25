// ingame/plyr/plyr_sub.c -- the player continued (FF3 0x23e208..0x243898,
// pinned 2026-10-14; inferred as ingame/puzzle/roku/roku_pzl.c, which it is
// not: FF3-only player code, named for what it holds).
//
// All of it is reconstructed: the ladder (mode 5), the costume / accessory
// accessors and the character tables, the player models (create, change,
// ready, set-up, draw with the accessory fades, release, the per-frame model
// update, joints), the map-object flags, the crawl, the camera-relative and
// the tank control, the viewfinder's turning and point, the stick and d-pad
// readers, the move speed, the turns, the frame's move (PlyrMoveUpdate) and
// the model's fade envelope.
//
// The player models are C++ objects in FF3 (CharPlayerModel 0x1c0 bytes,
// AcsModel 0x28, both GameModel; tools/reverse/rtti.py). Their constructors
// and methods are not reconstructed yet, so this file calls the FF3 methods
// by address where FF3 calls through the vtable (the class is always known
// here), allocating with the game's operator_new as FF3 does.

#include <math.h>                       // fabsf, sqrtf
#include <new>                          // placement new on FF3's C++ heap (operator_new)

#include "common/reipan_stub.h"
#include "ingame/plyr/plyr_sub.h"

#include "ingame/plyr/plyr_gauge.h"       // PlyrGaugeTimeStep, PlyrGaugeSeStop
#include "common/cpp_heap.h"            // operator_new, operator_delete
#include "common/vu0math.h"             // GetTrgtRot, RotLimitChk, RotVectorZXY, g3dAtan2f, Vu0Sinf / Cosf
#include "ingame/event/prg/ev_ladder.h"  // GetLadderList, LadderList::GetData
#include "ingame/enemy/ene_main.h"     // ene_wrk (the enemy work)
#include "graphics/motion/motion.h"     // MDL_MOT
#include "graphics/graph3d/gra3d.h"     // gra3dGetCamera, gra3dcamSetFov / Position / Target, gra3dApplyCamera
#include "graphics/graph3d/sgd_types.h" // SGDCOORDINATE
#include "ingame/camera/map_camera.h"   // ReqFinderInOverRap
#include "ingame/char_model_sub.h"     // CharDrawFind, CharDrawDelete, CharDrawSetPlayer
#include "ingame/enemy/enemy.h"         // EneSndSlowSet, EneReleaseKind5f
#include "ingame/ingame.h"              // GameModel, CharPlayerModel
#include "ingame/ingame_wrk.h"          // ingame_wrk
#include "ingame/map/foot_se.h"         // FootSePlayId (the crawl step)
#include "ingame/map/MapLoad.h"         // MapLoadGetRoomNoNow, MapLoadGetBuffID, MapLoadGetAreaData, MapLoadGetFreeMemAddr, MapLoadGetRoomData
#include "ingame/map/MapRcb.h"          // RCB_OBJ, RcbGetMotObj, RcbGetFloor
#include "ingame/photo/finder_photo.h"  // GetAlbumSaveDisp, GetPhotoModeWrk1e8, GetPhotoModeWrk, PhotoModeGetMode
#include "ingame/photo/fndr_parts.h"    // PhotoWrk1e8SeStop
#include "ingame/plyr/player.h"
#include "ingame/plyr/plyr_mdl.h"       // PlyrFinderStartCheck, PlyrFinderModeCancel, PlyrLookReset, PlyrLookMain
#include "ingame/sound/snd_ctrl.h"      // SndSePlay, SndSeIsPlaying, SndSeFadeStop, SndSeBankLoad / IsEntry / Release
#include "ingame/plyr/acs_model.h"      // AcsModel*
#include "sdk/libvu0.h"                 // sceVu0CopyVector / AddVector / SubVector / ScaleVector
#include "outgame/option.h"             // GetOptionWrk
#include "ingame/plyr/unit_ctl.h"        // GetStickRaw, GetLength2D, GetStickDir8, GetStickRot, GetDirTrg, ConvertRot2Dir, GetMidPos
#include "system/eeiop/cd_file_dat.h"   // the model / motion / SE bank file numbers
#include "system/os/system.h"           // GetFrameTimeScale
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // CheckPadNowPlaySe, CheckPadTrgPlaySe, pad_key
#include "ingame/story_menu.h"  // IsLockTblOn
#include "ingame/ingame_effect.h"     // IgEffectPlyrPutSet / IgEffectPlyrPutDelete (the player map-put)
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera
#include "reipan/io/reipan_game_mouse.h"   // PC: the mouse aims the Camera Obscura
#include "reipan/io/reipan_gyro.h"         // PC: and so does a gamepad's gyro

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@001c7c18


// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// Per character: 0 Rei, 1 Miku, 2 Kei. data 0x361448
static const PLYR_MDL_DAT plyr_mdl_dat[PLYR_NUM] =
{
    { 0, 0, SYS_REI_BD_BD },
    { 1, 1, SYS_MIKU_BD_BD },
    { 2, 2, SYS_KEI_BD_BD },
};

// The motion file by character and PlyrMdlCreate's mot (words; FF3 reads the
// low half). data 0x361578
static const u_int plyr_mot_file[PLYR_NUM][2] =
{
    { CH000_REAL_PK4, CH000_LIGHT_PK4 },
    { CH001_DEFAULT_PK4, CH001_DEFAULT_PK4 },
    { CH002_DEFAULT_PK4, CH002_DEFAULT_PK4 },
};

// A row of a character's model table holds n0 kind-0 entries, n1 kind-1
// entries and n2 kind-2 variants; the row is the costume. sdata 0x371c80,
// 0x371c88, 0x371c90 (gp-0x1870, -0x1868, -0x1860)
static const u_char plyr_costume_n0[PLYR_NUM] = { 1, 1, 1 };
static const u_char plyr_costume_n1[PLYR_NUM] = { 1, 1, 1 };
static const u_char plyr_costume_n2[PLYR_NUM] = { 4, 2, 2 };

// data 0x361460: Rei, 5 rows of 6
static const PLYR_COSTUME_MDL plyr_costume_mdl0[5 * 6] =
{
    { CH003_PK4, CH000_SHP }, { CH006_PK4, CH006_SHP }, { CH000_PK4, CH000_SHP }, { CH000_00_PK4, CH000_SHP }, { CH000_01_PK4, CH000_SHP }, { CH000_02_PK4, CH000_SHP },
    { CH200_PK4, CH000_SHP }, { CH006_PK4, CH006_SHP }, { CH200_PK4, CH000_SHP }, { CH200_00_PK4, CH000_SHP }, { CH200_01_PK4, CH000_SHP }, { CH200_02_PK4, CH000_SHP },
    { CH201_PK4, CH000_SHP }, { CH006_PK4, CH006_SHP }, { CH201_PK4, CH000_SHP }, { CH201_00_PK4, CH000_SHP }, { CH201_01_PK4, CH000_SHP }, { CH201_02_PK4, CH000_SHP },
    { CH202_PK4, CH000_SHP }, { CH006_PK4, CH006_SHP }, { CH202_PK4, CH000_SHP }, { CH202_00_PK4, CH000_SHP }, { CH202_01_PK4, CH000_SHP }, { CH202_02_PK4, CH000_SHP },
    { CH203_PK4, CH000_SHP }, { CH006_PK4, CH006_SHP }, { CH203_PK4, CH000_SHP }, { CH203_00_PK4, CH000_SHP }, { CH203_01_PK4, CH000_SHP }, { CH203_02_PK4, CH000_SHP },
};

// data 0x3614d8: Miku, 5 rows of 4
static const PLYR_COSTUME_MDL plyr_costume_mdl1[5 * 4] =
{
    { CH004_PK4, CH001_SHP }, { CH007_PK4, CH007_SHP }, { CH001_PK4, CH001_SHP }, { CH001_00_PK4, CH001_SHP },
    { CH210_PK4, CH001_SHP }, { CH007_PK4, CH007_SHP }, { CH210_PK4, CH001_SHP }, { CH210_00_PK4, CH001_SHP },
    { CH211_PK4, CH001_SHP }, { CH007_PK4, CH007_SHP }, { CH211_PK4, CH001_SHP }, { CH211_00_PK4, CH001_SHP },
    { CH212_PK4, CH001_SHP }, { CH007_PK4, CH007_SHP }, { CH212_PK4, CH001_SHP }, { CH212_00_PK4, CH001_SHP },
    { CH213_PK4, CH001_SHP }, { CH007_PK4, CH007_SHP }, { CH213_PK4, CH001_SHP }, { CH213_00_PK4, CH001_SHP },
};

// data 0x361528: Kei, 5 rows of 4 (rows 3 and 4 repeat row 2)
static const PLYR_COSTUME_MDL plyr_costume_mdl2[5 * 4] =
{
    { CH005_PK4, CH002_SHP }, { CH005_PK4, CH002_SHP }, { CH002_PK4, CH002_SHP }, { CH002_00_PK4, CH002_SHP },
    { CH220_PK4, CH002_SHP }, { CH005_PK4, CH002_SHP }, { CH220_PK4, CH002_SHP }, { CH220_00_PK4, CH002_SHP },
    { CH221_PK4, CH002_SHP }, { CH005_PK4, CH002_SHP }, { CH221_PK4, CH002_SHP }, { CH221_00_PK4, CH002_SHP },
    { CH221_PK4, CH002_SHP }, { CH005_PK4, CH002_SHP }, { CH221_PK4, CH002_SHP }, { CH221_00_PK4, CH002_SHP },
    { CH221_PK4, CH002_SHP }, { CH005_PK4, CH002_SHP }, { CH221_PK4, CH002_SHP }, { CH221_00_PK4, CH002_SHP },
};

// data 0x330ae0
static const PLYR_COSTUME_MDL *const plyr_costume_mdl[PLYR_NUM] =
{
    plyr_costume_mdl0, plyr_costume_mdl1, plyr_costume_mdl2,
};

// The first accessory AcsModel number per character (A000_PK4 + n). rodata
// 0x361428, copied to the stack by PlyrGetAcsModelNo.
static const int plyr_acs_base[PLYR_NUM] = { 9, 13, 17 };

static float plyr_crawl_se_dist;        /* sdata 0x371cb0 (gp-0x1840) */
static float plyr_stick_angle = -1.0f;  /* sdata 0x371d80 (gp-0x1770) */
static int   plyr_mdl_step_old;         /* sbss 0x373844 (gp+0x354): PlyrMoveSpeedCtrl's model step; no reader in the ROM */

// ══════════════════════════════════════════════════════════════════════
// The ladder (mode 5)
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Climbs ladder no from its foot: only from mode 0 without turn bits. The
// step count is (height - 6) / 2; anime 0x5f gets on.
// FF3 0x0023e208 (not a Ghidra function before 2026-10-14; called from 0x1a24dc).

void PlyrLadderStartBottom(u_char no)
{
    PLYR_WRK   *plyr = ingame_wrk.plyr_wrk;
    LADDER_DAT *lad;

    plyr->ladder = GetLadderList()->GetData(no);
    lad = ingame_wrk.plyr_wrk->ladder;
    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_NORMAL
        || (ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0
        || (ingame_wrk.plyr_wrk->flags3 & 0x3f800) != 0)
        return;

    PlyrTurnFlagClear();
    PlyrSetMode(PLYR_MODE_5, nullptr);
    PlyrSet264(0.0f);
    PlyrSetPos(lad->pos_bottom);
    PlyrSetRot(lad->rot_bottom);
    ingame_wrk.plyr_wrk->ladder_top  = (u_char)((lad->height - 6) / 2);
    ingame_wrk.plyr_wrk->ladder_step = 0;
    SetPlyrAnime(0x5f, 0, 0xff, 0xff, 1, 0.0f);
    ReqFinderInOverRap(8);
}

// ──────────────────────────────────────────────────────────────────────
// The same from the top: the player starts on the last step, anime 0x62.
// FF3 0x0023e330

void PlyrLadderStartTop(u_char no)
{
    PLYR_WRK   *plyr = ingame_wrk.plyr_wrk;
    LADDER_DAT *lad;
    u_char      top;

    plyr->ladder = GetLadderList()->GetData(no);
    lad = ingame_wrk.plyr_wrk->ladder;
    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_NORMAL
        || (ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0
        || (ingame_wrk.plyr_wrk->flags3 & 0x3f800) != 0)
        return;

    PlyrTurnFlagClear();
    PlyrSetMode(PLYR_MODE_5, nullptr);
    PlyrSet264(0.0f);
    PlyrSetPos(lad->pos_top);
    PlyrSetRot(lad->rot_top);
    top = (u_char)((lad->height - 6) / 2);
    ingame_wrk.plyr_wrk->ladder_top  = top;
    ingame_wrk.plyr_wrk->ladder_step = top;
    SetPlyrAnime(0x62, 0, 0xff, 0xff, 1, 0.0f);
    ReqFinderInOverRap(8);
}

// ──────────────────────────────────────────────────────────────────────
// Off the ladder: back to mode 0. The caller passes the arrival's map hit,
// which FF3 does not use here (PlyrLadderCtrl set it when the get-off anime
// began).
// FF3 0x0023e450

void PlyrLadderEnd(u_char mh_no)
{
    (void)mh_no;
    PlyrSet264(4.8f);
    PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
}

// ──────────────────────────────────────────────────────────────────────
// The ladder camera: camera a below the switch step, b above; a target flag
// of 1 looks at the player's point _180 instead.
// FF3 0x0023e480

void PlyrLadderCamera(void)
{
    LADDER_DAT *lad = ingame_wrk.plyr_wrk->ladder;
    float      *tgt;
    u_char      at_plyr;

    if (ingame_wrk.plyr_wrk->ladder_step < (lad->cam_switch - 2) / 2)
    {
        gra3dcamSetPosition(lad->cam_a_pos);
        at_plyr = lad->cam_a_plyr;
        tgt     = lad->cam_a_tgt;
    }
    else
    {
        gra3dcamSetPosition(lad->cam_b_pos);
        at_plyr = lad->cam_b_plyr;
        tgt     = lad->cam_b_tgt;
    }
    if (at_plyr == 1)
        tgt = ingame_wrk.plyr_wrk->_180;
    gra3dcamSetTarget(tgt, 1);
    gra3dApplyCamera(nullptr, 1, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Mode 5, every frame. Up / down come from the stick (option b11 == 0, the
// four-way ConvertRot2Dir of PlyrGetStickAngle once it reports one) or the d-pad
// (0 up, 2 down). On the rest anime 0x5e a step starts (0x60 up, 0x63 down)
// or, at either end, the get-off anime (0x61 at the top, 0x64 at the foot),
// which also moves the player's map hit. When a step anime ends the count
// moves and the next one starts at once if the input holds; when a get-off
// anime ends, PlyrLadderEnd.
// FF3 0x0023e540

void PlyrLadderCtrl(void)
{
    LADDER_DAT *lad = ingame_wrk.plyr_wrk->ladder;
    s_char      dir = -1;
    u_char      anime;

    if (GetOptionWrk()->now.b11 == 0)
    {
        float ang = PlyrGetStickAngle();

        if (ang != 10.0f)
            dir = (s_char)ConvertRot2Dir(ang, 0);
    }
    else
        dir = (s_char)(PlyrGetDpadDir(0) >> 1);

    anime = ingame_wrk.plyr_wrk->anime_no;
    if (anime == 0x5e)
    {
        if (dir == 0)
        {
            if (ingame_wrk.plyr_wrk->ladder_step < ingame_wrk.plyr_wrk->ladder_top)
            {
                SetPlyrAnime(0x60, 0xff, 0xff, 0xff, 1, 0.0f);
                return;
            }
            SetPlyrAnime(0x61, 0xff, 0xff, 0xff, 1, 0.0f);
            PlyrSetMhNo(lad->mh_top);
            return;
        }
        if (dir != 2)
            return;
        if (ingame_wrk.plyr_wrk->ladder_step != 0)
        {
            SetPlyrAnime(0x63, 0xff, 0xff, 0xff, 1, 0.0f);
            return;
        }
        SetPlyrAnime(0x64, 0xff, 0xff, 0xff, 1, 0.0f);
        PlyrSetMhNo(lad->mh_bottom);
        return;
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
        return;
    if (anime == 0x61)
    {
        PlyrLadderEnd(lad->mh_top);
        return;
    }
    if (anime == 0x64)
    {
        PlyrLadderEnd(lad->mh_bottom);
        return;
    }
    if (anime == 0x60)
        ingame_wrk.plyr_wrk->ladder_step++;
    else if (anime == 0x63)
        ingame_wrk.plyr_wrk->ladder_step--;

    if (dir != 0)
    {
        if (dir != 2)
        {
            SetPlyrAnime(0x5e, 0xff, 0xff, 0xff, 1, 0.0f);
            return;
        }
        if (ingame_wrk.plyr_wrk->ladder_step != 0)
        {
            SetPlyrAnime(0x63, 0xff, 0xff, 0xff, 1, 0.0f);
            return;
        }
        SetPlyrAnime(0x64, 0xff, 0xff, 0xff, 1, 0.0f);
        PlyrSetMhNo(lad->mh_bottom);
        return;
    }
    if (ingame_wrk.plyr_wrk->ladder_step < ingame_wrk.plyr_wrk->ladder_top)
    {
        SetPlyrAnime(0x60, 0xff, 0xff, 0xff, 1, 0.0f);
        return;
    }
    SetPlyrAnime(0x61, 0xff, 0xff, 0xff, 1, 0.0f);
    PlyrSetMhNo(lad->mh_top);
}

// ──────────────────────────────────────────────────────────────────────
// PlayerMain's mode 5.
// FF3 0x0023e788

void PlyrMode5Ctrl(void)
{
    PlyrLadderCtrl();
    PlyrLadderCamera();
}

// ──────────────────────────────────────────────────────────────────────
// The player model's turn callback (PlyrMdlSetup hands it to the motion
// control): a turn of 30 degrees or more starts a 10-frame ease of the
// facing from rot.y + rot + turn[1] towards rot_target[1].
// FF3 0x0023e7b0
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

void FUN_0023e7b0(float rot, void *arg)
{
    PLYR_WRK *plyr;

    // FF3 compares fabs((double)rot) with (double)0.5235987f
    if (fabsf(rot) < 0.523598731f)
        return;

    plyr = ingame_wrk.plyr_wrk;
    plyr->turn_cb[1] = rot;
    RotLimitChk(&plyr->turn_cb[1]);
    plyr->ease_time = 10.0f;
    plyr->ease_dir  = ingame_wrk.plyr_wrk->rot_target[1];
    plyr->ease_from = ingame_wrk.plyr_wrk->rot[1] + plyr->turn_cb[1] + plyr->turn[1];
    RotLimitChk(&plyr->ease_dir);
}

// ══════════════════════════════════════════════════════════════════════
// Costume and accessory
// ══════════════════════════════════════════════════════════════════════

// FF3 0x0023e880
void PlyrSetCostume(s_char no, u_char v)
{
    if (no == -1)
        PlyrGetCurWrk()->costume = v;
    else
        GetPlyrWrk(no)->costume = v;
}

// FF3 0x0023e8d8
u_char PlyrGetCostume(s_char no)
{
    if (no == -1)
        return PlyrGetCurWrk()->costume;
    return GetPlyrWrk(no)->costume;
}

// FF3 0x0023e920
void PlyrSetAcs(s_char no, u_char v)
{
    if (no == -1)
        PlyrGetCurWrk()->acs = v;
    else
        GetPlyrWrk(no)->acs = v;
}

// FF3 0x0023e978
u_char PlyrGetAcs(s_char no)
{
    if (no == -1)
        return PlyrGetCurWrk()->acs;
    return GetPlyrWrk(no)->acs;
}

// ──────────────────────────────────────────────────────────────────────
// The AcsModel number of the player's accessory (A000_PK4 + n), -1: none.
// FF3 0x0023e9c0

int PlyrGetAcsModelNo(u_char no)
{
    PLYR_WRK *plyr = GetPlyrWrk(no);
    int       base[PLYR_NUM];

    base[0] = plyr_acs_base[0];
    base[1] = plyr_acs_base[1];
    base[2] = plyr_acs_base[2];
    if (plyr->acs == 0)
        return -1;
    return base[no] + plyr->acs - 1;
}

// FF3 0x0023ea38
void PlyrSetCostumeSub(s_char no, u_char v)
{
    if (no == -1)
        PlyrGetCurWrk()->costume_sub = v;
    else
        GetPlyrWrk(no)->costume_sub = v;
}

// FF3 0x0023ea90
u_char PlyrGetCostumeSub(s_char no)
{
    if (no == -1)
        return PlyrGetCurWrk()->costume_sub;
    return GetPlyrWrk(no)->costume_sub;
}

// ──────────────────────────────────────────────────────────────────────
// The model table entry for kind (PlyrMdlCreate's 2nd argument): kind 0 the
// row's first entry, 1 its kind-1 entry, 2 the costume_sub-th variant;
// other kinds the table's first entry.
// FF3 0x0023ead8

const PLYR_COSTUME_MDL *PlyrGetCostumeMdlDat(u_char no, u_char kind)
{
    u_char costume = PlyrGetCostume(no);
    u_char sub     = PlyrGetCostumeSub(no);
    u_int  n01     = plyr_costume_n0[no] + plyr_costume_n1[no];
    u_int  row     = (plyr_costume_n2[no] + n01) & 0xff;
    u_int  idx     = 0;

    if (kind == 1)
        idx = (row * costume + plyr_costume_n0[no]) & 0xff;
    else if (kind == 0)
        idx = (row * costume) & 0xff;
    else if (kind == 2)
        idx = (row * costume + (n01 & 0xff) + sub) & 0xff;

    return &plyr_costume_mdl[no][idx];
}

// FF3 0x0023ebe8
const PLYR_MDL_DAT *PlyrGetMdlDat(u_char no)
{
    return &plyr_mdl_dat[no];
}

// ══════════════════════════════════════════════════════════════════════
// The player models
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Makes player no's models: the CharPlayerModel from the costume table
// (kind) and the motion file (mot: 0 real, 1 light; 0xff keeps the last),
// three AcsModels (A000..A002) and the accessory's, the fade envelope, and
// loads the character's SE bank. Player 1's 0x100000 flag is dropped first.
// Returns 0 when the model could not start its load (the model is deleted).
// FF3 0x0023eca8

// ──────────────────────────────────────────────────────────────────────
// Swaps the player model: the current player's models go, player no
// becomes current and gets its models of kind (mot -1: keep _2ec). Returns
// PlyrMdlCreate's result (a tail call; PLYR_FLASHLIGHT_SET reads it).
// FF3 0x0023ec08

int PlyrMdlChange(u_char no, u_char kind, s_char mot)
{
    u_char    cur  = PlyrGetCurNo();
    PLYR_WRK *plyr = GetPlyrWrk((s_char)no);

    if (mot == -1)
        mot = (s_char)plyr->_2ec;
    PlyrGetCostumeMdlDat(no, kind);     // FF3 drops the result
    PlyrMdlRelease(cur);
    PlyrSetCurrent(no);
    PlyrFinderModeCancel();
    return PlyrMdlCreate(no, kind, (u_char)mot);
}

// callcheck: PlyrMdlFxInit (PLYR_MDL_FX's constructor on operator_new memory), AcsModelCtor (new AcsModel)
int PlyrMdlCreate(u_char no, u_char kind, u_char mot)
{
    PLYR_WRK               *plyr = GetPlyrWrk(no);
    PLYR_WRK               *p1;
    const PLYR_COSTUME_MDL *cm;
    const PLYR_MDL_DAT     *dat;
    u_short                 mot_file;
    int                     acs;

    if (mot == 0xff)
        mot = plyr->_2ec;

    p1 = GetPlyrWrk(1);
    if ((p1->flags & PLYR_FLG_100000) != 0)
    {
        p1->flags &= ~(u_long)PLYR_FLG_100000;
        EneSndSlowSet(0);
    }

    cm = PlyrGetCostumeMdlDat(no, kind);
    plyr->_2e8 = kind;
    plyr->_2ec = mot;
    mot_file = (u_short)plyr_mot_file[no][mot];
    plyr->mdl_file = cm->mdl;
    plyr->mot_file = mot_file;
    plyr->shp_file = cm->shp;

    // FF3 sets the light-motion flag on the current player, not on plyr
    ingame_wrk.plyr_wrk->flags &= ~(u_long)0x8000;
    if (mot == 1)
        ingame_wrk.plyr_wrk->flags |= 0x8000;

    plyr->look_pitch = 0.0f;
    plyr->look_yaw   = 0.0f;

    plyr->mdl = new (operator_new(sizeof(CharPlayerModel))) CharPlayerModel(plyr);   // FF3: operator new(0x1c0)

    dat = &plyr_mdl_dat[no];
    if (static_cast<CharModel *>(plyr->mdl)->Load(dat->chr, dat->type, plyr->mdl_file, plyr->mot_file, plyr->shp_file) == 0)
    {
        if (plyr->mdl != nullptr)
            { plyr->mdl->~GameModel(); operator_delete(plyr->mdl); }    // FF3: delete through vtable +0x0c
        plyr->_2e4 = 0;
        plyr->mdl  = nullptr;
        return 0;
    }

    // FF3: new AcsModel (operator new 0x28); the host object is bigger
    plyr->acs_mdl[1] = new (operator_new(sizeof(AcsModel))) AcsModel;
    (static_cast<AcsModel *>(plyr->acs_mdl[1]))->Load(1);
    plyr->acs_mdl[2] = new (operator_new(sizeof(AcsModel))) AcsModel;
    (static_cast<AcsModel *>(plyr->acs_mdl[2]))->Load(2);
    plyr->acs_mdl[0] = new (operator_new(sizeof(AcsModel))) AcsModel;
    (static_cast<AcsModel *>(plyr->acs_mdl[0]))->Load(0);

    acs = PlyrGetAcsModelNo(no);
    if (acs >= 0)
    {
        plyr->acs_mdl[3] = new (operator_new(sizeof(AcsModel))) AcsModel;
        (static_cast<AcsModel *>(plyr->acs_mdl[3]))->Load((short)acs);
    }

    plyr->mdl_fx = new (operator_new(sizeof(PLYR_MDL_FX))) PLYR_MDL_FX;
    SndSeBankLoad(dat->se_bank);
    plyr->_2e4 = 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// 1 once all of player no's models are loaded (vtable +0x10); the first
// time, the enemy algorithm and the model's SE bank start and _2e4 becomes 1.
// FF3 0x0023ef10

int PlyrMdlIsReady(s_char no)
{
    PLYR_WRK  *plyr = (no == -1) ? PlyrGetCurWrk() : GetPlyrWrk(no);
    GameModel *last;

    if (plyr->_2e4 == 1)
        return 1;

    // FF3 asks each model through vtable +0x10: CharPlayerModel 0x1d35b8,
    // AcsModel 0x1d2cf0 (GameModel's)
    if (plyr->mdl->IsLoaded() == 0)
        return 0;
    if (plyr->acs_mdl[1]->IsLoaded() == 0)
        return 0;
    if (plyr->acs_mdl[2]->IsLoaded() == 0)
        return 0;
    if (plyr->acs != 0)
    {
        if (plyr->acs_mdl[0]->IsLoaded() == 0)
            return 0;
        last = plyr->acs_mdl[3];
    }
    else
        last = plyr->acs_mdl[0];
    if (last->IsLoaded() == 0)
        return 0;

    CharDrawSetPlayer(plyr);
    PlyrLookReset(plyr);
    IgEffectPlyrPutSet();
    plyr->_2e4 = 1;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Every player with a model is ready (IngameNowLoadingMain and 13 more).
// FF3 0x0023f080

int AllPlyrIsReady(void)
{
    int ready = 0;
    int made  = 0;
    int i;

    for (i = 0; i < PLYR_NUM; i++)
    {
        if (GetPlyrWrk((s_char)i)->mdl != nullptr)
        {
            made++;
            ready += PlyrMdlIsReady((s_char)i);
        }
    }
    return ready == made;
}

// ──────────────────────────────────────────────────────────────────────
// After the load: the model's turn callback, the AcsModels hung on the
// model's coordinates (A001 / A002 on joint 0x13, A000 on 7, the accessory
// on 2), and the anime reset (anime_no 0xff).
// FF3 0x0023f0f8

int PlyrMdlSetup(u_char no)
{
    PLYR_WRK *plyr = GetPlyrWrk((s_char)no);

    static_cast<CharModel *>(plyr->mdl)->SetYawFunc(FUN_0023e7b0, nullptr);
    // FF3 gets the joint indices through vtable +0x34 (CharPlayerModel 0x145528)
    (static_cast<AcsModel *>(plyr->acs_mdl[1]))->SetCoord(&plyr->mdl->GetCoord(0)[plyr->mdl->GetJointCoord(0x13)]);
    (static_cast<AcsModel *>(plyr->acs_mdl[2]))->SetCoord(&plyr->mdl->GetCoord(0)[plyr->mdl->GetJointCoord(0x13)]);
    (static_cast<AcsModel *>(plyr->acs_mdl[0]))->SetCoord(&plyr->mdl->GetCoord(0)[plyr->mdl->GetJointCoord(7)]);
    if (plyr->acs != 0)
        (static_cast<AcsModel *>(plyr->acs_mdl[3]))->SetCoord(&plyr->mdl->GetCoord(0)[plyr->mdl->GetJointCoord(2)]);
    plyr->anime_no = 0xff;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrMdlSetup for every player with a model; 1 when all returned 1.
// FF3 0x0023f240

int AllPlyrMdlIsLoaded(void)
{
    int done = 0;
    int made = 0;
    int i;

    for (i = 0; i < PLYR_NUM; i++)
    {
        if (GetPlyrWrk((s_char)i)->mdl != nullptr)
        {
            made++;
            done += PlyrMdlSetup((u_char)i);
        }
    }
    return done == made;
}

// ──────────────────────────────────────────────────────────────────────
// A fade of AcsModel no (PLYR_WRK +0x280, run by PlyrAcsFadeCtrl): state
// 1, its timer cleared, the start alpha, delay, end alpha and fade frames.
// The door passes (0, 0, 0, 0, 0) / (0, 0, 50, 128, 15), the flash recoil
// (cam, 128, 38, 0, 4).
// FF3 0x0023f2b8

void PlyrSet280Req(int no, float v0, float v1, float v2, float v3)
{
    PLYR_REQ280 *req = &PlyrGetCurWrk()->req280[no];

    req->state  = 1;
    req->val[3] = v3;
    req->val[0] = v0;
    req->val[1] = v1;
    req->val[2] = v2;
    req->time   = 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// The fade of AcsModel no (req280[no], PlyrSet280Req): the start shows or
// hides it at once (alpha speed +-128), then after the delay the alpha
// moves to the end value over the fade time; held there afterwards.
// FF3 0x0023f338

void PlyrAcsFadeCtrl(int no)
{
    PLYR_WRK    *plyr = PlyrGetCurWrk();
    AcsModel    *acs  = static_cast<AcsModel *>(plyr->acs_mdl[no]);
    PLYR_REQ280 *req  = &plyr->req280[no];
    float        step = PlyrGaugeTimeStep(GetAlbumSaveDisp());

    switch (req->state)
    {
    case 1:
        acs->alpha_spd = (req->val[0] == 128.0f) ? 128.0f : -128.0f;
        acs->FadeStep();
        if (req->val[3] <= 0.0f)
            req->state = 4;
        else if (0.0f < req->val[1])
            req->state = 2;
        else
            req->state = 3;
        req->time = 0.0f;
        break;

    case 2:
        req->time += step;
        if (req->val[1] <= req->time)
        {
            req->time  = 0.0f;
            req->state = 3;
        }
        break;

    case 3:
        req->time += step;
        acs->alpha_spd = ((req->val[2] - req->val[0]) / req->val[3]) * step;
        acs->FadeStep();
        if (req->val[3] <= req->time)
        {
            req->time  = 0.0f;
            req->state = 4;
        }
        break;

    case 4:
        acs->alpha_spd = (req->val[2] == 0.0f) ? -128.0f : 128.0f;
        acs->FadeStep();
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The player model may draw: not crawling (flags 0x18000000) and lock 6
// off (PlyrMdlDraw, MapDrawChars).
// FF3 0x0023f4f8

int PlyrMdlDrawAble(void)
{
    if ((ingame_wrk.plyr_wrk->flags & 0x18000000) != 0)
        return 0;
    return IsLockTblOn(6) == 0;
}

// ──────────────────────────────────────────────────────────────────────
// Accessory 2 draws before the body, the others after it (PlyrMdlDraw and
// two callers in other files; no is not used).
// FF3 0x0023f538

int PlyrAcsIsNot2(u_char no, u_char acs)
{
    (void)no;
    return acs != 2;
}

// ──────────────────────────────────────────────────────────────────────
// Draws player plyr's model (pass bit 0x200: the viewfinder's pass): the
// accessory (before the body when it is accessory 2), the body (FF3's
// vtable +0x20 or GameModel::DrawMdl by the camera state), AcsModel 1 / 2 (2 in
// the photo mode) during the camera anime and the flash recoils, AcsModel 0
// with the light motion (flags 0x8000). Nothing while crawling.
// FF3 0x0023f548

void PlyrMdlDraw(PLYR_WRK *plyr, u_int pass)
{
    int     idx   = GetPhotoModeWrk()->GetMode() ? 2 : 1;
    u_char *anime = &ingame_wrk.plyr_wrk->anime_no;
    int     after;
    u_int   sub;
    u_char  a;
    int     draw;

    if (plyr == nullptr || plyr->_2e4 == 0 || plyr->mdl == nullptr)
        return;
    if ((ingame_wrk.plyr_wrk->flags & 0x18000000) != 0)
        return;

    after = PlyrAcsIsNot2(PlyrGetCurNo(), plyr->acs);
    sub   = pass & 0x200;
    if (after == 0 && IsLockTblOn(6) == 0 && plyr->acs != 0
        && ((plyr->flags & PLYR_FLG_800000) == 0 || sub != 0))
    {
        plyr->acs_mdl[3]->UpdateMatrix();
        plyr->acs_mdl[3]->DrawMdl();
    }

    if (PlyrMdlDrawAble())
    {
        if ((plyr->flags & PLYR_FLG_800000) != 0 ? sub != 0 : (ingame_wrk.plyr_wrk->_0e0 & 1) == 0)
            plyr->mdl->DrawMdl();
        else
            plyr->mdl->DrawMdlPre();    // FF3: vtable +0x20 (CharPlayerModel 0x146038)
    }

    if (sub == 0)
        PlyrAcsFadeCtrl(idx);
    if (IsLockTblOn(6) == 0)
    {
        a    = *anime;
        draw = 0;
        if (a == 0x69 || a == 0x6a || a == 0x65 || a == 0x66 || a == 0x67 || a == 0x68
            || a == 0x96 || a == 0x97 || a == 0xad || a == 0xae)
        {
            if ((plyr->flags & PLYR_FLG_800000) == 0 && (ingame_wrk.plyr_wrk->_0e0 & 1) == 0)
                draw = 1;
        }
        if (!draw && (a == 0x82 || a == 0x98 || (sub != 0 && (u_int)(a - 0x18) < 5) || a == 0x95))
            draw = 1;
        if (draw)
        {
            plyr->acs_mdl[idx]->UpdateMatrix();
            plyr->acs_mdl[idx]->DrawMdl();
        }
    }

    if (sub == 0)
        PlyrAcsFadeCtrl(0);
    if ((plyr->flags & PLYR_FLG_800000) == 0 && (ingame_wrk.plyr_wrk->_0e0 & 1) == 0
        && (plyr->flags & 0x8000) != 0 && IsLockTblOn(6) == 0)
    {
        plyr->acs_mdl[0]->UpdateMatrix();
        plyr->acs_mdl[0]->DrawMdl();
    }

    if (after != 0 && IsLockTblOn(6) == 0 && plyr->acs != 0
        && ((plyr->flags & PLYR_FLG_800000) == 0 || sub != 0))
    {
        plyr->acs_mdl[3]->UpdateMatrix();
        plyr->acs_mdl[3]->DrawMdl();
    }
}

// ──────────────────────────────────────────────────────────────────────
// The current player model's per-frame update (PlayerMain's last call):
// skipped with lock 6 or no model in use. Outside the viewfinder and with
// _0e0 bit 0 clear the model also runs CharPlayerModel::FootIk and plyr_mdl.c's
// PlyrLookMain.
// FF3 0x0023f8b0

void PlyrMdlMain(void)
{
    PLYR_WRK  *plyr = ingame_wrk.plyr_wrk;
    GameModel *mdl;

    // PC: the null test first (FF3 reads +0x2e4 before testing plyr)
    if (IsLockTblOn(6) || plyr == nullptr || plyr->_2e4 == 0 || (mdl = plyr->mdl) == nullptr)
        return;

    if (plyr->anime_no == 0x18)
        static_cast<CharPlayerModel *>(mdl)->SetIdleFrame();
    mdl->UpdateMatrix();
    static_cast<CharPlayerModel *>(mdl)->UpdateShapes();
    if ((plyr->flags & PLYR_FLG_800000) == 0 && (ingame_wrk.plyr_wrk->_0e0 & 1) == 0)
    {
        static_cast<CharPlayerModel *>(mdl)->SetLight(1);
        static_cast<CharPlayerModel *>(mdl)->ArmIk();
        static_cast<CharPlayerModel *>(mdl)->FootIk();
        PlyrLookMain();
        return;
    }
    static_cast<CharPlayerModel *>(mdl)->SetLight(0);
    static_cast<CharPlayerModel *>(mdl)->ArmIk();
}

// ──────────────────────────────────────────────────────────────────────
// Deletes player no's models (0xff: the current player's work, but the SE
// bank of character 0xff: FF3 only passes 0..2), its fade envelope, stops
// its SEs and releases its SE bank.
// FF3 0x0023f9c0

int PlyrMdlRelease(u_char no)
{
    PLYR_WRK *plyr = (no == 0xff) ? PlyrGetCurWrk() : GetPlyrWrk((s_char)no);
    short     bank;

    if (plyr->mdl != nullptr)
    {
        if (CharDrawFind(plyr))
        {
            CharDrawDelete(plyr);
            IgEffectPlyrPutDelete();
        }
        if (plyr->mdl != nullptr)
            { plyr->mdl->~GameModel(); operator_delete(plyr->mdl); }    // FF3: delete through vtable +0x0c
        plyr->mdl  = nullptr;
        plyr->_2e4 = 0;
    }

    // FF3: delete through vtable +0x0c (AcsModel 0x2ac9b0; the host's
    // virtual destructor, then the game heap's operator delete)
    if (plyr->acs_mdl[1] != nullptr)
    {
        plyr->acs_mdl[1]->~GameModel();
        operator_delete(plyr->acs_mdl[1]);
        plyr->acs_mdl[1] = nullptr;
    }
    if (plyr->acs_mdl[2] != nullptr)
    {
        plyr->acs_mdl[2]->~GameModel();
        operator_delete(plyr->acs_mdl[2]);
        plyr->acs_mdl[2] = nullptr;
    }
    if (plyr->acs_mdl[0] != nullptr)
    {
        plyr->acs_mdl[0]->~GameModel();
        operator_delete(plyr->acs_mdl[0]);
        plyr->acs_mdl[0] = nullptr;
    }
    if (plyr->acs != 0 && plyr->acs_mdl[3] != nullptr)
    {
        plyr->acs_mdl[3]->~GameModel();
        operator_delete(plyr->acs_mdl[3]);
        plyr->acs_mdl[3] = nullptr;
    }
    if (plyr->mdl_fx != nullptr)
    {
        operator_delete(plyr->mdl_fx);
        plyr->mdl_fx = nullptr;
    }

    PlyrSeStopAll(no);
    bank = plyr_mdl_dat[no].se_bank;
    if (SndSeBankIsEntry(bank))
        SndSeBankRelease(bank, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// IngameEnd: the three players' models.
// FF3 0x0023fb40

void PlyrMdlReleaseAll(void)
{
    int i;

    for (i = 0; i < PLYR_NUM; i++)
        PlyrMdlRelease((u_char)i);
}

// ──────────────────────────────────────────────────────────────────────
// The SEs a player can leave running: the low-HP warning, the crawl loop,
// the album-save notice's and the photo work's loop.
// FF3 0x0023fb78

void PlyrSeStopAll(u_char no)
{
    PlyrDangerSeStop((s_char)no);
    PlyrCrawlSeStop(no);
    PlyrGaugeSeStop(GetAlbumSaveDisp());
    GetPhotoModeWrk1e8()->SeStop();
}

// ──────────────────────────────────────────────────────────────────────
// The angle about y of the current model's joint (0x1b: the facing used by
// PlyrViewRate; 3: PlyrCheckAction's).
// FF3 0x0023fbc8

float PlyrGetJointRotY(int joint)
{
    int coord;

    // PC: no player model while CharModel's load is a stub (FF3 always has one)
    if (ingame_wrk.plyr_wrk->mdl == nullptr)
        return 0.0f;

    // FF3: vtable +0x34 (CharPlayerModel 0x145528)
    coord = ingame_wrk.plyr_wrk->mdl->GetJointCoord(joint);
    return ingame_wrk.plyr_wrk->mdl->GetCoordRot(coord, 1);
}

// ──────────────────────────────────────────────────────────────────────
// The angle about axis of the current model's joint.
// FF3 0x0023fc18

float PlyrGetJointRot(int joint, int axis)
{
    int coord;

    // PC: no player model while CharModel's load is a stub (FF3 always has one)
    if (ingame_wrk.plyr_wrk->mdl == nullptr)
        return 0.0f;

    // FF3: vtable +0x34 (CharPlayerModel 0x145528)
    coord = ingame_wrk.plyr_wrk->mdl->GetJointCoord(joint);
    return ingame_wrk.plyr_wrk->mdl->GetCoordRot(coord, axis);
}

// ──────────────────────────────────────────────────────────────────────
// A point of the current model's joint in the world: in (nullptr: the
// joint's origin (0, 0, 0, 1)) through the joint's coordinate into out.
// Nothing happens for a joint the model does not have.
// FF3 0x0023fc78

void PlyrGetJointPos(int joint, float *out, float *in)
{
    sceVu0FVECTOR origin = { 0.0f, 0.0f, 0.0f, 1.0f };
    int           coord;

    // PC: no player model while CharModel's load is a stub (FF3 always has one)
    if (ingame_wrk.plyr_wrk->mdl == nullptr)
        return;

    // FF3: vtable +0x34 (CharPlayerModel 0x145528) and +0x2c (0x1d2e90)
    coord = ingame_wrk.plyr_wrk->mdl->GetJointCoord(joint);
    if (coord == -1)
        return;
    if (in == nullptr)
        in = origin;
    ingame_wrk.plyr_wrk->mdl->GetCoordPos(out, coord, in);
}

// ──────────────────────────────────────────────────────────────────────
// The character (0 Rei, 1 Miku, 2 Kei) whose model table's first row holds
// model file mdl, -1: none.
// FF3 0x0023fd28

int PlyrGetMdlChr(short mdl)
{
    s_char no;
    u_int  n, i;

    for (no = 0; no < PLYR_NUM; no++)
    {
        n = (plyr_costume_n2[no] + plyr_costume_n0[no] + plyr_costume_n1[no]) & 0xff;
        for (i = 0; i < n; i++)
        {
            if ((short)plyr_costume_mdl[no][i].mdl == mdl)
                return no;
        }
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// A model file of a character's first row turned into the same entry of
// its current costume's row; other files come back unchanged (SceneTestMain).
// FF3 0x0023fdc0 (was matched to FF2 SixPuzzleBookAnim)

short PlyrCostumeMdlConv(short mdl)
{
    s_char no;
    u_int  n, i;

    for (no = 0; no < PLYR_NUM; no++)
    {
        n = (plyr_costume_n2[no] + plyr_costume_n0[no] + plyr_costume_n1[no]) & 0xff;
        for (i = 0; i < n; i++)
        {
            if ((short)plyr_costume_mdl[no][i].mdl == mdl)
                return (short)plyr_costume_mdl[no][i + PlyrGetCostume(no) * n].mdl;
        }
    }
    return mdl;
}

// ──────────────────────────────────────────────────────────────────────
// The current model's motion object, reset through its vtable +0x18 with
// (-1, 1), as IngameEffSlotMdlReset does for the enemy models.
// FF3 0x0023fea0

void PlyrMdlMotReset(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    MDL_MOT  *mot;

    // PC: FF3 reads +0x2e4 / +0x26c of a null player work (low memory) and
    // returns when it is 0; a missing model is not checked there either.
    if (plyr == nullptr || plyr->mdl == nullptr)
        return;
    mot = plyr->mdl->mot;
    if (mot != nullptr)
        mot->ResetCloth(-1, 1);
}

// ══════════════════════════════════════════════════════════════════════
// Map objects, the crawl, moving and turning
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// PlayerMain: the map hit objects of kind 2, 3 and 4 at the player set
// flags2 0x800000, 0x2000000 and 0x400000 while the player is on them
// (entering clears the turn bits and, with the camera up, ends the finder
// with anime 0xf / 0x90 / 9); leaving clears them. During the crawl all
// four bits 0x3c00000 are cleared.
// FF3 0x0023fef8

void PlyrMapObjFlagCtrl(void)
{
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_PZL) != 0)
    {
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3c00000;
        return;
    }

    if (PlyrOnMapObj(2) == 0)
    {
        if ((ingame_wrk.plyr_wrk->flags2 & 0x800000) != 0)
            ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x800000;
    }
    else if ((ingame_wrk.plyr_wrk->flags2 & 0x83f800) == 0)
    {
        PlyrTurnFlagClear();
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        {
            PlyrFinderModeCancel();
            SetPlyrAnime(0xf, 0, 0xff, 0xff, 0, 0.0f);
        }
        ingame_wrk.plyr_wrk->flags2 |= 0x800000;
    }

    if (PlyrOnMapObj(3) == 0)
    {
        if ((ingame_wrk.plyr_wrk->flags2 & 0x2000000) != 0)
            ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x2000000;
    }
    else if ((ingame_wrk.plyr_wrk->flags2 & 0x203f800) == 0)
    {
        PlyrTurnFlagClear();
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        {
            PlyrFinderModeCancel();
            SetPlyrAnime(0x90, 0, 0xff, 0xff, 0, 0.0f);
        }
        ingame_wrk.plyr_wrk->flags2 |= 0x2000000;
    }

    if (PlyrOnMapObj(4) != 0)
    {
        if ((ingame_wrk.plyr_wrk->flags2 & 0x43f800) != 0)
            return;
        PlyrTurnFlagClear();
        if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        {
            PlyrFinderModeCancel();
            SetPlyrAnime(9, 0, 0xff, 0xff, 0, 0.0f);
        }
        ingame_wrk.plyr_wrk->flags2 |= 0x400000;
        return;
    }
    if ((ingame_wrk.plyr_wrk->flags2 & 0x400000) != 0)
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x400000;
}

// ──────────────────────────────────────────────────────────────────────
// The player's map hit object no and the floor it gives.
// FF3 0x00240148

void PlyrSetMhNo(int no)
{
    int           room = MapLoadGetBuffID(GetPlyrAreaNo());
    MAP_ROOM_DAT *dat;
    PLYR_WRK     *plyr;

    ingame_wrk.plyr_wrk->mh_no = no;
    dat  = MapLoadGetFreeMemAddr(room);
    plyr = ingame_wrk.plyr_wrk;
    plyr->floor = RcbGetFloor(MapLoadGetRoomData(dat, 3), ingame_wrk.plyr_wrk->mh_no);
}

// ──────────────────────────────────────────────────────────────────────
// Player 1 (Miku) gets down to crawl (anime 0x9e, then PlyrCrawlStart),
// unless turning or on a direction object (flags2 0x3fbf0).
// FF3 0x002401b8

void PlyrCrawlReq(void)
{
    if (PlyrGetCurNo() != 1 || (ingame_wrk.plyr_wrk->flags2 & 0x3fbf0) != 0)
        return;
    ingame_wrk.plyr_wrk->finder_pitch = 0.0f;
    SetPlyrAnime(0x9e, 0xff, 0xff, 0xff, 1, 0.0f);
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
    ingame_wrk.plyr_wrk->flags |= PLYR_FLG_4000000;
}

// ──────────────────────────────────────────────────────────────────────
// The same, already down: crawling at once with the crawl's 60 degree view.
// FF3 0x00240258

void PlyrCrawlSet(void)
{
    if (PlyrGetCurNo() != 1 || (ingame_wrk.plyr_wrk->flags2 & 0x3fbf0) != 0)
        return;
    ingame_wrk.plyr_wrk->finder_pitch = 0.0f;
    ingame_wrk.plyr_wrk->crawl_h      = -10.0f;
    gra3dcamSetFov(1.04719746f);        // 60 degrees
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
    ingame_wrk.plyr_wrk->flags |= PLYR_FLG_8000000;
}

// ──────────────────────────────────────────────────────────────────────
// Player 1 gets up (PlyrCrawlEnd); the crawl loop SE fades out.
// FF3 0x00240300

void PlyrCrawlEndReq(void)
{
    if (PlyrGetCurNo() != 1)
        return;
    EneReleaseKind5f();
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
    ingame_wrk.plyr_wrk->flags |= PLYR_FLG_10000000;
    if (ingame_wrk.plyr_wrk->crawl_se != -1 && SndSeIsPlaying(ingame_wrk.plyr_wrk->crawl_se))
    {
        SndSeFadeStop(ingame_wrk.plyr_wrk->crawl_se, 5);
        ingame_wrk.plyr_wrk->crawl_se = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Player 1's crawl ends at once: mode 0, the loop SE fades out.
// FF3 0x002403a8

void PlyrCrawlCancel(void)
{
    if (PlyrGetCurNo() != 1)
        return;
    EneReleaseKind5f();
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
    PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
    if (ingame_wrk.plyr_wrk->crawl_se != -1 && SndSeIsPlaying(ingame_wrk.plyr_wrk->crawl_se))
    {
        SndSeFadeStop(ingame_wrk.plyr_wrk->crawl_se, 5);
        ingame_wrk.plyr_wrk->crawl_se = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Flags 0x4000000, getting down to crawl: crawl_h follows the height of the
// middle of joints 0x10 and 4 over pos. When anime 0x9e has ended: a 60
// degree view, map camera 8, the crawl proper (0x8000000) and its loop SE
// (SYS_MIKU_BD's SE 5: Miku crawls).
// FF3 0x00240448

void PlyrCrawlStart(void)
{
    sceVu0FVECTOR mid;
    sceVu0FVECTOR p0;
    sceVu0FVECTOR p1;
    PLYR_WRK     *plyr;

    PlyrGetJointPos(0x10, p0, nullptr);
    PlyrGetJointPos(4, p1, nullptr);
    GetMidPos(mid, p0, p1);
    ingame_wrk.plyr_wrk->crawl_h = mid[1] - ingame_wrk.plyr_wrk->pos[1];

    if (CheckPlyrAnimeEnd(0x9e))
    {
        gra3dcamSetFov(1.04719746f);    // 60 degrees
        ReqFinderInOverRap(8);
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
        ingame_wrk.plyr_wrk->flags |= PLYR_FLG_8000000;
        plyr = ingame_wrk.plyr_wrk;
        plyr->crawl_se = SndSePlay(0x1b7f0005, 100, 0x1000, 0, nullptr, 0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Flags 0x10000000, getting up: crawl_h drops 1 per frame; at -32 (or -24
// while still on a kind-3 object, which then keeps flags2 0x2000000 and
// plays anime 0x90) the crawl ends: map camera 8, mode 0.
// FF3 0x00240538

void PlyrCrawlEnd(void)
{
    float h;

    if (PlyrOnMapObj(3) == 0)
    {
        h = ingame_wrk.plyr_wrk->crawl_h - 1.0f;
        if (h <= -32.0f)
        {
            ReqFinderInOverRap(8);
            ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
            PlyrTurnFlagClear();
            PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
            return;
        }
        ingame_wrk.plyr_wrk->crawl_h = h;
        return;
    }

    h = ingame_wrk.plyr_wrk->crawl_h - 1.0f;
    if (h <= -24.0f)
    {
        ReqFinderInOverRap(8);
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_PZL;
        PlyrTurnFlagClear();
        PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
        ingame_wrk.plyr_wrk->flags2 |= 0x2000000;
        SetPlyrAnime(0x90, 0, 0xff, 0xff, 0, 0.0f);
        return;
    }
    ingame_wrk.plyr_wrk->crawl_h = h;
}

// ──────────────────────────────────────────────────────────────────────
// Flags 0x8000000, crawling: PlyrCrawlMove, PlyrFinderStartCheck outside modes 1..4
// and lock 3; the loop SE stops while the low-HP warning plays and restarts
// after it.
// FF3 0x00240680

void PlyrCrawlCtrl(void)
{
    PLYR_WRK *plyr;

    PlyrCrawlMove();
    if ((u_int)(ingame_wrk.plyr_wrk->mode - 1) > 3 && IsLockTblOn(3) == 0)
        PlyrFinderStartCheck();

    if (ingame_wrk.plyr_wrk->danger_se != -1 && SndSeIsPlaying(ingame_wrk.plyr_wrk->danger_se))
    {
        PlyrCrawlSeStop(PlyrGetCurNo());
        return;
    }
    plyr = ingame_wrk.plyr_wrk;
    if (plyr->crawl_se == -1)
        plyr->crawl_se = SndSePlay(0x1b7f0005, 100, 0x1000, 0, nullptr, 0);
}

// FF3 0x00240760
void PlyrCrawlSeStop(u_char no)
{
    PLYR_WRK *plyr = GetPlyrWrk((s_char)no);

    if (plyr->crawl_se != -1 && SndSeIsPlaying(plyr->crawl_se))
    {
        SndSeFadeStop(plyr->crawl_se, 3);
        plyr->crawl_se = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// While crawling, the distance crawled (speed * the timer step) plays the
// rustle SE 0x1c4f at the player every 100.
// FF3 0x002407c0

void PlyrCrawlSeTimer(float speed)
{
    float step = PlyrGaugeTimeStep(GetAlbumSaveDisp());
    float d;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) == 0)
        return;

    d = speed * step;
    if (100.0f < plyr_crawl_se_dist + d)
    {
        plyr_crawl_se_dist = plyr_crawl_se_dist + (d - 100.0f);
        FootSePlayId(FOOT_42_0_DOMA_4BAI_BD_BD, ingame_wrk.plyr_wrk->pos, 0x3200, 0x1000);
        return;
    }
    plyr_crawl_se_dist = plyr_crawl_se_dist + d;
}

// ──────────────────────────────────────────────────────────────────────
// The direction objects (map hits of kind 3 data with type 0 / 1) at the
// player: each found goes to obj0 / obj1; returns the last one's direction
// (+0x24, degrees) in radians, 0 without one.
// FF3 0x00240858

float PlyrGetMapObjDir(RCB_OBJ **obj0, RCB_OBJ **obj1)
{
    RCB_OBJ *list[4];
    float   deg = 0.0f;
    int     n, i;

    n = RcbGetMotObj(MapLoadGetAreaData(MapLoadGetRoomNoNow(), 3), ingame_wrk.plyr_wrk->mh_no, ingame_wrk.plyr_wrk->pos, list);
    for (i = 0; i < n; i++)
    {
        if (list[i]->prm[1] == 0)
        {
            *obj0 = list[i];
            deg = (float)list[i]->prm[2];
        }
        if (list[i]->prm[1] == 1)
        {
            *obj1 = list[i];
            deg = (float)list[i]->prm[2];
        }
    }
    return (deg * 3.14159274f) / 180.0f;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrAnimeCtrl: on a direction object (or keeping the last one's facing)
// the player's facing against it gives flags2 0x10 (along, within 65
// degrees), 0x20 (against, beyond 115) or 0x200 (across); the bits 0x40 /
// 0x80 / 0x100 step the side-walk state by the stick (flags3 0x3f0) and the
// anime ends. When it applies, the model's +0x1a4 is cleared and
// PlyrDirAnimeNo's anime set (1 returned); otherwise the bits go, +0x1a4 is
// 1 and PlyrAnimeCtrl picks the anime.
// FF3 0x00240938

int PlyrMapDirAnimeCtrl(void)
{
    RCB_OBJ *obj0 = nullptr;
    RCB_OBJ *obj1 = nullptr;
    float   dir, rel;
    int     on = 0;
    u_long  f2;
    int     anime;

    dir = PlyrGetMapObjDir(&obj0, &obj1);
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
        return 0;
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_HOLD) != 0)
        return 0;
    if (obj0 == nullptr && obj1 == nullptr)
        dir = ingame_wrk.plyr_wrk->mapobj_dir;

    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x230;
    rel = dir - ingame_wrk.plyr_wrk->rot[1];
    RotLimitChk(&rel);
    rel = (rel * 180.0f) / 3.14159274f;
    if ((-180.0f <= rel && rel < -115.0f) || (115.0f < rel && rel <= 180.0f))
        ingame_wrk.plyr_wrk->flags2 |= 0x20;
    else if (rel <= -65.0f || 65.0f <= rel)
        ingame_wrk.plyr_wrk->flags2 |= 0x200;
    else
        ingame_wrk.plyr_wrk->flags2 |= 0x10;

    if (obj0 == nullptr && obj1 == nullptr && (ingame_wrk.plyr_wrk->flags2 & 0x200) != 0)
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f0;
    else if ((obj0 == nullptr && (ingame_wrk.plyr_wrk->flags2 & 0x110) == 0x10 && (ingame_wrk.plyr_wrk->flags3 & 0x3f0) != 0)
             || (obj1 == nullptr && (ingame_wrk.plyr_wrk->flags2 & 0x120) == 0x20 && (ingame_wrk.plyr_wrk->flags3 & 0x3f0) != 0))
    {
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xc0;
        ingame_wrk.plyr_wrk->flags2 |= 0x100;
    }

    if ((ingame_wrk.plyr_wrk->flags2 & 0x1c0) != 0)
        on = 1;
    else if (obj0 != nullptr && (ingame_wrk.plyr_wrk->flags2 & 0x10) != 0)
        on = 1;
    else if (obj1 != nullptr)
        on = (ingame_wrk.plyr_wrk->flags2 & 0x20) != 0;

    if (!on)
    {
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f0;
        static_cast<CharPlayerModel *>(ingame_wrk.plyr_wrk->mdl)->foot_ik = 1;
        return 0;
    }

    ingame_wrk.plyr_wrk->mapobj_dir = dir;
    static_cast<CharPlayerModel *>(ingame_wrk.plyr_wrk->mdl)->foot_ik = 0;
    f2 = ingame_wrk.plyr_wrk->flags2;
    if ((f2 & 0x3f800) != 0)
        return 0;

    if ((f2 & 0x30) != 0)
    {
        // pushing towards the object's side the player faces
        u_long push = ingame_wrk.plyr_wrk->flags3 & (((f2 & 0x10) != 0) ? 0x10 : 0x20);

        if ((ingame_wrk.plyr_wrk->flags3 & 0x3f0) == 0)
            ingame_wrk.plyr_wrk->flags2 = (f2 & ~(u_long)0x180) | 0x40;
        else if ((f2 & 0x40) != 0)
        {
            if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
                ingame_wrk.plyr_wrk->flags2 = (f2 & ~(u_long)0x140) | 0x80;
        }
        else if ((f2 & 0x100) != 0)
        {
            if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
                ingame_wrk.plyr_wrk->flags2 = f2 & ~(u_long)0x3f0;
        }
        else if (push != 0)
            ingame_wrk.plyr_wrk->flags2 = (f2 & ~(u_long)0x140) | 0x80;
        else if ((f2 & 0x80) != 0)
            ingame_wrk.plyr_wrk->flags2 = f2 & ~(u_long)0x140;
    }

    anime = PlyrDirAnimeNo();
    if (anime != -1)
        SetPlyrAnime((u_char)anime, 0xff, 0xff, 0xff, 1, 0.0f);
    return anime != -1;
}

// ──────────────────────────────────────────────────────────────────────
// A direction object (either type) at the player.
// FF3 0x00240d88

int PlyrOnMapDirObj(void)
{
    RCB_OBJ *obj0 = nullptr;
    RCB_OBJ *obj1 = nullptr;

    PlyrGetMapObjDir(&obj0, &obj1);
    return obj0 != nullptr || obj1 != nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrNormalCtrl's move direction (the camera-relative control): the stick
// angle (PlyrGetStickAngle; mode 11 keeps 10 = none) through the camera's
// yaw towards the player gives the target facing rot_target[1]. A running ease
// (FUN_0023e7b0) bends it from ease_from while it stays within 36 degrees of
// ease_dir; after a map camera change (flags2 0x80000) the old facing holds
// until the stick turns enough (60 degrees in the first 7 frames, else 8).
// A turn in progress (flags2 0x3f800) goes to PlyrTurnCtrl.
// FF3 0x00240dd0
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

void PlyrMoveDirCtrl(void)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    float         ang  = 10.0f;
    float         rot[3];
    sceVu0FVECTOR cam_rot;
    float         d, t, a, b, c;
    float         ots_yaw = 0.0f;   // PC
    int           ots;              // PC

    rot[0] = 0.0f;
    rot[1] = 0.0f;
    rot[2] = 0.0f;

    t = ingame_wrk.plyr_wrk->_20c - 1.0f;
    if (t < 0.0f)
        t = 0.0f;
    ingame_wrk.plyr_wrk->_20c = t;

    if ((ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0)
    {
        PlyrTurnCtrl(plyr);
        if ((ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0)
            return;
        PlyrMoveSpeedCtrl(0.0f);
        return;
    }

    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_11)
        ang = PlyrGetStickAngle();
    PlyrMoveSpeedCtrl(ang);

    // PC: under the over-the-shoulder camera the stick is relative to its
    // heading, not to the line from the camera to the player (which the
    // shoulder offset skews), and it never cuts, so no cut hold applies.
    ots = 0;
    if (ang == 10.0f)
        (void)ReiPan_OverShoulderMoveYaw(0.0f, 0, &ots_yaw);

    if (ang != 10.0f)
    {
        GetTrgtRot(gra3dGetCamera()->matCoord[3], ingame_wrk.plyr_wrk->pos, cam_rot, 2);
        if (ReiPan_OverShoulderMoveYaw(cam_rot[1], 1, &ots_yaw))
        {
            cam_rot[1] = ots_yaw;
            ots = 1;
        }
        rot[0] = ang + cam_rot[1];
        plyr->cam_dir = cam_rot[1];
        RotLimitChk(&rot[0]);

        if (0.0f < plyr->ease_time)
        {
            // FF3 compares fabs((double)x) with (double)0.62831855f (36 degrees)
            if (fabsf(plyr->ease_dir - rot[0]) <= 0.628318548f)
            {
                d = rot[0] - plyr->ease_from;
                RotLimitChk(&d);
                rot[0] = (d * (10.0f - plyr->ease_time)) / 10.0f + plyr->ease_from;
                RotLimitChk(&rot[0]);
                plyr->ease_time -= PlyrGaugeTimeStep(GetAlbumSaveDisp());
                return;
            }
            plyr->ease_time = 0.0f;
            return;
        }

        if (ots && (ingame_wrk.plyr_wrk->flags2 & 0x80000) != 0)
            PlyrMapCamChgClear();       // PC: no cut to hold the facing across
        if ((ingame_wrk.plyr_wrk->flags2 & 0x80000) == 0)
        {
            ingame_wrk.plyr_wrk->rot_target[1] = rot[0];
            PlyrTurnStart(rot[0], plyr);
            return;
        }

        d = plyr->_214 - ang;
        RotLimitChk(&d);
        if (0.0f < plyr->camchg_time)
        {
            a = 12.0f / GetFrameTimeScale();
            c = plyr->camchg_time;
            b = 5.0f / GetFrameTimeScale();
            if (b < a - c && 1.04719746f < fabsf(d))
            {
                ingame_wrk.plyr_wrk->rot_target[1] = rot[0];
                PlyrMapCamChgClear();
                return;
            }
        }
        else if (0.139626339f < fabsf(d))
        {
            ingame_wrk.plyr_wrk->rot_target[1] = rot[0];
            PlyrMapCamChgClear();
            return;
        }
        ingame_wrk.plyr_wrk->rot_target[1] = plyr->cam_dir;
        return;
    }

    if (plyr->camchg_time <= 0.0f && (ingame_wrk.plyr_wrk->flags2 & 0x80000) != 0)
        PlyrMapCamChgClear();
    if (0.0f < plyr->ease_time)
        plyr->ease_time = 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrNormalCtrl with option b11 == 1 (the tank control): the d-pad's
// left / right turn on the spot (0.0733 rad per frame; with the run button
// up a side step, flags2 0x4000000), down turns to the back (PlyrTurnStart),
// up walks. A turn in progress runs PlyrTurnCtrl, or PlyrQuickTurnCtrl for
// the quick turn (flags2 0x400).
// FF3 0x00241128

void PlyrTankMoveCtrl(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_int     dir  = 0xff;
    int       run;
    float     t;

    t = plyr->_20c - 1.0f;
    if (!(0.0f <= t))
        t = 0.0f;
    plyr->_20c    = t;
    plyr->turn[1] = 0.0f;

    if ((ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0)
    {
        if ((ingame_wrk.plyr_wrk->flags2 & 0x400) != 0)
            PlyrQuickTurnCtrl(plyr);
        else
            PlyrTurnCtrl(plyr);
        if ((ingame_wrk.plyr_wrk->flags2 & 0x3f800) == 0)
            PlyrMoveSpeedCtrl(0.0f);
        return;
    }

    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_11)
        dir = PlyrGetDpadDir(1);
    run = CheckPadNowPlaySe(pad_key[PAD_KEY_02], -1, 0) != 0;

    if ((ingame_wrk.plyr_wrk->flags2 & 0x4000000) != 0
        && ((dir != 0xff && dir != 2 && dir != 6)
            || (dir == 0xff && CheckPlayerNeckSW() == 0 && (ingame_wrk.plyr_wrk->flags2 & 2) == 0)
            || run))
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x4000000;
    if ((dir == 2 || dir == 6) && !run)
        ingame_wrk.plyr_wrk->flags2 |= 0x4000000;

    PlyrMoveSpeedCtrl((dir == 0xff) ? 10.0f : 0.0f);
    switch (dir)
    {
    case 1: case 2: case 3:
        plyr->turn[1] = 0.0733038262f;  // 4.2 degrees
        break;
    case 4:
        PlyrTurnStart(plyr->rot[1] + 3.14159274f, plyr);
        break;
    case 5: case 6: case 7:
        plyr->turn[1] = -0.0733038262f;
        break;
    }
    RotLimitChk(&plyr->turn[1]);
}

// ──────────────────────────────────────────────────────────────────────
// PC: this frame's turn from a gamepad's gyro, in radians (yaw right, pitch
// up), as MioPan's finder takes it: an angle, not a rate, so not scaled by the
// frame time. Scaled by how far the viewfinder is zoomed in (its fov rate,
// FinderViewCameraCtrl; never past 1), so the world moves the same distance
// under the hand, and while crawling as the stick and the mouse are. The
// game's "Point of view" is the stick's; the gyro has its own inversions.

static void PlyrFinderGyroTurn(float *yaw, float *pitch)
{
    float rate;

    ReiPan_GyroRequestAim();
    if (ReiPan_GyroLook(yaw, pitch) == 0)
        return;

    rate = GetPhotoModeWrk()->GetZoom() * (1.85f - GetPhotoModeWrk1e8()->GetDispRate());
    if (rate > 1.0f)
        rate = 1.0f;
    else if (rate < 0.2f)
        rate = 0.2f;
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
        rate *= 0.600000024f;
    *yaw *= rate;
    *pitch *= rate;
}

// ──────────────────────────────────────────────────────────────────────
// PlyrFinderCtrl, the camera up: the right stick's x beyond 50 of 128
// turns the player (2.2 degrees per frame at full, 4 with pad_key[5]; 0.6
// times while crawling; within 25 degrees of hold_yaw while held), its y
// pitches the view (finder_pitch, -60..60 degrees; while held within
// -55..16 degrees of hold_pitch, crawling within 30) and moves the finder
// point (PlyrFinderPointMove). Outside modes 1..4 and the hold, the other
// stick steps (anime 0x19 forward, 0x1a back, 0x1b / 0x1c sideways) or the
// camera idle (0x18) plays.
// FF3 0x002413a8
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

void PlyrFinderMoveCtrl(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    x, y;
    float     fx, fy, v, t, turn, pitch, d, up, down, frame, r;
    int       limit;
    int       anime;

    t = plyr->_20c - 1.0f;
    if (!(0.0f <= t))
        t = 0.0f;
    plyr->_20c = t;

    GetStickRaw(5, &x, &y);
    float mouse_yaw = 0.0f;             // PC: the mouse's aim this frame
    float mouse_pitch = 0.0f;
    float gyro_yaw = 0.0f;              // PC: the gyro's
    float gyro_pitch = 0.0f;

    // FF3 compares in double: fabs((double)fx) against 50.0
    fx   = (float)x - 128.0f;
    turn = 0.0f;
    if (fabsf(fx) >= 50.0f)
    {
        v    = ((0.0f < fx) ? fx - 50.0f : fx + 50.0f) / 78.0f;
        turn = CheckPadNowPlaySe(pad_key[PAD_KEY_05], -1, 0) ? 0.0698131695f : 0.0383972414f;
        turn *= v;
    }
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
        turn *= 0.600000024f;
    turn *= GetFrameTimeScale();

    // PC: the mouse and the gyro aim too, inside the same limits as the stick
    // (the hold below, the pitch range further down).
    ReiPan_GameMouseRequestLook();
    ReiPan_GameMouseLook(&mouse_yaw, &mouse_pitch);
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
    {
        mouse_yaw *= 0.600000024f;
        mouse_pitch *= 0.600000024f;
    }
    if (GetOptionWrk()->now.b09 == 1)
        mouse_pitch = -mouse_pitch;     // "Point of view: Reverse"
    PlyrFinderGyroTurn(&gyro_yaw, &gyro_pitch);
    turn += mouse_yaw + gyro_yaw;
    if (ingame_wrk.plyr_wrk->status == 4)
    {
        d = (ingame_wrk.plyr_wrk->rot[1] + turn) - ingame_wrk.plyr_wrk->hold_yaw;
        RotLimitChk(&d);
        if (0.436332315f < d || d < -0.436332315f)    // 25 degrees
        {
            d = (0.436332315f < d) ? ingame_wrk.plyr_wrk->hold_yaw + 0.436332315f
                                   : ingame_wrk.plyr_wrk->hold_yaw - 0.436332315f;
            RotLimitChk(&d);
            d -= ingame_wrk.plyr_wrk->rot[1];
            RotLimitChk(&d);
            turn = d;
        }
    }
    plyr->turn[1] = turn;
    RotLimitChk(&plyr->turn[1]);

    // FF3 compares in double: fabs((double)fy) against 50.0
    fy    = -((float)y - 128.0f);
    pitch = 0.0f;
    if (fabsf(fy) >= 50.0f)
    {
        v     = ((0.0f < fy) ? fy - 50.0f : fy + 50.0f) / 78.0f;
        pitch = CheckPadNowPlaySe(pad_key[PAD_KEY_05], -1, 0) ? 0.0558505356f : 0.0314159244f;
        pitch = pitch * v * PlyrGaugeTimeStep(GetAlbumSaveDisp());
    }
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
        pitch *= 0.600000024f;
    pitch *= GetFrameTimeScale();
    pitch += mouse_pitch + gyro_pitch;  // PC

    limit = 1;
    up    = 0.0f;
    down  = 0.0f;
    if (ingame_wrk.plyr_wrk->status == 4)
    {
        up   = 0.279252678f;            // 16 degrees
        down = 0.959931076f;            // 55 degrees
    }
    else if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
    {
        up   = 0.523598731f;            // 30 degrees
        down = 0.523598731f;
    }
    else
        limit = 0;
    if (limit)
    {
        d = (ingame_wrk.plyr_wrk->finder_pitch + pitch) - ingame_wrk.plyr_wrk->hold_pitch;
        RotLimitChk(&d);
        if (up < d || d < -down)
        {
            d = (up < d) ? ingame_wrk.plyr_wrk->hold_pitch + up : ingame_wrk.plyr_wrk->hold_pitch - down;
            RotLimitChk(&d);
            d -= ingame_wrk.plyr_wrk->finder_pitch;
            RotLimitChk(&d);
            pitch = d;
        }
    }
    ingame_wrk.plyr_wrk->finder_pitch += pitch;
    if (1.04719746f < ingame_wrk.plyr_wrk->finder_pitch)
        ingame_wrk.plyr_wrk->finder_pitch = 1.04719746f;
    if (ingame_wrk.plyr_wrk->finder_pitch < -1.04719746f)
        ingame_wrk.plyr_wrk->finder_pitch = -1.04719746f;
    RotLimitChk(&ingame_wrk.plyr_wrk->finder_pitch);

    PlyrFinderPointMove(fx, fy);

    if ((u_int)(ingame_wrk.plyr_wrk->mode - 1) < 4 || ingame_wrk.plyr_wrk->status == 4)
        return;

    frame = static_cast<CharPlayerModel *>(ingame_wrk.plyr_wrk->mdl)->GetIdleFrame();
    r     = PlyrGetFinderStickAngle();
    plyr->move_rot[1] = r;
    if (r == 10.0f)
    {
        SetPlyrAnime(0x18, (ingame_wrk.plyr_wrk->anime_no == 0x69) ? 0 : 7, 0xff, 0xff, 1, frame);
        return;
    }
    if (fabsf(r) < 0.785398185f)        // 45 degrees
        anime = 0x19;
    else if (2.3561945f < fabsf(r))     // 135 degrees
        anime = 0x1a;
    else
        anime = (0.0f < r) ? 0x1b : 0x1c;
    SetPlyrAnime((u_char)anime, 7, 0xff, 0xff, 1, 0.0f);
    ingame_wrk.plyr_wrk->flags2 |= 2;
    PlyrCrawlSeTimer(1.66666663f);
}

// ──────────────────────────────────────────────────────────────────────
// Flags 0x8000000, crawling: the stick's x beyond 100 of 128 turns (2.2
// degrees per frame, 4 with pad_key[5] held), y beyond 90 moves forward /
// back (move_rot[1] 0 / pi) with anime 6, or 0x15 and flags2 bit 0 with
// pad_key[2] held (lock 5 off; it also moves without the stick). Option b11
// == 1 reads the d-pad as the stick. Nothing moves in modes 1..4, with lock 3
// or held (status 4).
// FF3 0x00241990
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

void PlyrCrawlMove(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    x, y;
    float     fx, fy, turn, speed;

    GetStickRaw(2, &x, &y);
    if ((u_int)(ingame_wrk.plyr_wrk->mode - 1) < 4 || IsLockTblOn(3))
    {
        plyr->turn[1] = 0.0f;
        plyr->move_rot[1] = 10.0f;
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)3;
        return;
    }

    if (GetOptionWrk()->now.b11 == 1)
    {
        if (CheckPadNowPlaySe(0x8000, -1, 0))
            x = 0;
        if (CheckPadNowPlaySe(0x2000, -1, 0))
            x = 0xff;
        if (CheckPadNowPlaySe(0x1000, -1, 0))
            y = 0;
        if (CheckPadNowPlaySe(0x4000, -1, 0))
            y = 0xff;
    }

    // FF3 compares in double: fabs((double)fx) against 100.0 / 90.0
    fx   = (float)x - 128.0f;
    turn = 0.0f;
    if (fabsf(fx) >= 100.0f)
    {
        if (0.0f < fx)
            fx -= 100.0f;
        else
            fx += 100.0f;
        fx /= 28.0f;
        turn = CheckPadNowPlaySe(pad_key[PAD_KEY_05], -1, 0) ? 0.0698131695f : 0.0383972414f;
        turn *= fx;
    }
    plyr->turn[1] = turn * 0.600000024f;
    RotLimitChk(&plyr->turn[1]);

    if (ingame_wrk.plyr_wrk->status == 4)
        return;

    fy = (float)y - 128.0f;
    if (fabsf(fy) >= 90.0f)
    {
        plyr->move_rot[1] = (0.0f <= fy) ? 3.14159274f : 0.0f;
        if (CheckPadNowPlaySe(pad_key[PAD_KEY_02], -1, 0) == 0 || IsLockTblOn(5))
        {
            SetPlyrAnime(6, 0, 0xff, 0xff, 0, 0.0f);
            speed = 1.66666663f;
            ingame_wrk.plyr_wrk->flags2 &= ~(u_long)1;
            ingame_wrk.plyr_wrk->flags2 |= 2;
        }
        else
        {
            SetPlyrAnime(0x15, 0, 0xff, 0xff, 0, 0.0f);
            speed = 3.33333325f;
            ingame_wrk.plyr_wrk->flags2 &= ~(u_long)2;
            ingame_wrk.plyr_wrk->flags2 |= 1;
        }
    }
    else
    {
        if (CheckPadNowPlaySe(pad_key[PAD_KEY_02], -1, 0) == 0 || IsLockTblOn(5))
        {
            SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
            ingame_wrk.plyr_wrk->flags2 &= ~(u_long)3;
            plyr->move_rot[1] = 10.0f;
            return;
        }
        SetPlyrAnime(0x15, 0, 0xff, 0xff, 0, 0.0f);
        speed = 3.33333325f;
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)2;
        ingame_wrk.plyr_wrk->flags2 |= 1;
        plyr->move_rot[1] = 0.0f;
    }
    PlyrCrawlSeTimer(speed);
}

// ──────────────────────────────────────────────────────────────────────
// The left stick's angle on an analog pad (id 0x79) when pushed beyond 29
// with a direction, else 10.0 (none; also with lock 3). A change under one
// degree from the last angle reported keeps the last one.
// FF3 0x00241db0
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

float PlyrGetStickAngle(void)
{
    float  ang  = 10.0f;
    float  keep = plyr_stick_angle;
    u_char x, y;
    float  mag;

    if (IsLockTblOn(3) == 0 && (u_char)pad_wrk[0].id == 0x79)
    {
        GetStickRaw(2, &x, &y);
        mag  = GetLength2D((float)(x - 0x80), (float)(y - 0x80));
        keep = plyr_stick_angle;
        if (GetStickDir8(2) != 0xff && (short)(int)mag > 0x1d)
        {
            ang  = GetStickRot(2);
            keep = ang;
            // FF3 compares in double with (double)0.017453292f
            if (fabsf(ang - plyr_stick_angle) < 0.0174532924f)
            {
                ang  = plyr_stick_angle;
                keep = plyr_stick_angle;
            }
        }
    }
    plyr_stick_angle = keep;
    return ang;
}

// ──────────────────────────────────────────────────────────────────────
// The d-pad's direction: 0 up, 2 right, 4 down, 6 left, odd values the
// diagonals, 0xff none (and with lock 3). Mode 1 takes down only on the
// press. Without a d-pad direction an analog pad's left stick answers
// (GetStickDir8); mode 1 then takes its down only when GetDirTrg(3) saw
// it this frame.
// FF3 0x00241ea8

u_int PlyrGetDpadDir(u_char mode)
{
    u_int dir = 0xff;

    if (IsLockTblOn(3))
        return 0xff;

    if (mode == 1)
    {
        if (CheckPadNowPlaySe(0x1000, -1, 0))
            dir = 0;
        else if (CheckPadTrgPlaySe(0x4000, -1, 0))
            dir = 4;
    }
    else
    {
        if (CheckPadNowPlaySe(0x1000, -1, 0))
            dir = 0;
        else if (CheckPadNowPlaySe(0x4000, -1, 0))
            dir = 4;
    }

    if (dir != 4)
    {
        if (CheckPadNowPlaySe(0x2000, -1, 0))
            dir = (dir == 0xff) ? 2 : (dir + 2) / 2;
        else if (CheckPadNowPlaySe(0x8000, -1, 0))
        {
            if (dir == 0xff)
                dir = 6;
            else
            {
                if (dir == 0)
                    dir = 8;
                dir = (dir + 6) / 2;
            }
        }
    }

    if (dir == 0xff && (u_char)pad_wrk[0].id == 0x79)
    {
        dir = GetStickDir8(2);
        if (mode == 1 && dir == 4 && GetDirTrg(3) == 0)
            dir = 0xff;
    }
    return dir;
}

// ──────────────────────────────────────────────────────────────────────
// The angle of the stick that steps in the viewfinder (GetStickRot(6)),
// 10.0: none.
// FF3 0x00242030

float PlyrGetFinderStickAngle(void)
{
    float ang = 10.0f;

    if (GetStickDir8(6) != 0xff)
        ang = GetStickRot(6);
    return ang;
}

// ──────────────────────────────────────────────────────────────────────
// PlayerMain: the frame's move. The model's motion steps (GameAnimeModel::SetAnimeSpeed)
// and gives the move (PlyrMotMoveGet); crawling or in the viewfinder it is
// turned by move_rot first. The turns (turn times the timer step, and the
// model callback's turn_cb) go into rot outside mode 6 and _0e0 & 0xf; the
// move turned by the facing is added to pos (the last two kept in
// move_last / move_old). The listener and _180 follow joints 2 and 1; the
// map hits update, and a turn on a direction object takes the move back.
// The frame's flags2 is kept in flags3.
// FF3 0x00242080

void PlyrMoveUpdate(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    float     step = PlyrGaugeTimeStep(GetAlbumSaveDisp());

    static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->SetAnimeSpeed(step);
    PlyrMotMoveGet();
    if ((ingame_wrk.plyr_wrk->flags & (PLYR_FLG_800000 | PLYR_FLG_8000000)) != 0 && plyr->move_rot[1] != 10.0f)
        RotVectorZXY(plyr->move, plyr->move, plyr->move_rot);

    sceVu0ScaleVector(plyr->turn, plyr->turn, step);
    if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_DOOR && (ingame_wrk.plyr_wrk->_0e0 & 0xf) == 0)
    {
        sceVu0AddVector(plyr->rot, plyr->rot, plyr->turn_cb);
        sceVu0AddVector(plyr->rot, plyr->rot, plyr->turn);
    }
    RotLimitChk(&plyr->rot[1]);
    RotVectorZXY(plyr->move, plyr->move, plyr->rot);

    sceVu0CopyVector(plyr->pos_old, plyr->pos);
    sceVu0CopyVector(plyr->move_old, plyr->move_last);
    sceVu0CopyVector(plyr->move_last, plyr->move);
    sceVu0AddVector(plyr->pos, plyr->pos, plyr->move);

    PlyrGetJointPos(2, ingame_wrk.plyr_wrk->listen_pos, nullptr);
    PlyrGetJointPos(1, ingame_wrk.plyr_wrk->_180, nullptr);
    PlyrMapPosUpdate();
    if (PlyrOnMapDirObj() && (ingame_wrk.plyr_wrk->flags2 & 0x3f800) != 0)
    {
        sceVu0CopyVector(plyr->pos, plyr->pos_old);
        sceVu0CopyVector(plyr->move_last, plyr->move_old);
        PlyrMapPosUpdate();
    }

    // FF3: v - v for each
    sceVu0SubVector(plyr->turn, plyr->turn, plyr->turn);
    sceVu0SubVector(plyr->turn_cb, plyr->turn_cb, plyr->turn_cb);
    sceVu0SubVector(plyr->move, plyr->move, plyr->move);
    ingame_wrk.plyr_wrk->flags3 = ingame_wrk.plyr_wrk->flags2;
}

// ──────────────────────────────────────────────────────────────────────
// The move's speed in flags2 bits 0..3: 1 runs, 2 walks, 0 stands. The run
// button (pad_key[2], lock 5 off, no map object bits) runs, or walks again
// on a run step's anime; the stick's push (PlyrGetMashInput) walks, or runs
// on a walk step's; with neither, a run / walk that the model's anime still
// carries (GameAnimeModel::GetAnimeFlag) holds. A map object with a quick turn walks.
// Returns 1 when the run button chose; the model's step goes to a static
// nothing reads. angle (the callers' stick angle) is not used.
// FF3 0x002422c8

int PlyrMoveSpeedCtrl(float angle)
{
    PLYR_WRK *plyr;
    int       step;
    u_long    f2;
    u_long    spd = 0;
    int       run = 0;
    u_char    a;

    (void)angle;
    // FF3: the model's vtable +0x60 (CharPlayerModel 0x146640)
    step = static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();
    plyr = ingame_wrk.plyr_wrk;

    if (IsLockTblOn(3))
    {
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xf;
        plyr_mdl_step_old = step;
        return 0;
    }

    if (CheckPadNowPlaySe(pad_key[PAD_KEY_02], -1, 0) && IsLockTblOn(5) == 0
        && (ingame_wrk.plyr_wrk->flags2 & 0x3c00000) == 0)
    {
        f2  = ingame_wrk.plyr_wrk->flags2;
        run = 1;
        spd = 1;
        if ((f2 & 0x3f0) == 0 && plyr->anime_no != 5)
        {
            if ((ingame_wrk.plyr_wrk->flags3 & 0x1800) != 0)
                spd = 2;
            else if ((f2 & 2) != 0 && plyr->anime_step == step)
                spd = 2;
        }
    }
    else if (PlyrGetMashInput() != 0
             || ((ingame_wrk.plyr_wrk->flags2 & 0x3c00000) != 0 && CheckPadNowPlaySe(pad_key[PAD_KEY_02], -1, 0)))
    {
        f2  = ingame_wrk.plyr_wrk->flags2;
        spd = 2;
        if ((f2 & 0x3f0) == 0)
        {
            if ((ingame_wrk.plyr_wrk->flags3 & 0x2000) != 0)
                spd = 1;
            else if ((f2 & 1) != 0 && plyr->anime_step == step)
                spd = 1;
        }
    }
    else if ((ingame_wrk.plyr_wrk->flags2 & 0x1c0) == 0)
    {
        if ((ingame_wrk.plyr_wrk->flags2 & 1) != 0 && plyr->anime_no != 0x14)
        {
            if (static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeFlag(0) == 0)
                spd = 1;
        }
        else if ((ingame_wrk.plyr_wrk->flags2 & 2) != 0)
        {
            a = plyr->anime_no;
            if (a != 0xb && a != 5 && a != 0x10 && a != 0x91)
                spd = (static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeFlag(0) != 0) ? 0 : 2;
        }
    }

    f2 = ingame_wrk.plyr_wrk->flags2;
    if ((f2 & 0x3c00000) != 0 && (f2 & 0x400) != 0)
        spd = 2;
    ingame_wrk.plyr_wrk->flags2 = f2 & ~(u_long)0xf;
    ingame_wrk.plyr_wrk->flags2 |= spd;
    plyr_mdl_step_old = step;
    return run;
}

// ──────────────────────────────────────────────────────────────────────
// A turn of plyr towards dir. With the camera-relative control off a
// direction object (or the tank control), flags2 gets the turn's kind:
// 0x800 standing (anime below 5), 0x1000 walking, 0x2000 running; 0x4000
// beyond 60 degrees, 0x8000 beyond 135; 0x10000 / 0x20000 the side (a
// large turn while moving by the step's foot). The tank control's quick
// turns (0x400) take 10 frames on a direction object, 46 / 45 / 53 on the
// map objects 0x400000 / 0x800000 / 0x2000000. Otherwise an event turn
// anime (PlyrAnimeEventNo) plays: its length + 3 frames, rot kept in
// turn_from, the target in rot_target. Without one (or a large turn right
// after a camera change) the turn speed is dir's difference, capped at 9.7
// degrees per frame (6 on a map object).
// FF3 0x00242590

void PlyrTurnStart(float dir, PLYR_WRK *plyr)
{
    PLYR_WRK *cur  = ingame_wrk.plyr_wrk;
    RCB_OBJ   *obj0 = nullptr;
    RCB_OBJ   *obj1 = nullptr;
    float     d    = dir - plyr->rot[1];
    float     lim, sgn, t;
    int       s, no, n;
    u_long    side;

    PlyrGetMapObjDir(&obj0, &obj1);     // FF3 drops the direction
    RotLimitChk(&d);
    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;

    if ((PlyrOnMapDirObj() == 0 && GetOptionWrk()->now.b11 == 0) || GetOptionWrk()->now.b11 == 1)
    {
        if (cur->anime_no < 5)
            ingame_wrk.plyr_wrk->flags2 |= 0x800;
        else if ((ingame_wrk.plyr_wrk->flags2 & 2) != 0)
            ingame_wrk.plyr_wrk->flags2 |= 0x1000;
        else if ((ingame_wrk.plyr_wrk->flags2 & 1) != 0)
            ingame_wrk.plyr_wrk->flags2 |= 0x2000;

        if (2.35619426f < fabsf(d))     // 135 degrees
            ingame_wrk.plyr_wrk->flags2 |= 0x8000;
        else if (1.04719746f < fabsf(d))    // 60 degrees
            ingame_wrk.plyr_wrk->flags2 |= 0x4000;

        if ((ingame_wrk.plyr_wrk->flags2 & 0x800) != 0 || fabsf(d) <= 1.04719746f)
            side = (0.0f < d) ? 0x10000 : 0x20000;
        else if (cur->anime_no == 8 || cur->anime_no == 0x17)
            side = 0x20000;
        else
        {
            // FF3: the model's vtable +0x60 (CharPlayerModel 0x146640)
            s    = static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();
            side = (s != 0 && s != 3) ? 0x20000 : 0x10000;
        }
        ingame_wrk.plyr_wrk->flags2 |= side;
    }

    if (GetOptionWrk()->now.b11 == 1)
    {
        t = 0.0f;
        if (PlyrOnMapDirObj())
            t = 10.0f;
        else if ((ingame_wrk.plyr_wrk->flags2 & 0x400000) != 0)
            t = 46.0f;
        else if ((ingame_wrk.plyr_wrk->flags2 & 0x800000) != 0)
            t = 45.0f;
        else if ((ingame_wrk.plyr_wrk->flags2 & 0x2000000) != 0)
            t = 53.0f;
        if (t != 0.0f)
        {
            ingame_wrk.plyr_wrk->flags2 |= 0x400;
            plyr->_000 = t;
            return;
        }
    }

    if (((ingame_wrk.plyr_wrk->flags2 & 0x8000) != 0 && 0.0f < ingame_wrk.plyr_wrk->_20c)
        || (no = PlyrAnimeEventNo()) < 0)
    {
        lim = GetFrameTimeScale() * (((ingame_wrk.plyr_wrk->flags2 & 0x2c00000) == 0) ? 0.16929692f : 0.104719751f);
        sgn = (0.0f < d) ? 1.0f : -1.0f;
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;
        cur->turn[1] = (lim < fabsf(d)) ? lim * sgn : d;
        RotLimitChk(&cur->turn[1]);
        return;
    }

    static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetWalkStep();    // FF3 calls the vtable +0x60 and drops it
    n = static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeLength(no);
    t = (float)(n + 3) / GetFrameTimeScale();
    plyr->_004 = t;
    plyr->_000 = t;
    sceVu0CopyVector(plyr->turn_from, plyr->rot);
    sceVu0CopyVector(plyr->rot_target, plyr->rot);
    plyr->rot_target[1] = dir;
    RotLimitChk(&plyr->rot_target[1]);
}

// ──────────────────────────────────────────────────────────────────────
// Ends the map camera change hold (flags2 0x80000): _20c = 4 frames.
// FF3 0x00242ab0

void PlyrMapCamChgClear(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if ((ingame_wrk.plyr_wrk->flags2 & 0x80000) != 0)
    {
        plyr->_20c = 4.0f / GetFrameTimeScale();
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x80000;
    }
}

// ──────────────────────────────────────────────────────────────────────
// A turn in progress: what is left to rot_target[1] from the model's facing
// (_0b4) against the start (turn_from[1]), over the turn length _004, gives the
// current player's turn[1]; _000 counts down until the turn anime ends,
// which clears the turn bits (flags2 0x3f800 and 0xf).
// FF3 0x00242b30

void PlyrTurnCtrl(PLYR_WRK *plyr)
{
    float     step = PlyrGaugeTimeStep(GetAlbumSaveDisp());
    PLYR_WRK *cur  = ingame_wrk.plyr_wrk;
    float     d;

    plyr->_0b4 = plyr->rot_target[1] - static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetMotionYaw();
    RotLimitChk(&plyr->_0b4);
    d = plyr->_0b4 - plyr->turn_from[1];
    RotLimitChk(&d);
    cur->turn[1] = d / plyr->_004;
    RotLimitChk(&cur->turn[1]);

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) == 0)
    {
        plyr->_000 -= step;
        return;
    }
    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;
    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xf;
}

// ──────────────────────────────────────────────────────────────────────
// The tank control's quick turn (flags2 0x400): rot.y turns a fixed step
// every frame (20 degrees, or 4.0 / 4.1 / 3.5 on the map objects 0x400000
// / 0x800000 / 0x2000000) while _000 counts down by the timer step; then
// the turn bits are cleared.
// FF3 0x00242c30

void PlyrQuickTurnCtrl(PLYR_WRK *plyr)
{
    float  step = PlyrGaugeTimeStep(GetAlbumSaveDisp());
    u_long f2;
    float  add;

    if (plyr->_000 <= step)
    {
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x3f800;
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0xf;
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x400;
        return;
    }
    plyr->_000 -= step;

    f2 = ingame_wrk.plyr_wrk->flags2;
    if ((f2 & 0x400000) != 0)
        add = 0.0698131695f;
    else if ((f2 & 0x800000) != 0)
        add = 0.0713998303f;
    else if ((f2 & 0x2000000) != 0)
        add = 0.0604152419f;
    else
        add = 0.34906584f;
    ingame_wrk.plyr_wrk->rot[1] += add;
    RotLimitChk(&ingame_wrk.plyr_wrk->rot[1]);
}

// ──────────────────────────────────────────────────────────────────────
// Clears the turn, direction and move bits (0x403ffff) of flags2 and
// flags3; every mode / position change calls it.
// FF3 0x00242d50

void PlyrTurnFlagClear(void)
{
    ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x403ffff;
    ingame_wrk.plyr_wrk->flags3 &= ~(u_long)0x403ffff;
}

// ──────────────────────────────────────────────────────────────────────
// The viewfinder point (finder_x / finder_y, on the 640x448 screen): the
// stick's offset (x, y) beyond 50 moves it 2 per timer step along its
// angle, within 302..338 x 209..239; otherwise it drifts back to the centre
// (320, 224) at 1.9 per step without overshooting.
// FF3 0x00242d80
// callcheck: fptodp, fabs, dpcmp (FF3's soft-float double compare of fabs; a float compare gives the same result)

void PlyrFinderPointMove(float x, float y)
{
    PLYR_GAUGE *disp = GetAlbumSaveDisp();
    float            ang  = 10.0f;
    float            spd  = 0.0f;
    float            dx, dy, step;

    // FF3 compares in double: fabs((double)x) / fabs((double)y) against 50.0
    if (fabsf(x) >= 50.0f || fabsf(y) >= 50.0f)
    {
        ang  = g3dAtan2f(x, y);
        GetLength2D(x, y);             // FF3 drops the magnitude
        step = PlyrGaugeTimeStep(disp);
        spd  = step + step;
    }
    else if (ingame_wrk.plyr_wrk->finder_x != 320.0f || ingame_wrk.plyr_wrk->finder_y != 224.0f)
    {
        ang = g3dAtan2f(320.0f - ingame_wrk.plyr_wrk->finder_x, -(224.0f - ingame_wrk.plyr_wrk->finder_y));
        spd = PlyrGaugeTimeStep(disp) * 1.89999998f;
    }
    if (ang == 10.0f)
        return;

    dx = spd * Vu0Sinf(ang);
    dy = -spd * Vu0Cosf(ang);

    ingame_wrk.plyr_wrk->finder_x += dx;
    step = PlyrGaugeTimeStep(disp);
    if (spd == step + step)
    {
        if (ingame_wrk.plyr_wrk->finder_x < 302.0f)
            ingame_wrk.plyr_wrk->finder_x = 302.0f;
        else if (338.0f < ingame_wrk.plyr_wrk->finder_x)
            ingame_wrk.plyr_wrk->finder_x = 338.0f;
    }
    else if (0.0f < dx)
    {
        if (320.0f < ingame_wrk.plyr_wrk->finder_x)
            ingame_wrk.plyr_wrk->finder_x = 320.0f;
    }
    else if (dx < 0.0f)
    {
        if (ingame_wrk.plyr_wrk->finder_x < 320.0f)
            ingame_wrk.plyr_wrk->finder_x = 320.0f;
    }

    ingame_wrk.plyr_wrk->finder_y += dy;
    step = PlyrGaugeTimeStep(disp);
    if (spd == step + step)
    {
        if (ingame_wrk.plyr_wrk->finder_y < 209.0f)
            ingame_wrk.plyr_wrk->finder_y = 209.0f;
        else if (239.0f < ingame_wrk.plyr_wrk->finder_y)
            ingame_wrk.plyr_wrk->finder_y = 239.0f;
    }
    else if (0.0f < dy)
    {
        if (224.0f < ingame_wrk.plyr_wrk->finder_y)
            ingame_wrk.plyr_wrk->finder_y = 224.0f;
    }
    else if (dy < 0.0f)
    {
        if (ingame_wrk.plyr_wrk->finder_y < 224.0f)
            ingame_wrk.plyr_wrk->finder_y = 224.0f;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The flash recoil on a direction or map object (flags2 0xc003f0): the
// motion's move is dropped (PlyrMotMoveGet).
// FF3 0x00243090

int PlyrIsFlashRecoilDir(void)
{
    if (PlyrIsFlashRecoil() == 0)
        return 0;
    return (ingame_wrk.plyr_wrk->flags2 & 0xc003f0) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// The motion's move for this frame into PLYR_WRK.move (lock 4: unchanged):
// in the first two frames of an anime's end count, the model's root move
// for anime_attr (CharModel::GetRootMove per axis; x and y negated), 0.3 per step
// forward for the viewfinder steps 0x19..0x1c, none in the flash recoil
// on an object. None on the ladder without a frame, during side steps and
// quick turns (flags2 0x4000400). With flags bit 0 (a new anime) the move
// is dropped while the anime is unchanged and running, and for some event
// anime; bit 0 clears otherwise. Crawling moves at 0.6.
// FF3 0x002430e0

void PlyrMotMoveGet(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    u_char    a;

    if (IsLockTblOn(4))
        return;

    if (plyr->anime_end_cnt < 2)
    {
        if (PlyrIsFlashRecoilDir())
        {
            plyr->move[0] = 0.0f;
            plyr->move[1] = 0.0f;
            plyr->move[2] = 0.0f;
        }
        else if ((u_int)(plyr->anime_no - 0x19) < 4)
        {
            plyr->move[0] = 0.0f;
            plyr->move[1] = 0.0f;
            plyr->move[2] = PlyrGaugeTimeStep(GetAlbumSaveDisp()) * 0.300000012f;
        }
        else
        {
            plyr->move[0] = -static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetRootMove(0, plyr->anime_attr);
            plyr->move[1] = -static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetRootMove(1, plyr->anime_attr);
            plyr->move[2] = static_cast<CharModel *>(ingame_wrk.plyr_wrk->mdl)->GetRootMove(2, plyr->anime_attr);
        }
    }

    if (ingame_wrk.plyr_wrk->mode == PLYR_MODE_5 && static_cast<GameAnimeModel *>(ingame_wrk.plyr_wrk->mdl)->GetAnimeTime() == 0.0f)
    {
        plyr->move[2] = 0.0f;
        plyr->move[0] = 0.0f;
        plyr->move[1] = 0.0f;
    }
    if ((ingame_wrk.plyr_wrk->flags2 & 0x4000400) != 0)
    {
        plyr->move[0] = 0.0f;
        plyr->move[1] = 0.0f;
        plyr->move[2] = 0.0f;
    }

    if ((ingame_wrk.plyr_wrk->flags & 1) != 0)
    {
        a = ingame_wrk.plyr_wrk->anime_no;
        if (ingame_wrk.plyr_wrk->_193 == a)
        {
            if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_ANIME_END) != 0)
                ingame_wrk.plyr_wrk->flags &= ~(u_long)1;
            else
            {
                plyr->move[0] = 0.0f;
                plyr->move[1] = 0.0f;
                plyr->move[2] = 0.0f;
            }
        }
        else if ((u_int)(a - 7) < 2 || a == 0xd || a == 0xe || a == 0x12 || a == 0x13
                 || a == 0x16 || a == 0x17 || a == 0x93 || a == 0x94)
        {
            plyr->move[0] = 0.0f;
            plyr->move[1] = 0.0f;
            plyr->move[2] = 0.0f;
        }
        else
            ingame_wrk.plyr_wrk->flags &= ~(u_long)1;
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_8000000) != 0)
        sceVu0ScaleVector(plyr->move, plyr->move, 0.600000024f);
}

// FF3 0x00243348
float PlyrGetCamChgTime(void)
{
    return 12.0f / GetFrameTimeScale();
}

// ══════════════════════════════════════════════════════════════════════
// The model's fade envelope (PLYR_WRK.mdl_fx)
// ══════════════════════════════════════════════════════════════════════

// FF3 0x00243390 (the object's constructor)
PLYR_MDL_FX::PLYR_MDL_FX()
{
    state = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Starts an envelope: kind 1 rise / hold / fall, 5 a pulse, 8 a fall. A
// running rise..fall (states 1..4) ignores another kind 1..4 request, a
// running pulse or fall (5..8) ignores kinds below 5.
// FF3 0x00243398

void PLYR_MDL_FX::Req(int in, int hold, int out, u_char level, u_char kind)
{
    if (state >= 5 && kind < 5)
        return;
    if ((u_int)(state - 1) < 4 && (u_int)(kind - 1) < 4)
        return;

    in_time     = (float)in;
    state       = kind;
    hold_time   = (float)hold;
    this->level = level;
    out_time    = (float)out;
    value       = 0;
    time        = 0.0f;
}

// FF3 0x00243418
void PLYR_MDL_FX::ReqPulse(int time, u_char level)
{
    Req(time, 0, 0, level, 5);
}

// FF3 0x00243440
void PLYR_MDL_FX::ReqOut(int time)
{
    Req(0, 0, time, value, 8);
}

// ──────────────────────────────────────────────────────────────────────
// One frame of the envelope (step: the timer step), handed to the model.
// The rise and the pulse follow (sin(t * 250 / in - 90 deg) + 1) / 2 of the
// level; the falls are linear from the level.
// FF3 0x00243468

void PLYR_MDL_FX::Main(GameModel *mdl, float step)
{
    u_char v;
    float  t, s, f;
    int    end;

    switch (state)
    {
    case 0:
        value = 0;
        v = 0;
        break;

    case 1:
        time  = 0.0f;
        state = 2;
        // fall through
    case 2:
        if (0.0f < in_time)
        {
            t = (time + step) * 250.0f;
            time += step;
            s = Vu0Sinf(((t / in_time + -90.0f) * 3.14159274f) / 180.0f);
            v = (u_char)(int)((s + 1.0f) * 0.5f * (float)level);
            value = v;
            if (in_time <= time)
            {
                time  = 0.0f;
                state = 3;
            }
        }
        else
        {
            time  = 0.0f;
            state = 3;
            v = value;
        }
        break;

    case 3:
        if (0.0f < hold_time)
        {
            t = time;
            time = t + step;
            v = value;
            if (t + step < hold_time)
                break;
        }
        else
        {
            v = value;
            if (hold_time == -1.0f)
                break;
        }
        state = 4;
        time  = 0.0f;
        level = v;
        break;

    case 4:
        if (out_time <= 0.0f)
        {
            v = value;
            break;
        }
        t   = time + step;
        end = out_time <= t;
        time = t;
        f = ((float)level * (out_time - t)) / out_time;
        v = (u_char)(int)f;
        value = v;
        if (end)
            state = 0;
        break;

    case 5:
        time  = 0.0f;
        state = 6;
        // fall through
    case 6:
        t = (time + step) * 250.0f;
        time += step;
        s = Vu0Sinf(((t / in_time + -90.0f) * 3.14159274f) / 180.0f);
        v = (u_char)(int)((s + 1.0f) * 0.5f * (float)level);
        value = v;
        if (time < in_time)
            break;
        time  = 0.0f;
        state = 7;
        break;

    case 8:
        t   = time + step;
        f   = out_time;
        end = f <= t;
        time = t;
        f = ((float)level * (f - t)) / f;
        v = (u_char)(int)f;
        value = v;
        if (end)
            state = 0;
        break;

    default:
        v = value;
        break;
    }

    mdl->SetTexFrame(v);
}

// ──────────────────────────────────────────────────────────────────────
// PlayerMain: while a ghost (an enemy slot in state 3 with hp whose model
// id is 0x1a) is within 60 of the player, the model pulses (60 frames up
// to 100); once none is, a held pulse fades out over 30 frames.
// FF3 0x00243798

void PLYR_MDL_FX::GhostCtrl()
{
    ENE_WRK *ene;
    sceVu0FVECTOR d;
    int           near = 0;
    u_char        i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        ene = &ene_wrk[i];
        if (ene->state != 3 || ene->hp == 0 || ene->cmn->id != 0x1a)
            continue;
        // FF3: VU0 length of the xyz difference (vf1 = 1)
        sceVu0SubVector(d, ingame_wrk.plyr_wrk->pos, ene->pos);
        if (sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < 60.0f)
            near = 1;
    }

    if (near)
    {
        if (state < 5)
            ReqPulse(0x3c, 100);
    }
    else if (state == 7)
        ReqOut(0x1e);
}
