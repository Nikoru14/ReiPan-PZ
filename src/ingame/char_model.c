/* ==========================================================================
 *  ingame/char_model.c
 *
 *  FF3's character model classes (see char_model.h): CharModel and its
 *  player / enemy / NPC / cat derivations, and the script handler that plays
 *  their footstep and voice SEs. FF3 0x1441d8..0x1479b0 (FF3-only C++; no
 *  FF2 counterpart), with the inline virtuals of 0x2ad470..0x2ad5b0. The
 *  ROM tables (anime scripts by model type, the joint map, the bounding
 *  boxes) are char_model_dat.inc (tools/reverse/chardat.py).
 * ======================================================================== */
#include "ingame/char_model.h"

#include <math.h>                       // sqrtf, fabsf
#include <new>                          // placement new on FF3's C++ heap (operator_new)
#include <string.h>                     // memcpy

#include "common/cpp_heap.h"            // operator_new / operator_delete
#include "common/heapctrl.h"            // heapCtrlMalloc / heapCtrlFree
#include "common/pk4.h"                 // Pk4GetData
#include "common/vu0math.h"             // NormalizeAngle, RotLimitChk
#include "graphics/effect/effect_obj.h" // EffThunderIsLightning, EffThunderSetLight
#include "graphics/graph2d/tim2.h"      // PK4SendVram
#include "graphics/graph3d/g3dLight.h"  // g3dutilSetLightDefault
#include "graphics/graph3d/g3dLightEx.h"    // gra3dSetLightIntens
#include "graphics/graph3d/gra3d.h"     // the light set, the camera, CheckModelBoundingBox
#include "graphics/graph3d/gra3dBoundingBox.h"  // gra3dbbApplyFromBounds, gra3dbbCalcCenterBase
#include "graphics/graph3d/gra3dMirror.h"   // gra3dGenerateBlendedLight
#include "graphics/graph3d/gra3dMisc.h" // gra3dGenerateLightDataToChar, _SetLightToShadow, the projector spot
#include "graphics/graph3d/sgd_types.h" // SGDCOORDINATE
#include "graphics/mmanage.h"           // ModelManager, mdl_flip_yz
#include "graphics/model_object.h"      // ModelObject
#include "graphics/motion/motion.h"     // MOT_CTRL (BmdMotion)
#include "ingame/cat/cat_ctrl.h"        // CAT_WRK
#include "ingame/enemy/ene_main.h"      // ENE_WRK
#include "ingame/ingame.h"              // IngameEffSlotMdlReset
#include "ingame/ingame_wrk.h"          // ingame_wrk
#include "ingame/map/MapCtrl.h"         // MapAreaGetHeight
#include "ingame/map/MapDraw.h"         // MapDrawGetLightPtr, MapDrawGetLightPtrDoor
#include "ingame/map/foot_se.h"         // FootSePlayFloor, FootSePlayId
#include "ingame/photo/n_plyr_camera.h" // PLYR_CAMERA_WRK, PlyrCameraSetLight, PlyrCameraGetLightWrk
#include "ingame/plyr/player.h"         // PLYR_WRK, GetPlyrAreaNo, PlyrGetCurNo, PlyrLookCtrl
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "main/gphase.h"                // GetGPhaseCtrl
#include "reipan/reipan_memory.h"       // ReiPan_GetHostPointer
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP

#include "graphics/effect/effect_sub.h" // Vector2Rot

#include "ingame/char_model_dat.inc"    // char_scr_tbl, char_joint_tbl, char_bounds_tbl, char_bounds_dflt

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────
// File statics and helpers
// ──────────────────────────────────────────────────────────────────────

static const sceVu0FVECTOR char_origin = { 0.0f, 0.0f, 0.0f, 1.0f };   // rdata 0x329500
static const sceVu0FVECTOR char_zero   = { 0.0f, 0.0f, 0.0f, 0.0f };   // data 0x329530

// The phase control, fetched once (sdata gp-0xca4; the player model's
// GetBoundsDraw and FootIk test for phase 0x22).
static GPHASE_CTRL *char_gphase_ctrl;

static short CharGetPhaseId(void)
{
    if (char_gphase_ctrl == nullptr)
        char_gphase_ctrl = GetGPhaseCtrl();
    return char_gphase_ctrl->leaf->id;
}

// v clamped to [lo, hi] (FF3's inline helper, linkonce 0x002ad688).
static float CharClamp(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (hi < v)
        return hi;
    return v;
}

// out = T * rot, T the unit matrix moved to pos: rot placed at pos (the
// ROM multiplies through a copy of the unit matrix at data 0x3293c0 whose
// translation row got pos.xyz).
static void CharPlaceMatrix(sceVu0FMATRIX out, const float *pos, sceVu0FMATRIX rot)
{
    sceVu0FMATRIX t;

    sceVu0UnitMatrix(t);
    t[3][0] += pos[0];
    t[3][1] += pos[1];
    t[3][2] += pos[2];
    sceVu0MulMatrix(out, t, rot);
}

// The light set made for a model from the map's (the enemies', NPCs' and
// cats' default; the same values as data 0x3506f0): no static light,
// self-reflection off, both flashlights folded in.
static void CharSetCreationDefault(GRA3DEMULATIONLIGHTDATACREATIONDATA *cd)
{
    memset(cd, 0, sizeof(*cd));
    cd->fAngleScale           = 1.0f;
    cd->fDiffuseScale         = 0.5f;
    cd->fMaplightScale        = 1.0f;
    cd->bEnableFlashlight     = 1;
    cd->bEmulateFlashlight    = 1;
    cd->bEnableFlashlight2    = 1;
    cd->bEmulateFlashlight2   = 1;
}

// Light slot 2 of ld becomes a light from the camera (its direction negated).
static void CharSetCameraLight(GRA3DLIGHTDATA *ld, const float *diffuse)
{
    float *dir = gra3dGetCamera()->matCoord[2];
    G3DLIGHT *l = &ld->aLight[2];

    l->vDirection[0] = -dir[0];
    l->vDirection[1] = -dir[1];
    l->vDirection[2] = -dir[2];
    l->vDirection[3] = -dir[3];
    l->vDiffuse[0] = diffuse[0];
    l->vDiffuse[1] = diffuse[1];
    l->vDiffuse[2] = diffuse[2];
    l->vDiffuse[3] = diffuse[3];
    ld->aStatus[2].bEnable = 1;
}

// The script table of model type type (types above 32 use 32's).
// FF3 0x001441d8

static const u_short **CharGetScrTbl(int type)
{
    if (type > 0x20)
        type = 0x20;
    return (const u_short **)char_scr_tbl[type];
}

// The default box: row 0 of the one-row table, its next links followed (FF3
// folded the index to 0, so a link would loop for ever; the row has none).
// FF3 0x001443f8

static const CHAR_BOUNDS *CharGetBoundsDefault(int no)
{
    while (char_bounds_dflt[no].next != -1)
        no = char_bounds_dflt[no].next;
    return &char_bounds_dflt[no];
}

