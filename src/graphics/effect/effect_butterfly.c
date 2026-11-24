/* ==========================================================================
 *  graphics/effect/effect_butterfly.c
 *
 *  The crimson butterflies (see effect_butterfly.h). FF2's module (MioPan
 *  effect_butterfly.c) rewritten for FF3, 0x159188..0x15b348: the model is
 *  a GameMotionModel per butterfly (EffectButterflyMdlCreate) where FF2 had
 *  an ANI_CTRL, every distance and speed is 1/25 of FF2's, the guide's turn
 *  limit compares in float (FF2: double) and its pitch clamp flattens only Y
 *  (FF2 zeroed X and Y), the facing takes GetTrgtRotType2 mode 2 and only
 *  wraps the yaw, and the drawn alpha fades with the XZ distance to the
 *  player. FF3 0x15b320 is the file's empty static initialiser, 0x15b328 its
 *  _GLOBAL__I_ (nothing to run on the host).
 *
 *  The shared model (BUTTERFLY_MDL_PK4 / BUTTERFLY_MOT_PK4) is loaded into
 *  the file cache by effect_data.c's EffButterflyMdlLoad at InitEffects; each
 *  butterfly's own GameMotionModel takes it from there.
 * ======================================================================== */
#include "graphics/effect/effect_butterfly.h"

#include <math.h>                       // sqrtf, fabsf
#include <string.h>                     // memset

#include "common/vu0math.h"             // NormalizeAngle, Vu0Acosf
#include "graphics/effect/effect_sub.h" // Get2PDist3D, Vector2Rot, Get2PosRot, GetTrgtRotType2, Set3DPosTexure2
#include "graphics/game_model.h"        // GameMotionModel
#include "graphics/graph3d/gra3d.h"     // GRA3DLIGHTDATA, gra3dGetCamera, gra3dcamGetPosition
#include "graphics/mmanage.h"           // ModelManager (PlayMotion / IsMotionEnd), mdl_flip_yz
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/plyr/player.h"         // PLYR_WRK
#include "reipan/os/reipan_rand.h"      // ReiPan_Rand, REIPAN_RAND_MAXF
#include "system/eeiop/cd_file_dat.h"   // BUTTERFLY_MDL_PK4, BUTTERFLY_MOT_PK4

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// The five guide presets (data 0x2f6a60, stride 0x30): FF2's values, type 2
// the fast one.
static BUTTERFLY_TARGET_PARAMETER ButterflyTargetType[5] =
{
    {  500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000, 0 },  // data 0x2f6a60
    {  500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000, 0 },  // data 0x2f6a90
    { 1000, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000, 0 },  // data 0x2f6ac0
    {  500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000, 0 },  // data 0x2f6af0
    {  500, 45, 120, 120, 30, 30, 0, 4, 8, 750, 3000, 0 },  // data 0x2f6b20
};

// Their pointer table (data 0x2f6b50).
static BUTTERFLY_TARGET_PARAMETER *ButterflyTargetParamTbl[5] =
{
    &ButterflyTargetType[0], &ButterflyTargetType[1], &ButterflyTargetType[2],
    &ButterflyTargetType[3], &ButterflyTargetType[4],
};

// The butterflies' own light set (bss 0x398970): every light off, ambient
// 3.0. Passed to EffectDrawButterflyOne, which does not read it (FF2 alike).
static GRA3DLIGHTDATA ButterflyLight;

BUTTERFLY_DISP_CTRL     ButterFlyDispCtrl;      // bss 0x399f80
BUTTERFLY_PARTICLE_CTRL ButterFlyParticleCtrl;  // bss 0x399f90

// Set3DPosTexure2 registers (TEX1, ALPHA, ZBUF, TEST, CLAMP, the strip's
// GIF tag): the glow adds (ALPHA 0x48, rdata 0x3527c8), the motes blend
// (ALPHA 0x44, rdata 0x3527f8); both without depth writes.
static u_long ButterflyFreaRegs[6] =
{
    0x161, 0x48, 0x10a000118ULL, 0x5000d, 0, 0x302a400000008004ULL,
};
static u_long ButterflyParticleRegs[6] =
{
    0x161, 0x44, 0x10a000118ULL, 0x5000d, 0, 0x302a400000008004ULL,
};

// The effect page sprite both draw (effdat[0x54]).
#define BUTTERFLY_FREA_TEX      0x54

static const sceVu0FVECTOR butterfly_origin = { 0.0f, 0.0f, 0.0f, 1.0f };  // data 0x329500

