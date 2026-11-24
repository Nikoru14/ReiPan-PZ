/* ==========================================================================
 *  ingame/camera/map_camera.c
 *
 *  Story-mode camera controller, FF3 0x1fada8..0x1fd9c0. From MioPan
 *  ingame/camera/map_camera.c (FF2 map_camera.o), checked against FF3.
 *
 *  CameraMain() picks one driver per frame:
 *
 *    pFinCamTarget set           -> keep re-aiming at the pinned target
 *    crawling / getting up       -> FinderViewCameraCtrl (finder_view.c)
 *    PLYR_WRK._0e0 bit 0         -> CameraSetFromRotPair
 *    camera up (viewfinder)      -> the rail camera reset / step, then
 *                                   FinderViewCameraCtrl
 *    mode 10 starting            -> CamStanceMain (the stance camera)
 *    ladder (mode 5)             -> nothing (the ladder sets its own)
 *    otherwise                   -> TattooEvCameraMain's camera effect, then
 *                                   PlyrApproachCameraCtrl or
 *                                   MapCamNormalCameraCtrl
 *
 *  FF3 against FF2: no sister (the map cameras follow the current player
 *  work), camera types >= 100 are rail cameras (0x139ba0..0x13a178),
 *  the four-corner cameras gain a corner order and a player-relative target,
 *  the approach camera gains a damage framing between the player and the
 *  attacker plus the damage camera shots, the death camera's distances
 *  shrink 25 times (FF3's world scale), the quake loses its SE and gains PAL
 *  timings, and FF2's assert / debug calls are gone.
 * ======================================================================== */

#include "ingame/camera/map_camera.h"

#include <math.h>                       // sqrtf
#include <string.h>                     // memset

#include "common/vu0math.h"             // RotVectorZXY, GetRandValF
#include "graphics/effect/effect.h"     // SetEffects (6: the overlap)
#include "graphics/graph3d/gra3d.h"
#include "ingame/camera/cam_rail.h"     // the rail and stance cameras
#include "ingame/camera/dmg_cam_shot.h" // DmgCamShotInit, DmgCamShotMain
#include "ingame/camera/event_camera.h" // EventCameraMain, EventCameraInitCtrlReq
#include "ingame/enemy/ene_main.h"      // ENE_WRK
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/map/MapLoad.h"         // MapLoadGetBuffID, MapLoadGetOffsetVector
#include "ingame/map/RegDat.h"          // MB_OUT_RECT, MDAT_CAM_SP
#include "ingame/map/map_rectangle.h"   // the Mrec camera queries
#include "ingame/photo/finder_photo.h"  // GetAlbumSaveDisp
#include "ingame/photo/finder_view.h"   // FinderViewTurnMain, FinderViewCameraCtrl
#include "ingame/photo/photo.h"         // PeepStateClear, CameraSetFromRotPair
#include "ingame/plyr/player.h"
#include "ingame/plyr/plyr_gauge.h"     // PlyrGaugeTimeStep
#include "ingame/plyr/plyr_mdl.h"       // PlyrFinderModeCancel
#include "ingame/plyr/unit_ctl.h"       // UnitGetTrgtRot, GetMidPos
#include "outgame/option.h"             // GetOptionWrk
#include "sdk/libvu0.h"
#include "system/os/system.h"           // GetPALMode, GetFrameTimeScale
#include "ingame/tattoo_event.h"         // TattooEvCameraMain
#include "system/pad/pad.h"             // pad_wrk
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera

// ──────────────────────────────────────────────────────────────────────
// Callees whose file is not settled yet.

// ──────────────────────────────────────────────────────────────────────

// The map camera's state (bss 0x3f3250; FF2 MAP_CAMERA_CTRL without the
// sister's CamTarget).
// layout: host -- pFinCamTarget / pRectStat are EE pointers at +0x0c / +0x10.
struct MAP_CAMERA_CTRL                  /* 0x20 on the EE */
{
    /* 0x00 */ int    EventCamFlg;      // event_camera.c owns the camera
    /* 0x04 */ int    SpecialCamFlg;    // last frame used a four-corner camera
    /* 0x08 */ int    RoomNo;           // GetPlyrAreaNo at the last camera change (passed on, unused)
    /* 0x0c */ float *pFinCamTarget;    // MapCamSetFinCamera's target, nullptr: none
    /* 0x10 */ void  *pRectStat;        // MAP_CAM_DATA (FF2 MDAT_CAM) or MDAT_CAM_SP, by CamDataType
    /* 0x14 */ int    CamDataType;      // rectangle type in force, -1: none
    /* 0x18 */ int    BattleCamAccept;  // the player has left the battle rectangle once
    /* 0x1c */ int    RectCamLastTime;  // the rectangle camera ran last frame
};
static_assert(offsetof(MAP_CAMERA_CTRL, SpecialCamFlg) == 0x04, "MAP_CAMERA_CTRL.SpecialCamFlg");
static_assert(offsetof(MAP_CAMERA_CTRL, RoomNo) == 0x08, "MAP_CAMERA_CTRL.RoomNo");

// The quake shake (bss 0x3f3340).
struct QUAKE_CAMERA_CTRL                /* 0x14 */
{
    /* 0x00 */ float Power[2];          // [0] eye shake, [1] target shake
    /* 0x08 */ int   Counter;
    /* 0x0c */ int   AllTime;
    /* 0x10 */ short RequestFlg;
    /* 0x12 */ short LoopNum;           // aftershocks left
};
static_assert(sizeof(QUAKE_CAMERA_CTRL) == 0x14, "QUAKE_CAMERA_CTRL size");
static_assert(offsetof(QUAKE_CAMERA_CTRL, RequestFlg) == 0x10, "QUAKE_CAMERA_CTRL.RequestFlg");
static_assert(offsetof(QUAKE_CAMERA_CTRL, LoopNum) == 0x12, "QUAKE_CAMERA_CTRL.LoopNum");

static MAP_CAMERA_CTRL   map_camera_ctrl;   /* bss 0x3f3250 */
static APPROACH_CAMERA   app_camera;        /* bss 0x3f3270 */
static QUAKE_CAMERA_CTRL QuakeCameraCtrl;   /* bss 0x3f3340 */

static u_short ori_fior_tm;                 /* sdata 0x37105c */
static u_short fior_tm;                     /* sdata 0x37105e */


// Frames the approach slide and the damage framing take (NTSC; PAL divides
// by GetFrameTimeScale). FF2's slide was 20.
#define APPROACH_MOVE_FRAME 29.0f

static void MapCamSpCameraCtrl(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position);
static void MapCamRectangleCameraCtrl(PLYR_WRK *plyr, int DataType, int RectCamFlg);
static void MapCamUpdateRoomNo(MAP_CAMERA_CTRL *pMcCtrl);
static void FinderInOverRapCtrl(void);
static void PlyrApproachCameraCtrl(int RectCamFlg);
static void PlyrShoulderCameraSet(float *Position, float *Target);
static void QuakeCameraInit(void);
static int  QuakeCameraMain(void);