// The box of character no: its row, the next links followed.
// FF3 0x00144420

static const CHAR_BOUNDS *CharGetBounds(int no)
{
    while (char_bounds_tbl[no].next != -1)
        no = char_bounds_tbl[no].next;
    return &char_bounds_tbl[no];
}

// Character no's box through mat into the world box bb; 0 for a zero box.
// No callers in FF3.
// FF3 0x00144450

static int CharApplyBounds(float (*bb)[4], float (*mat)[4], int no)
{
    const CHAR_BOUNDS *b = CharGetBounds(no);

    if (memcmp(b->min, char_zero, sizeof(sceVu0FVECTOR)) == 0
        && memcmp(b->max, char_zero, sizeof(sceVu0FVECTOR)) == 0)
        return 0;
    gra3dbbApplyFromBounds(bb, (float *)b->min, (float *)b->max, mat);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// AnimeCodeInterpretorChar
// ──────────────────────────────────────────────────────────────────────

// Command type 3 (the player only): 0 / 1 the footstep of the floor under
// joint 5 / 0x11 (the left / right foot), 2 / 3 the plain footstep SE 0x1bff
// there, 4 / 5 the current player's voice (two takes; 5 has one for Kei).
// FF3 0x00144200 (vtable +0x1c)

void AnimeCodeInterpretorChar::Command3(short arg)
{
    sceVu0FVECTOR pos;
    u_int se;

    switch ((u_short)arg)
    {
    case 0:
    case 2:
        if (ingame_wrk.plyr_wrk == nullptr)
            return;
        mot->ani_ctrl->GetCoordPos(pos, owner->GetJointCoord(5), (float *)char_origin);    // FF3: vtable +0x34
        if (arg == 0)
            FootSePlayFloor(pos, ingame_wrk.plyr_wrk->mh_no, 0x3200, 0x1000);
        else
            FootSePlayId(0x1bff, pos, 0x3200, 0x1000);
        return;

    case 1:
    case 3:
        if (ingame_wrk.plyr_wrk == nullptr)
            return;
        mot->ani_ctrl->GetCoordPos(pos, owner->GetJointCoord(0x11), (float *)char_origin);
        if (arg == 1)
            FootSePlayFloor(pos, ingame_wrk.plyr_wrk->mh_no, 0x3200, 0x1000);
        else
            FootSePlayId(0x1bff, pos, 0x3200, 0x1000);
        return;

    case 4:
        if (ingame_wrk.plyr_wrk == nullptr)
            return;
        switch (PlyrGetCurNo())
        {
        case 0:  se = 0x1a9f0000; break;
        case 1:  se = 0x1b7f0000; break;
        default: return;
        }
        SndSePlay(se, 100, 0x1000, 0, nullptr, 0);
        return;

    case 5:
        if (ingame_wrk.plyr_wrk == nullptr)
            return;
        switch (PlyrGetCurNo())
        {
        case 0:  se = 0x1a9f0000; break;
        case 1:  se = 0x1b7f0000; break;
        case 2:  se = 0x1b810000; break;
        default: return;
        }
        SndSePlay(se | 1, 100, 0x1000, 0, nullptr, 0);
        return;
    }
}

// ──────────────────────────────────────────────────────────────────────
// CharModel
// ──────────────────────────────────────────────────────────────────────

// No watch control or root-move data yet; character and type come with Load.
// FF3 0x00144ed0 (complete; base-object twin 0x00144f08)
// callcheck: GameAnimeModelCtorBase (the base-class constructor)

CharModel::CharModel()
{
    ctrl = nullptr;
    rmd  = nullptr;
}

// The root-move data and the watch control deleted (through their
// deleting destructors), then GameAnimeModel's.
// FF3 0x00144fb0 (deleting 0x00145020; base 0x00144f40)
// callcheck: GameAnimeModelDtorBase (the base class destructor)

CharModel::~CharModel()
{
    if (rmd != nullptr)
    {
        rmd->~CharRmdData();            // FF3: vtable +0x0c
        operator_delete(rmd);
    }
    if (ctrl != nullptr)
    {
        ctrl->~CharWatchControl();      // FF3: vtable +0x04
        operator_delete(ctrl);
    }
}

// Character chr, model type type, then GameMotionModel's load of the three
// files.
// FF3 0x00145090

int CharModel::Load(short chr, short type, short mdl, short mot, short shp)
{
    this->chr  = chr;
    this->type = type;
    return GameMotionModel::Load(mdl, mot, shp);
}

// The shape motions stopped, then the script started (GameAnimeModel's).
// FF3 0x001450e0 (vtable +0x40)

void CharModel::StartAnime(int no, int start, int skip, int blend, float time, float speed)
{
    StopShapes();                       // FF3: vtable +0x50
    GameAnimeModel::StartAnime(no, start, skip, blend, time, speed);
}

// Every shape motion stopped.
// FF3 0x00145170 (vtable +0x50)

void CharModel::StopShapes()
{
    if (mot != nullptr)
        mot->StopShapeAll();            // FF3: vtable +0x30
}

// GameAnimeModel's build, then the type's scripts, the watch control, the
// shapes and the root-move data (the motion PK4's member 1).
// FF3 0x001451a0 (vtable +0x00)

void CharModel::Build()
{
    GameAnimeModel::Build();
    scr->scr_tbl = CharGetScrTbl(type);
    CreateWatchControl();               // FF3: vtable +0x44
    CreateShapes();                     // FF3: vtable +0x48
    CreateRmdData(Pk4GetData(mot_data, 1));     // FF3: vtable +0x4c
}

// A plain watch control (FF3 0x28 bytes).
// FF3 0x00145220 (vtable +0x44)

void CharModel::CreateWatchControl()
{
    ctrl = new (operator_new(sizeof(CharWatchControl))) CharWatchControl(this);  // FF3 0x28 bytes
}

// The root-move data (FF3 0x14 bytes).
// FF3 0x00145280 (vtable +0x4c)

void CharModel::CreateRmdData(void *data)
{
    rmd = new (operator_new(sizeof(CharRmdData))) CharRmdData(data);  // FF3 0x14 bytes
}

// The motion's root move on axis this step: while a blend runs, the blend's
// (attr 0) or the motion's from frame 0 (attr 1); else the motion's at its
// frame. 0 without root-move data or while CanRootMove says no.
// FF3 0x001452d0

float CharModel::GetRootMove(int axis, u_short attr)
{
    float    frame, step;
    MOT_CTRL *mc;

    if (rmd == nullptr || CanRootMove() == 0)   // FF3: vtable +0x5c
        return 0.0f;

    frame = GetMotFrame();
    step  = mot->GetBoneMotion()->GetStep();    // FF3: vtable +0x40
    mc    = mot->GetBoneMotion();
    if (mc->blend == 0)
        return rmd->GetMove(GetMotNo(), axis, frame, step);
    if (attr == 0)
        return rmd->GetBlendMove(axis, step);
    if (attr == 1)
        return rmd->GetMove(GetMotNo(), axis, 0.0f, step);
    return 0.0f;
}

// The bone motion's root-yaw callback and its argument.
// FF3 0x00145410

void CharModel::SetYawFunc(BMD_YAW_FUNC func, void *arg)
{
    MOT_CTRL *mc = mot->GetBoneMotion();        // FF3: vtable +0x40

    mc->yaw_arg  = arg;
    mc->yaw_func = func;
}

// The bone motion's root yaw to the motion's end; 0 without a motion object.
// FF3 0x00145458

float CharModel::GetMotionYaw()
{
    if (mot == nullptr)
        return 0.0f;
    return mot->GetBoneMotion()->motion_yaw;
}

// Drawn when the box is in view.
// FF3 0x00145498 (vtable +0x14)

int CharModel::IsDrawn()
{
    return IsInView() != 0;
}

// 1 when the world box is on screen; also without a box, and always for
// characters 0x1d, 0x24, 0x27 and 0x28.
// FF3 0x001454b8

int CharModel::IsInView()
{
    float bb[8][4];

    if (CalcBBox(bb) == 0)
        return 1;
    if (chr == 0x27 || chr == 0x28 || chr == 0x24 || chr == 0x1d)
        return 1;
    return CheckModelBoundingBox(bb);
}

// Joint joint's coordinate for this character, -1: none.
// FF3 0x00145528 (vtable +0x34)

int CharModel::GetJointCoord(int joint)
{
    return char_joint_tbl[chr][joint];
}

// The script handler: an AnimeCodeInterpretorChar, which also becomes the
// player's handler (FF3 0x10 bytes).
// FF3 0x00145550 (vtable +0x3c)
// callcheck: MotScrHdlCtor2 (the base constructor, in the new-expression)

void CharModel::CreateAnimeHandler()
{
    AnimeCodeInterpretorChar *h;

    h = new (operator_new(sizeof(AnimeCodeInterpretorChar))) AnimeCodeInterpretorChar(mot, scr, this);  // FF3 0x10 bytes
    hdl = h;
    scr->hdl = h;
}

// The default box.
// FF3 0x001455b0

const CHAR_BOUNDS *CharModel::GetBoundsDefault()
{
    return CharGetBoundsDefault(0);
}

// This character's box.
// FF3 0x001455c8

const CHAR_BOUNDS *CharModel::GetCharBounds()
{
    return CharGetBounds(chr);
}

// 0 for a zero box (both corners at 0).
// FF3 0x001455e0

int CharModel::HasBounds(const CHAR_BOUNDS *b)
{
    if (memcmp(b->min, char_zero, sizeof(sceVu0FVECTOR)) == 0
        && memcmp(b->max, char_zero, sizeof(sceVu0FVECTOR)) == 0)
        return 0;
    return 1;
}

// b as its two corners.
// FF3 0x00145650 (vtable +0x54)

void CharModel::GetBounds(const CHAR_BOUNDS *b, float *min, float *max)
{
    for (int i = 0; i < 4; i++)
    {
        min[i] = b->min[i];
        max[i] = b->max[i];
    }
}

// The box drawn with: GetBounds.
// FF3 0x00145670 (vtable +0x58)

void CharModel::GetBoundsDraw(const CHAR_BOUNDS *b, float *min, float *max)
{
    GetBounds(b, min, max);             // FF3: vtable +0x54
}

// The default box at coordinate 0, as drawn.
// FF3 0x00145698

int CharModel::CalcBBoxDefault(float (*bb)[4])
{
    return ApplyBounds(bb, GetBoundsDefault(), 0, 1);
}

// The world box: this character's box on its root joint (joint 0x1b), or
// the default box when the model has none.
// FF3 0x001456e0

int CharModel::CalcBBox(float (*bb)[4])
{
    int coord = GetJointCoord(0x1b);    // FF3: vtable +0x34

    if (coord < 0)
        return CalcBBoxDefault(bb);
    return ApplyBounds(bb, GetCharBounds(), coord, 0);
}

// b's corners (draw: GetBoundsDraw's, else GetBounds') through coordinate
// coord's world matrix into bb; 0 for a zero box.
// FF3 0x00145768

int CharModel::ApplyBounds(float (*bb)[4], const CHAR_BOUNDS *b, int coord, int draw)
{
    sceVu0FVECTOR min, max;

    if (HasBounds(b) == 0)
        return 0;
    if (draw != 0)
        GetBoundsDraw(b, min, max);     // FF3: vtable +0x58
    else
        GetBounds(b, min, max);         // FF3: vtable +0x54
    gra3dbbApplyFromBounds(bb, min, max, mot->ani_ctrl->GetCoord(0)[coord].matLocalWorld);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CharCatModel
// ──────────────────────────────────────────────────────────────────────

// Part 0: at the cat's position, turned by its facing (half a turn: the
// models face -z). Part 1: on part 0's joint 0x1c (its world matrix
// flipped back to model space).
// FF3 0x00144500 (vtable +0x1c)

void CharCatModel::GetWorldMatrix(sceVu0FMATRIX out)
{
    sceVu0FMATRIX rot;
    float         ry;

    if (part == 0)
    {
        ry = cat->rot[1] - 3.1415927f;
        RotLimitChk(&ry);
        sceVu0UnitMatrix(rot);
        sceVu0RotMatrixY(rot, rot, NormalizeAngle(ry));
        CharPlaceMatrix(out, cat->pos, rot);
    }
    else
    {
        GameModel *body = cat->mdl;
        int        coord = body->GetJointCoord(0x1c);   // FF3: vtable +0x34

        sceVu0MulMatrix(out, body->GetCoord(0)[coord].matLocalWorld, mdl_flip_yz);
    }
}

// The cat's light set: its area's room lights made for a character at
// joint 1 (part 1: the body's joint 1), the ambient raised by 0.9 and a
// light from the camera (0.25 grey) in slot 2.
// FF3 0x001446e8 (vtable +0x20)
// callcheck: gra3dGetCamera (in CharSetCameraLight)

void CharCatModel::DrawMdlPre()
{
    GRA3DLIGHTDATA                      ld;     // 0x1610 bytes
    GRA3DEMULATIONLIGHTDATACREATIONDATA cd;
    GRA3DLIGHTDATA *src;
    sceVu0FVECTOR   pos;
    const float     grey[4] = { 0.25f, 0.25f, 0.25f, 0.0f };
    int             area;

    area = cat->area;
    if (area == -1)
        area = GetPlyrAreaNo();
    src = MapDrawGetLightPtr(area, 0);
    if (src == nullptr)                 // PC: the area's room is not loaded (FF3 reads EE address 0)
        return;
    memcpy(&ld, src, sizeof(ld));
    CharSetCreationDefault(&cd);
    gra3dGenerateLightDataToChar(&ld, src, &cd);

    if (part == 0)
        GetCoordPos(pos, GetJointCoord(1), (float *)char_origin);   // FF3: vtable +0x34 / +0x2c
    else
        cat->mdl->GetCoordPos(pos, cat->mdl->GetJointCoord(1), (float *)char_origin);
    gra3dEmulateLightData(&ld, &ld, pos, cd.fMaplightScale);

    ld.vAmbient[0] += 0.9f;
    ld.vAmbient[1] += 0.9f;
    ld.vAmbient[2] += 0.9f;
    CharSetCameraLight(&ld, grey);
    gra3dSetLightData(&ld, nullptr);
}

// The cats' watch control (FF3 0x28 bytes).
// FF3 0x001448a0 (vtable +0x44)

void CharCatModel::CreateWatchControl()
{
    ctrl = new (operator_new(sizeof(CharCatWatchControl))) CharCatWatchControl(this);  // FF3 0x28 bytes
}

// ──────────────────────────────────────────────────────────────────────
// CharEnemyModel
// ──────────────────────────────────────────────────────────────────────

// The enemy; no bone matrices yet.
// FF3 0x00144948 (complete; base-object twin 0x00144900)
// callcheck: CharModelCtor (the base-class constructor)

CharEnemyModel::CharEnemyModel(ENE_WRK *ene)
{
    this->ene = ene;
    bone_num  = 0;
    bone      = nullptr;
}

// The bone matrices back to the system heap, then CharModel's.
// FF3 0x001449f0 (deleting 0x00144a50; base 0x00144990)
// callcheck: CharModelDtorBase (the base class destructor)

CharEnemyModel::~CharEnemyModel()
{
    if (bone != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), bone, 0, 0, 0);
}

// The effect slots using this model let go of it before it is built.
// FF3 0x00144ab0 (vtable +0x04)

void CharEnemyModel::BuildPre()
{
    IngameEffSlotMdlReset(id, 0);
}

// At the enemy's body position, turned z, then -x, then y (half a turn
// added: the models face -z).
// FF3 0x00144ad0 (vtable +0x1c)

void CharEnemyModel::GetWorldMatrix(sceVu0FMATRIX out)
{
    sceVu0FMATRIX rot;
    float rx = -ene->rot[0];
    float ry = ene->rot[1] - 3.1415927f;
    float rz = ene->rot[2];

    RotLimitChk(&rx);
    RotLimitChk(&ry);
    RotLimitChk(&rz);
    sceVu0UnitMatrix(rot);
    sceVu0RotMatrixZ(rot, rot, NormalizeAngle(rz));
    sceVu0RotMatrixX(rot, rot, NormalizeAngle(rx));
    sceVu0RotMatrixY(rot, rot, NormalizeAngle(ry));
    CharPlaceMatrix(out, ene->pos, rot);
}

// GameAnimeModel's, then the bones' world matrices kept.
// FF3 0x00144c40 (vtable +0x18)

void CharEnemyModel::SetWorldMatrix(sceVu0FMATRIX m)
{
    GameAnimeModel::SetWorldMatrix(m);
    SaveBones();
}

// Every bone's world matrix (and the root's) into bone, made the first time
// (bone count + 1 matrices from the system heap).
// FF3 0x00144c68

void CharEnemyModel::SaveBones()
{
    if (mot == nullptr)
        return;

    if (bone == nullptr)
    {
        bone_num = mot->GetBoneMotion()->GetBoneNum() + 1;  // FF3: vtable +0x40
        bone = (sceVu0FMATRIX *)heapCtrlMalloc(GetSystemHeapWrkP(), bone_num << 6, 0, 0, 0);
        if (bone == nullptr)
            return;
    }
    for (int i = 0; i < bone_num; i++)
        sceVu0CopyMatrix(bone[i], mot->ani_ctrl->GetCoord(0)[i].matLocalWorld);
}

// in through the saved matrix of coordinate coord (nothing before the
// first frame).
// FF3 0x00144d70 (vtable +0x2c)

void CharEnemyModel::GetCoordPos(float *out, int coord, float *in)
{
    if (bone != nullptr)
        sceVu0ApplyMatrix(out, bone[coord], in);
}

// Row axis of the saved matrix of coordinate coord.
// FF3 0x00144db0 (vtable +0x30)

void CharEnemyModel::GetCoordAxis(float *out, int coord, int axis)
{
    if (bone != nullptr)
    {
        out[0] = bone[coord][axis][0];
        out[1] = bone[coord][axis][1];
        out[2] = bone[coord][axis][2];
        out[3] = bone[coord][axis][3];
    }
}

// The enemy's light set: the player's room lights made for a character at
// the enemy's joint 1, with a light from the camera in its own colour
// (_4e0, the paralysis look) in slot 2.
// FF3 0x00144dd8 (vtable +0x20)
// callcheck: gra3dGetCamera (in CharSetCameraLight)

void CharEnemyModel::DrawMdlPre()
{
    GRA3DLIGHTDATA                      ld;     // 0x1610 bytes
    GRA3DEMULATIONLIGHTDATACREATIONDATA cd;
    GRA3DLIGHTDATA *src;

    src = MapDrawGetLightPtr(GetPlyrAreaNo(), 0);
    if (src == nullptr)                 // PC: the area's room is not loaded (FF3 reads EE address 0)
        return;
    memcpy(&ld, src, sizeof(ld));
    CharSetCreationDefault(&cd);
    gra3dGenerateLightDataToChar(&ld, src, &cd);
    gra3dEmulateLightData(&ld, &ld, ene->jpos1, cd.fMaplightScale);
    CharSetCameraLight(&ld, ene->_4e0);
    gra3dSetLightData(&ld, nullptr);
}

// ──────────────────────────────────────────────────────────────────────
// CharNpcModel
// ──────────────────────────────────────────────────────────────────────

// The NPCs' root-move data (FF3 0x14 bytes).
// FF3 0x00145840 (vtable +0x4c)
// callcheck: CharRmdDataCtorBase (the base constructor, in the new-expression)

void CharNpcModel::CreateRmdData(void *data)
{
    rmd = new (operator_new(sizeof(CharPlayerRmdData))) CharPlayerRmdData(data);  // FF3 0x14 bytes
}

// At the NPC's position, turned by its facing (half a turn added).
// FF3 0x001458a0 (vtable +0x1c)

void CharNpcModel::GetWorldMatrix(sceVu0FMATRIX out)
{
    sceVu0FMATRIX rot;
    float ry = cam->rot[1] - 3.1415927f;

    RotLimitChk(&ry);
    sceVu0UnitMatrix(rot);
    sceVu0RotMatrixY(rot, rot, NormalizeAngle(ry));
    CharPlaceMatrix(out, cam->pos, rot);
}

// The NPC's light set at its joint 1, in its area (-1: the player's).
// FF3 0x001459b8 (vtable +0x20)

void CharNpcModel::DrawMdlPre()
{
    sceVu0FVECTOR pos;
    int area = cam->area;

    if (area == -1)
        area = GetPlyrAreaNo();
    GetCoordPos(pos, GetJointCoord(1), (float *)char_origin);   // FF3: vtable +0x34 / +0x2c
    PlyrCameraSetLight(cam->act.no, area, pos);
}

// The shadow: the default box's centre lit by the NPC light set's blend
// (0.25 in the real world, 0.4 in the other), drawn into the box.
// FF3 0x00145a48 (vtable +0x28)

void CharNpcModel::DrawSecond()
{
    ModelObject  *obj = cam->mdl->mot->ani_ctrl;
    float         bb[8][4];
    sceVu0FVECTOR center;
    G3DLIGHT      light;

    CalcBBoxDefault(bb);
    gra3dbbCalcCenterBase(center, bb);
    _SetLightToShadow(PlyrCameraGetLightWrk());
    gra3dGenerateBlendedLight(&light, center, (ingame_wrk.b03 == 1) ? 0.25f : 0.4f);
    gra3dLightEnableAll(0);
    obj->DrawShadowBox(&light, bb);
}

// ──────────────────────────────────────────────────────────────────────
// CharPlayerModel
// ──────────────────────────────────────────────────────────────────────

// The player; the three lights cleared (the flashlight a spot along +z,
// the second off, the self-reflection light a point light), foot IK on,
// the flashlight on at full power, no shapes yet. foot_y is left as is
// (FootIk sets it before reading it).
// FF3 0x00145c50 (complete; base-object twin 0x00145b00)
// callcheck: CharModelCtor (the base-class constructor)

CharPlayerModel::CharPlayerModel(PLYR_WRK *plyr)
{
    this->plyr = plyr;

    memset(&fl, 0, sizeof(fl));
    fl.vPosition[3]  = 1.0f;
    fl.vDirection[2] = 1.0f;            // data 0x329460: (0, 0, 1, 0)
    fl.Type          = G3DLIGHT_SPOT;
    fl.fFalloff      = 1.0f;

    memset(&fl2, 0, sizeof(fl2));
    fl2.vPosition[3] = 1.0f;
    fl2.Type         = INVALID_G3DLIGHTTYPE;
    fl2.fFalloff     = 1.0f;

    memset(&reflectionlight, 0, sizeof(reflectionlight));
    reflectionlight.vPosition[3] = 1.0f;
    reflectionlight.Type         = G3DLIGHT_POINT;
    reflectionlight.fFalloff     = 1.0f;

    foot_on     = 0;
    foot_ik     = 1;
    light_scale = 0.55f;
    fl_scale    = 0.9f;
    fl_power    = 1;
    fl_on       = 0;
    hand  = nullptr;
    blink = nullptr;
    face  = nullptr;
}

// The shapes deleted, then CharModel's.
// FF3 0x00145dd8 (deleting 0x00145e10; base 0x00145da0)
// callcheck: CharModelDtorBase (the base class destructor)

CharPlayerModel::~CharPlayerModel()
{
    ReleaseShapes();
}

// The blink, the face (in its world's default state) and the light hand.
// FF3 0x00145e50 (vtable +0x48)
// callcheck: CharPlayerShapeCtorBase (the Mepati base constructor)
// callcheck: CharPlayerShapeDefaultDreamFaceInstance (CharPlayerShapeDefaultDreamFace::Instance)
// callcheck: CharPlayerShapeLightHandInstance (CharPlayerShapeLightHand::Instance)

void CharPlayerModel::CreateShapes()
{
    blink = new (operator_new(sizeof(CharPlayerShapeMepati))) CharPlayerShapeMepati(this);  // FF3 0x14 bytes

    face = new (operator_new(sizeof(CharPlayerShape))) CharPlayerShape(this);  // FF3 0x14 bytes
    if (ingame_wrk.b03 == 1)
        face->SetStatus(CharPlayerShapeDefaultRealFace::Instance(), 1);
    else
        face->SetStatus(CharPlayerShapeDefaultDreamFace::Instance(), 2);

    hand = new (operator_new(sizeof(CharPlayerShape))) CharPlayerShape(this);  // FF3 0x14 bytes
    hand->SetStatus(CharPlayerShapeLightHand::Instance(), 8);
}

// The three shapes deleted (through their deleting destructors).
// FF3 0x00145f60

void CharPlayerModel::ReleaseShapes()
{
    CharPlayerShape *shape[3] = { blink, face, hand };

    for (int i = 0; i < 3; i++)
    {
        if (shape[i] != nullptr)
        {
            shape[i]->~CharPlayerShape();   // FF3: vtable +0x04
            operator_delete(shape[i]);
        }
    }
}

// The player's root-move data (FF3 0x14 bytes).
// FF3 0x00145fd8 (vtable +0x4c)
// callcheck: CharRmdDataCtorBase (the base constructor, in the new-expression)

void CharPlayerModel::CreateRmdData(void *data)
{
    rmd = new (operator_new(sizeof(CharPlayerRmdData))) CharPlayerRmdData(data);  // FF3 0x14 bytes
}

// The player's light set at its joint 1.
// FF3 0x00146038 (vtable +0x20)

void CharPlayerModel::DrawMdlPre()
{
    GRA3DEMULATIONLIGHTDATACREATIONDATA *data = GetLightCreationData();
    sceVu0FVECTOR pos;

    GetCoordPos(pos, GetJointCoord(1), (float *)char_origin);   // FF3: vtable +0x34 / +0x2c
    SetLightData(pos, data);
}

// The room's lights (the door's area while a door opens) made for the
// player at pos as data says; the projector spot when it is on, the ambient
// raised by 0.9, the lightning in slot 1 while it flashes, data's static
// light from the camera in slot 2.
// FF3 0x001460b0
// callcheck: gra3dGetCamera (in CharSetCameraLight)

void CharPlayerModel::SetLightData(float *pos, GRA3DEMULATIONLIGHTDATACREATIONDATA *data)
{
    GRA3DLIGHTDATA ld;                  // 0x1610 bytes
    GRA3DLIGHTDATA *src;
    int area;
    int id;

    area = GetPlyrAreaNo();
    src  = MapDrawGetLightPtrDoor(area, area == 0x12);
    if (src == nullptr)                 // PC: the area's room is not loaded (FF3 reads EE address 0)
        return;
    gra3dGenerateLightDataToChar(&ld, src, data);
    if (gra3dIsSpecialLightActive() != 0)
    {
        id = gra3dGetProjectorSpotId();
        if (id != 0x7fffffff)
        {
            ld.aLight[id] = gra3dGetProjectorSpot();
            ld.aStatus[id].bEnable = 1;
        }
    }
    gra3dEmulateLightData(&ld, &ld, pos, data->fMaplightScale);

    ld.vAmbient[0] += 0.9f;
    ld.vAmbient[1] += 0.9f;
    ld.vAmbient[2] += 0.9f;

    if (EffThunderIsLightning() != 0)
    {
        EffThunderSetLight(&ld.aLight[1]);
        ld.aStatus[1].bEnable         = 1;
        ld.aStatus[1].bEnableToChar   = 1;
        ld.aStatus[1].bEnableToShadow = 1;
    }
    if (data->bEnableStaticDirLight != 0)
    {
        g3dutilSetLightDefault(&ld.aLight[2], G3DLIGHT_DIRECTIONAL);
        CharSetCameraLight(&ld, data->vStaticDirLightColor);
    }
    gra3dSetLightData(&ld, nullptr);
}

// How the player's light set is made (bss 0x391610, filled on the first
// call): self-reflection on, the flashlights not folded in; each call the
// static light's grey (0.18 while a look anime plays, else 0.06) and
// whether it is on (not while the light is held, unless looking).
// FF3 0x00146258

GRA3DEMULATIONLIGHTDATACREATIONDATA *CharPlayerModel::GetLightCreationData()
{
    static GRA3DEMULATIONLIGHTDATACREATIONDATA cd;
    static bool                                cd_init;     // sdata gp-0x6a0
    float grey;

    if (!cd_init)
    {
        cd_init = true;
        memset(&cd, 0, sizeof(cd));
        cd.vStaticDirLightColor[0] = 0.06f;
        cd.vStaticDirLightColor[1] = 0.06f;
        cd.vStaticDirLightColor[2] = 0.06f;
        cd.fAngleScale             = 1.0f;
        cd.fDiffuseScale           = 1.0f;
        cd.fMaplightScale          = light_scale;
        cd.bEnableSelfreflection   = 1;
        cd.bEmulateSelfreflection  = 1;
    }

    grey = (IsLookAnime() == 0) ? 0.06f : 0.18f;
    cd.vStaticDirLightColor[0] = grey;
    cd.vStaticDirLightColor[1] = grey;
    cd.vStaticDirLightColor[2] = grey;

    // 0x8000: the light is held (the light motion file)
    if ((ingame_wrk.plyr_wrk->flags & 0x8000) == 0 || IsLookAnime() != 0)
        cd.bEnableStaticDirLight = 1;
    else
        cd.bEnableStaticDirLight = 0;
    return &cd;
}

// At the player's position, turned by its facing (half a turn added).
// FF3 0x00146390 (vtable +0x1c)

void CharPlayerModel::GetWorldMatrix(sceVu0FMATRIX out)
{
    sceVu0FMATRIX rot;
    float ry = plyr->rot[1] - 3.1415927f;

    RotLimitChk(&ry);
    sceVu0UnitMatrix(rot);
    sceVu0RotMatrixY(rot, rot, NormalizeAngle(ry));
    CharPlaceMatrix(out, plyr->pos, rot);
}

// CharModel's box, made at least as deep as the root joint's (0x1b) reach
// from joint 2 on the floor plane (+2); in phase 0x22 deeper by 10 and 5
// wider on each side.
// FF3 0x001464a8 (vtable +0x58)
// callcheck: GetGPhaseCtrl (in CharGetPhaseId)

void CharPlayerModel::GetBoundsDraw(const CHAR_BOUNDS *b, float *min, float *max)
{
    sceVu0FVECTOR root, j2;
    float dx, dz, len;

    CharModel::GetBoundsDraw(b, min, max);
    GetCoordPos(root, GetJointCoord(0x1b), (float *)char_origin);   // FF3: vtable +0x34 / +0x2c
    GetCoordPos(j2, GetJointCoord(2), (float *)char_origin);
    dx  = root[0] - j2[0];
    dz  = root[2] - j2[2];
    len = sqrtf(dx * dx + dz * dz);
    if (max[2] < len)
        max[2] = len + 2.0f;

    if (CharGetPhaseId() == 0x22)
    {
        max[2] += 10.0f;
        min[0] -= 5.0f;
        max[0] += 5.0f;
    }
}

// The step of a walk-type motion (0..3 within its group of four), 0 when
// the motion is none of them.
// FF3 0x00146640 (vtable +0x60)

int CharPlayerModel::GetWalkStep()
{
    static const short grp[13] = { 7, 0x10, 0x18, 0x1f, 0x38, 0x44, 0x3e, 0x4a, 0x50, 0x5c, 0x56, 0x62, 0xe6 };
    short no = GetMotNo();

    for (int i = 0; i < 13; i++)
    {
        if (grp[i] <= no && no < grp[i] + 4)
            return (s_char)(no - grp[i]);
    }
    return 0;
}

// The feet on the floor: outside the anime 0x26..0x3d / 0x5e..0x64 and the
// twelve of data 0x350790, with foot_ik on, each foot (joint 5 / 0x11)
// above its floor (eased by 0.3 a frame, 2.07 below the area height; both
// at the player's height when either floor is more than 5 off it, or in
// phase 0x22) is pulled down by turning the leg (joints 0xd, 9 / 0x19,
// 0x15). A jump of more than 5 starts over.
// FF3 0x001466e8
// callcheck: GetGPhaseCtrl (in CharGetPhaseId)

void CharPlayerModel::FootIk()
{
    static const int skip[12] = { 0x8f, 0x9c, 0x9d, 0x5d, 0xa6, 0x6f, 0x78, 0x7f, 0x75, 0xa1, 0xa2, 0xa3 };   // data 0x350790
    sceVu0FVECTOR lpos, rpos, lfloor, rfloor;
    float py, dl, dr;
    int   anime, area, i;
    ModelObject *obj;

    anime = GetAnimeNo();
    if ((u_int)(anime - 0x26) < 0x18 || (u_int)(anime - 0x5e) < 7)
    {
        foot_on = 0;
        return;
    }
    for (i = 0; i < 12; i++)
    {
        if (anime == skip[i])
        {
            foot_on = 0;
            return;
        }
    }
    if (foot_ik == 0)
    {
        foot_on = 0;
        return;
    }

    GetCoordPos(lpos, GetJointCoord(5), (float *)char_origin);      // FF3: vtable +0x34 / +0x2c
    GetCoordPos(rpos, GetJointCoord(0x11), (float *)char_origin);
    area = GetPlyrAreaNo();
    MapAreaGetHeight(lfloor, lpos, area, 0);
    MapAreaGetHeight(rfloor, rpos, area, 0);

    py = plyr->pos[1];
    if (fabsf(py - lfloor[1]) > 5.0f || fabsf(py - rfloor[1]) > 5.0f || CharGetPhaseId() == 0x22)
    {
        lfloor[1] = py;
        rfloor[1] = py;
    }
    lfloor[1] -= 2.07f;
    rfloor[1] -= 2.07f;

    if (foot_on == 0)
    {
        foot_y[0] = lfloor[1];
        foot_on   = 1;
        foot_y[1] = rfloor[1];
    }
    else
    {
        dl = lfloor[1] - foot_y[0];
        foot_y[0] += dl * 0.3f;
        dr = rfloor[1] - foot_y[1];
        foot_y[1] += 0.3f * dr;
        if (fabsf(dl) > 5.0f || fabsf(dr) > 5.0f)
        {
            foot_on = 0;
            return;
        }
        lfloor[1] = foot_y[0];
        rfloor[1] = foot_y[1];
    }

    if (lfloor[1] < lpos[1])
    {
        obj = mot->ani_ctrl;
        obj->IkLimb(lfloor, GetJointCoord(0xd), GetJointCoord(9), GetJointCoord(5));
    }
    if (rfloor[1] < rpos[1])
    {
        obj = mot->ani_ctrl;
        obj->IkLimb(rfloor, GetJointCoord(0x19), GetJointCoord(0x15), GetJointCoord(0x11));
    }
}

// The lantern arm: the hand (joint 7) chain bent toward its own position
// lowered by the look pitch (x2.5) up to joint 0xc, joint 0xc turned about
// x by the look yaw (x0.4), the bones recomputed.
// FF3 0x00146ac8

void CharPlayerModel::ArmIk()
{
    ModelObject   *obj = mot->ani_ctrl;
    SGDCOORDINATE *c12 = &obj->GetCoord(0)[GetJointCoord(0xc)];     // FF3: vtable +0x34
    SGDCOORDINATE *c7  = &obj->GetCoord(0)[GetJointCoord(7)];
    sceVu0FVECTOR  target;
    sceVu0FMATRIX  rot;

    target[0] = c7->matLocalWorld[3][0];
    target[1] = c7->matLocalWorld[3][1] - plyr->look_pitch * 2.5f;
    target[2] = c7->matLocalWorld[3][2];
    target[3] = c7->matLocalWorld[3][3];
    obj->IkChain(target, GetJointCoord(7), GetJointCoord(0xc), 2);

    sceVu0UnitMatrix(rot);
    sceVu0RotMatrixX(rot, rot, NormalizeAngle(plyr->look_yaw * 0.4f));
    sceVu0MulMatrix(c12->matCoord, c12->matCoord, rot);
    obj->CalcBoneCoordinate();
}

// Anime 0xa1 or 0xa2 (the look).
// FF3 0x00146c90

int CharPlayerModel::IsLookAnime()
{
    return (u_int)(GetAnimeNo() - 0xa1) < 2;
}

// The flashlights for this frame (off unless the light is held or the
// dream light is on): the flashlight copies the light set's spot (slot 38),
// then points along the look (the viewfinder's pitch and the facing while
// it is up; the camera's direction for camera views; else the head's look
// plus the root's yaw) from a point at the model's joint 1 (or the camera's
// for camera views), its range from fl_scale; the self-reflection light
// sits in front of the chest. look: PlyrLookCtrl runs first (not with the
// dream light, which shines straight ahead, wider and dimmer).
// FF3 0x00146cb8

void CharPlayerModel::UpdateLight(int look)
{
    PLYR_WRK     *pw = ingame_wrk.plyr_wrk;
    float range      = fl_scale * 80.8f;
    float refl_range = fl_scale * 5.0f;
    float pos_up     = -5.16f;
    float pos_dist   = -4.0f;
    float dir_up     = 5.2f;
    float dir_dist   = 11.44f;
    float intens;
    float pitch, yaw, root_yaw;
    sceVu0FVECTOR dir, pos, jp;
    sceVu0FMATRIX m;

    if ((pw->flags & 0x8000) == 0 && fl_on == 0)
    {
        fl2.Type = INVALID_G3DLIGHTTYPE;
        fl.Type  = INVALID_G3DLIGHTTYPE;
        return;
    }

    intens = 1.0f;
    if (IsLookAnime() == 0)
    {
        intens = 0.74f;
        if (gra3dIsMonotoneDrawEnable() != 0)
            intens = 0.2f;
    }

    if (look != 0 && fl_on == 0)
        PlyrLookCtrl();

    root_yaw = GetCoordRot(GetJointCoord(0x1b), 1);     // FF3: vtable +0x34

    if (fl_on == 0)
    {
        if ((pw->_0e0 & 1) == 0)
        {
            if ((pw->flags & PLYR_FLG_800000) != 0)
            {
                pitch    = plyr->finder_pitch;
                yaw      = plyr->rot[1];
                dir_up   = 1.8f;
                dir_dist = 8.0f;
                pos_dist = -5.2f;
                pos_up   = -0.92f;
                goto lights;
            }
            pitch = plyr->look_pitch;
            yaw   = plyr->look_yaw + root_yaw;
        }
        else
        {
            dir_up = 1.8f;
            Vector2Rot(gra3dcamGetDirection(), &pitch, &yaw);
            dir_dist = 8.0f;
            pitch -= 3.1415927f;
            RotLimitChk(&pitch);
        }
        RotLimitChk(&yaw);
    }
    else
    {
        pitch = 0.0f;
        yaw   = root_yaw;
        RotLimitChk(&yaw);
        range    = fl_scale * 120.4f;
        dir_up   = 5.6f;
        dir_dist = 0.64f;
        pos_dist = -1.8f;
        intens   = 0.39f;
        pos_up   = -9.64f;
    }

lights:
    fl = *gra3dGetLightRef(GRA3D_LID_SPOT_FLASHLIGHT);

    CalcLightDir(dir_up, dir_dist, pitch, yaw, dir);
    {
        float n = Vu0Rsqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);   // PC: PS2 rsqrt

        fl.vDirection[0] = dir[0] * n;
        fl.vDirection[1] = dir[1] * n;
        fl.vDirection[2] = dir[2] * n;
        fl.vDirection[3] = dir[3];
    }

    if ((pw->_0e0 & 1) == 0)
    {
        CalcLightPos(pos_up, pos_dist, pitch, root_yaw, pos);
    }
    else
    {
        float        *cam = gra3dcamGetPosition();
        sceVu0FVECTOR off = { 2.3f, 7.0f, -5.2f, 1.0f };

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixX(m, m, NormalizeAngle(pitch));
        sceVu0RotMatrixY(m, m, NormalizeAngle(yaw));
        sceVu0ApplyMatrix(off, m, off);
        pos[0] = cam[0] + off[0];
        pos[1] = cam[1] + off[1];
        pos[2] = cam[2] + off[2];
        pos[3] = 1.0f;
    }
    fl.vPosition[0] = pos[0];
    fl.vPosition[1] = pos[1];
    fl.vPosition[2] = pos[2];
    fl.vPosition[3] = pos[3];

    if (fl_power == 0)
        intens = 1.0f;
    for (int i = 0; i < 4; i++)
    {
        fl.vDiffuse[i]  = 1.0f;
        fl.vSpecular[i] = 1.0f;
    }
    gra3dSetLightIntens(&fl, intens);
    fl2.Type = INVALID_G3DLIGHTTYPE;
    fl.fMinRange = range * 0.125f;
    fl.fMaxRange = range;
    fl.fFalloff  = 1.0f;

    // the self-reflection light: 1.58 up, 3.12 ahead of joint 1 along the look
    {
        sceVu0FVECTOR v = { 0.0f, 1.58f, 3.12f, 1.0f };

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixX(m, m, NormalizeAngle(pitch));
        sceVu0RotMatrixY(m, m, NormalizeAngle(yaw));
        GetCoordPos(jp, GetJointCoord(1), (float *)char_origin);    // FF3: vtable +0x34 / +0x2c
        m[3][0] += jp[0];
        m[3][1] += jp[1];
        m[3][2] += jp[2];
        sceVu0ApplyMatrix(reflectionlight.vPosition, m, v);
    }
    reflectionlight.vDiffuse[0] = fl.vDiffuse[0];
    reflectionlight.vDiffuse[1] = fl.vDiffuse[1];
    reflectionlight.vDiffuse[2] = fl.vDiffuse[2];
    reflectionlight.vDiffuse[3] = fl.vDiffuse[3];
    reflectionlight.fMaxRange = refl_range;
    reflectionlight.Type      = G3DLIGHT_POINT;
    reflectionlight.fMinRange = refl_range * 0.5f;
}

