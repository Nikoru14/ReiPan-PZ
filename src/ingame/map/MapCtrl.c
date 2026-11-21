// ingame/map/MapCtrl.c -- map control: map start / frame / draw / release,
// and the area-level height, wall and line helpers.
// FF3 0x0011cea8..0x0011d648; FF3-only (no FF2 counterpart, name invented;
// MapAreaGetHeight / MapAreaHitLineCheck / MapAreaAtPos are FF2 MhCtl.c's
// MhCtlGetMapHeight / MhCtlHitLineCheck / MhCtlGetRoomNo, moved here and
// made door-aware).
#include "ingame/map/MapCtrl.h"

#include "common/ProcCtl.h"                     // CClassPtr
#include "graphics/graph3d/gra3d.h"             // gra3dSetGsRegisterDefault
#include "graphics/graph3d/gra3dSGD.h"          // _SetPREVIOUSTRI2PRIM
#include "graphics/graph3d/gra3dShadow.h"       // gra3dshadowClearProjectModel
#include "graphics/gs_reg.h"                    // SetGsRegCached
#include "graphics/mmanage.h"                   // ModelManager::DrawShadow
#include "ingame/ingame.h"                      // GameModel, CharPlayerModel
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/map/FurnSave.h"                // FurnSaveMain, FurnSaveFree
#include "ingame/map/LightCtl.h"                // CLightCtl
#include "ingame/map/MapDraw.h"                 // MapDrawRooms, MapDrawLightReset, MapDrawRelease
#include "ingame/map/MapEff.h"                  // MapEffResetAll, MapEffDeleteAll, MapLightRegReset
#include "ingame/map/MapLight.h"                // MapDoorVecInit, MapDoorVecRelease
#include "ingame/map/MapLoad.h"
#include "ingame/map/MapObjWrk.h"               // MapObjInit, MapObjMain, MapObjReleaseAll
#include "ingame/map/MapPut.h"                  // MapPutResetAll
#include "ingame/map/MapRcb.h"                  // RcbGetFloor
#include "ingame/map/map_height.h"              // MhGetMapHeight, MhHitLineCheck
#include "ingame/map/map_hit_check.h"           // MrecWallPushOut
#include "ingame/map/map_rectangle.h"           // MrecReset
#include "ingame/photo/n_plyr_camera.h"         // GetPlyrCameraWrk, PlyrCameraIsDrawn, PlyrCameraGetMdl
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_FLG_800000
#include "ingame/plyr/plyr_sub.h"               // AllPlyrIsReady, PlyrMdlDrawAble
#include "ingame/story_door.h"                  // MapDoorModeGet, MapDoorAreaAtPos, MapDoorRoomMhGet
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera

#define MAP_PLYR_SHADOW_DROP 16.68f     // data 0x36b89c

static int map_start_no = -1;           // sdata gp-0x7c58 (0x36b898)
static int map_chars_drawn;             // sdata gp-0x7c5c (0x36b894): MapDrawChars ran this frame

// The CLightCtl of the running map (data 0x38e6d0, ptr +8 = 0x38e6d8).
// Constructed by MapCtrl_static_init 0x11d548 (_GLOBAL__I_ 0x11d608,
// _GLOBAL__D_ 0x11d628).
static CClassPtr<CLightCtl> map_light_ctl;