// The XYZ distance of two points (the inline VU0 length the ROM uses; the
// four-corner cameras' points all have y 0).
static float MapCamLength(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ──────────────────────────────────────────────────────────────────────
// The current camera record and its rectangle type (IngameSetDrawEnable,
// PlyrMapCamCheck). FF3-only getters.
// FF3 0x001fada8.

int MapCamGetCurType(void)
{
    return map_camera_ctrl.CamDataType;
}

// FF3 0x001fadb8. Types 1 / 8 keep an MDAT_CAM here, type 9 an MDAT_CAM_SP;
// MAP_CAM_DATA names the words of both that the callers read.

MAP_CAM_DATA *MapCamGetCurData(void)
{
    return static_cast<MAP_CAM_DATA *>(map_camera_ctrl.pRectStat);
}

// ──────────────────────────────────────────────────────────────────────
// The per-frame camera (see the file comment). Always returns 0.
// FF3 0x001fadc8; FF2 CameraMain 0x001d9338 (from MioPan map_camera.c:136,
// FF3's dispatch on the player's flags and modes replaces FF2's finder
// modes 5 / 6).

int CameraMain(void)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    int              RectCamFlg = pMcCtrl->RectCamLastTime;
    int              type;
    int              event_cam;

    // PC: the over-the-shoulder camera puts the game's own camera back first,
    // so the cameras below ease from their own shot.
    ReiPan_OverShoulderBeginFrame();

    pMcCtrl->RectCamLastTime = 0;

    if (pMcCtrl->pFinCamTarget != nullptr)
    {
        ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_PINNED);  // PC
        gra3dcamSetTarget(pMcCtrl->pFinCamTarget, 1);
        gra3dApplyCamera(nullptr, 1, 1);
    }
    else
    {
        if (ingame_wrk.plyr_wrk->flags & PLYR_FLG_400000)
            FinderViewTurnMain();

        if (ingame_wrk.plyr_wrk->flags & (PLYR_FLG_8000000 | PLYR_FLG_10000000))
        {
            ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_CRAWL);  // PC
            FinderViewCameraCtrl();
        }
        else if (ingame_wrk.plyr_wrk->_0e0 & 1)
        {
            ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_PEEP);  // PC
            CameraSetFromRotPair();
        }
        else if (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000)
        {
            ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_FINDER);  // PC
            type = MrecGetCameraType();
            if (type < 100)
            {
                CamRailOff();
            }
            else
            {
                CamRailStart(GetPlyrAreaNo(), type);
                CamRailMain();
            }
            FinderViewCameraCtrl();
            TattooEvCameraMain(RectCamFlg);
        }
        else if (PlyrMode10IsStarting(ingame_wrk.plyr_wrk))
        {
            ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_STANCE);  // PC
            CamStanceMain();
        }
        else if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_5)
        {
            if (ingame_wrk.plyr_wrk->mode == PLYR_MODE_7 && app_camera.mode != 0)
            {
                ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_APPROACH);  // PC
                PlyrApproachCameraCtrl(RectCamFlg);
            }
            else if (TattooEvCameraMain(RectCamFlg) != 0)
            {
                if (app_camera.mode != 0)
                {
                    ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_APPROACH);  // PC
                    PlyrApproachCameraCtrl(RectCamFlg);
                }
                else
                {
                    event_cam = pMcCtrl->EventCamFlg;
                    MapCamNormalCameraCtrl(RectCamFlg);
                    // PC: the ordinary room camera, not an event's: the shot
                    // the over-the-shoulder camera may replace.
                    ReiPan_OverShoulderNoteCamera(event_cam == 0 ? REIPAN_OTS_CAM_MAP
                                                                 : REIPAN_OTS_CAM_EVENT);
                }
            }
            else
            {
                ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_TATTOO);  // PC
            }
        }
        else
        {
            ReiPan_OverShoulderNoteCamera(REIPAN_OTS_CAM_LADDER);  // PC
        }
    }

    // PC: the over-the-shoulder camera, when it may replace this frame's shot;
    // before the quake, which shakes whichever camera is on screen.
    ReiPan_OverShoulderCameraCtrl();

    if (QuakeCameraCtrl.RequestFlg != 0)
        QuakeCameraMain();

    FinderInOverRapCtrl();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Reset at the start of a game; FF3 also clears the furniture photo flag.
// FF3 0x001faf98; FF2 CameraMainInit 0x001d9410 (from MioPan
// map_camera.c:188, no CamTarget).

void CameraMainInit(void)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;

    pMcCtrl->EventCamFlg     = 0;
    pMcCtrl->CamDataType     = -1;
    pMcCtrl->RectCamLastTime = 0;
    pMcCtrl->SpecialCamFlg   = 0;
    pMcCtrl->RoomNo          = -1;
    pMcCtrl->pFinCamTarget   = nullptr;
    pMcCtrl->pRectStat       = nullptr;
    pMcCtrl->BattleCamAccept = 0;

    memset(&app_camera, 0, sizeof(APPROACH_CAMERA));
    QuakeCameraInit();
    PeepStateClear();
}

// ──────────────────────────────────────────────────────────────────────
// The event camera takes the camera (1) or gives it back (0, and its work
// is cleared).
// FF3 0x001faff8; FF2 MapCamSetEventCameraFlg 0x001d9470 (both clear the
// work with EventCameraInitCtrlReq).

void MapCamSetEventCameraFlg(int flg)
{
    if (flg != 0)
    {
        map_camera_ctrl.EventCamFlg = 1;
        return;
    }
    map_camera_ctrl.EventCamFlg = 0;
    EventCameraInitCtrlReq();
}

// FF3 0x001fb030; FF2 MapCamGetEventCameraFlg 0x001d94a8.

int MapCamGetEventCameraFlg(void)
{
    return map_camera_ctrl.EventCamFlg;
}

// ──────────────────────────────────────────────────────────────────────
// The map-data camera: the event camera while it runs, else the camera of
// the rectangle the player stands in -- a battle rectangle (8) while an
// enemy is active and the player has left one since, the same rectangle as
// last frame, a four-corner one (9) or the ordinary one (1). RectCamFlg: the
// rectangle camera also ran last frame (no snap).
// FF3 0x001fb040; FF2 MapCamNormalCameraCtrl 0x001d94b8 (from MioPan
// map_camera.c:243; FF3 follows the current player, drops the floating
// ghosts' CamChangeFlg, and keeps the old record when the four-corner query
// fails).