// PlyrMdlMain's call: UpdateLight.
// FF3 0x001472e8

void CharPlayerModel::SetLight(int look)
{
    UpdateLight(look);
}

// The shadow: the default box's centre lit by the player's light set's
// blend (0.25 in the real world, 0.4 in the other; the lightning while it
// flashes), drawn into the box with the shadow texture (the pack at EE
// 0xfce000, loaded at boot) sent first.
// FF3 0x00147300 (vtable +0x28)

void CharPlayerModel::DrawSecond()
{
    ModelObject  *obj = mot->ani_ctrl;
    float         bb[8][4];
    sceVu0FVECTOR center;
    G3DLIGHT      light;

    CalcBBoxDefault(bb);
    gra3dbbCalcCenterBase(center, bb);
    _SetLightToShadow(GetLightCreationData());
    if (EffThunderIsLightning() == 0)
        gra3dGenerateBlendedLight(&light, center, (ingame_wrk.b03 == 1) ? 0.25f : 0.4f);
    else
        EffThunderSetLight(&light);
    gra3dLightEnableAll(0);
    PK4SendVram(ReiPan_GetHostPointer(0xfce000), 0, -1, -1);
    obj->DrawShadowBox(&light, bb);
}

// The point up above the model and dist along the turned z axis (turned x
// by pitch, then y by yaw), w 1.
// FF3 0x001473e8