// rand() / RAND_MAX, inlined at every use in the ROM.
static inline float ButterflyRand(void)
{
    return (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
}

static void EffectButterflyMakeMatrix(GameMotionModel *mdl, float *pos, float *rot);
static void EffectDrawButterflyOne(GameMotionModel *mdl, GRA3DLIGHTDATA *light, u_char alpha);
static void EffectDrawButterflyFrea(float rot_x, float rot_y, float *pos, u_char alpha);
static int  EffectButterflyUpdate(BUTTERFLY_DISP *bfly);
static int  EffectButterflyUpdateTypeDefault(BUTTERFLY_DISP *bfly);
static int  EffectButterflyGetLockOnInterval(int type, float *pos, float *target);
static int  EffectButterflyUpdateTypeMoveToTarget(BUTTERFLY_DISP *bfly);
static void EffectButterflyMdlCreate(BUTTERFLY_DISP *bfly);
static void EffectButterflyGetVelocity(float speed, float *velocity, float *pos, float *target,
                                       float *dir, int type, int lock_on);
static void ButterflyDelete(SINGLE_LINK_LIST *list, SLL_CELL *cell);
static void EffectGetButterflyPosition(float *pos, BUTTERFLY_DISP *bfly);
static BUTTERFLY_DISP *EffectButterflyGetDispPtr(int id);
static BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGet(int type);
static void EffectButterflyParticleReq(float alpha, SINGLE_LINK_LIST *list, float *pos,
                                       float *velocity, float *accel);
static void EffectButterflyParticleReqAdjustParam(float alpha, SINGLE_LINK_LIST *list,
                                                  float *pos, float *rot);
static void ButterflyParticleOneDraw(float rot_x, float rot_y, BUTTERFLY_PARTICLE *part);
static int  ButterflyParticleUpdate(BUTTERFLY_PARTICLE *part);

// The ROM's rsqrt normalise (vmul.xyz / vaddy.x / vaddz.x / vrsqrt /
// vmulq.xyz): xyz scaled to unit length, w kept.
static inline void ButterflyNormalizeXYZ(float *out, const float *v)
{
    float q = Vu0Rsqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);     // PC: PS2 rsqrt

    out[0] = v[0] * q;
    out[1] = v[1] * q;
    out[2] = v[2] * q;
    out[3] = v[3];
}

// ──────────────────────────────────────────────────────────────────────
// Init
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00159188
void EffectButterflyInit(void)
{
    int i;

    SingleLinkListInit(&ButterFlyDispCtrl.ButterflyList, sizeof(BUTTERFLY_DISP));     // FF3: 0x80

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
        ButterflyLight.aStatus[i].bEnable = 0;

    ButterflyLight.vAmbient[0] = 3.0f;
    ButterflyLight.vAmbient[2] = 3.0f;
    ButterflyLight.vAmbient[1] = 3.0f;
    ButterflyLight.vAmbient[3] = 1.0f;
}

// ──────────────────────────────────────────────────────────────────────
// Drawing
// ──────────────────────────────────────────────────────────────────────

// Starts a new random wingbeat (motion 0 or 1) when the last one ended and
// hands the model its world matrix: the y / z flip rotated X, Y, Z (each
// angle through NormalizeAngle twice), then moved to pos.
// FF3 0x00159208
// callcheck: rand (ButterflyRand: ReiPan_Rand)
static void EffectButterflyMakeMatrix(GameMotionModel *mdl, float *pos, float *rot)
{
    sceVu0FMATRIX m;

    if (mdl == nullptr)
        return;

    sceVu0RotMatrixX(m, mdl_flip_yz, NormalizeAngle(NormalizeAngle(rot[0])));
    sceVu0RotMatrixY(m, m, NormalizeAngle(NormalizeAngle(rot[1])));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(NormalizeAngle(rot[2])));
    m[3][0] += pos[0];                  // FF3: vadd.xyz on row 3
    m[3][1] += pos[1];
    m[3][2] += pos[2];

    // PC: guard -- the ROM reads mot unchecked (EffectButterflyMdlCreate
    // builds it from the cached files before the first frame).
    if (mdl->mot != nullptr && mdl->mot->IsMotionEnd() != 0)
        mdl->mot->PlayMotion((int)(ButterflyRand() + 0.5f), 0, 0.0f, 1.0f);

    mdl->SetWorldMatrix(m);
}

// The model at alpha (FF2 EffectDrawButterflyOne; the light is not read).
// FF3 0x00159368
static void EffectDrawButterflyOne(GameMotionModel *mdl, GRA3DLIGHTDATA *light, u_char alpha)
{
    (void)light;

    if (mdl == nullptr)
        return;

    mdl->SetAlpha(alpha);
    mdl->DrawMdl();
}

// The glow around a butterfly: a 6.832 x 6.836 billboard of effdat[0x54]
// facing the camera (FF2's 170.8 x 170.9 / 25).
// FF3 0x001593b0
static void EffectDrawButterflyFrea(float rot_x, float rot_y, float *pos, u_char alpha)
{
    sceVu0FMATRIX m;

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    m[3][0] += pos[0];                  // FF3: vadd.xyz on row 3
    m[3][1] += pos[1];
    m[3][2] += pos[2];

    Set3DPosTexure2(6.832f, 6.836f, m, ButterflyFreaRegs, BUTTERFLY_FREA_TEX,
                    0x80, 0x80, 0x80, alpha, 0);
}

// ──────────────────────────────────────────────────────────────────────
// The per-frame pass
// ──────────────────────────────────────────────────────────────────────