void MapCamNormalCameraCtrl(int RectCamFlg)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    PLYR_WRK        *plyr = ingame_wrk.plyr_wrk;
    MDAT_CAM_SP     *pCamSp;
    MB_OUT_RECT     *pRect;
    int              GetCamDataType;

    if (pMcCtrl->EventCamFlg != 0)
    {
        if (EventCameraMain() != 0)
            pMcCtrl->EventCamFlg = 0;
        return;
    }

    GetCamDataType = -1;

    if ((plyr->flags & PLYR_FLG_000020) == 0)
    {
        pMcCtrl->BattleCamAccept = 0;
    }
    else if (MrecCheckHitRect(plyr->floor, plyr->pos, 8) == 0)
    {
        pMcCtrl->BattleCamAccept = 1;
    }
    else if (pMcCtrl->BattleCamAccept != 0)
    {
        GetCamDataType = 8;
    }

    if (GetCamDataType == -1)
    {
        if (pMcCtrl->CamDataType != -1
            && MrecCheckOnSameCamRect(pMcCtrl->pRectStat, plyr->floor, plyr->pos, pMcCtrl->CamDataType) != 0)
        {
            GetCamDataType = pMcCtrl->CamDataType;
        }
        else
        {
            GetCamDataType = MrecCheckHitRect(plyr->floor, plyr->pos, 9) ? 9 : 1;
        }
    }

    if (GetCamDataType == 9)
    {
        if (pMcCtrl->SpecialCamFlg == 0)
            MapCamUpdateRoomNo(pMcCtrl);
        pMcCtrl->SpecialCamFlg = 1;

        pCamSp = MrecGetCameraSpInfo(&pRect, pMcCtrl->pRectStat, plyr->floor, plyr->pos);
        if (pCamSp != nullptr && pRect != nullptr)
        {
            MapCamSpCameraCtrl(pCamSp, pRect, plyr->pos);
            pMcCtrl->pRectStat   = pCamSp;
            pMcCtrl->CamDataType = GetCamDataType;
        }
    }
    else
    {
        if (pMcCtrl->SpecialCamFlg == 1)
            MapCamUpdateRoomNo(pMcCtrl);
        pMcCtrl->SpecialCamFlg = 0;

        MapCamRectangleCameraCtrl(plyr, GetCamDataType, RectCamFlg);
        pMcCtrl->pRectStat   = MrecGetCameraInfo();
        pMcCtrl->CamDataType = GetCamDataType;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The four corners of a rectangle as XZ points (y 0, w 1). FF3-only helper
// (FF2 inlined it).
// FF3 0x001fb1e8.

static void MapCamSpGetRectPoints(float (*Point)[4], MB_OUT_RECT *pRect)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        Point[i][0] = pRect->vec[i][0];
        Point[i][1] = 0.0f;
        Point[i][2] = pRect->vec[i][2];
        Point[i][3] = 1.0f;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Which record sits at each corner: the record's own corner number, or its
// index when that is not 0..3. FF3-only.
// FF3 0x001fb258.

static void MapCamSpGetCornerOrder(int *Order, MDAT_CAM_SP *pCamSp)
{
    int i;

    for (i = 0; i < 4; i++)
        Order[i] = pCamSp->st[i].corner > 3 ? i : (int)pCamSp->st[i].corner;
}

// ──────────────────────────────────────────────────────────────────────
// Foot of the perpendicular from Point onto the line Line0-Line1 in the XZ
// plane; a degenerate line gives Point itself.
// FF3 0x001fb290; FF2 MapCamGetPerpendicularNode (from MioPan
// map_camera.c:543).

static void MapCamGetPerpendicularNode(float *Node, float *Line0, float *Line1, float *Point)
{
    float LineX10 = Line1[0] - Line0[0];
    float LineY10 = Line1[2] - Line0[2];
    float dot;
    float len2;

    if (LineX10 == 0.0f && LineY10 == 0.0f)
    {
        Node[0] = Point[0];
        Node[1] = 0.0f;
        Node[2] = Point[2];
        Node[3] = 1.0f;
        return;
    }

    len2 = LineY10 * LineY10 + LineX10 * LineX10;
    dot  = (Point[2] - Line0[2]) * LineY10 + (Point[0] - Line0[0]) * LineX10;
    Node[1] = 0.0f;
    Node[0] = (LineX10 * dot) / len2 + Line0[0];
    Node[3] = 1.0f;
    Node[2] = (LineY10 * dot) / len2 + Line0[2];
}

// ──────────────────────────────────────────────────────────────────────
// Blends the four corner records with the weights w (by corner) into the
// camera: eye with the room offset, target with the room offset or, with
// PlyrRel, relative to the player position, roll and fov. The common tail
// of the two four-corner cameras (FF2 wrote it out in each; so does FF3).

static void MapCamSpCameraApply(MDAT_CAM_SP *pCamSp, const float *w, float scale, float *Position, int PlyrRel)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    MDAT_CAM_SP_ONE *st[4];
    sceVu0FVECTOR    CamPosition;
    sceVu0FVECTOR    CamTarget;
    int              Order[4];
    int              i;

    MapCamSpGetCornerOrder(Order, pCamSp);
    for (i = 0; i < 4; i++)
        st[i] = &pCamSp->st[Order[i]];

    for (i = 0; i < 3; i++)
        CamPosition[i] = (st[2]->Cam_Pos[i] * w[2] + st[0]->Cam_Pos[i] * w[0]
                          + st[3]->Cam_Pos[i] * w[3] + st[1]->Cam_Pos[i] * w[1]) * scale;
    CamPosition[3] = 1.0f;
    MapCamGra3dcamSetPositionAddOffset(CamPosition, pMcCtrl->RoomNo);

    for (i = 0; i < 3; i++)
        CamTarget[i] = (st[2]->View_Pos[i] * w[2] + st[0]->View_Pos[i] * w[0]
                        + st[3]->View_Pos[i] * w[3] + st[1]->View_Pos[i] * w[1]) * scale;
    CamTarget[3] = 0.0f;
    if (PlyrRel == 0)
    {
        MapCamGra3dcamSetTargetAddOffset(CamTarget, pMcCtrl->RoomNo);
    }
    else
    {
        sceVu0AddVector(CamTarget, CamTarget, Position);
        gra3dcamSetTarget(CamTarget, 1);
    }

    gra3dcamSetRoll((st[2]->RotZ * w[2] + st[0]->RotZ * w[0] + st[3]->RotZ * w[3] + st[1]->RotZ * w[1]) * scale);
    gra3dcamSetFov((st[2]->Proj * w[2] + st[0]->Proj * w[0] + st[3]->Proj * w[3] + st[1]->Proj * w[1]) * scale);

    gra3dApplyCamera(nullptr, 0, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Diagonal blend: the two diagonals P0-P2 and P1-P3 each give a 0..1
// parameter from the player's perpendicular foot; the four corner weights
// sum to two, hence the 0.5. PlyrRel: the target is relative to the player.
// FF3 0x001fb370; FF2 MapCamSpCameraDiagonal (from MioPan map_camera.c:357;
// FF3 adds the corner order and PlyrRel).
// callcheck: MapCamGra3dcamSetPositionAddOffset, MapCamGra3dcamSetTargetAddOffset, MapCamSpGetCornerOrder, gra3dApplyCamera, gra3dcamSetFov, gra3dcamSetRoll, gra3dcamSetTarget (in MapCamSpCameraApply)

static void MapCamSpCameraDiagonal(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position, int PlyrRel)
{
    sceVu0FVECTOR Point[4];
    sceVu0FVECTOR Node0;
    sceVu0FVECTOR Node1;
    float         w[4];
    float         Length02;
    float         Length13;
    float         d;
    int           i;

    if (pCamSp == nullptr || pRect == nullptr)
        return;

    MapCamSpGetRectPoints(Point, pRect);

    MapCamGetPerpendicularNode(Node0, Point[0], Point[2], Position);
    Length02 = MapCamLength(Point[0], Point[2]);
    w[2] = 0.0f;
    w[0] = 1.0f;
    if (Length02 != 0.0f)
    {
        d    = MapCamLength(Point[0], Node0);
        w[2] = d / Length02;
        w[0] = (Length02 - d) / Length02;
    }

    MapCamGetPerpendicularNode(Node1, Point[1], Point[3], Position);
    Length13 = MapCamLength(Point[1], Point[3]);
    w[3] = 0.0f;
    w[1] = 1.0f;
    if (Length13 != 0.0f)
    {
        d    = MapCamLength(Point[1], Node1);
        w[3] = d / Length13;
        w[1] = (Length13 - d) / Length13;
    }

    for (i = 0; i < 4; i++)
    {
        if (w[i] < 0.0f)
            w[i] = 0.0f;
        if (w[i] > 1.0f)
            w[i] = 1.0f;
    }

    MapCamSpCameraApply(pCamSp, w, 0.5f, Position, PlyrRel);
}

// ──────────────────────────────────────────────────────────────────────
// Parallel blend: the adjacent edges P0-P1 and P1-P2 each give a 0..1
// parameter and the corner weights are their bilinear products.
// FF3 0x001fb890; FF2 MapCamSpCameraParallel (from MioPan map_camera.c:450;
// FF3 adds the corner order and PlyrRel).
// callcheck: MapCamGra3dcamSetPositionAddOffset, MapCamGra3dcamSetTargetAddOffset, MapCamSpGetCornerOrder, gra3dApplyCamera, gra3dcamSetFov, gra3dcamSetRoll, gra3dcamSetTarget (in MapCamSpCameraApply)

static void MapCamSpCameraParallel(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position, int PlyrRel)
{
    sceVu0FVECTOR Point[4];
    sceVu0FVECTOR Node0;
    sceVu0FVECTOR Node1;
    float         w[4];
    float         Length01;
    float         Length12;
    float         Influence03 = 1.0f;
    float         Influence12 = 0.0f;
    float         Influence01 = 1.0f;
    float         Influence23 = 0.0f;
    float         d;

    MapCamSpGetRectPoints(Point, pRect);

    MapCamGetPerpendicularNode(Node0, Point[0], Point[1], Position);
    Length01 = MapCamLength(Point[0], Point[1]);
    if (Length01 != 0.0f)
    {
        d           = MapCamLength(Point[0], Node0);
        Influence12 = d / Length01;
        Influence03 = (Length01 - d) / Length01;
    }

    MapCamGetPerpendicularNode(Node1, Point[1], Point[2], Position);
    Length12 = MapCamLength(Point[1], Point[2]);
    if (Length12 != 0.0f)
    {
        d           = MapCamLength(Point[1], Node1);
        Influence23 = d / Length12;
        Influence01 = (Length12 - d) / Length12;
    }

    w[3] = Influence23 * Influence03;
    w[1] = Influence01 * Influence12;
    w[0] = Influence01 * Influence03;
    w[2] = Influence23 * Influence12;

    MapCamSpCameraApply(pCamSp, w, 1.0f, Position, PlyrRel);
}

// ──────────────────────────────────────────────────────────────────────
// The four-corner camera: record type 0 / 2 parallel, 3 and the rest
// diagonal; 2 and 3 aim relative to the player.
// FF3 0x001fbd00; FF2 MapCamSpCameraCtrl 0x001d9720 (from MioPan
// map_camera.c:330, FF3's types 2 / 3 added).

static void MapCamSpCameraCtrl(MDAT_CAM_SP *pCamSp, MB_OUT_RECT *pRect, float *Position)
{
    if (pCamSp == nullptr || pRect == nullptr)
        return;

    MapCamUpdateRoomNo(&map_camera_ctrl);

    switch (pCamSp->type)
    {
    case 0:
        MapCamSpCameraParallel(pCamSp, pRect, Position, 0);
        break;
    case 2:
        MapCamSpCameraParallel(pCamSp, pRect, Position, 1);
        break;
    case 3:
        MapCamSpCameraDiagonal(pCamSp, pRect, Position, 1);
        break;
    default:
        MapCamSpCameraDiagonal(pCamSp, pRect, Position, 0);
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The record's roll (when there is a record) and fov (when positive).
// FF3 0x001fbdf0; FF2 MapCamSetFovRoll 0x001da1f0 (from MioPan
// map_camera.c:612).

static void MapCamSetFovRoll(void)
{
    float Roll;
    float Fov;

    if (MrecGetCameraRotZ(&Roll) != 0)
        gra3dcamSetRoll(Roll);

    Fov = MrecGetCameraPrj();
    if (Fov > 0.0f)
        gra3dcamSetFov(Fov);
}

// ──────────────────────────────────────────────────────────────────────
// Eye and target straight from the record.
// FF3 0x001fbfc0; FF2 MapCamTypeFix 0x001da240 (from MioPan map_camera.c:629).

static void MapCamTypeFix(void)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    sceVu0FVECTOR    Position;
    sceVu0FVECTOR    Target;

    if (MrecGetCameraPos(Position) == 1)
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);

    if (MrecGetCameraInterest(Target) == 1)
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);
}

// ──────────────────────────────────────────────────────────────────────
// The eye rides the player (plus the record's offset); the target is the
// record's.
// FF3 0x001fc030; FF2 MapCamTypePositionFollow 0x001da2b0 (from MioPan
// map_camera.c:657).

static void MapCamTypePositionFollow(int init)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    sceVu0FVECTOR    Position;
    sceVu0FVECTOR    Target;
    sceVu0FVECTOR    Offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    float           *NowPosition;
    float            margin;

    if (MapCamGetObjPosition(Position, 0) == 0)
    {
        NowPosition = gra3dcamGetPosition();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraPos(Offset);
                sceVu0AddVector(Position, Position, Offset);
                MapCamGra3dcamSetPositionMargin(margin, Position, NowPosition);
            }
        }
        else
        {
            MrecGetCameraPos(Offset);
            sceVu0AddVector(Position, Position, Offset);
            Position[3] = 1.0f;
            gra3dcamSetPosition(Position);
        }
    }

    if (MrecGetCameraInterest(Target) == 1)
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);
}