void CharPlayerModel::CalcLightDir(float up, float dist, float pitch, float yaw, float *out)
{
    sceVu0FVECTOR v = { 0.0f, 0.0f, dist, 1.0f };
    sceVu0FMATRIX m;

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(pitch));
    sceVu0RotMatrixY(m, m, NormalizeAngle(yaw));
    m[3][1] += up;
    sceVu0ApplyMatrix(out, m, v);
    out[3] = 1.0f;
}

// CalcLightDir's point from the model's joint 1.
// FF3 0x001474e8

void CharPlayerModel::CalcLightPos(float up, float dist, float pitch, float yaw, float *out)
{
    sceVu0FVECTOR v = { 0.0f, 0.0f, dist, 1.0f };
    sceVu0FVECTOR jp;
    sceVu0FMATRIX m;

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(pitch));
    sceVu0RotMatrixY(m, m, NormalizeAngle(yaw));
    m[3][1] += up;
    sceVu0ApplyMatrix(out, m, v);
    GetCoordPos(jp, GetJointCoord(1), (float *)char_origin);    // FF3: vtable +0x34 / +0x2c
    out[0] += jp[0];
    out[1] += jp[1];
    out[2] += jp[2];
    out[3] = 1.0f;
}

// The camera idle (anime 0x18): unless blending, the bone motion stands at
// the frame the view pitch gives.
// FF3 0x00147638