// Every butterfly: pose and place the model, draw the glow (flickering) and
// the model at the butterfly's alpha times the player-distance fade (0
// closer than 10 units in XZ, ramping to 1 at 20), step it, and on average
// every twentieth frame throw a burst of trail motes.
// FF3 0x001594a8
// callcheck: rand (ButterflyRand: ReiPan_Rand)
void EffectButterflyMain(void)
{
    SLL_CELL       *cell;
    SLL_CELL       *next;
    BUTTERFLY_DISP *bfly;
    GRA3DCAMERA    *cam;
    sceVu0FVECTOR   pos;
    float           rot_x, rot_y;
    float           dx, dz, dist;
    float           fade;

    cell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList);

    cam = gra3dGetCamera();
    Get2PosRot(gra3dcamGetPosition(), cam->vTarget, &rot_x, &rot_y);

    while (cell != nullptr)
    {
        next = cell->pNext;

        bfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell);

        EffectButterflyMakeMatrix(bfly->mdl, bfly->Position, bfly->Rot);
        EffectGetButterflyPosition(pos, bfly);

        // FF3: vsub.xyz / vmul.xyz / vaddz.x -- x and z only.
        dx = ingame_wrk.plyr_wrk->pos[0] - pos[0];
        dz = ingame_wrk.plyr_wrk->pos[2] - pos[2];
        dist = sqrtf(dx * dx + dz * dz);

        fade = 0.0f;
        if (dist >= 9.999907f)
        {
            fade = 1.0f;
            if (dist < 19.999914f)
                fade = (dist - 9.999907f) / 10.000007f;
        }
        fade *= bfly->AlphaRate;

        EffectDrawButterflyFrea(rot_x, rot_y, pos, (u_char)(int)((ButterflyRand() * 10.0f + 8.0f) * fade));
        EffectDrawButterflyOne(bfly->mdl, &ButterflyLight, (u_char)(int)(fade * 128.0f));

        if (EffectButterflyUpdate(bfly) == 0)
        {
            ButterflyDelete(&ButterFlyDispCtrl.ButterflyList, cell);
        }
        else if (ButterflyRand() * 20.0f <= 1.0f)
        {
            EffectButterflyParticleReqAdjustParam(fade, &ButterFlyParticleCtrl.ParticleList,
                                                  pos, bfly->Rot);
        }

        cell = next;
    }
}

// MoveType 0 / 1; anything else ends the butterfly.
// FF3 0x00159740
static int EffectButterflyUpdate(BUTTERFLY_DISP *bfly)
{
    if (bfly->MoveType == 0)
        return EffectButterflyUpdateTypeDefault(bfly);
    if (bfly->MoveType == 1)
        return EffectButterflyUpdateTypeMoveToTarget(bfly);
    return 0;
}

// The ambient butterfly. Every tenth frame X / Y of the (own-frame) velocity
// take a random nudge, kept only inside +-0.7, and are scaled by 0.02 (FF2
// 0.5); the Y nudge leans down above y 200 and up below 0. Each frame the
// velocity goes through the facing to move the position, Z (forward) is
// re-rolled toward 10..12 (8 below 10) and scaled by 0.02, and the alpha
// fades over the first and last 32 frames of the 450.
// FF3 0x00159780
// callcheck: rand (ButterflyRand: ReiPan_Rand)
static int EffectButterflyUpdateTypeDefault(BUTTERFLY_DISP *bfly)
{
    sceVu0FMATRIX m;
    sceVu0FVECTOR rot;
    sceVu0FVECTOR pos;
    float vel_x, vel_y;
    float nudge_x, nudge_y;
    float vel_z;
    int   life;

    if (bfly->LifeTime < 1)
        return 0;

    if (bfly->MoveInterval < 1 && bfly->Velocity[2] != 0.0f)
    {
        nudge_x = (ButterflyRand() * 2.0f - 1.0f) * 0.5f;

        if (bfly->Position[1] > 200.0f)
            nudge_y = (ButterflyRand() - 1.0f) * 0.5f;
        else if (bfly->Position[1] >= 0.0f)
            nudge_y = (ButterflyRand() * 2.0f - 1.0f) * 0.5f;
        else
            nudge_y = ButterflyRand() * 0.5f;

        vel_x = bfly->Velocity[0];
        if (vel_x + nudge_x <= 0.7f && vel_x + nudge_x >= -0.7f)
            vel_x = vel_x + nudge_x;
        bfly->Velocity[0] = vel_x;

        vel_y = bfly->Velocity[1];
        if (vel_y + nudge_y <= 0.7f && vel_y + nudge_y >= -0.7f)
            vel_y = vel_y + nudge_y;

        bfly->MoveInterval = 10;
        bfly->Velocity[1] = vel_y * 0.02f;
        bfly->Velocity[0] = bfly->Velocity[0] * 0.02f;
    }

    // Face the way it moved last frame; FF3 wraps only the yaw.
    GetTrgtRotType2(bfly->OldPosition, bfly->Position, bfly->Rot, 2);
    while (bfly->Rot[1] < -3.1415927f)
        bfly->Rot[1] += 6.2831855f;
    while (bfly->Rot[1] >= 3.1415927f)
        bfly->Rot[1] -= 6.2831855f;

    // The velocity is in the butterfly's frame.
    sceVu0UnitMatrix(m);
    rot[0] = NormalizeAngle(bfly->Rot[0]);
    rot[1] = NormalizeAngle(bfly->Rot[1]);
    rot[2] = NormalizeAngle(bfly->Rot[2]);
    rot[3] = 0.0f;                      // the ROM leaves w unset; sceVu0RotMatrix reads xyz
    sceVu0RotMatrix(m, m, rot);
    m[3][0] += bfly->Position[0];       // FF3: vadd.xyz on row 3
    m[3][1] += bfly->Position[1];
    m[3][2] += bfly->Position[2];
    sceVu0ApplyMatrix(pos, m, bfly->Velocity);

    sceVu0CopyVector(bfly->OldPosition, bfly->Position);
    sceVu0CopyVector(bfly->Position, pos);

    if (bfly->Velocity[2] != 0.0f)
    {
        vel_z = bfly->Velocity[2] + (ButterflyRand() * 2.0f - 1.0f) / 5.0f;
        if (vel_z < 10.0f)
            vel_z = 8.0f;
        else if (vel_z > 12.0f)
            vel_z = 12.0f;
        bfly->Velocity[2] = vel_z * 0.02f;
    }

    life = bfly->LifeTime;
    if (life >= 418)
        bfly->AlphaRate = (float)(450 - life) * 0.03125f;
    else if (life <= 32)
        bfly->AlphaRate = (float)life * 0.03125f;
    else
        bfly->AlphaRate = 1.0f;

    bfly->LifeTime = life - 1;
    bfly->MoveInterval--;

    return 1;
}

