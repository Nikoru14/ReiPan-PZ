/* ==========================================================================
 *  ingame/ingame.c
 *
 *  The in-game driver. FF3 0x1d0710..0x1d2610; FF2 ingame/ingame.c (whose
 *  INGAME_WRK part FF3 moved to ingame/ingame_wrk.c).
 *
 *  Written so far: phase 12 and its parameter setters, IngameInit/Pre/End,
 *  the new-game reset, the load/save steps and IngameNowLoadingMain
 *  (0x1d0710..0x1d18c8), IngameMain/IngameDrawSub and their helpers
 *  (0x1d18c8..0x1d20e0), GPhaseCtrlRequest, the channel-0 sound mute count,
 *  a standard SE helper and the sound listener (0x1d20e0..0x1d2358), the
 *  model resets (0x1d2358..0x1d25c8, on minimal GameModel/MDL_MOT stand-ins).
 *  FF3 0x001d25c8 is the GPhaseCtrl atexit thunk that ends the file
 *  (compiler-generated).
 * ======================================================================== */
#include "ingame/ingame.h"

#include "album/prg/album_view.h"
#include "ingame/candle_ctrl.h"
#include "common/cmn_fade_layer.h"
#include "graphics/scene/IngameScene.h"   // ScnTestIngameInit / End / Main
#include "graphics/effect/effect.h"
#include "graphics/effect/effect_data.h"     // ScreenEffectParamInit, MonotoneSaveStore / LoadRestore
#include "graphics/effect/effect_oth.h"
#include "ingame/enemy/ene_main.h"
#include "graphics/graph2d/font.h"                     // FontTexSendVram
#include "graphics/graph2d/g2d_draw.h"                 // InitG2DDraw
#include "graphics/graph3d/gra3d.h"
#include "graphics/graph3d/gra3dMirror.h"             // gra3dDrawModelCb
#include "graphics/graph3d/sgd_types.h"               // SgdAddr
#include "graphics/gs_reg.h"                            // SetGsRegCached
#include "graphics/mmanage.h"
#include "ingame/memo/memo.h"
#include "graphics/graph2d/message.h"   // MsgGetPage, MsgGetPageBreakNum, MsgPage*
#include "graphics/motion/motion.h"                     // MDL_MOT
#include "graphics/scene/scene.h"
#include "graphics/scene/scene_dat.h"    // SceneWrkRelease
#include "ingame/camera/cam_rail.h"     // CamRailDataLoad, CamRailWrkClear, CamRailDataFree
#include "ingame/camera/finder_camera.h"
#include "ingame/story_menu_photo.h"            // DocFlagInit / NewGame / End
#include "ingame/story_menu_film.h"             // FilmMenuSortInit
#include "ingame/story_develop.h"               // DevelopFilmInit / NewGame / End
#include "ingame/camera/map_camera.h"
#include "ingame/map/BgSe.h"                           // GetLayoutSeWrk, CBgSe
#include "ingame/map/map_height.h"                     // MhFirstInit
#include "ingame/char_model_sub.h"     // CharDrawInit
#include "ingame/cine_film.h"          // CineInit / Main / Draw / End
#include "ingame/cine_menu.h"                // CineMenuStateClear
#include "ingame/enemy/enemy.h"
#include "ingame/enemy/ene_spe_obj.h"                     // GetEneSpeObjTbl
#include "ingame/event/telephone.h"
#include "ingame/event/ev_queue.h"                    // EvQueueInit/Reset/End
#include "ingame/event/prg/ev_chapter.h"        // EvChapterDispMain
#include "ingame/event/prg/ev_exe.h"
#include "ingame/event/prg/ev_macro.h"        // EventLoadRestore
#include "ingame/event/prg/ev_main.h"
#include "ingame/ingame_wrk.h"
#include "ingame/item/prg/item.h"              // AllPlyrItemInit / NewGame / End
#include "ingame/item/prg/soul_list.h"         // SoulListInit
#include "common/WindowMsg.h"
#include "ingame/map/MarkEvent.h"
#include "ingame/map/DoorStat.h"                // DoorStatLoad / Apply / Free, DoorStatClear
#include "ingame/map/FurnSave.h"                // FurnSaveAlloc / FurnSaveLoad
#include "ingame/map/MapDraw.h"
#include "ingame/map/MapFog.h"
#include "ingame/map/MapLBuff.h"
#include "ingame/map/MapLoad.h"
#include "ingame/map/MapPut.h"
#include "ingame/map/MapCtrl.h"
#include "ingame/map/foot_se.h"
#include "ingame/map/map_bgm.h"                 // map_bgmInit / Main / FadeIn / Release
#include "ingame/map/map_rectangle.h"
#include "ingame/story_menu_map.h"     // IsMapEnable
#include "ingame/menu/menu_cassette.h"
#include "ingame/menu/anim_2d.h"
#include "ingame/photo/finder_photo.h"                 // PhotoModeWrk*
#include "ingame/menu/menu_file_list.h"         // MemoMenuReqInit
#include "ingame/photo/photo_obj.h"              // PhotoObjMain
#include "ingame/movie_room_menu/prg/movie_projecter.h"
#include "ingame/pause/prg/pause.h"                      // IsPauseEnable
#include "ingame/event/prg/ev_command.h"     // GetFinderBankWrk
#include "ingame/menu/start_menu.h"          // Finder{State,Slot}Wrk
#include "ingame/mission/mission_mode.h"
#include "ingame/story_night.h"                 // NightChangeInit
#include "ingame/photo/n_plyr_camera.h"
#include "ingame/photo/photo.h"
#include "ingame/photo/photo_dat.h"
#include "ingame/plyr/player.h"
#include "ingame/plyr/plyr_sub.h"                       // PlyrMdlCreate, AllPlyrIsReady, PlyrGetJointRotY, PlyrMdlReleaseAll
#include "ingame/story_door.h"
#include "ingame/map/MapEff.h"          // MapLightRegReset, MapLightRegClear
#include "ingame/map/MapSnap.h"
#include "ingame/map/MapDoor.h"
#include "ingame/puzzle/puzzle.h"
#include "ingame/radio.h"                       // Radio*
#include "ingame/enemy/random_ghost.h"          // RandGhostWrkClear, RandGhostTimeSet, RandGhostSetAll
#include "ingame/sound/snd_ctrl.h"                      // SndSetVolume, SndSeBankLoad, SndSetListener
#include "ingame/subtitle/subtitle.h"
#include "main/gphase.h"                                // GetGPhaseCtrl, GPHASE_CTRL::SetNext
#include "album/prg/album.h"                 // AlbumIngameFlagClear
#include "outgame/option.h"                             // GetOptionWrk, OptionGetSndVolume0, OptionApplySound
#include "outgame/title_menu.h"                         // TitleMenuFlagSet
#include "reipan/reipan_memory.h"                       // ReiPan_GetHostPointer
#include "sdk/libvu0.h"                                 // sceVu0SubVector
#include "save_load/prg/game_data_save.h"
#include "system/b2d/BpCtl.h"                            // BpCtlMain
#include "system/pad/pad.h"                             // CheckPadTrgPlaySe, pad_key
#include "system/os/eecdvd.h"         // LoadReq / LoadReqGetAddr / IsLoadEnd / IsLoadEndAll
#include "system/eeiop/file_cache.h"   // file_cache
#include "ingame/story_menu.h"                         // IsLockTblOn, IsMenuEnable
#include "ingame/cat/cat_ctrl.h"                       // CatCtrl*, CatMdlCreate
#include "graphics/graph2d/fade.h"
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@0011cea8 (map)