void CharPlayerModel::SetIdleFrame()
{
    MOT_CTRL *mc = mot->GetBoneMotion();        // FF3: vtable +0x40

    if (mc->blend == 0)
    {
        mc->SetFrame(GetIdleFrame());
        mc->speed = 0.0f;
    }
}

// The viewfinder's pitch (x3, within +-60 deg) as a frame 0..9 of the idle
// (9 looking down to 0 looking up).
// FF3 0x001476a0

float CharPlayerModel::GetIdleFrame()
{
    float p = -plyr->finder_pitch * 3.0f;
    float lim = 1.0471976f;

    p = CharClamp(p, -lim, lim);
    return CharClamp((1.0f - p / lim) * 9.0f * 0.5f, 0.0f, 9.0f);
}

// The costume's hidden meshes: shape motions stopped by character (Rei:
// 2, 9..0x14; Miku: 8..0x13; Kei: 1..3, 9..0x14).
// FF3 0x00147750 (vtable +0x50)

void CharPlayerModel::StopShapes()
{
    static const int range[3][2] = { { 9, 0x14 }, { 8, 0x13 }, { 9, 0x14 } };
    short s = GetShapeChr();

    if (s == 0)
        mot->StopShape(2);              // FF3: vtable +0x2c
    if (s == 2)
    {
        mot->StopShape(1);
        mot->StopShape(2);
        mot->StopShape(3);
    }
    if ((u_short)s >= 3)                // PC: FF3 reads past its stack table here; no player has such a character
        return;
    for (int i = range[s][0]; i <= range[s][1]; i++)
        mot->StopShape(i);
}