// ──────────────────────────────────────────────────────────────────────
// The record's eye; the target rides the player.
// FF3 0x001fc138; FF2 MapCamTypeTargetFollow 0x001da3d0 (from MioPan
// map_camera.c:695).

static void MapCamTypeTargetFollow(int init)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    sceVu0FVECTOR    Position;
    sceVu0FVECTOR    Target;
    sceVu0FVECTOR    Offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    float           *NowTarget;
    float            margin;

    if (MrecGetCameraPos(Position) == 1)
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);

    if (MapCamGetObjPosition(Target, 0) == 0)
    {
        NowTarget = gra3dcamGetTarget();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraInterest(Offset);
                sceVu0AddVector(Target, Target, Offset);
                MapCamGra3dcamSetTargetMargin(margin, Target, NowTarget);
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);
            sceVu0AddVector(Target, Target, Offset);
            Target[3] = 1.0f;
            gra3dcamSetTarget(Target, 1);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Both ends ride the player: a trailing camera.
// FF3 0x001fc258; FF2 MapCamTypePositionTargetFollow 0x001da4f8 (from
// MioPan map_camera.c:734).

static void MapCamTypePositionTargetFollow(int init)
{
    sceVu0FVECTOR Position;
    sceVu0FVECTOR Target;
    sceVu0FVECTOR Offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    float        *Now;
    float         margin;

    if (MapCamGetObjPosition(Position, 0) == 0)
    {
        Now = gra3dcamGetPosition();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraPos(Offset);
                sceVu0AddVector(Position, Position, Offset);
                MapCamGra3dcamSetPositionMargin(margin, Position, Now);
            }
        }
        else
        {
            MrecGetCameraPos(Offset);
            sceVu0AddVector(Position, Position, Offset);
            Position[3] = 1.0f;
            gra3dcamSetPosition(Position);
        }
    }

    if (MapCamGetObjPosition(Target, 0) == 0)
    {
        Now = gra3dcamGetTarget();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraInterest(Offset);
                sceVu0AddVector(Target, Target, Offset);
                MapCamGra3dcamSetTargetMargin(margin, Target, Now);
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);
            sceVu0AddVector(Target, Target, Offset);
            Target[3] = 1.0f;
            gra3dcamSetTarget(Target, 1);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// As MapCamTypeTargetFollow, keeping the target's height while it follows.
// FF3 0x001fc3f0; FF2 MapCamTypeTargetFollowYFix 0x001da690 (from MioPan
// map_camera.c:786). FF3's snap (init) reads the record's offset but aims at
// the bare player position.