// The guide's lock-on odds by distance (x 25 against the preset's
// NearDistance / FarDistance). The caller locks on when rand * interval < 1,
// so the near value 0 means it always locks on close up.
// FF3 0x00159bf8
static int EffectButterflyGetLockOnInterval(int type, float *pos, float *target)
{
    BUTTERFLY_TARGET_PARAMETER *param = EffectButterflyTargetParameterPtrGetPublic(type);
    int   near_dist = param->NearDistance;
    int   far_dist = param->FarDistance;
    float dist = Get2PDist3D(pos, target);

    if (dist * 25.0f < (float)near_dist)
        return param->LockOnIntervalNear;
    if (dist * 25.0f < (float)far_dist)
        return param->LockOnIntervalMiddle;
    return param->LockOnIntervalFar;
}

// The guide butterfly: every tenth frame a new world-space velocity toward
// the target (locked on or wobbled), then the facing, the move, and the
// fades: OutTime running drives the alpha down and ends the butterfly,
// InTime running drives it up. LifeTime never counts down here.
// FF3 0x00159c90
// callcheck: rand (ButterflyRand: ReiPan_Rand)
static int EffectButterflyUpdateTypeMoveToTarget(BUTTERFLY_DISP *bfly)
{
    BUTTERFLY_TARGET_PARAMETER *param = EffectButterflyTargetParameterPtrGetPublic(bfly->Type);
    float speed = (float)param->Speed / 2500.0f;
    float r;
    int   interval;
    int   cnt;

    if (bfly->LifeTime < 1)
        return 0;

    if (bfly->MoveInterval < 1)
    {
        r = ButterflyRand();
        interval = EffectButterflyGetLockOnInterval(bfly->Type, bfly->Position, bfly->Target);
        EffectButterflyGetVelocity(speed, bfly->Velocity, bfly->Position, bfly->Target,
                                   bfly->Velocity, bfly->Type, r * (float)interval < 1.0f);
        bfly->MoveInterval = 10;
    }

    GetTrgtRotType2(bfly->OldPosition, bfly->Position, bfly->Rot, 2);
    while (bfly->Rot[1] < -3.1415927f)
        bfly->Rot[1] += 6.2831855f;
    while (bfly->Rot[1] >= 3.1415927f)
        bfly->Rot[1] -= 6.2831855f;

    sceVu0CopyVector(bfly->OldPosition, bfly->Position);
    sceVu0AddVector(bfly->Position, bfly->Position, bfly->Velocity);   // FF3: vadd.xyzw

    if (bfly->OutTime != 0)
    {
        cnt = bfly->Count++;
        bfly->AlphaRate = 1.0f - (float)cnt / (float)bfly->OutTime;
        if (bfly->Count > bfly->OutTime)
        {
            bfly->LifeTime = 0;
            bfly->OutTime = 0;
            bfly->Count = 0;
        }
    }
    else if (bfly->InTime != 0)
    {
        cnt = bfly->Count++;
        bfly->AlphaRate = (float)cnt / (float)bfly->InTime;
        if (bfly->Count > bfly->InTime)
        {
            bfly->InTime = 0;
            bfly->Count = 0;
        }
    }
    else
    {
        bfly->AlphaRate = 1.0f;
    }

    bfly->MoveInterval--;

    return 1;
}