// unassigned@0011d398 (map)


int PadIsPressMode(int pad);                            // scePadInfoPressMode(...) == 1



// unassigned@00198e28



// MAP_CAM_DATA / MapCamGetCurData: ingame/camera/map_camera.h.

// unassigned@001d2a40 (model objects; the run after main/main.c). Minimal
// polymorphic stand-ins for what the model resets below call; neither class
// is constructed on the host yet (their owners are stubs), so the pointers
// that reach them are nullptr for now.
//
// The motion object a model keeps at +4 (MDL_MOT) is in graphics/motion/motion.h.

// GameModel is in ingame/ingame.h.

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// Phase-request priority (data 0x359f08): when a request meets a pending one,
// whichever comes first here wins.
static const int gphase_req_priority[18] =
{
    11, 10, 31, 50, 39, 67, 66, 49, 35, 48, 38, 62, 44, 37, 58, 34, 41, 32,
};

static int snd_volume0_mute_cnt;                        // sdata 0x373314

PHASE12_PARAM  phase12_param;                           // bss 0x3eb388
static int      phase12_fade_layer;                     // sdata 0x373308
static CWindowMsg *phase12_sel_win;                        // sdata 0x37330c: own or phase12_param.win

// IngameDrawSub's skip flags of the current frame (sdata 0x370abc); cleared
// by IngameInit, read by effect code through IngameGetDrawFlags and by
// IngameDrawRoomMorphCb.
static int ingame_draw_flags;

int draw_en_371300;                                     // sdata 0x371300
int draw_en_371304;                                     // sdata 0x371304
int draw_en_371308;                                     // sdata 0x371308
int draw_en_371348;                                     // sdata 0x371348
int draw_en_37126c;                                     // sdata 0x37126c
int draw_en_37127c = 1;                                 // sdata 0x37127c (mmanage.c's; ModelCcs::Update reads it)

// Up vector of the sound listener: {0, *(float *)0x370ac0 = -1, 0, 0}.
static const float snd_listener_up_y = -1.0f;           // sdata 0x370ac0

// ──────────────────────────────────────────────────────────────────────
// Phase 12 parameters
// ──────────────────────────────────────────────────────────────────────

// Set the parameters of the next phase-12 run; the fade layer stays off
// unless Phase12FadeLayerOn follows.
// FF3 0x001d0710.