static void MapCamTypeTargetFollowYFix(int init)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    sceVu0FVECTOR    Position;
    sceVu0FVECTOR    Target;
    sceVu0FVECTOR    Offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    float           *NowTarget;
    float            margin;

    if (MrecGetCameraPos(Position) == 1)
        MapCamGra3dcamSetPositionAddOffset(Position, pMcCtrl->RoomNo);

    if (MapCamGetObjPosition(Target, 0) == 0)
    {
        NowTarget = gra3dcamGetTarget();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraInterest(Offset);
                sceVu0AddVector(Target, Target, Offset);
                Target[1] = NowTarget[1];
                MapCamGra3dcamSetTargetMargin(margin, Target, NowTarget);
            }
        }
        else
        {
            MrecGetCameraInterest(Offset);
            Target[3] = 1.0f;
            gra3dcamSetTarget(Target, 1);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// As MapCamTypePositionFollow, keeping the eye's height while it follows.
// FF3 0x001fc508; FF2 MapCamTypePositionFollowYFix 0x001da7c0 (from MioPan
// map_camera.c:826).

static void MapCamTypePositionFollowYFix(int init)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    sceVu0FVECTOR    Position;
    sceVu0FVECTOR    Target;
    sceVu0FVECTOR    Offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    float           *NowPosition;
    float            margin;

    if (MapCamGetObjPosition(Position, 0) == 0)
    {
        NowPosition = gra3dcamGetPosition();
        if (init == 0)
        {
            margin = MrecGetCameraAsobi();
            if (margin >= 0.0f)
            {
                MrecGetCameraPos(Offset);
                sceVu0AddVector(Position, Position, Offset);
                Position[1] = NowPosition[1];
                MapCamGra3dcamSetPositionMargin(margin, Position, NowPosition);
            }
        }
        else
        {
            MrecGetCameraPos(Offset);
            sceVu0AddVector(Position, Position, Offset);
            Position[3] = 1.0f;
            gra3dcamSetPosition(Position);
        }
    }

    if (MrecGetCameraInterest(Target) == 1)
        MapCamGra3dcamSetTargetAddOffset(Target, pMcCtrl->RoomNo);
}

// ──────────────────────────────────────────────────────────────────────
// The ordinary (single record) rectangle camera. MrecSetCameraInfo latches
// the record at the player; a change or a fresh entry (RectCamFlg 0) snaps
// the follow modes. Types >= 100 are FF3's rail cameras.
// FF3 0x001fbe40; FF2 MapCamRectangleCameraCtrl 0x001da0f8 (from MioPan
// map_camera.c:566).

static void MapCamRectangleCameraCtrl(PLYR_WRK *plyr, int DataType, int RectCamFlg)
{
    MAP_CAMERA_CTRL *pMcCtrl = &map_camera_ctrl;
    int              NoCamera = 0;
    int              InitFlg = 0;
    int              ret;
    int              type;

    pMcCtrl->RectCamLastTime = 1;

    ret = MrecSetCameraInfo(plyr->floor, plyr->pos, DataType);
    if (ret == 1)
        MapCamUpdateRoomNo(pMcCtrl);
    if (ret != 0 || RectCamFlg == 0)
        InitFlg = 1;

    type = MrecGetCameraType();
    if (type < 100)
    {
        CamRailOff();
        switch (type)
        {
        case 0:
        case 4:
            MapCamTypeFix();
            break;
        case 1:
        case 5:
            MapCamTypePositionFollow(InitFlg);
            break;
        case 2:
        case 6:
            MapCamTypeTargetFollow(InitFlg);
            break;
        case 3:
        case 7:
            MapCamTypePositionTargetFollow(InitFlg);
            break;
        case 8:
            MapCamTypePositionFollowYFix(InitFlg);
            break;
        case 9:
            MapCamTypeTargetFollowYFix(InitFlg);
            break;
        default:
            NoCamera = 1;
            break;
        }
        if (NoCamera == 0)
            MapCamSetFovRoll();
    }
    else
    {
        if (InitFlg != 0)
            CamRailStart(GetPlyrAreaNo(), type);
        CamRailMain();
    }

    gra3dApplyCamera(nullptr, 0, 1);
}

// ──────────────────────────────────────────────────────────────────────
// The followed position: the current player's. Always 0.
// FF3 0x001fc618; FF2 MapCamGetObjPosition 0x001da8e8 (no sister in FF3).