// A butterfly's own model (FF2 EffectInitAniCtrlButterflyOne): a
// GameMotionModel over the cached BUTTERFLY_MDL_PK4 / BUTTERFLY_MOT_PK4,
// built at once, playing wingbeat 0 or 1.
// FF3 0x00159f18
// callcheck: rand (ButterflyRand: ReiPan_Rand)
// callcheck: operator_new, GameMotionModelCtor (new GameMotionModel)
static void EffectButterflyMdlCreate(BUTTERFLY_DISP *bfly)
{
    GameMotionModel *mdl = new GameMotionModel();

    mdl->Load(BUTTERFLY_MDL_PK4, BUTTERFLY_MOT_PK4, -1);
    bfly->mdl = mdl;
    if (mdl != nullptr)
        mdl->IsLoaded();                // builds the motion object once the files are in

    // PC: guard -- the ROM calls through mot unchecked.
    if (bfly->mdl->mot != nullptr)
        bfly->mdl->mot->PlayMotion((int)(ButterflyRand() + 0.5f), 0, 0.0f, 1.0f);
}

// ──────────────────────────────────────────────────────────────────────
// Requests
// ──────────────────────────────────────────────────────────────────────

// An ambient butterfly at pos, flying off on a random forward velocity.
// FF3 0x00159ff0
// callcheck: rand (ButterflyRand: ReiPan_Rand)
void EffectButterflyReq(float *pos)
{
    BUTTERFLY_DISP bfly;
    sceVu0FMATRIX  m;
    sceVu0FVECTOR  next;
    float          rot_x, rot_y;
    SLL_CELL      *cell;

    memset(&bfly, 0, sizeof(bfly));     // FF3: 0x80
    sceVu0CopyVector(bfly.Position, pos);

    bfly.Velocity[0] = (ButterflyRand() * 2.0f - 1.0f) * 0.02f;
    bfly.Velocity[1] = (ButterflyRand() * 2.0f - 1.0f) * 0.02f;
    bfly.Velocity[2] = (ButterflyRand() * 5.0f + 10.0f) * 0.02f;
    bfly.Velocity[3] = 1.0f;

    Vector2Rot(bfly.Velocity, &rot_x, &rot_y);
    bfly.Rot[0] = 0.0f;
    bfly.Rot[1] = rot_y;
    bfly.Rot[2] = 0.0f;
    bfly.Rot[3] = 0.0f;

    // No rotation yet: the first step goes along the raw velocity.
    sceVu0UnitMatrix(m);
    m[3][0] += bfly.Position[0];        // FF3: vadd.xyz on row 3
    m[3][1] += bfly.Position[1];
    m[3][2] += bfly.Position[2];
    sceVu0ApplyMatrix(next, m, bfly.Velocity);

    sceVu0CopyVector(bfly.OldPosition, bfly.Position);
    sceVu0CopyVector(bfly.Position, next);

    bfly.AlphaRate = 0.0f;
    bfly.LifeTime = 450;
    bfly.mdl = nullptr;
    bfly.MoveType = 0;
    bfly.MoveInterval = 10;

    cell = SingleLinkListAddEnd(&ButterFlyDispCtrl.ButterflyList, &bfly);
    if (cell != nullptr)
        EffectButterflyMdlCreate((BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell));
}

// A guide butterfly id with preset type at pos, launched straight at target
// (the first velocity is not wobbled), fading in over the preset's InTime.
// FF3 0x0015a1f8 (EvSetButterfly)
void EffectButterflyReqTarget(int id, int type, float *pos, float *target)
{
    BUTTERFLY_TARGET_PARAMETER *param = EffectButterflyTargetParameterPtrGet(type);
    float          speed = (float)param->Speed / 2500.0f;
    BUTTERFLY_DISP bfly;
    sceVu0FVECTOR  dir;
    sceVu0FVECTOR  next;
    float          rot_x, rot_y;
    SLL_CELL      *cell;

    memset(&bfly, 0, sizeof(bfly));     // FF3: 0x80
    sceVu0CopyVector(bfly.Position, pos);
    sceVu0CopyVector(bfly.Target, target);

    sceVu0SubVector(dir, target, pos);
    EffectButterflyGetVelocity(speed, bfly.Velocity, pos, target, dir, type, 0);
    bfly.Velocity[3] = 1.0f;

    sceVu0AddVector(next, bfly.Position, bfly.Velocity);   // FF3: vadd.xyzw (w becomes 2)
    sceVu0CopyVector(bfly.OldPosition, bfly.Position);
    sceVu0CopyVector(bfly.Position, next);

    bfly.LifeTime = 450;
    bfly.MoveInterval = 10;
    bfly.InTime = param->InTime;
    bfly.MoveType = 1;
    bfly.Type = type;
    bfly.OutTime = 0;
    bfly.Count = 0;
    bfly.AlphaRate = 0.0f;
    bfly.mdl = nullptr;
    bfly.Id = id;

    Vector2Rot(bfly.Velocity, &rot_x, &rot_y);
    bfly.Rot[0] = 0.0f;
    bfly.Rot[1] = rot_y;
    bfly.Rot[2] = 0.0f;
    bfly.Rot[3] = 0.0f;

    cell = SingleLinkListAddEnd(&ButterFlyDispCtrl.ButterflyList, &bfly);
    if (cell != nullptr)
        EffectButterflyMdlCreate((BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell));
}

