/* ==========================================================================
 *  ingame/map/MapPut.c
 *
 *  Placed map objects: the put pool (150 x MAP_PUT, bss 0x389260), the put
 *  list, the per-frame draw list (culling, depth and first / last order)
 *  and the light set-up per put. FF2 MapPut.c rewritten for FF3: the
 *  matrices carry no 25x scale, model puts keep a MAP_OBJ_MDL, the light
 *  cases became flag kinds (MapPutSetLight) and MapPutDraw takes the frame's
 *  draw flags.
 *
 *  Reconstructed from FF3 0x0011bf28..0x0011cea8 (MioPan's MapPut.c as the
 *  starting point). sdata 0x36b868..0x36b890, sbss gp-0x92c..-0x924.
 * ======================================================================== */

#include <string.h>                     // memset
#include "ingame/map/MapPut.h"
#include "graphics/graph3d/gra3d.h"     // GRA3DLIGHTDATA, gra3dGetCamera, gra3dSetLightData, CheckModelBoundingBox
#include "graphics/graph3d/g3dCore.h"   // g3dGetAmbientRef
#include "graphics/graph3d/gra3dSGDData.h"  // sgdGetBoundingBox
#include "graphics/mmanage.h"           // ModelManager::ExecPrelightMatrix
#include "graphics/motion/motion.h"     // MDL_MOT, ModelObject
#include "common/vu0math.h"             // NormalizeAngle
#include "ingame/ingame_wrk.h"          // ingame_wrk
#include "ingame/plyr/player.h"         // PLYR_WRK, PLYR_FLG_800000
#include "ingame/map/MapDraw.h"         // MapDrawIsEnableFlashlightOnly, MapDrawGetLightPtr4BuffID
#include "ingame/map/MapLight.h"        // MapLightSetPlayerOnly, MapLightMakeRoomReal
#include "ingame/map/MapObjWrk.h"       // MAP_OBJ_MDL, MapObjGetNoByPut
#include "ingame/movie_room_menu/prg/movie_projecter.h"    // IsMoviePhase
#include "reipan/gameplay/reipan_ots_geometry.h"   // PC: the camera collision's scenery

static MAP_PUT       map_put_list[MAP_PUT_MAX];  // bss 0x389260
static sceVu0FVECTOR map_put_ambient;   // bss 0x38e6c0: the ambient MapPutLightPush saved
static MAP_PUT      *map_put_st;        // sbss gp-0x92c (0x372bc4): the put list
static MAP_PUT      *map_put_now;       // sbss gp-0x928: the put being drawn
static int           map_put_light_push;    // sbss gp-0x924
static int           map_put_scene_load;    // sdata 0x36b868 (gp-0x7c88): written only
static int           map_put_draw_stop;     // sdata 0x36b86c (gp-0x7c84)

// data 0x329400: diag(1, -1, -1, 1), the model space -> put matrix flip of
// the bounding box (its owner is probably the 0x203310 run, next to the
// unit matrix at 0x3293c0 that MAP_OBJ_MDL::SetPosRot starts from).
static sceVu0FMATRIX map_put_flip_yz = {
    { 1.0f,  0.0f,  0.0f, 0.0f },
    { 0.0f, -1.0f,  0.0f, 0.0f },
    { 0.0f,  0.0f, -1.0f, 0.0f },
    { 0.0f,  0.0f,  0.0f, 1.0f },
};

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011bf28 / 0x0011bf30. SceneInitAfterModelLoad sets it around its
// room set-up; nothing reads it.

// FF3 0x0011bf28.
void MapPutSetSceneLoad(int on)
{
    map_put_scene_load = on;
}