void Phase12ParamSet(const PHASE12_PARAM *param)
{
    phase12_param = *param;
    phase12_fade_layer = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d0770.

void Phase12FadeLayerOn(void)
{
    phase12_fade_layer = 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 12: a message (type 2) or selection window over the current screen.
// Without a caller window, init makes one (state_cnt 1 once created) and
// waits for its layout data.
// FF3 0x001d0780. The window is built with new CWindowMsg (SelWinCtor 0x10fd98).
// callcheck: operator_new, SelWinCtor (new CWindowMsg)

int init_Phase12(void)
{
    int has_cancel = phase12_param.next_cancel != -1;

    if (phase12_param.win == nullptr)
    {
        if (GetGPhaseCtrl()->leaf->state_cnt == 0)
        {
            phase12_sel_win = new CWindowMsg;
            phase12_sel_win->Open();
            GetGPhaseCtrl()->leaf->state_cnt = 1;
        }

        if (!phase12_sel_win->IsReady())
            return 0;

        phase12_sel_win->SetItem(-1, -1);
    }
    else
    {
        phase12_sel_win = phase12_param.win;
    }

    phase12_sel_win->SetFlags(1, 1, 0);

    if (phase12_param.type == 2)
        MsgPageReset();

    if (phase12_fade_layer)
    {
        capCtl *layer = GetCmnFadeLayer();

        layer->SetItemHandle(nullptr);
        if (phase12_param.type == 2)
            layer->Show(has_cancel ? 2 : 10);
        else
            layer->Show(has_cancel ? 0 : 10);
    }

    if (phase12_param.type == 2)
        phase12_sel_win->StartMsg(phase12_param.msg0, phase12_param.msg1, has_cancel);
    else
        phase12_sel_win->StartSel(phase12_param.msg0, phase12_param.msg1, has_cancel,
                       phase12_param.type);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Fades the fade layer in over the node's state_wrk (0..0x80, +0x10 a
// frame), runs the window, then picks the next phase by the result:
// 1 next_ok, 2/4 next_alt, 3 next_cancel. Message windows (type 2) turn
// pages with the confirm button and end on the last page; selection windows
// report their result and get the decide/cancel SE here.
// FF3 0x001d0930.

int one_Phase12(void)
{
    int   res;
    short next;

    if (phase12_fade_layer)
    {
        GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;
        int          alpha;

        GetCmnFadeLayer()->base_alpha = (short)leaf->state_wrk;
        alpha = (short)leaf->state_wrk + 0x10;
        if (alpha < 0)
            alpha = 0;
        else if (alpha > 0x80)
            alpha = 0x80;
        GetGPhaseCtrl()->leaf->state_wrk = (short)alpha;
    }

    if (phase12_param.win == nullptr)
    {
        LayoutAnimMain();
        phase12_sel_win->Main();
    }

    res = 0;
    if (phase12_param.type == 2)
    {
        int has_cancel = phase12_param.next_cancel != -1;
        int breaks     = MsgGetPageBreakNum(phase12_param.msg0, phase12_param.msg1);

        if (breaks == 0 || MsgIsPageWait() == 1)
        {
            ReiPan_MenuMouseConfirm();  // PC: a click turns the page
            if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], 0x1a9b0003, 0))
            {
                if (breaks == 0)
                {
                    res = 1;
                }
                else
                {
                    if (MsgGetPage() < breaks)
                        MsgPageNext();
                    if (MsgGetPage() >= breaks)
                    {
                        res = 1;
                        MsgSetPage((signed char)(breaks - 1));
                    }
                }
            }
            else if (has_cancel)
            {
                res = CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], 0x1a9b0001, 0) ? 3 : 0;
            }
        }
    }
    else
    {
        res = phase12_sel_win->result;
        if (res == 3)
            SndSePlay(0x1a9b0001, 100, 0x1000, 0, 0, 0);
        else if (res == 1 || res == 2 || res == 4)
            SndSePlay(0x1a9b0003, 100, 0x1000, 0, 0, 0);
    }

    if (res == 0)
        return 0;

    switch (res)
    {
    case 1:  next = phase12_param.next_ok;     break;
    case 2:  next = phase12_param.next_alt;    break;
    case 3:  next = phase12_param.next_cancel; break;
    case 4:  next = phase12_param.next_alt;    break;
    default: return 1;                          // FF3: other results end without a request
    }

    GetGPhaseCtrl()->SetNext(next, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Fades phase 12's own window (and the fade layer) out over state_wrk
// (+0x19 a frame), then hands the result to end_func and deletes the window.
// FF3 0x001d0be0.

int end_Phase12(void)
{
    if (phase12_param.win == nullptr && (short)GetGPhaseCtrl()->leaf->state_wrk <= 0x80)
    {
        short t = (short)GetGPhaseCtrl()->leaf->state_wrk;

        phase12_sel_win->SetAlpha(0x80 - t);
        if (phase12_fade_layer)
            GetCmnFadeLayer()->base_alpha = 0x80 - (short)GetGPhaseCtrl()->leaf->state_wrk;

        t = (short)GetGPhaseCtrl()->leaf->state_wrk;
        GetGPhaseCtrl()->leaf->state_wrk = (short)(t + 0x19);

        LayoutAnimMain();
        phase12_sel_win->Main();
        return 0;
    }

    if (phase12_param.end_func != nullptr)
        phase12_param.end_func(phase12_sel_win->result);

    if (phase12_param.win == nullptr)
    {
        phase12_sel_win->Close();
        delete phase12_sel_win;                         // vtable +4: deleting destructor
        if (phase12_fade_layer)
            GetCmnFadeLayer()->Term();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d0d88.

int IngameGetDrawFlags(void)
{
    return ingame_draw_flags;
}

// ──────────────────────────────────────────────────────────────────────
// init_Story_Main's work: nothing to wait for.
// FF3 0x001d0d90 (hidden in init_Story_Main's Ghidra body until 2026-10-14).

int IngameMainInit(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// New game from the start menu: put every in-game subsystem back to its
// state before play, keeping what IngameInit allocated.
// FF3 0x001d0d98; no FF2 counterpart.

void IngameNewGameReset(void)
{
    GetCandleControl()->Init();
    GetFinderBankWrk()->Init();
    PuzzleClearInfoReset();
    IngameWrkInitNotPlayData();
    PlyrWrkInitKeepEquip();
    RandGhostWrkClear();
    GetPhotoModeWrk()->ClearFlag();
    EnemyWrkClear();
    PlyrCameraReset();
    CatCtrlReset();
    TelInit();
    CassetteHeardClear();
    CineInit();
    DocFlagNewGame();
    DevelopFilmNewGame();
    AllPlyrItemNewGame();
    MemoWrkClear();
    EvQueueReset();
    ScnTestIngameInit();
    SndVolume0MuteReset();
    map_bgmRelease(1);
    foot_seRelease();
    EventRelease();
    FootSeInit();
    map_bgmInit();
    PhotoDatInit();
    CharDrawInit();
    EventInit();
    DoorStatClear();
    MapLightRegReset();
    MapLBuffInit();
    MarkEventFlagClear();
}

// ──────────────────────────────────────────────────────────────────────
// In-game start (init_Story_LoadGame): reset or allocate every in-game
// subsystem, load the event, album, puzzle and map work data. Returns 1.
// FF3 0x001d0ed8; FF2 IngameInit's role (FF2 took chapter/difficulty and
// had far fewer subsystems).

int IngameInit(void)
{
    ingame_draw_flags = 0;
    MapFogReset();
    IngameWrkInit();
    ingame_wrk.b02 = ingame_wrk.b01 - 1;

    GetFinderSlotWrk()->Init();
    GetFinderStateWrk()->Init();
    MissionWrkInit();
    file_cache.ClearAll();
    model_collision_set.Init();
    AllPlyrItemInit();
    MemoWrkInit();
    MemoNoticeCreate();
    DocFlagInit();
    DevelopFilmInit();
    RadioInit();
    ScnTestIngameInit();
    NightChangeInit();
    SubTitleCtrlStructInit();
    FilmMenuSortInit();
    MemoMenuReqInit();
    PlyrWrkInitAll();
    PlyrModeInit();
    EnemyMainInit();
    GetPhotoModeWrk()->Init();
    FootSeInit();
    map_bgmInit();
    PhotoDatInit();
    CharDrawInit();
    MhFirstInit();
    EventInit();
    EventDataLoad(0);
    CameraMainInit();
    GetCandleControl()->Init();
    GetFinderBankWrk()->Init();
    SoulListInit();
    PuzzleClearInfoReset();
    InitEffects();
    ScreenEffectParamInit();
    SceneWrkClear();
    MovieReqInit();
    TelInit();
    TelTalkClear();
    CassetteInit();
    CassetteHeardClear();
    CineInit();
    CineMenuStateClear();
    PlyrCameraInit();
    CatCtrlInit();
    AlbumIngameFlagClear();
    InitG2DDraw();
    DoorStatLoad();
    DoorStatClear();
    MapDoorInit();
    Anim2dInit();
    MapSnapInit();
    EvQueueInit();
    MapAreaDataAlloc();
    CamRailDataLoad();
    CamRailWrkClear();
    FurnSaveAlloc();
    MapLightRegReset();
    MapLoadIngameInit();
    MarkEventFlagClear();
    GetLayoutSeWrk()->StopAll();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// end_Story_Start's work: nothing to wait for.
// FF3 0x001d1178 (hidden in end_Story_Start's Ghidra body until 2026-10-14).

int IngameStartEnd(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// init_Story_LoadGame_Mc: wait until every file load has finished.
// FF3 0x001d1180; was GalPictureLoadWait (wrong FF2 match).

int IngameLoadWait(void)
{
    return IsLoadEndAll() != 0;
}

// ──────────────────────────────────────────────────────────────────────
// end_Story_LoadGame_Mc: apply the loaded options and restore the player,
// player camera and layer state from the save. Returns 1.
// FF3 0x001d11a0.

int IngameLoadEnd(void)
{
    OptionApplySound();
    PlyrLoadRestore();
    EnemyMainLoadRestore();
    PlyrCameraLoadRestore();
    CatCtrlLoadRestore();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// When the title chose "load", rebuild runtime state from the loaded save
// and fade in over 30 frames. one_Story_LoadGame / one_Story_Start.
// FF3 0x001d11e0.

void IngameLoadRestore(void)
{
    if (title_start_flg.load == 0)
        return;

    EventLoadRestore();
    PlyrLoadRestore();
    EnemyMainLoadRestore();
    MapLightRegClear();
    MonotoneLoadRestore();
    GetCandleControl()->LoadRestore();
    TelLoadRestore();
    PlyrCameraLoadRestore();
    CatCtrlLoadRestore();
    RadioLoadRestore();
    GetPhotoModeWrk()->LoadRestore();
    FadeInReq(0, 0, 0, 30);
}

// ──────────────────────────────────────────────────────────────────────
// one_Story_Start for a new game: reset, reload the per-game data.
// FF3 0x001d1298.

void IngameStartNewGame(void)
{
    EventRelease();
    IngameNewGameReset();
    MapAreaDataLoad();
    FurnSaveLoad();
    EventDataLoad(1);
}

// ──────────────────────────────────────────────────────────────────────
// one_Story_NowLoading's work, stepped by the leaf node's state_cnt:
// 0 waits for the file loads, 1 sets up the player, area and models,
// 2 waits until every subsystem's data is in (a finder-mode start also
// waits 150 frames on state_wrk) and starts play, 3 returns 1.
// FF3 0x001d12d0.

int IngameNowLoadingMain(void)
{
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        if (IsLoadEndAll())
            GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        if (title_start_flg.mode == 2)
        {
            FINDER_BANK_ENTRY *entry = GetFinderModeWrk()->GetDef();

            MapAreaNoSet(entry->area);
            PlyrSetCurrent(entry->plyr);
            ingame_wrk.plyr_wrk->_2e8 = 2;
            ingame_wrk.plyr_wrk->_2ec = 1;
        }
        else if (title_start_flg.load == 0 && EventGetMode() != 2)
        {
            MapAreaNoSet(1);
            MapAreaSideSet(0);
        }

        MapStartNoSet(MapAreaNoGet());
        MapPlayStartReset();
        PlyrMdlCreate(PlyrGetCurNo(), ingame_wrk.plyr_wrk->_2e8, ingame_wrk.plyr_wrk->_2ec);

        if (IngameIsIdOver2e(MapAreaNoGet()) == 1)
        {
            SndSeBankLoad(0x1b7d);
            SndSeBankLoad(0x1bf5);
            SndSeBankLoad(0x1bf7);
            GetEneSpeObjTbl()->Start();
        }

        MapSnapStateUpdate();
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        return 0;

    case 2:
        if (title_start_flg.mode == 2)
        {
            int t = (short)GetGPhaseCtrl()->leaf->state_wrk + 1;

            if (t < 0)
                t = 0;
            else if (t > 150)
                t = 150;
            GetGPhaseCtrl()->leaf->state_wrk = (short)t;
        }

        if (IsLoadEndAll()
            && GetCandleControl()->DataLoad()
            && AllPlyrIsReady()
            && MapLoadCheckLoadNow() == 0
            && CheckSetEffectsOK()
            && MapPlayStartIsReady()
            && MapSnapIsLoaded())
        {
            if (title_start_flg.mode == 2 && (short)GetGPhaseCtrl()->leaf->state_wrk != 150)
                return 0;

            if (IngameIsIdOver2e(MapAreaNoGet()) == 1)
            {
                int plyr = PlyrGetCurNo();

                if (plyr != 1)
                    PlyrCameraMdlCreate(0, -1, 0);
                if (plyr != 2)
                    PlyrCameraMdlCreate(1, -1, 0);
                CatMdlCreate(0, -1, 0);
            }

            AllPlyrMdlIsLoaded();                       // result unused in FF3
            PlayerMain();
            if (title_start_flg.mode == 2 || title_start_flg.load == 0)
                EventOpenConditionClear();
            DoorStatApply();
            RandGhostTimeSet(-1);
            RandGhostSetAll();
            map_bgmFadeIn(0x1e);
            GetGPhaseCtrl()->leaf->state_cnt = 3;
            return 0;
        }

        MapLoadWaitMain();
        return 0;

    case 3:
        return 1;

    default:
        return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// init_Story_SaveGame_Mc: store the runtime state the save needs (not from
// the out-game menus). Returns 1.
// FF3 0x001d16f0.

int IngameSaveInit(void)
{
    if (IngameIsOutgame() == 0)
    {
        MonotoneSaveStore();
        RadioSaveStore();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// end_Story_SaveGame_Mc.
// FF3 0x001d1728.

int IngameSaveEnd(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// In-game end (end_Story_Main): release what IngameInit allocated, stop
// sound, effects and subtitles, reset the title flags. Returns 1.
// FF3 0x001d1730; FF2 IngameEnd's role.

int IngameEnd(void)
{
    CamRailDataFree();
    EvQueueEnd();
    map_bgmRelease(1);
    foot_seRelease();
    EventRelease();
    MapAreaDataFree();
    MapSnapEnd();
    LayoutAnimResetAll();
    GetCandleControl()->Term();
    MapDoorDelete();
    DoorStatFree();
    DoorStatEnd();
    CineEnd();
    PlyrMdlReleaseAll();
    EnemyReleaseAll();
    MapReleaseAll();
    AllPlyrItemEnd();
    MemoNoticeDelete();
    MemoWrkEnd();
    DocFlagEnd();
    DevelopFilmEnd();
    RadioEnd();
    ScnTestIngameEnd();
    SubTitleStop();
    CatCtrlEnd();
    PlyrCameraEnd();
    model_collision_set.Term();
    GetEneSpeObjTbl()->Release();
    EffectsReleaseAll();
    SndIngameStopAll(0);
    MovieReqEnd();
    SceneWrkRelease();
    GetLayoutSeWrk()->StopAll();
    IngameSndMuteReset();
    TitleStartFlagClear();
    TitleMenuFlagSet();
    file_cache.ReleaseAll();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// pre_Story_Main: poll the file cache's loads.
// FF3 0x001d18a0.

void IngamePre(void)
{
    file_cache.PollAll();
}

// ──────────────────────────────────────────────────────────────────────
// after_Story_Main: empty.
// FF3 0x001d18c0 (hidden in after_Story_Main's Ghidra body until 2026-10-14).

void IngameAfter(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// One game frame (one_Story_Normal and the puzzle/menu phases that keep the
// game running): missions, action checks, events, player, effects, cameras,
// the menu/map/pause requests. skip bits leave out parts:
//   0x01 player and finder mode, 0x02 events, 0x04 effect slots, 0x08 photo
//   mode, 0x10 action checks, 0x20 model texture, 0x40 scene test,
//   0x80 player camera and common layers.
// Returns 1 when a phase change is pending.
// FF3 0x001d18c8; FF2 IngameMain's role (different contents). Ghidra's
// IngameMain also spans the callback 0x1d1c38: its gra3dSetGsRegisterDefault,
// MapDrawRooms and MapPutDraw calls are in IngameDrawRoomMorphCb.
// callcheck: MapDrawRooms, MapPutDraw, gra3dSetGsRegisterDefault (IngameDrawRoomMorphCb's, see above)

int IngameMain(u_int skip)
{
    int pause;

    model_collision_set.ClearHit();

    if (AllPlyrIsReady() && IsLockTblOn(4) == 0 && (skip & 0x10) == 0)
        PlyrCheckAction(PlyrGetJointRotY(3), ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->floor);

    if ((skip & 0x02) == 0)
        EventMain();

    LayoutAnimMain();

    if (AllPlyrIsReady() && IsLockTblOn(4) == 0 && (skip & 0x10) == 0)
        PlyrCheckActionPhaseReq(PlyrGetJointRotY(3), ingame_wrk.plyr_wrk->pos, ingame_wrk.plyr_wrk->floor);

    MapFrameUpdate();

    if (AllPlyrIsReady() && (skip & 0x01) == 0)
        PlayerMain();

    if ((skip & 0x04) == 0)
        EnemyMain();

    if ((skip & 0x80) == 0)
    {
        PlyrCameraMain();
        CatCtrlMain();
    }

    if (ingame_wrk.b03 == 1)
        GetEneSpeObjTbl()->Main();
    else
        GetLayoutSeWrk()->Main();

    if (title_start_flg.mode == 2 && (skip & 0x01) == 0)
        GetFinderModeWrk()->Main();

    PhotoObjMain();

    if (CameraMain() != 1)
        IngameSndListenerSet(0);

    if ((skip & 0x08) == 0)
        GetPhotoModeWrk()->Main();

    GetCandleControl()->Main();
    RadioMain();
    map_bgmMain();

    if (IsMenuEnable() && CheckPadTrgPlaySe(pad_key[PAD_KEY_MENU], -1, 0))
        GPhaseCtrlRequest(0x25, 0);

    if (IsMapEnable() && CheckPadTrgPlaySe(pad_key[PAD_KEY_MAP] | pad_key[PAD_KEY_MAP2], -1, 0))
        GPhaseCtrlRequest(0x3a, 0);

    TelMain();
    TelTalkMain();
    CineMain();

    // Pause on the pause button, and also when pad 0 is not usable: its
    // +0x1a4 word is 0 or it refuses pressure mode.
    pause = 1;
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_PAUSE], -1, 0) == 0 && pad_wrk[0].connected != 0)
    {
        pause = 0;
        if (PadIsPressMode(0) == 0)
            pause = 1;
    }
    if (IsPauseEnable() && pause)
        GPhaseCtrlRequest(0x2c, 0);

    if ((skip & 0x40) == 0)
        ScnTestIngameMain();

    if ((skip & 0x20) == 0)
        MemoNoticeMenuCheck();

    return GetGPhaseCtrl()->next != -1;
}

// ──────────────────────────────────────────────────────────────────────
// GS callback of IngameDrawRoomMorph: draw the rooms and placed objects
// again into the morph pass, then reset the alpha register.
// FF3 0x001d1c38 (inside IngameMain's range in Ghidra, no function there:
// only referenced as a constant).

static int IngameDrawRoomMorphCb(void)
{
    gra3dSetGsRegisterDefault();
    MapDrawRooms(0x200);
    MapPutDraw(ingame_draw_flags | 0x200);
    return SetGsRegCached(0, 0x3f, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Draw the current room's morph model with a copy of the camera, unless the
// room is not loaded, the player mode is past 6 or draw_en_371300 is off.
// FF3 0x001d1c78. The "morph" is the room object's SGD 0
// (ModelObject::GetSgd), which gra3dDrawModelCb draws as a mirror.

static void IngameDrawRoomMorph(void)
{
    MAP_ROOM_DAT *room = MapLoadGetCurRoomAddr();
    GRA3DCAMERA   cam;
    ModelManager *mdl;
    ModelObject  *obj;
    SGDFILEHEADER *morph;

    if (room == nullptr || PlyrIsMode233Over6() != 0 || draw_en_371300 == 0)
        return;

    cam = *gra3dGetCamera();
    mdl = room->mdl.mot;
    obj   = mdl->ani_ctrl;
    morph = obj->GetSgd(0);
    if (morph != nullptr)
        gra3dDrawModelCb(&cam, morph, SgdAddr(obj->GetCoord(0)), IngameDrawRoomMorphCb);
}

// ──────────────────────────────────────────────────────────────────────
// Compute this frame's draw-enable words from the map camera's data, the
// player state and the current phase.
// FF3 0x001d1d88.

static void IngameSetDrawEnable(void)
{
    MAP_CAM_DATA *cam = MapCamGetCurData();
    int           en0 = 1;
    int           en1 = 1;
    int           en2;
    int           not_22;

    if (cam != nullptr)
    {
        switch (MapCamGetCurType())
        {
        case 1:
        case 8:
            en1 = cam->draw_en1;
            en0 = cam->draw_en0;
            break;
        case 9:
            en1 = cam->draw_en1_t9;
            en0 = cam->draw_en0_t9;
            break;
        }
    }

    en2 = en0 == 0;
    // PC: the over-the-shoulder camera sees the room from anywhere, as the
    // viewfinder does, so nothing a fixed angle hid may be left out.
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) || ReiPan_OverShoulderActive())
    {
        en0 = 1;
        en1 = 1;
        en2 = 1;
    }
    if (ingame_wrk.plyr_wrk->mode == 5)
    {
        en0 = 1;
        en1 = 0;
    }

    not_22 = GetGPhaseCtrl()->leaf->id != 0x22;
    draw_en_37127c = not_22;
    if (title_start_flg.mode == 2)
        en0 = 0;

    draw_en_371300 = en0;
    draw_en_371304 = en1;
    draw_en_371308 = en2;
    draw_en_371348 = not_22;
    draw_en_37126c = draw_en_37127c;
}

// ──────────────────────────────────────────────────────────────────────
// One game frame's drawing: map, fog, rooms, placed objects, effects, album
// and photo overlays, fade, subtitles. skip bits (with the player's own,
// PlyrGetDrawSkipFlags) leave out parts: 0x04 rooms, 0x10 effects, 0x20
// candles, 0x40 album/event, 0x80 photo mode, 0x100 fade, 0x400 model
// textures, 0x2000 passed to EffectDrawMain. Afterwards every draw-enable
// word is back to 1.
// FF3 0x001d1ed0; FF2 IngameDrawSub 0x001c90b8 (same role, other contents:
// MapFogProc is the only call they share).

void IngameDrawSub(u_int skip)
{
    skip |= PlyrGetDrawSkipFlags();
    ingame_draw_flags = skip;

    MapLoadCheckLoadNow();
    IngameSetDrawEnable();
    MapPutDrawFlagClear();
    MapFogProc(GetPlyrAreaNo(), ingame_wrk.plyr_wrk->floor);

    if ((skip & 0x20) == 0)
        EffSoraDraw();

    IngameDrawRoomMorph();

    if ((skip & 0x04) == 0)
        MapDrawAll();

    CineDraw();
    MapPutDraw(skip);
    MapDrawChars(skip);
    FontTexSendVram();
    MapSnapUpdate(ingame_wrk.plyr_wrk->pos);

    if ((skip & 0x10) == 0)
        EffectDrawMain(skip & 0x2000);

    if ((skip & 0x40) == 0)
        GetCandleControl()->Draw();

    if ((skip & 0x80) == 0)
        GetPhotoModeWrk()->Draw();

    if (title_start_flg.mode == 2)
        GetFinderModeWrk()->Draw();

    if ((skip & 0x100) == 0)
        FadeMain();

    if ((skip & 0x40) == 0)
        EvChapterDispMain();

    EneBlinkCapture();
    BpCtlMain();
    RadioDraw();
    SubTitleMain(GetOptionWrk()->now.subtitle, 1);

    if ((skip & 0x400) == 0)
    {
        MemoNoticeMain();
        MemoNoticeDraw();
    }

    if ((skip & 0x10) == 0)
        EffectOthBufCopy();

    draw_en_371348 = 1;
    draw_en_371300 = 1;
    draw_en_371304 = 1;
    draw_en_371308 = 1;
    draw_en_37126c = 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d21e0; meaning of id 0x2e not known.

int IngameIsIdOver2e(int id)
{
    return id > 0x2e;
}

// ──────────────────────────────────────────────────────────────────────
// Request phase id unless a pending request outranks it (see
// gphase_req_priority); returns the phase actually set as next. Ids missing
// from the list always replace the pending one.
// FF3 0x001d20e0; FF2 had no arbitration (SetNextGPhase direct).

short GPhaseCtrlRequest(short id, int link)
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();
    short        next = ctrl->next;
    int          i;

    if (next != -1)
    {
        for (i = 0; i < 18; i++)
        {
            if (gphase_req_priority[i] == id)
                break;
            if (gphase_req_priority[i] == next)
            {
                id = next;
                break;
            }
        }
    }

    GetGPhaseCtrl()->SetNext(id, link);
    return id;
}

// ──────────────────────────────────────────────────────────────────────
// An SE at the standard volume and pitch.
// FF3 0x001d21f0.

void SndSePlayStd(int se)
{
    SndSePlay(se, 100, 0x1000, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d2218; was GPhaseCtrlRequestReset (wrong guess). GameSystemStart.

void SndVolume0MuteReset(void)
{
    snd_volume0_mute_cnt = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d2220 (IngameEnd).

void IngameSndMuteReset(void)
{
    SndVolume0MuteReset();
}

// ──────────────────────────────────────────────────────────────────────
// Nested mute of sound channel 0 (scene and movie phases): the first mute
// sets its volume to 0, the last unmute restores the option volume.
// FF3 0x001d2238.

void SndVolume0Mute(int mute)
{
    int volume;

    if (mute == 0)
    {
        if (--snd_volume0_mute_cnt != 0)
            return;
        volume = OptionGetSndVolume0();
    }
    else
    {
        if (snd_volume0_mute_cnt != 0)
        {
            snd_volume0_mute_cnt++;
            return;
        }
        volume = 0;
        snd_volume0_mute_cnt = 1;
    }
    SndSetVolume(0, volume);
}

// ──────────────────────────────────────────────────────────────────────
// Point the 3D sound listener at the camera (from_plyr 0) or the current
// player: position, velocity (old position - position), camera direction,
// up = -Y.
// FF3 0x001d2298 (its memset of the listener is the "= {}" here).
// callcheck: memset (the listener's "= {}")

void IngameSndListenerSet(int from_plyr)
{
    SND_3D_SET    l = {};
    sceVu0FVECTOR vel;
    sceVu0FVECTOR up = { 0.0f, snd_listener_up_y, 0.0f, 0.0f };
    PLYR_WRK     *plyr;

    l.dir = gra3dcamGetDirection();
    if (from_plyr == 0)
    {
        l.pos = gra3dcamGetPosition();
        sceVu0SubVector(vel, gra3dcamGetPositionOld(), l.pos);
    }
    else
    {
        plyr = ingame_wrk.plyr_wrk;
        l.pos = plyr->listen_pos;
        sceVu0SubVector(vel, plyr->pos_old, plyr->pos);
    }
    l.vel = vel;
    SndSetListener(&l, up);
}

// ──────────────────────────────────────────────────────────────────────
// The model resets: for every live model, call its motion object's EE
// vtable +0x34, then +0x18 with (-1, arg).

// The three player models. FF3 0x001d2358.

void IngamePlyrMdlReset(int arg)
{
    PLYR_WRK *plyr;
    MDL_MOT  *mot;
    int       i;

    for (i = 0; i < 3; i++)
    {
        plyr = GetPlyrWrk((s_char)i);
        if (plyr == nullptr || plyr->_2e4 == 0)
            continue;
        mot = plyr->mdl->mot;
        if (mot != nullptr)
        {
            mot->WriteShapeAll();
            mot->ResetCloth(-1, arg);
        }
    }
}

// The two player-camera models; the camera colour is reset, keeping its
// first word. FF3 0x001d23f8.

void IngamePlyrCamMdlReset(int arg)
{
    PLYR_CAMERA_WRK *cam;
    MDL_MOT         *mot;
    int              keep;
    int              i;

    for (i = 0; i < 2; i++)
    {
        cam = GetPlyrCameraWrk((s_char)i);
        if (cam == nullptr || cam->ready == 0 || cam->mdl == nullptr)
            continue;
        mot = cam->mdl->mot;
        if (mot != nullptr)
        {
            mot->WriteShapeAll();
            mot->ResetCloth(-1, arg);
            keep = cam->color.on;
            PlyrCameraColorReset(&cam->color);
            cam->color.on = keep;
        }
    }
}

// The 16 effect slots in states 2..4 whose +0x1f4 model has id 'id' (-1:
// all). FF3 0x001d24b8.

void IngameEffSlotMdlReset(short id, int arg)
{
    ENE_WRK *slot;
    MDL_MOT      *mot;
    int           i;

    for (i = 0; i < ENE_WRK_MAX; i++)
    {
        slot = &ene_wrk[i];
        if ((u_int)(slot->state - 2) >= 3)
            continue;
        if (id != -1 && slot->cmn->id != id)
            continue;
        mot = slot->mdl->mot;
        if (mot != nullptr)
        {
            mot->WriteShapeAll();
            mot->ResetCloth(-1, arg);
        }
    }
}

// FF3 0x001d2588 (scene code).

void IngameMdlResetAll(int arg)
{
    IngamePlyrMdlReset(arg);
    IngamePlyrCamMdlReset(arg);
    IngameEffSlotMdlReset(-1, arg);
}

// FF3 0x001d25c8 is the compiler's atexit thunk for a GPhaseCtrl static
// (C++ static destruction here; nothing to write).