// Steer a guide: aim at the target, wobbled by a random pitch / yaw from
// the preset unless locked on (the pitch clamped so it never turns more
// than a quarter turn away, mirrored by the side the target is on); then,
// when the new heading's yaw differs from dir's by more than TurnMax, turn
// it to dir's yaw +- TurnMax instead. The result is normalised (xyz) and
// scaled by speed (xyzw).
// FF3 0x0015a398
// callcheck: rand (ButterflyRand: ReiPan_Rand)
static void EffectButterflyGetVelocity(float speed, float *velocity, float *pos, float *target,
                                       float *dir, int type, int lock_on)
{
    BUTTERFLY_TARGET_PARAMETER *param = EffectButterflyTargetParameterPtrGet(type);
    float         turn_max = ((float)param->TurnMax * 3.1415927f) / 180.0f;
    sceVu0FVECTOR wrk;
    sceVu0FVECTOR tgt_n;
    sceVu0FVECTOR tgt_y0;
    sceVu0FMATRIX m;
    float         rot_x, rot_y;
    float         dot, angle;
    float         vel_rx, vel_ry;
    float         dir_rx, dir_ry;
    float         diff;

    sceVu0SubVector(wrk, target, pos);  // FF3: vsub.xyzw

    if (lock_on == 0)
    {
        rot_x = (((float)param->BureUpDown * ButterflyRand() - (float)param->BureUpDown * 0.5f)
                 * 3.1415927f) / 180.0f;
        rot_y = (((float)param->BureLeftRight * ButterflyRand() - (float)param->BureLeftRight * 0.5f)
                 * 3.1415927f) / 180.0f;

        ButterflyNormalizeXYZ(tgt_n, wrk);
        tgt_y0[0] = wrk[0];             // FF3 flattens Y only (FF2 zeroed X too)
        tgt_y0[1] = 0.0f;
        tgt_y0[2] = wrk[2];
        tgt_y0[3] = wrk[3];
        ButterflyNormalizeXYZ(tgt_y0, tgt_y0);

        dot = tgt_n[0] * tgt_y0[0] + tgt_n[1] * tgt_y0[1] + tgt_n[2] * tgt_y0[2];
        if (dot < -1.0f || dot > 1.0f)
            dot = 0.0f;
        angle = Vu0Acosf(dot);

        if (tgt_n[0] < 0.0f)
        {
            if (rot_x + angle > 1.5707964f)
                rot_x = 1.5707964f - angle;
            else if (rot_x + angle < -1.5707964f)
                rot_x = -1.5707964f - angle;
        }
        else
        {
            if (rot_x + angle > 1.5707964f)
                rot_x = angle + 1.5707964f;
            else if (rot_x + angle < -1.5707964f)
                rot_x = angle + -1.5707964f;
        }

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
        sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
        sceVu0ApplyMatrix(wrk, m, wrk);
    }

    Vector2Rot(wrk, &vel_rx, &vel_ry);
    Vector2Rot(dir, &dir_rx, &dir_ry);

    if (fabsf(vel_ry - dir_ry) > turn_max)
    {
        if (vel_ry < 0.0f)
            vel_ry += 6.2831855f;
        if (dir_ry < 0.0f)
            dir_ry += 6.2831855f;

        diff = vel_ry - dir_ry;
        if (diff < -3.1415927f)
            diff += 6.2831855f;
        if (diff > 3.1415927f)
            diff -= 6.2831855f;

        if (diff < 0.0f || diff > 3.1415927f)
        {
            rot_y = (dir_ry - turn_max) - vel_ry;
            if (rot_y < 0.0f)
                rot_y += 6.2831855f;
            if (rot_y > 6.2831855f)
                rot_y -= 6.2831855f;
        }
        else
        {
            rot_y = (dir_ry + turn_max) - vel_ry;
            if (rot_y < 0.0f)
                rot_y += 6.2831855f;
            if (rot_y > 3.1415927f)
                rot_y -= 6.2831855f;
        }

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
        sceVu0ApplyMatrix(wrk, m, wrk);
    }

    ButterflyNormalizeXYZ(wrk, wrk);
    velocity[0] = wrk[0] * speed;       // FF3: vmulx.xyzw
    velocity[1] = wrk[1] * speed;
    velocity[2] = wrk[2] * speed;
    velocity[3] = wrk[3] * speed;
}

// ──────────────────────────────────────────────────────────────────────
// Removal and lookups
// ──────────────────────────────────────────────────────────────────────

// The butterfly's model goes with its cell.
// FF3 0x0015a8e0
static void ButterflyDelete(SINGLE_LINK_LIST *list, SLL_CELL *cell)
{
    BUTTERFLY_DISP *bfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell);

    if (bfly->mdl != nullptr)
        delete bfly->mdl;               // vtable +0x0c, the deleting destructor
    bfly->mdl = nullptr;
    SingleLinkListRemove(list, cell);
}