// FF3 0x0011bf30.
void MapPutClearSceneLoad(void)
{
    map_put_scene_load = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Screen z of a put: its position pointer, its matrix pointer's row 3 or
// its bounding-box centre, with w 1, through matWorldScreen.
// FF3 0x0011bf38.

static float MapPutGetScreenZ(MAP_PUT *put)
{
    sceVu0FVECTOR v;
    float        *src;

    if (put->flg & MAP_PUT_POS_PTR)
        src = put->pos;
    else if (put->flg & MAP_PUT_MATRIX_PTR)
        src = put->mp + 12;
    else
        src = put->c_pos;

    v[0] = src[0];
    v[1] = src[1];
    v[2] = src[2];
    v[3] = 1.0f;                        /* sdata 0x36b870 */
    sceVu0ApplyMatrix(v, gra3dGetCamera()->matWorldScreen, v);
    return v[2];
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011bfd0 / 0x0011bfd8. EvPlyrMdlChangeStart stops the put drawing,
// EvPlyrMdlChangeWait starts it again.

// FF3 0x0011bfd0.
int MapPutGetDrawStop(void)
{
    return map_put_draw_stop;
}

// FF3 0x0011bfd8.
void MapPutSetDrawStop(int on)
{
    map_put_draw_stop = on;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011bfe0.

void MapPutSetFlg(int room, u_int flg)
{
    for (MAP_PUT *put = map_put_st; put != nullptr; put = put->next)
    {
        if (put->buff == (short)room)
            put->flg |= flg;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Field setters / getters (nullptr-safe except where noted).

// FF3 0x0011c058.
void MapPutSetFirst(MAP_PUT *put, short first_id)
{
    if (put != nullptr)
        put->first_id = first_id;
}

// FF3 0x0011c070.
void MapPutSetZoffset(float dist, MAP_PUT *put)
{
    if (put != nullptr)
        put->z_offset = dist;
}

// FF3 0x0011c0b0.
void MapPutSetMatrixPtr(MAP_PUT *put, float *mtx)
{
    if (put != nullptr)
        put->mp = mtx;
}

// FF3 0x0011c0d0.
void MapPutSetLitPtr(MAP_PUT *put, void *lit)
{
    if (put != nullptr)
        put->lit = static_cast<GRA3DLIGHTDATA *>(lit);
}

// FF3 0x0011c0e0.
void MapPutSetWork(MAP_PUT *put, void *work)
{
    if (put != nullptr)
        put->work = work;
}

// FF3 0x0011c110.
float *MapPutGetMatrix(MAP_PUT *put)
{
    return put != nullptr ? &put->mat[0][0] : nullptr;
}

// FF3 0x0011c120.
u_int *MapPutGetFlag(MAP_PUT *put)
{
    return put != nullptr ? &put->flg : nullptr;
}

// FF3 0x0011c130.
void *MapPutGetLitPtr(MAP_PUT *put)
{
    return put != nullptr ? put->lit : nullptr;
}

// FF3 0x0011c148.
MAP_OBJ_MDL *MapPutGetMdl(MAP_PUT *put)
{
    return put != nullptr ? put->mdl : nullptr;
}

// FF3 0x0011c160.
void *MapPutGetWork(MAP_PUT *put)
{
    return put != nullptr ? put->work : nullptr;
}

// Where FF2 asked GetObjDrawFLG. FF3 0x0011c178.
static int MapPutGetObjDrawFlg(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// A put's position: its position pointer, its matrix pointer's row 3, or
// its matrix's row 3. FF3 0x0011c180.

int MapPutGetPos(void *out, MAP_PUT *put)
{
    const float *src;

    if (put->flg & MAP_PUT_POS_PTR)
        src = put->pos;
    else if (put->flg & MAP_PUT_MATRIX_PTR)
        src = put->mp + 12;
    else
        src = put->mat[3];

    memcpy(out, src, sizeof(sceVu0FVECTOR));
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The eight corners of the put model's bounding box in the world.
// FF3 0x0011c1c0.

static void MapPutGetBoundingBox(sceVu0FVECTOR *bb, MAP_PUT *put)
{
    sceVu0FMATRIX mat;

    sgdGetBoundingBox(put->mdl->mot->ani_ctrl->GetSgd(0), bb);
    sceVu0MulMatrix(mat, put->mat, map_put_flip_yz);
    for (int i = 0; i < 8; i++)
    {
        bb[i][3] = 1.0f;                /* sdata 0x36b874 */
        sceVu0ApplyMatrix(bb[i], mat, bb[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011c2d8: the first free put (buff < 0), nullptr when all 150 are
// used.

static MAP_PUT *MapPutGetFreeArea(void)
{
    for (int i = 0; i < MAP_PUT_MAX; i++)
    {
        MAP_PUT *put = &map_put_list[i];
        if (put->buff < 0)
        {
            put->mdl = nullptr;
            return put;
        }
    }
    return nullptr;
}

// FF3 0x0011c310: at the end of the put list.
static void MapPutAddList(MAP_PUT *put)
{
    if (map_put_st == nullptr)
    {
        map_put_st = put;
        put->befo = nullptr;
    }
    else
    {
        MAP_PUT *wp = map_put_st;
        while (wp->next != nullptr)
            wp = wp->next;
        put->befo = wp;
        wp->next = put;
    }
    put->next = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// A new put. Its matrix (unless MAP_PUT_MATRIX_PTR): scale, then rotation
// x, y, z (radians), then pos. A model put also places its model and gets
// its bounding-box centre.
// FF3 0x0011c360. PC: FF3 passes a callback in the model argument
// (MAP_PUT_FUNC_OBJECT); here MapPutSetObjSub takes both.

static MAP_PUT *MapPutSetObjSub(short buff, MAP_OBJ_MDL *mdl, MAP_PUT_DRAW_FUNC func, float *pos,
                                float *rot, float *scale, void *lit, u_int flg)
{
    MAP_PUT *put = MapPutGetFreeArea();

    if (put == nullptr)
        return nullptr;

    put->buff = buff;
    if ((flg & MAP_PUT_MATRIX_PTR) == 0)
    {
        sceVu0UnitMatrix(put->mat);
        put->mat[0][0] = scale[0];
        put->mat[1][1] = scale[1];
        put->mat[2][2] = scale[2];
        sceVu0RotMatrixX(put->mat, put->mat, NormalizeAngle(NormalizeAngle(rot[0])));
        sceVu0RotMatrixY(put->mat, put->mat, NormalizeAngle(NormalizeAngle(rot[1])));
        sceVu0RotMatrixZ(put->mat, put->mat, NormalizeAngle(NormalizeAngle(rot[2])));
        put->mat[3][0] += pos[0];
        put->mat[3][1] += pos[1];
        put->mat[3][2] += pos[2];
    }

    if (flg & MAP_PUT_FUNC_OBJECT)
    {
        put->func = func;
    }
    else
    {
        put->mdl = mdl;
        if (mdl != nullptr)
        {
            sceVu0FVECTOR bb[8];

            mdl->SetPosRot(pos, rot);
            MapPutGetBoundingBox(bb, put);
            for (int i = 0; i < 4; i++)
                put->c_pos[i] = (bb[0][i] + bb[7][i]) * 0.5f;
        }
    }

    put->flg = flg;
    put->lit = static_cast<GRA3DLIGHTDATA *>(lit);
    put->z_offset = 0.0f;
    put->mp = nullptr;
    MapPutAddList(put);
    return put;
}

// The model put; FF3's body is MapPutSetObjSub above.
MAP_PUT *MapPutSetObj(short room, MAP_OBJ_MDL *mdl, float *pos, float *rot, float *scale,
                      void *lit, u_int flags)
{
    return MapPutSetObjSub(room, mdl, nullptr, pos, rot, scale, lit, flags);
}

// FF3 0x0011c520: a callback put that MapPutSetMatrixPtr places.
// callcheck: MapPutSetObj (as MapPutSetObjSub, which takes the callback apart from the model)
MAP_PUT *MapPutSetFunc(int buff, MAP_PUT_DRAW_FUNC func, u_int flags)
{
    sceVu0FVECTOR zero = { 0.0f, 0.0f, 0.0f, 0.0f };

    return MapPutSetObjSub((short)buff, nullptr, func, zero, zero, zero, nullptr,
                           flags | MAP_PUT_FUNC_OBJECT | MAP_PUT_MATRIX_PTR);
}

// FF3 0x0011c558 (no callers).
// callcheck: MapPutSetObj (as MapPutSetObjSub)
MAP_PUT *MapPutSetFuncPos(int buff, MAP_PUT_DRAW_FUNC func, float *pos, float *rot, float *scale,
                          u_int flags)
{
    return MapPutSetObjSub((short)buff, nullptr, func, pos, rot, scale, nullptr,
                           flags | MAP_PUT_FUNC_OBJECT);
}

// FF3 0x0011c578: a callback put at *pos (the map effects).
// callcheck: MapPutSetObj (as MapPutSetObjSub, which takes the callback apart from the model)
MAP_PUT *MapPutSetFuncObj(int buff, MAP_PUT_DRAW_FUNC func, float *pos, u_int flags)
{
    sceVu0FVECTOR zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    MAP_PUT      *put = MapPutSetObjSub((short)buff, nullptr, func, zero, zero, zero, nullptr,
                                        flags | MAP_PUT_FUNC_OBJECT | MAP_PUT_MATRIX_PTR | MAP_PUT_POS_PTR);

    put->pos = pos;                     // FF3 stores without the nullptr check
    return put;
}

// FF3 0x0011c5c0.
void MapPutChangeObj(MAP_PUT *put)
{
    put->func = nullptr;
    put->flg &= ~MAP_PUT_FUNC_OBJECT;
}

// FF3 0x0011c5d8: a model put drawn by func from now on (MapDBuff, the
// room objects).
void MapPutSetDrawFunc(MAP_PUT *put, MAP_PUT_DRAW_FUNC func, u_int flags, int dist, short first_id)
{
    put->first_id = first_id;
    put->func = func;
    put->flg |= MAP_PUT_FUNC_OBJECT | flags;
    put->z_offset += (float)dist;
}

// ──────────────────────────────────────────────────────────────────────
// Out of the put list and free. FF3 0x0011c608 (FF2's name, typo
// included). FF3 still walks the list looking for put first (an assert
// without its message); the walk changes nothing.

static void MpaPutDeleteOne(MAP_PUT *put)
{
    MAP_PUT *np;

    if (put == nullptr)
        return;

    np = put->next;
    if (put->befo != nullptr)
        put->befo->next = np;
    else
        map_put_st = np;
    if (np != nullptr)
        np->befo = put->befo;

    put->mdl = nullptr;
    put->buff = -1;
    put->func = nullptr;
}

// FF3 0x0011c670.
void MpaPutDeleteOneObj(MAP_PUT *put)
{
    MpaPutDeleteOne(put);
}

// FF3 0x0011c688.
void MapPutDelete(short buff)
{
    for (int i = 0; i < MAP_PUT_MAX; i++)
    {
        if (map_put_list[i].buff == buff)
            MpaPutDeleteOne(&map_put_list[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Whether put goes before cur in the draw list. MAP_PUT_FIRST puts come
// before ordinary ones, MAP_PUT_LAST ones after; within a group the bigger
// first_id first, then (and for ordinary puts) the bigger z_num.
// FF3: inlined into MapPutSort as a switch over
// (put->flg & 0x8400) << 16 | (cur->flg & 0x8400).

static int MapPutSortBefore(const MAP_PUT *cur, const MAP_PUT *put)
{
    u_int mode = (put->flg & (MAP_PUT_FIRST | MAP_PUT_LAST)) << 16 |
                 (cur->flg & (MAP_PUT_FIRST | MAP_PUT_LAST));

    switch (mode)
    {
    case 0:                             // both ordinary
        return cur->z_num < put->z_num;
    case MAP_PUT_LAST:                  // ordinary before a last
    case (u_int)MAP_PUT_FIRST << 16:    // a first before an ordinary
    case (u_int)MAP_PUT_FIRST << 16 | MAP_PUT_LAST:
        return 1;
    case (u_int)MAP_PUT_FIRST << 16 | MAP_PUT_FIRST:
    case (u_int)MAP_PUT_LAST << 16 | MAP_PUT_LAST:
        if (cur->first_id < put->first_id)
            return 1;
        if (cur->first_id == put->first_id)
            return cur->z_num < put->z_num;
        return 0;
    default:
        return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The draw list (next_draw) of the puts to draw this frame. Hidden puts are
// left out; 0x800 / 0x4000 puts are culled by their bounding box (the
// sway furniture 0x26b..0x282 with a fixed y range or, with the camera up,
// not at all; out-of-view 0x800 puts are dropped outside the movie phases,
// the others stay with visible 0 so their matrices are still set); far
// 0x4000 puts are dropped. FF3 0x0011c6e8.

static MAP_PUT *MapPutSort(void)
{
    MAP_PUT *st = nullptr;

    for (MAP_PUT *put = map_put_st; put != nullptr; put = put->next)
    {
        u_int kind;

        put->visible = 1;
        if (put->flg & MAP_PUT_NODRAW)
            continue;

        kind = put->flg & MAP_PUT_KIND_MASK;
        if (kind == MAP_PUT_REAL_LIGHT || kind == MAP_PUT_SWAY)
        {
            sceVu0FVECTOR bb[8];
            int           check = 1;

            MapPutGetBoundingBox(bb, put);
            if (kind == MAP_PUT_SWAY)
            {
                if ((u_int)(MapObjGetNoByPut(put) - 0x26b) < 0x18)
                {
                    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) == 0)
                    {
                        const float d = 6.1f;   /* sdata 0x36b878 */
                        bb[0][1] += d;
                        bb[1][1] += d;
                        bb[2][1] -= d;
                        bb[3][1] -= d;
                        bb[4][1] += d;
                        bb[5][1] += d;
                        bb[6][1] -= d;
                        bb[7][1] -= d;
                    }
                    else
                    {
                        check = 0;
                    }
                }
                else
                {
                    bb[0][1] = bb[1][1] = bb[4][1] = bb[5][1] = 100.0f;     /* sdata 0x36b87c */
                    bb[2][1] = bb[3][1] = bb[6][1] = bb[7][1] = -100.0f;    /* sdata 0x36b880 */
                }
            }
            if (check && CheckModelBoundingBox(bb) == 0)
            {
                put->visible = 0;
                if (kind == MAP_PUT_REAL_LIGHT && IsMoviePhase() == 0)
                    continue;
            }
        }

        put->z_num = -(MapPutGetScreenZ(put) + put->z_offset);
        if (kind == MAP_PUT_SWAY && put->z_num + put->z_offset > -30000.0f)  /* sdata 0x36b884 */
            continue;

        put->next_draw = nullptr;
        if (st == nullptr)
        {
            st = put;
            continue;
        }

        MAP_PUT *bp = st;
        MAP_PUT *wp = st;
        while (wp != nullptr && !MapPutSortBefore(wp, put))
        {
            bp = wp;
            wp = wp->next_draw;
        }

        if (wp == st)
        {
            put->next_draw = st;
            st = put;
        }
        else if (wp == nullptr)
        {
            bp->next_draw = put;
        }
        else
        {
            put->next_draw = bp->next_draw;
            bp->next_draw = put;
        }
    }

    return st;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011ca30.

void MapPutDrawFlagClear(void)
{
    for (MAP_PUT *put = map_put_st; put != nullptr; put = put->next)
        put->drawn = 0;
}

// FF3 0x0011ca68.
MAP_PUT *MapPutGetNowHdl(void)
{
    return map_put_now;
}

// ──────────────────────────────────────────────────────────────────────
// Save the lights and ambient once before a put changes them, restore
// them after it. FF3 0x0011ca70 / 0x0011cab8.

// FF3 0x0011ca70.
static void MapPutLightPush(void)
{
    if (map_put_light_push == 0)
    {
        gra3dLightPushData();
        memcpy(map_put_ambient, g3dGetAmbientRef(), sizeof(sceVu0FVECTOR));
        map_put_light_push = 1;
    }
}

// FF3 0x0011cab8.
static void MapPutLightPop(void)
{
    if (map_put_light_push != 0)
    {
        gra3dLightPopData();
        gra3dSetAmbient(map_put_ambient);
        map_put_light_push = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The lights for one visible put, by its kind. FF3 0x0011caf0 (FF2 had
// this inline in MapPutDraw); returns 0.

static int MapPutSetLight(MAP_PUT *put)
{
    u_int kind;

    if (MapDrawIsEnableFlashlightOnly() == 0)
        return 0;

    kind = put->flg & MAP_PUT_KIND_MASK;
    switch (kind)
    {
    case MAP_PUT_PLAYER_LIGHT:
        break;

    case MAP_PUT_PRELIGHT:
        MapPutLightPush();
        gra3dSetLightData(put->lit, nullptr);
        break;

    case MAP_PUT_REAL_LIGHT:
    case MAP_PUT_SWAY:
    {
        if (kind == MAP_PUT_SWAY && put->z_num + put->z_offset > -31000.0f)  /* sdata 0x36b888 */
            break;

        GRA3DLIGHTDATA ld;
        GRA3DLIGHTDATA room;
        sceVu0FVECTOR  pos;

        MapPutLightPush();
        MapLightMakeRoomReal(&room, put->lit, kind != MAP_PUT_REAL_LIGHT);
        MapPutGetPos(pos, put);
        if (MapObjGetNoByPut(put) == 0x260)
            pos[1] = -35.0f;            /* sdata 0x36b88c */
        gra3dEmulateLightDataObj(&ld, &room, pos, 1.0f);    /* sdata 0x36b890 */
        gra3dSetLightData(&ld, nullptr);
        return 0;
    }

    case MAP_PUT_ROOM_LIGHT:
        gra3dSetLightData(MapDrawGetLightPtr4BuffID(put->buff, 1), nullptr);
        gra3dApplyLight();
        return 0;

    case MAP_PUT_REQ_MDL:
        gra3dSetLightData(put->lit, nullptr);
        gra3dApplyLight();
        put->mdl->mot->ExecPrelightMatrix(&put->mat[0][0]);
        put->lit = nullptr;
        put->flg &= ~MAP_PUT_REQ_MDL;
        break;

    default:
        if (put->lit != nullptr)
        {
            MapPutLightPush();
            gra3dSetLightData(put->lit, nullptr);
            gra3dApplyLight();
            return 0;
        }
        break;
    }

    MapLightSetPlayerOnly();
    return 0;
}

// FF3 0x0011ccf8: empty, no callers.
void MapPutNop(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Draw the puts in draw-list order. A model put gets its matrix once a
// frame (drawn), then is drawn when visible (not with flags 8); a callback
// put calls func(put, flags). Lights are set before and restored after
// each visible put. FF3 0x0011cd00.

void MapPutDraw(u_int flags)
{
    MAP_PUT *put;

    if (map_put_draw_stop != 0)
        return;

    put = MapPutSort();
    map_put_light_push = 0;
    // PC: placed models (furniture, doors, props) are scenery for the
    // over-the-shoulder camera's collision; puts drawn by a function
    // (characters, ghosts, the camera in the player's hands) are not, and
    // neither is the mirror pass (0x200).
    if ((flags & 0x200) == 0)
        ReiPan_OtsGeomBeginScenery();
    for (; put != nullptr; put = put->next_draw)
    {
        map_put_now = put;
        if (put->visible)
            MapPutSetLight(put);

        if (put->flg & MAP_PUT_FUNC_OBJECT)
        {
            if (put->visible)
                MapPutLightPush();
            ReiPan_OtsGeomSuspend();
            put->func(put, flags);
            ReiPan_OtsGeomResume();
        }
        else if (MapPutGetObjDrawFlg() != 0)
        {
            if (put->drawn == 0)
            {
                put->mdl->SetMatrix(&put->mat[0][0]);
                put->drawn = 1;
            }
            if (put->visible == 0)
                continue;
            if ((flags & 8) == 0)
                put->mdl->Draw(flags, put->flg & MAP_PUT_GS);
        }

        if (put->visible)
            MapPutLightPop();
    }
    if ((flags & 0x200) == 0)
        ReiPan_OtsGeomEndScenery();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0011ce38.

void MapPutResetAll(void)
{
    gra3dLightEnableAll(0);
    memset(map_put_list, 0, sizeof(map_put_list));
    for (int i = 0; i < MAP_PUT_MAX; i++)
    {
        map_put_list[i].buff = -1;
        map_put_list[i].mdl = nullptr;
        map_put_list[i].func = nullptr;
    }
    map_put_draw_stop = 0;
    map_put_st = nullptr;
    map_put_now = nullptr;
}