// The door modes in which the player stands in the door's rooms.
static inline int MapDoorModeInDoor(int mode)
{
    return mode >= 0 && (mode < MAP_DOOR_MODE_CLOSE || mode == MAP_DOOR_MODE_WAIT);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011cea8 (IngameNowLoadingMain, before MapPlayStartReset).

void MapStartNoSet(int no)
{
    map_start_no = no;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011ceb0.

int MapAreaGetHeight(float *out, float *pos, int area, int limit)
{
    int id = MapLoadGetBuffID(area);
    int ret;

    if (id < 0)
        return -1;

    if (MapLoadGetRoomData(MapLoadGetFreeMemAddr(id), 2) == nullptr)
    {
        // FF3: one lq / sq of the whole vector.
        out[0] = pos[0];
        out[1] = pos[1];
        out[2] = pos[2];
        out[3] = pos[3];
        return 1;
    }

    ret = MhGetMapHeight(out, pos, id, limit);
    if (ret < 0 && MapDoorModeGet() == MAP_DOOR_MODE_AFT)
        ret = MhGetMapHeight(out, pos, id ^ 1, limit);
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011cf88.

int MapAreaHitLineCheck(float *from, float *to, int area)
{
    int id = MapLoadGetBuffID(area);

    if (id < 0)
        return -1;
    return MhHitLineCheck(from, to, id);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011cfe8 (PlyrMapPosUpdate and 0x13acf0).

int MapAreaHitCircle(float r, float *pos, float *pos_old, int area, short floor, int mh_no)
{
    if (MapDoorModeInDoor(MapDoorModeGet()))
    {
        int a = MapDoorAreaAtPos(floor, pos);
        int id;

        if (a >= 0)
            area = a;
        id    = MapLoadGetBuffID(area);
        mh_no = MapDoorRoomMhGet(id);
        floor = RcbGetFloor(MapLoadGetRoomData(MapLoadGetFreeMemAddr(id), 3), mh_no);
    }
    return MrecWallPushOut(r, pos, pos, pos_old, area, floor, mh_no);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d0d8.

int MapAreaAtPos(short floor, float *pos)
{
    int id;

    if (MapDoorModeInDoor(MapDoorModeGet()))
        return MapDoorAreaAtPos(floor, pos);

    id = MapLoadGetBuffID4Pos(floor, pos);
    if (id < 0)
        return id;
    return MapLoadGetRoomArea(id);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d160. FF3 calls CLightCtl::Update on the pointer unchecked;
// the in-game loop only runs after MapPlayStartReset made it.

int MapFrameUpdate(void)
{
    FurnSaveMain();
    MapObjMain();
    map_light_ctl.ptr->Update();
    MapLoadWaitMain();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The current room receives the player's shadow, its light lowered for the
// pass. FF3 0x0011d1a0.

static void MapDrawPlyrShadow(void)
{
    MAP_ROOM_DAT    *room = MapLoadGetCurRoomAddr();
    CharPlayerModel *mdl;
    G3DLIGHT        *light;

    if (room == nullptr || MapLoadGetRoomNoNow() < 0 || room->mdl.mot == nullptr || AllPlyrIsReady() == 0)
        return;

    mdl   = (CharPlayerModel *)ingame_wrk.plyr_wrk->mdl;
    light = &mdl->fl;
    light->vPosition[1] += MAP_PLYR_SHADOW_DROP;
    room->mdl.mot->DrawShadow(light);
    light->vPosition[1] -= MAP_PLYR_SHADOW_DROP;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d240.

static void MapDrawPlyrMdl(void)
{
    // PC: AllPlyrIsReady answers 1 with no player model, and there is none
    // while CharModel's load is a stub (FF3 always has one here)
    if (AllPlyrIsReady() != 0 && ingame_wrk.plyr_wrk->mdl != nullptr)
        ingame_wrk.plyr_wrk->mdl->Draw();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d278.

static void MapDrawPlyrCameraMdl(void)
{
    s_char     no;
    GameModel *mdl;

    for (no = 0; no < 2; no++)
    {
        if (PlyrCameraIsDrawn(GetPlyrCameraWrk(no), 1) != 0 && (mdl = PlyrCameraGetMdl(no)) != nullptr)
            mdl->Draw();
    }
}

// ──────────────────────────────────────────────────────────────────────
// IngameDrawSub, after MapPutDraw. skip: IngameDrawSub's draw skip bits
// (4 no shadow, 1 no player, 0x800 no camera models). FF3 0x0011d2e0.

void MapDrawChars(u_int skip)
{
    PLYR_WRK *plyr;

    if (map_chars_drawn != 0)
        return;
    map_chars_drawn = 1;

    plyr = ingame_wrk.plyr_wrk;
    if ((plyr->flags & PLYR_FLG_800000) == 0 && (plyr->_0e0 & 1) == 0)
    {
        if ((skip & 0x4) == 0)
            MapDrawPlyrShadow();
        // PC: not while the over-the-shoulder camera is inside her shoulder.
        if ((skip & 0x1) == 0 && PlyrMdlDrawAble() != 0 && !ReiPan_OverShoulderHidePlayer())
            MapDrawPlyrMdl();
    }
    if ((skip & 0x800) == 0)
        MapDrawPlyrCameraMdl();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d398 (IngameDrawSub unless skip bit 4; 0x252910).

int MapDrawAll(void)
{
    MapLoadCheckLoadNow();
    _SetPREVIOUSTRI2PRIM(nullptr);
    gra3dSetGsRegisterDefault();
    map_chars_drawn = 0;
    gra3dshadowClearProjectModel();
    MapDrawRooms(0);
    SetGsRegCached(0, 0x3f, 1);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// IngameNowLoadingMain waits on it. FF3 0x0011d3e8 (no null check).

int MapPlayStartIsReady(void)
{
    if (map_light_ctl.ptr->IsLoadEnd() == 0)
        return 0;
    map_light_ctl.ptr->Start();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d430 (IngameNowLoadingMain).
// callcheck: operator_new, LightCtlCtor (new CLightCtl)

void MapPlayStartReset(void)
{
    MrecReset();
    MapDrawLightReset();
    MapEffResetAll();
    MapObjInit();
    MapPutResetAll();
    MapDoorVecInit();

    map_light_ctl.Delete();
    map_light_ctl.ptr = new CLightCtl;
    map_light_ctl.ptr->LoadReq();

    MapLoadInit(map_start_no < 0 ? 1 : map_start_no);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011d4d8 (IngameEnd).

void MapReleaseAll(void)
{
    map_light_ctl.Delete();
    MapDoorVecRelease();
    MapEffDeleteAll();
    MapObjReleaseAll();
    FurnSaveFree();
    MapLightRegReset();
    MapDrawRelease();
}