// Where the butterfly is drawn: the model's coordinate 1 (the body swings
// round the path point), for the glow, the trail and the player fade.
// FF3 0x0015a948
static void EffectGetButterflyPosition(float *pos, BUTTERFLY_DISP *bfly)
{
    bfly->mdl->GetCoordPos(pos, 1, (float *)butterfly_origin);
}

// FF3 0x0015a980 (EffectsReleaseAll, IgEffectButterflyAllCut)
void EffectButterflyAllCut(void)
{
    SLL_CELL *cell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList);
    SLL_CELL *next;

    while (cell != nullptr)
    {
        next = cell->pNext;
        ButterflyDelete(&ButterFlyDispCtrl.ButterflyList, cell);
        cell = next;
    }
}

// The guide butterfly id (ambient ones never match: MoveType 1 and Id).
// FF3 0x0015a9e8
static BUTTERFLY_DISP *EffectButterflyGetDispPtr(int id)
{
    SLL_CELL       *cell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList);
    BUTTERFLY_DISP *bfly;

    while (cell != nullptr)
    {
        SLL_CELL *next = cell->pNext;

        bfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell);
        if (bfly->MoveType == 1 && bfly->Id == id)
            return bfly;
        cell = next;
    }
    return nullptr;
}

// FF3 0x0015aa78 (EvReleaseButterfly)
void EffectButterflyFadeOut(int id)
{
    BUTTERFLY_DISP *bfly = EffectButterflyGetDispPtr(id);

    if (bfly != nullptr)
        bfly->OutTime = EffectButterflyTargetParameterPtrGet(bfly->Type)->OutTime;
}

// FF3 0x0015aab8
void EffectButterflyAllFadeOut(void)
{
    SLL_CELL       *cell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList);
    BUTTERFLY_DISP *bfly;

    while (cell != nullptr)
    {
        SLL_CELL *next = cell->pNext;

        bfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell);
        if (bfly->MoveType == 1)
            bfly->OutTime = EffectButterflyTargetParameterPtrGet(bfly->Type)->OutTime;
        cell = next;
    }
}

// FF3 0x0015ab38 (EvMoveButterfly)
void EffectButterflyChangeTarget(int id, float *target)
{
    BUTTERFLY_DISP *bfly = EffectButterflyGetDispPtr(id);

    if (bfly != nullptr)
        sceVu0CopyVector(bfly->Target, target);
}

// FF3 0x0015ab70
void EffectButterflyChangeType(int id, int type)
{
    BUTTERFLY_DISP *bfly = EffectButterflyGetDispPtr(id);

    if (bfly != nullptr)
        bfly->Type = type;
}

// FF3 0x0015aba0
void EffectButterflyAllChangeTarget(float *target)
{
    SLL_CELL       *cell = SingleLinkListBeginCell(&ButterFlyDispCtrl.ButterflyList);
    BUTTERFLY_DISP *bfly;

    while (cell != nullptr)
    {
        SLL_CELL *next = cell->pNext;

        bfly = (BUTTERFLY_DISP *)SingleLinkListCellBodyPtr(cell);
        if (bfly->MoveType == 1)
            sceVu0CopyVector(bfly->Target, target);
        cell = next;
    }
}

// Every butterfly, ambient ones included.
// FF3 0x0015ac20 (IgEffectButterflyReq caps at 10)
int EffectButterflyNumGet(void)
{
    return (int)ButterFlyDispCtrl.ButterflyList.RegCount;
}

// The preset, type > 4 (unsigned) read as 0.
// FF3 0x0015ac30
static BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGet(int type)
{
    if ((u_int)type > 4)
        type = 0;
    return ButterflyTargetParamTbl[type];
}

// The same without the clamp in FF3 (FF2 clamped both).
// FF3 0x0015ac50
BUTTERFLY_TARGET_PARAMETER *EffectButterflyTargetParameterPtrGetPublic(int type)
{
    return ButterflyTargetParamTbl[type];
}

// ──────────────────────────────────────────────────────────────────────
// The trail motes
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0015ac68 (InitEffects)
void EffectButterflyParticleInit(void)
{
    SingleLinkListInit(&ButterFlyParticleCtrl.ParticleList, sizeof(BUTTERFLY_PARTICLE));     // 0x40
}

// FF3 0x0015ac88
void EffectButterflyParticleMain(void)
{
    SLL_CELL           *cell;
    SLL_CELL           *next;
    BUTTERFLY_PARTICLE *part;
    GRA3DCAMERA        *cam;
    float               rot_x, rot_y;

    cell = SingleLinkListBeginCell(&ButterFlyParticleCtrl.ParticleList);

    cam = gra3dGetCamera();
    Get2PosRot(gra3dcamGetPosition(), cam->vTarget, &rot_x, &rot_y);

    while (cell != nullptr)
    {
        part = (BUTTERFLY_PARTICLE *)SingleLinkListCellBodyPtr(cell);
        next = cell->pNext;

        ButterflyParticleOneDraw(rot_x, rot_y, part);
        if (ButterflyParticleUpdate(part) == 0)
            SingleLinkListRemove(&ButterFlyParticleCtrl.ParticleList, cell);

        cell = next;
    }
}