int MapCamGetObjPosition(float *pos, int obj_id)
{
    (void)obj_id;
    sceVu0CopyVector(pos, ingame_wrk.plyr_wrk->pos);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Starts the finder overlap for time frames; FF3 also turns the approach
// camera's crossfade on.
// FF3 0x001fc638; FF2 ReqFinderInOverRap 0x001da908.

void ReqFinderInOverRap(u_short time)
{
    ori_fior_tm = time;
    fior_tm     = time;
    ApproachCameraCrossFadeSW(1);
}

// FF3 0x001fc668; FF2 FinderInOverRapCtrl 0x001da930.

static void FinderInOverRapCtrl(void)
{
    if (fior_tm != 0)
    {
        fior_tm--;
        SetEffects(6, 1, (int)ori_fior_tm);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Eye / target from room-relative map data: plus the origin of the player's
// area buffer. room is not used.
// FF3 0x001fc6a8; FF2 MapCamGra3dcamSetPositionAddOffset 0x001da968 (FF3
// goes through MapLoadGetBuffID).

void MapCamGra3dcamSetPositionAddOffset(float *vec, int room)
{
    sceVu0FVECTOR CamPos;
    sceVu0FVECTOR RoomCenter;

    (void)room;
    MapLoadGetOffsetVector(RoomCenter, MapLoadGetBuffID(GetPlyrAreaNo()));
    sceVu0AddVector(CamPos, RoomCenter, vec);
    CamPos[3] = 1.0f;
    gra3dcamSetPosition(CamPos);
}

// FF3 0x001fc718; FF2 MapCamGra3dcamSetTargetAddOffset 0x001da9c8.

void MapCamGra3dcamSetTargetAddOffset(float *vec, int room)
{
    sceVu0FVECTOR CamTarget;
    sceVu0FVECTOR RoomCenter;

    (void)room;
    MapLoadGetOffsetVector(RoomCenter, MapLoadGetBuffID(GetPlyrAreaNo()));
    sceVu0AddVector(CamTarget, RoomCenter, vec);
    CamTarget[3] = 1.0f;
    gra3dcamSetTarget(CamTarget, 1);
}

// ──────────────────────────────────────────────────────────────────────
// NextPos = NowPos moved towards TargetPos, stopping margin short of it;
// 0 (NextPos untouched) when they are already that close.
// FF3 0x001fc810; FF2 MapCamCalcFollowPointWithMargin 0x001daae0 (from
// MioPan map_camera.c:1013).

static int MapCamCalcFollowPointWithMargin(float margin, float *NextPos, float *TargetPos, float *NowPos)
{
    float length = MapCamLength(NowPos, TargetPos);

    if (margin < length && length != 0.0f)
    {
        sceVu0SubVector(NextPos, TargetPos, NowPos);
        sceVu0ScaleVector(NextPos, NextPos, (length - margin) / length);
        sceVu0AddVector(NextPos, NextPos, NowPos);
        NextPos[3] = 1.0f;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Follow with slack; inside the margin the current eye is set again.
// FF3 0x001fc788; FF2 MapCamGra3dcamSetPositionMargin 0x001daa30.

void MapCamGra3dcamSetPositionMargin(float margin, float *pos, float *now)
{
    sceVu0FVECTOR DirVec;

    if (MapCamCalcFollowPointWithMargin(margin, DirVec, pos, now) != 0)
        gra3dcamSetPosition(DirVec);
    else
        gra3dcamSetPosition(gra3dcamGetPosition());
}

// FF3 0x001fc7c8; FF2 MapCamGra3dcamSetTargetMargin 0x001daa88.

void MapCamGra3dcamSetTargetMargin(float margin, float *pos, float *now)
{
    sceVu0FVECTOR DirVec;

    if (MapCamCalcFollowPointWithMargin(margin, DirVec, pos, now) != 0)
        gra3dcamSetTarget(DirVec, 1);
    else
        gra3dcamSetTarget(gra3dcamGetTarget(), 1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001fc8c0; FF2 MapCamUpdateRoomNo 0x001dabb0.

static void MapCamUpdateRoomNo(MAP_CAMERA_CTRL *pMcCtrl)
{
    pMcCtrl->RoomNo = GetPlyrAreaNo();
}

// ──────────────────────────────────────────────────────────────────────
// Pins the camera; CameraMain keeps re-aiming at *target until
// MapCamCutFinCamera.
// FF3 0x001fc8e8; FF2 MapCamSetFinCamera 0x001dabe8.

void MapCamSetFinCamera(float *pos, float *target)
{
    map_camera_ctrl.pFinCamTarget = target;
    gra3dcamSetPosition(pos);
    gra3dcamSetTarget(target, 1);
    gra3dApplyCamera(nullptr, 1, 1);
}

// FF3 0x001fc930; FF2 MapCamCutFinCamera 0x001dac30.

void MapCamCutFinCamera(void)
{
    map_camera_ctrl.pFinCamTarget = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The approach camera (PlyrDeathCtrl): slide to dist from ipos raised by
// offy, starting at dist2 (left from the last damage request).
// FF3 0x001fc940; FF2 ReqPlyrApproachCameraCtrl 0x001dac40.

void ReqPlyrApproachCameraCtrl(float offy, float dist, float *ipos)
{
    app_camera.mode = 1;
    app_camera.offy = offy;
    app_camera.dist = dist;
    app_camera.flow = APPROACH_CAMERA_FLOW_INIT;
    sceVu0CopyVector(app_camera.ipos, ipos);
    app_camera.pEneWrk = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The damage camera (PlyrDamageCameraReq, PlyrDeathCtrl): frames the player
// and the attacker ene, easing from dist2 to dist; the shoulder camera's
// target starts at the enemy's mpos.
// FF3 0x001fc970; FF2 ReqPlyrDamageCameraCtrl 0x001dac70 (FF3 keeps the two
// followed points instead of computing the shoulder eye here).

void ReqPlyrDamageCameraCtrl(float offy, float dist2, float dist, float *ipos, ENE_WRK *ene)
{
    app_camera.mode  = 1;
    app_camera.offy  = offy;
    app_camera.flow  = APPROACH_CAMERA_FLOW_DAMAGE_INIT;
    app_camera.dist  = dist;
    app_camera.dist2 = dist2;
    sceVu0CopyVector(app_camera.ipos, ipos);
    app_camera.pEneWrk = ene;
    app_camera.cnt     = 0.0f;
    sceVu0CopyVector(app_camera.NeckInitVector, ene->mpos);
    app_camera.pEnePos  = ene->pos;
    app_camera.pPlyrPos = ingame_wrk.plyr_wrk->pos;
}

// ──────────────────────────────────────────────────────────────────────
// The death camera: looks at the ground in front of the player's facing
// trot and sits offy2 above ipos.
// FF3 0x001fc9d0; FF2 ReqPlyrDeadCameraCtrl 0x001dace8.

void ReqPlyrDeadCameraCtrl(float offy, float offy2, float dist, float *ipos, float *trot)
{
    app_camera.mode  = 1;
    app_camera.offy  = offy;
    app_camera.flow  = APPROACH_CAMERA_FLOW_DEAD_INIT;
    app_camera.offy2 = offy2;
    app_camera.dist  = dist;
    sceVu0CopyVector(app_camera.ipos, ipos);
    sceVu0CopyVector(app_camera.trot, trot);
}

// FF3 0x001fca18; FF2 EndPlyrApproachCameraCtrl 0x001dada0.

void EndPlyrApproachCameraCtrl(void)
{
    app_camera.mode = 0;
}

// The damage framing is off when a followed point is missing or the
// attacker no longer acts (state 3 with hp left).
static int PlyrDamageCameraLost(APPROACH_CAMERA *apcm)
{
    return apcm->pPlyrPos == nullptr || apcm->pEnePos == nullptr
        || (apcm->pEneWrk != nullptr
            && (apcm->pEneWrk->state != ENE_STATE_ACT || apcm->pEneWrk->hp == 0));
}

// ──────────────────────────────────────────────────────────────────────
// The damage framing: the target eases from tstart to the mid point of the
// player and the attacker (raised by offy) over 29 frames (t from cnt), the
// eye stands back from that point along the map camera's direction (mpos)
// at a distance easing dist2 -> dist. With the framing lost it holds the
// current camera. Stores npos / inpos and applies.
// FF3 0x001fca28 (FF3-only).

static void PlyrDamageCameraFollow(void)
{
    APPROACH_CAMERA *apcm = &app_camera;
    sceVu0FVECTOR    mid;
    sceVu0FVECTOR    pos;
    sceVu0FVECTOR    target;
    sceVu0FVECTOR    dir;
    sceVu0FVECTOR    a;
    sceVu0FVECTOR    b;
    float            t = (APPROACH_MOVE_FRAME - apcm->cnt) / APPROACH_MOVE_FRAME;
    float            len;

    if (PlyrDamageCameraLost(apcm))
    {
        sceVu0CopyVector(mid, apcm->ipos);
    }
    else
    {
        sceVu0CopyVector(a, apcm->pPlyrPos);
        sceVu0CopyVector(b, apcm->pEnePos);
        GetMidPos(mid, a, b);
    }
    mid[1] -= apcm->offy;

    sceVu0SubVector(target, mid, apcm->tstart);
    sceVu0ScaleVector(target, target, t);
    sceVu0AddVector(target, apcm->tstart, target);

    if (PlyrDamageCameraLost(apcm))
    {
        sceVu0CopyVector(pos, gra3dcamGetPosition());
        sceVu0CopyVector(target, gra3dcamGetTarget());
    }
    else
    {
        len = MapCamLength(mid, apcm->mpos);
        sceVu0SubVector(dir, apcm->mpos, mid);
        sceVu0ScaleVector(dir, dir, Ps2Divf((apcm->dist2 - apcm->dist) * (1.0f - t) + apcm->dist, len));   // PC: PS2 division
        sceVu0AddVector(pos, mid, dir);
    }

    sceVu0CopyVector(apcm->npos, pos);
    sceVu0CopyVector(apcm->inpos, target);
    gra3dcamSetPosition(pos);
    gra3dcamSetTarget(target, 1);
    gra3dApplyCamera(nullptr, 1, 1);
}

// The frames a slide takes: 29, scaled to PAL.
static float PlyrApproachMoveFrames(void)
{
    if (GetPALMode() != 0)
        return APPROACH_MOVE_FRAME / GetFrameTimeScale();
    return APPROACH_MOVE_FRAME;
}

// ──────────────────────────────────────────────────────────────────────
// The approach camera's state machine (APPROACH_CAMERA_FLOW). Timed flows
// step by the player's timer step (the slow-mode gauge).
// FF3 0x001fcca0; FF2 PlyrApproachCameraCtrl 0x001dadb0 (from MioPan
// map_camera.c:1148; FF3 rewrote the approach / damage half: flows 2..7,
// the damage framing and the shots).
// callcheck: GetPALMode, GetFrameTimeScale (in PlyrApproachMoveFrames)

static void PlyrApproachCameraCtrl(int RectCamFlg)
{
    APPROACH_CAMERA *apcm = &app_camera;
    PLYR_WRK        *plyr;
    sceVu0FVECTOR    itv;
    sceVu0FVECTOR    ptv;
    sceVu0FVECTOR    tw;
    sceVu0FVECTOR    tr;
    sceVu0FVECTOR    step_v;
    float            step;
    float            d;

    switch (apcm->flow)
    {
    case APPROACH_CAMERA_FLOW_INIT:
        // Let the map camera place itself, then slide from dist2 to dist
        // in front of the look point, along the line from the camera.
        MapCamNormalCameraCtrl(RectCamFlg);

        sceVu0CopyVector(apcm->inpos, apcm->ipos);
        apcm->inpos[1] = apcm->ipos[1] - apcm->offy;
        sceVu0CopyVector(itv, apcm->inpos);

        UnitGetTrgtRot(itv, gra3dcamGetPosition(), tr, 3);
        tw[0] = 0.0f;
        tw[1] = 0.0f;
        tw[2] = apcm->dist2;
        tw[3] = 0.0f;
        RotVectorZXY(tw, tw, tr);
        sceVu0AddVector(apcm->npos, itv, tw);

        UnitGetTrgtRot(itv, gra3dcamGetPosition(), tr, 3);
        tw[0] = 0.0f;
        tw[1] = 0.0f;
        tw[2] = apcm->dist;
        tw[3] = 0.0f;
        RotVectorZXY(tw, tw, tr);
        sceVu0AddVector(ptv, itv, tw);

        apcm->cnt = PlyrApproachMoveFrames();
        sceVu0SubVector(apcm->imv, itv, apcm->inpos);
        sceVu0DivVector(apcm->imv, apcm->imv, apcm->cnt);
        sceVu0SubVector(apcm->pmv, ptv, apcm->npos);
        sceVu0DivVector(apcm->pmv, apcm->pmv, apcm->cnt);

        apcm->flow = ingame_wrk.plyr_wrk->_147 == 12 ? APPROACH_CAMERA_FLOW_KEEP : APPROACH_CAMERA_FLOW_MOVE;
        break;

    case APPROACH_CAMERA_FLOW_MOVE:
        if (apcm->cnt > 0.0f)
        {
            step = PlyrGaugeTimeStep(GetAlbumSaveDisp());
            sceVu0ScaleVector(step_v, apcm->pmv, step);
            sceVu0AddVector(apcm->npos, apcm->npos, step_v);
            sceVu0ScaleVector(step_v, apcm->imv, step);
            sceVu0AddVector(apcm->inpos, apcm->inpos, step_v);
            apcm->cnt -= step;
        }
        else if (apcm->pEneWrk == nullptr)
        {
            apcm->flow = APPROACH_CAMERA_FLOW_KEEP;
        }
        else
        {
            apcm->cnt  = 0.0f;
            apcm->flow = APPROACH_CAMERA_FLOW_SHOT;
            ApproachCameraCrossFadeSW(1);
            DmgCamShotInit(ingame_wrk.plyr_wrk->_147, apcm);
        }
        goto set_camera;

    case APPROACH_CAMERA_FLOW_FINDER_OFF:
        if (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000)
            PlyrFinderModeCancel();
        apcm->flow = APPROACH_CAMERA_FLOW_KEEP;
        // fall through
    case APPROACH_CAMERA_FLOW_KEEP:
        SetEffects(6, 1, 0x1d);
        MapCamNormalCameraCtrl(RectCamFlg);
        sceVu0CopyVector(apcm->mpos, gra3dcamGetPosition());
        if (PlyrDamageCameraLost(apcm))
        {
            SetEffects(6, 1, 0x1d);
            sceVu0CopyVector(apcm->npos, gra3dcamGetPosition());
            sceVu0CopyVector(apcm->inpos, gra3dcamGetTarget());
            apcm->flow = APPROACH_CAMERA_FLOW_MAP;
        }
        else
        {
            PlyrDamageCameraFollow();
        }
        goto set_camera;

    case APPROACH_CAMERA_FLOW_MAP:
        MapCamNormalCameraCtrl(RectCamFlg);
        sceVu0CopyVector(apcm->npos, gra3dcamGetPosition());
        sceVu0CopyVector(apcm->inpos, gra3dcamGetTarget());
        goto set_camera;

    case APPROACH_CAMERA_FLOW_SHOT:
        plyr = ingame_wrk.plyr_wrk;
        if ((u_char)(plyr->mode - 1) < 4 || plyr->_147 == 13)
        {
            SetEffects(6, 1, 10);
            if (DmgCamShotMain(apcm) == 0)
            {
                sceVu0CopyVector(apcm->PosInitVector, apcm->npos);
                sceVu0CopyVector(apcm->NeckInitVector, apcm->inpos);
            }
            PlyrShoulderCameraSet(apcm->PosInitVector, apcm->NeckInitVector);
            apcm->cnt += PlyrGaugeTimeStep(GetAlbumSaveDisp());
        }
        else
        {
            apcm->cnt  = 0.0f;
            apcm->flow = APPROACH_CAMERA_FLOW_KEEP;
        }
        break;

    case APPROACH_CAMERA_FLOW_DAMAGE_INIT:
        MapCamNormalCameraCtrl(RectCamFlg);
        apcm->cnt = PlyrApproachMoveFrames();

        sceVu0CopyVector(apcm->tstart, apcm->ipos);
        apcm->tstart[1] = apcm->ipos[1] - apcm->offy;

        // Never start or end further away than the map camera is now.
        d = MapCamLength(apcm->tstart, gra3dcamGetPosition());
        apcm->dist  = d < apcm->dist2 ? d : apcm->dist2;
        apcm->dist2 = d < apcm->dist ? d : apcm->dist;

        sceVu0CopyVector(apcm->mpos, gra3dcamGetPosition());
        PlyrDamageCameraFollow();

        if (ingame_wrk.plyr_wrk->_147 == 13)
        {
            DmgCamShotInit(13, apcm);
            apcm->cnt  = 0.0f;
            apcm->flow = APPROACH_CAMERA_FLOW_SHOT;
        }
        else if (ingame_wrk.plyr_wrk->_147 == 12)
        {
            apcm->cnt  = 0.0f;
            apcm->flow = APPROACH_CAMERA_FLOW_FINDER_OFF;
        }
        else
        {
            apcm->flow = APPROACH_CAMERA_FLOW_DAMAGE;
        }
        break;

    case APPROACH_CAMERA_FLOW_DAMAGE:
        if (apcm->cnt > 0.0f)
        {
            apcm->cnt -= PlyrGaugeTimeStep(GetAlbumSaveDisp());
            MapCamNormalCameraCtrl(RectCamFlg);
            sceVu0CopyVector(apcm->mpos, gra3dcamGetPosition());
            PlyrDamageCameraFollow();
            return;
        }
        apcm->flow = APPROACH_CAMERA_FLOW_KEEP;
        if (apcm->pEneWrk != nullptr)
        {
            apcm->cnt  = 0.0f;
            apcm->flow = APPROACH_CAMERA_FLOW_SHOT;
            ReqFinderInOverRap(10);
            DmgCamShotInit(ingame_wrk.plyr_wrk->_147, apcm);
            return;
        }
        break;

    case APPROACH_CAMERA_FLOW_TALK_INIT:
        sceVu0CopyVector(apcm->npos, gra3dcamGetPosition());
        sceVu0CopyVector(apcm->inpos, gra3dcamGetTarget());

        sceVu0CopyVector(itv, apcm->ipos);
        itv[1] = apcm->ipos[1] - apcm->offy;

        UnitGetTrgtRot(ingame_wrk.plyr_wrk->pos, apcm->npos, tr, 3);
        tw[0] = 0.0f;
        tw[1] = 0.0f;
        tw[2] = apcm->dist;
        tw[3] = 0.0f;
        RotVectorZXY(tw, tw, tr);
        sceVu0AddVector(apcm->npos, itv, tw);
        sceVu0CopyVector(apcm->inpos, itv);

        apcm->flow = APPROACH_CAMERA_FLOW_TALK_KEEP;
        break;

    case APPROACH_CAMERA_FLOW_TALK_KEEP:
        SetEffects(6, 1, 0x10);
    set_camera:
        gra3dcamSetPosition(apcm->npos);
        gra3dcamSetTarget(apcm->inpos, 1);
        gra3dApplyCamera(nullptr, 1, 1);
        return;

    case APPROACH_CAMERA_FLOW_DEAD_INIT:
        // Look at a point 17 in front of the player's facing, raised by
        // offy (FF2: 425), from dist behind it at offy2 above ipos.
        sceVu0CopyVector(apcm->npos, gra3dcamGetPosition());
        sceVu0CopyVector(apcm->inpos, gra3dcamGetTarget());

        tw[0] = 0.0f;
        tw[1] = -apcm->offy;
        tw[2] = 17.0f;
        tw[3] = 0.0f;
        RotVectorZXY(tw, tw, apcm->trot);
        sceVu0AddVector(itv, apcm->ipos, tw);

        tw[0] = 0.0f;
        tw[1] = 0.0f;
        tw[2] = apcm->dist;
        tw[3] = 0.0f;
        UnitGetTrgtRot(apcm->inpos, apcm->npos, tr, 2);
        RotVectorZXY(tw, tw, tr);
        sceVu0AddVector(ptv, itv, tw);
        ptv[1] = apcm->ipos[1] - apcm->offy2;

        gra3dcamSetPosition(ptv);
        gra3dcamSetTarget(itv, 1);
        gra3dApplyCamera(nullptr, 1, 1);

        apcm->flow = APPROACH_CAMERA_FLOW_DEAD_KEEP;
        break;

    default:
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001fd4a8; FF2 ApproachCameraCrossFadeSW 0x001db2a8.

void ApproachCameraCrossFadeSW(int sw)
{
    app_camera.crossfade = sw;
}

// FF3 0x001fd4b8; FF2 GetApproachCameraCrossFade 0x001db2b8.

int GetApproachCameraCrossFade(void)
{
    return app_camera.crossfade;
}

// ──────────────────────────────────────────────────────────────────────
// The over-the-shoulder shot: no roll, fov pi/4.
// FF3 0x001fd4c8; FF2 PlyrShoulderCameraSet 0x001db380.

static void PlyrShoulderCameraSet(float *Position, float *Target)
{
    gra3dcamSetPosition(Position);
    gra3dcamSetTarget(Target, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(0.78539807f);
    gra3dApplyCamera(nullptr, 1, 1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001fd520; FF2 QuakeCameraInit 0x001db3d8.

static void QuakeCameraInit(void)
{
    QuakeCameraCtrl.LoopNum    = 0;
    QuakeCameraCtrl.RequestFlg = 0;
    QuakeCameraCtrl.Counter    = 0;
    QuakeCameraCtrl.AllTime    = 0;
}

// FF3 0x001fd540; FF2 QuakeCameraStop 0x001db400.

void QuakeCameraStop(void)
{
    QuakeCameraCtrl.RequestFlg = 0;
}

// ──────────────────────────────────────────────────────────────────────
// A shake of Power for Time frames, then LoopNum aftershocks; the target
// shakes 1.62 times harder than the eye. No callers in the retail ELF.
// FF3 0x001fd550; FF2 QuakeCameraReq 0x001db410 (FF3 drops the quake SE).

void QuakeCameraReq(float Power, u_int Time, u_int LoopNum)
{
    QuakeCameraCtrl.Power[0]   = Power;
    QuakeCameraCtrl.AllTime    = Time;
    QuakeCameraCtrl.Power[1]   = Power * 1.62f;
    QuakeCameraCtrl.RequestFlg = 1;
    QuakeCameraCtrl.LoopNum    = (short)LoopNum;
    QuakeCameraCtrl.Counter    = Time;
}

// FF3 0x001fd588; FF2 QuakeCameraGetReq 0x001db460. No callers.

int QuakeCameraGetReq(void)
{
    return QuakeCameraCtrl.RequestFlg;
}

// ──────────────────────────────────────────────────────────────────────
// The per-frame shake: a random offset in the camera's own basis for the
// eye and for the target, faded out over the last 15 frames (12 in PAL),
// with the big motor on every other frame. One frame of lead-in; between
// aftershocks a random gap of 300..600 frames (250..500 in PAL).
// FF3 0x001fd598; FF2 QuakeCameraMain 0x001db480 (from MioPan
// map_camera.c:1487; FF3 inlines VibrateRequest2 with a long time).

static int QuakeCameraMain(void)
{
    QUAKE_CAMERA_CTRL *pQuakeCamera = &QuakeCameraCtrl;
    GRA3DCAMERA       *pCam = gra3dGetCamera();
    float             *rCamPos = gra3dcamGetPosition();
    sceVu0FMATRIX      TmpMat;
    sceVu0FVECTOR      Val[2];
    sceVu0FVECTOR      CameraXd;
    sceVu0FVECTOR      CameraYd;
    sceVu0FVECTOR      CameraZd;
    sceVu0FVECTOR      CameraPosition;
    sceVu0FVECTOR      CameraTarget;
    float              hi;
    float              lo;
    float              fade;
    float              dat;
    float              V0;
    float              V1;
    float              V2;
    int                lead;
    int                i;

    if (pQuakeCamera->RequestFlg == 0)
        return 0;

    lead = pQuakeCamera->Counter >= pQuakeCamera->AllTime;
    pQuakeCamera->Counter--;
    if (lead)
        return 0;

    if (pQuakeCamera->Counter <= 0)
    {
        if (pQuakeCamera->LoopNum == 0)
        {
            pQuakeCamera->RequestFlg = 0;
            QuakeCameraStop();
            return 0;
        }
        hi = GetPALMode() != 0 ? 250.0f : 300.0f;
        lo = GetPALMode() != 0 ? 500.0f : 600.0f;
        pQuakeCamera->LoopNum--;
        pQuakeCamera->Counter = (int)(u_int)((hi - lo) * GetRandValF(1.0f) + lo);
        return 0;
    }

    if (pQuakeCamera->Counter & 1)
    {
        if (GetOptionWrk()->now.vibration != 0)
        {
            pad_wrk[0].vib_big_pow  = 0xa0;
            pad_wrk[0].vib_big_time = 20000;
        }
    }

    // Camera basis: Z along the view, Y straight down rolled by the camera
    // roll, X their cross product.
    sceVu0SubVector(CameraZd, pCam->vTarget, rCamPos);
    CameraYd[0] = 0.0f;
    CameraYd[1] = -1.0f;
    CameraYd[2] = 0.0f;
    CameraYd[3] = 0.0f;
    sceVu0UnitMatrix(TmpMat);
    sceVu0RotMatrixZ(TmpMat, TmpMat, -gra3dcamGetRoll());
    sceVu0ApplyMatrix(CameraYd, TmpMat, CameraYd);
    sceVu0OuterProduct(CameraXd, CameraZd, CameraYd);

    for (i = 0; i < 2; i++)
    {
        dat = pQuakeCamera->Power[i];
        V0  = (dat - -dat) * GetRandValF(1.0f) + -dat;
        V1  = (dat - -dat) * GetRandValF(1.0f) + -dat;
        V2  = (dat - -dat) * GetRandValF(1.0f) + -dat;

        if (pQuakeCamera->Counter < (GetPALMode() != 0 ? 12 : 15))
        {
            fade = GetPALMode() != 0 ? 12.0f : 15.0f;
            V0 *= (float)pQuakeCamera->Counter / fade;
            V1 *= (float)pQuakeCamera->Counter / fade;
            V2 *= (float)pQuakeCamera->Counter / fade;
        }

        Val[i][0] = CameraZd[0] * V2 + CameraYd[0] * V1 + CameraXd[0] * V0;
        Val[i][3] = 0.0f;
        Val[i][1] = CameraZd[1] * V2 + CameraYd[1] * V1 + CameraXd[1] * V0;
        Val[i][2] = CameraZd[2] * V2 + CameraYd[2] * V1 + CameraXd[2] * V0;
    }

    sceVu0AddVector(CameraPosition, rCamPos, Val[0]);
    sceVu0AddVector(CameraTarget, pCam->vTarget, Val[1]);
    gra3dcamSetPosition(CameraPosition);
    gra3dcamSetTarget(CameraTarget, 1);
    gra3dApplyCamera(nullptr, 1, 1);
    return 1;
}