// The three shapes' frame.
// FF3 0x00147868

void CharPlayerModel::UpdateShapes()
{
    blink->Update();                    // FF3: vtable +0x08
    face->Update();
    hand->Update();
}

// chr, with the cat-holding variants 3 / 4 as 0 / 1.
// FF3 0x001478c8

short CharPlayerModel::GetShapeChr()
{
    short s = chr;

    if (s == 3)
        s = 0;
    if (s == 4)
        s = 1;
    return s;
}

// The dream light on: no prelighting, black fog.
// FF3 0x001478e8

void CharPlayerModel::SetDreamLight()
{
    fl_on = 1;
    gra3dClearPrelight();
    gra3dSetFogColor(0, 0, 0);
    gra3dApplyFog();
}

// The dream light off: the rooms prelit again.
// FF3 0x00147918

void CharPlayerModel::ClearDreamLight()
{
    fl_on = 0;
    gra3dPrelight(0);
}

// The root move runs except during anime 0xad / 0xae.
// FF3 0x00147938 (vtable +0x5c)

int CharPlayerModel::CanRootMove()
{
    return (u_int)(GetAnimeNo() - 0xad) > 1;
}

// ──────────────────────────────────────────────────────────────────────
// The rest of FF3's file is compiler output: FF3 0x00147960, FF3
// 0x00147970 and FF3 0x00147980 (the atexit destructors of the three
// shape-state singletons CharPlayerShapeDefaultRealFace /
// DefaultDreamFace / LightHand, whose Instance functions CreateShapes
// inlined; implicit for the C++ statics in char_model_sub.c) and FF3 0x00147990
// (the atexit thunk of the GPhaseCtrl singleton, GPhaseCtrlTerm(0x41e468)).