// One mote: 30 frames, alpha from the butterfly's (FF3's AlphaScale).
// FF3 0x0015ad68
static void EffectButterflyParticleReq(float alpha, SINGLE_LINK_LIST *list, float *pos,
                                       float *velocity, float *accel)
{
    BUTTERFLY_PARTICLE part;

    sceVu0CopyVector(part.Position, pos);
    sceVu0CopyVector(part.Velocity, velocity);
    sceVu0CopyVector(part.Acceleration, accel);
    part.LifeTime = 30;
    part.AlphaRate = 1.0f;
    part.AlphaScale = alpha;
    part._3c = 0;                       // PC: the ROM copies its stack word unset

    SingleLinkListAddEnd(list, &part);
}

// Two to four motes off a butterfly, each thrown along its facing turned by
// a random pitch (rand * 180 + 350 - 90 degrees) and yaw (rand * 180 + 180
// - 90), at 0.04..0.2 a frame (FF2's 1..5 / 25), sinking at 0.004.
// FF3 0x0015adc8
// callcheck: rand (ButterflyRand: ReiPan_Rand)
static void EffectButterflyParticleReqAdjustParam(float alpha, SINGLE_LINK_LIST *list,
                                                  float *pos, float *rot)
{
    sceVu0FVECTOR accel = { 0.0f, 0.004f, 0.0f, 0.0f };
    sceVu0FVECTOR dir;
    sceVu0FVECTOR velocity;
    sceVu0FMATRIX m;
    float bfly_rx = rot[0];
    float bfly_ry = rot[1];
    float rx, ry;
    int   num;
    int   i;

    num = (int)(ButterflyRand() * 3.0f + 0.5f) + 2;

    for (i = 0; i < num; i++)
    {
        dir[0] = 0.0f;
        dir[1] = 0.0f;
        dir[2] = 1.0f;
        dir[3] = 1.0f;
        dir[2] *= (ButterflyRand() * 40.0f) / 250.0f + 0.04f;

        rx = bfly_rx + (((ButterflyRand() * 180.0f + 350.0f) - 90.0f) * 3.1415927f) / 180.0f;
        ry = bfly_ry + (((ButterflyRand() * 180.0f + 180.0f) - 90.0f) * 3.1415927f) / 180.0f;

        if (rx < -3.1415927f)
            rx += 6.2831855f;
        else if (rx > 3.1415927f)
            rx -= 6.2831855f;
        if (ry < -3.1415927f)
            ry += 6.2831855f;
        else if (ry > 3.1415927f)
            ry -= 6.2831855f;

        sceVu0UnitMatrix(m);
        sceVu0RotMatrixX(m, m, NormalizeAngle(rx));
        sceVu0RotMatrixY(m, m, NormalizeAngle(ry));
        sceVu0ApplyMatrix(velocity, m, dir);

        EffectButterflyParticleReq(alpha, list, pos, velocity, accel);
    }
}

// A 0.128 x 0.128 billboard of effdat[0x54] facing the camera (FF2 3.2 / 25).
// FF3 0x0015b138
static void ButterflyParticleOneDraw(float rot_x, float rot_y, BUTTERFLY_PARTICLE *part)
{
    sceVu0FMATRIX m;

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    m[3][0] += part->Position[0];       // FF3: vadd.xyz on row 3
    m[3][1] += part->Position[1];
    m[3][2] += part->Position[2];

    Set3DPosTexure2(0.128f, 0.128f, m, ButterflyParticleRegs, BUTTERFLY_FREA_TEX,
                    0x80, 0x80, 0x80, (u_char)((int)(part->AlphaRate * 128.0f) & 0xff), 0);
}

// FF3 0x0015b230
static int ButterflyParticleUpdate(BUTTERFLY_PARTICLE *part)
{
    if (part->LifeTime < 1)
        return 0;

    sceVu0AddVector(part->Position, part->Position, part->Velocity);       // FF3: vadd.xyzw
    sceVu0AddVector(part->Velocity, part->Velocity, part->Acceleration);
    part->LifeTime--;
    part->Position[3] = 1.0f;
    part->AlphaRate = (part->AlphaScale * (float)part->LifeTime) / 30.0f;
    return 1;
}

// FF3 0x0015b2b8 (EffectsReleaseAll, IgEffectButterflyAllCut)
void EffectButterflyParticleAllCut(void)
{
    SLL_CELL *cell = SingleLinkListBeginCell(&ButterFlyParticleCtrl.ParticleList);
    SLL_CELL *next;

    while (cell != nullptr)
    {
        next = cell->pNext;
        SingleLinkListRemove(&ButterFlyParticleCtrl.ParticleList, cell);
        cell = next;
    }
}
