/* ==========================================================================
 *  ingame/photo/photo.c
 *
 *  Phase 45, the peeping view (see photo.h). FF3 0x226138..0x2274e0, FF3-only
 *  code in the link-order slot of FF2's photo.c (FF2's FurnPhotoFlg* sat
 *  here; FF3's flag is the peeping state).
 *
 *  The cached getters of the sdata (GPhaseCtrl gp-0xca4, OptionWrk gp-0xaec)
 *  are shared with other files; nothing depends on the cache, so calls go to
 *  the getters directly. The atexit thunk that ends the file,
 *  PeepGPhaseCtrlStaticDtor 0x2274c0 = GPhaseCtrlTerm(0x41e468), is not
 *  written: the host's static destroys itself.
 * ======================================================================== */
#include "ingame/photo/photo.h"

#include <math.h>                       // fabsf, sqrtf
#include <stdlib.h>                     // abs

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer
#include "common/vu0math.h"             // NormalizeAngle
#include "graphics/effect/effect.h"     // SetEffects, EffectRelease
#include "graphics/effect/effect_sub.h" // Vector2Rot
#include "graphics/graph2d/fade.h"
#include "graphics/graph3d/gra3d.h"     // gra3dcam*, gra3dApplyCamera
#include "ingame/ingame.h"              // IngameMain, IngameDrawSub
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/photo/finder_photo.h"  // GetPhotoModeWrk
#include "ingame/plyr/player.h"         // PlyrSetPos, PlyrSetRot, SetPlyrAnime, PlyrClear234
#include "ingame/plyr/plyr_mdl.h"       // PlyrIsFinderModeOff, PlyrFinderModeEnd
#include "ingame/plyr/plyr_sub.h"       // PlyrTurnFlagClear
#include "ingame/plyr/unit_ctl.h"       // GetStickRaw
#include "ingame/story_menu.h"          // LockTblInc, LockTblDec, IsLockTblOn
#include "main/gphase.h"                // GetGPhaseCtrl
#include "outgame/option.h"             // GetOptionWrk
#include "system/pad/pad_ctrl.h"        // CheckPadTrgPlaySe, pad_key
#include "system/pad/vib_pulse.h"       // VibPulseReqDist, VibPulseClear, VibPulseMain
#include "sdk/libvu0.h"
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

#define PEEP_LOOK_SPEED     0.10471975f         // sdata 0x3717f4: 6 degrees (/ look_dist)
#define PEEP_LOOK_SLOW      0.7f                // sdata 0x3717f8: slows over the last 30% of the limit
#define PEEP_SLIDE_FROM     0.5f                // sdata 0x3717fc: slides past half of the limit
#define PEEP_SLIDE_SPEED    0.01f               // sdata 0x371800
#define PEEP_STICK_DEAD     50.0f               // sdata 0x371810
#define PEEP_STICK_SLIDE    0.05f               // sdata 0x371814: the second stick's slide step

// The four peeping spots (data 0x32ded0).
static const PEEP_DAT peep_dat[PEEP_DAT_NUM] =
{
    { { -110.0f, -85.0f,  367.0f }, 90.0f, { -110.0f, -116.0f,  365.0f }, 10.0f, {  233.0f, -104.0f,  372.0f }, 1.0f, 0.7f, 15.0f, 20.0f, 0.8f },
    { { -138.0f, -94.0f,  819.0f },  0.0f, { -139.0f, -103.0f,  834.0f }, 10.0f, { -139.0f,  -55.0f,  835.0f }, 1.0f, 0.7f, 20.0f, 20.0f, 0.8f },
    { {   10.0f, -30.0f, 1201.0f },  0.0f, {   10.0f,  -62.0f, 1206.0f }, 10.0f, {   13.0f,  -50.0f, 1273.0f }, 0.0f, 0.0f, 18.5f, 10.0f, 0.8f },
    { {  326.0f, -67.0f,  560.0f },  0.0f, {  325.0f,  -77.0f,  571.0f }, 10.0f, {  325.0f,  -26.0f,  588.0f }, 2.0f, 2.0f, 20.0f, 20.0f, 0.8f },
};

// The two points PeepPulseStart passes first (data 0x32deb0 / 0x32dec0).
static sceVu0FVECTOR peep_pulse_p0 = { 0.0f, 0.0f,    0.0f, 0.0f };
static sceVu0FVECTOR peep_pulse_p1 = { 0.0f, 0.0f, 1200.0f, 0.0f };

static PEEP_WRK   peep_wrk;             // bss 0x3f6560
static PEEP_VIEW  peep_view;            // bss 0x3f6610
static PEEP_VIEW *peep_cur;             // sdata 0x3717f0 (gp-0x1d00): the selected record

static int    peep_state;               // sbss 0x373798 (gp+0x2a8): PEEP_STATE
static s_char peep_step;                // sbss 0x3737a0 (gp+0x2b0): one_Story_Peeping's step 0..7
static char   peep_help_shown;          // sbss 0x3737a1 (gp+0x2b1): the fade layer's label is up
static int    peep_alpha;               // sbss 0x3737a4 (gp+0x2b4): fade alpha mirrored into the fade layer
static void  *peep_eff;                 // sbss 0x3737a8 (gp+0x2b8): effect 7 while the view is on

// *v moves up by step, but not past lim (it snaps to lim when it would).
// FF3 0x00226138.
static void PeepStepUp(float lim, float step, float *v)
{
    if (lim <= *v + step)
    {
        *v = lim;
        return;
    }
    *v = *v + step;
}

// *v moves down by step, but not past lim. FF3 0x00226160.
static void PeepStepDown(float lim, float step, float *v)
{
    if (*v - step <= lim)
    {
        *v = lim;
        return;
    }
    *v = *v - step;
}

// The turn step toward lim: 6 degrees / look_dist, slowing linearly to 0
// over the last 30% when cur already lies on lim's side.
// FF3 0x00226188.
static float PeepLookSpeed(float cur, float lim, float dist)
{
    float a = fabsf(cur);
    float b = fabsf(lim);
    float r = PEEP_LOOK_SPEED / dist;

    if (cur * lim >= 0.0f && a >= b * PEEP_LOOK_SLOW)
        r = (r * (b - a)) / (b - b * PEEP_LOOK_SLOW);
    return r;
}

// The slide step: 0.01 once cur is past half of lim on lim's side.
// FF3 0x00226240.
static float PeepSlideSpeed(float cur, float lim)
{
    float a = fabsf(cur);
    float b = fabsf(lim);

    if (cur * lim < 0.0f)
        return 0.0f;
    if (a < b * PEEP_SLIDE_FROM)
        return 0.0f;
    return PEEP_SLIDE_SPEED;
}

// Non-zero when the step is at least 0.001. FF3 0x002262c8.
static int PeepSpeedIsMoving(float speed)
{
    return (int)(speed * 1000.0f);      // sdata 0x371804
}

// Copies record no into the view work (degrees -> radians). FF3 0x00226cd0.
static PEEP_VIEW *PeepDatLoad(int no)
{
    const PEEP_DAT *d = &peep_dat[no];

    // Degrees -> radians as the ROM does it: * pi (sdata 0x371820), / 180 (0x371824).

    peep_view.plyr_pos[0]   = d->plyr_pos[0];
    peep_view.plyr_pos[1]   = d->plyr_pos[1];
    peep_view.plyr_pos[2]   = d->plyr_pos[2];
    peep_view.plyr_pos[3]   = 1.0f;                // sdata 0x371828
    peep_view.plyr_rot[0]   = 0.0f;
    peep_view.plyr_rot[1]   = (d->plyr_rot_y * 3.1415927f) / 180.0f;
    peep_view.plyr_rot[2]   = 0.0f;
    peep_view.plyr_rot[3]   = 0.0f;
    peep_view.cam_pos[0]    = d->cam_pos[0];
    peep_view.cam_pos[1]    = d->cam_pos[1];
    peep_view.cam_pos[2]    = d->cam_pos[2];
    peep_view.cam_pos[3]    = 0.0f;
    peep_view.cam_target[0] = d->cam_target[0];
    peep_view.cam_target[1] = d->cam_target[1];
    peep_view.cam_target[2] = d->cam_target[2];
    peep_view.cam_target[3] = 0.0f;
    peep_view.fov           = d->fov;
    peep_view.look_dist     = d->look_dist;
    peep_view.slide_y_max   = d->slide_y_max;
    peep_view.slide_x_max   = d->slide_x_max;
    peep_view.look_x_max    = (d->look_x_max * 3.1415927f) / 180.0f;
    peep_view.look_y_max    = (d->look_y_max * 3.1415927f) / 180.0f;
    return &peep_view;
}

// FF3 0x002262e8.
static void PeepDatSelect(int no)
{
    if (no >= 0)
        peep_cur = PeepDatLoad(no);
}

// The pivot (cam_pos moved look_dist toward the target, w 1), the view's
// rotation (Vector2Rot of target - pos) and the up vector it turns.
// FF3 0x00226310.
static void PeepCalcPivot(float *pivot, float *rot, float *up)
{
    PEEP_VIEW    *v = peep_cur;
    sceVu0FVECTOR dir;
    sceVu0FVECTOR ang;
    sceVu0FMATRIX unit;
    sceVu0FMATRIX m;
    float         len;

    sceVu0SubVector(pivot, v->cam_target, v->cam_pos);
    len = sqrtf((v->cam_target[0] - v->cam_pos[0]) * (v->cam_target[0] - v->cam_pos[0])
              + (v->cam_target[1] - v->cam_pos[1]) * (v->cam_target[1] - v->cam_pos[1])
              + (v->cam_target[2] - v->cam_pos[2]) * (v->cam_target[2] - v->cam_pos[2]));
    sceVu0ScaleVector(pivot, pivot, Ps2Divf(v->look_dist, len));   // PC: PS2 division
    sceVu0AddVector(pivot, pivot, v->cam_pos);
    pivot[3] = 1.0f;                    // sdata 0x371808

    sceVu0SubVector(dir, v->cam_target, v->cam_pos);
    Vector2Rot(dir, &rot[0], &rot[1]);
    rot[2] = 0.0f;
    rot[3] = 0.0f;

    ang[0] = NormalizeAngle(rot[0]);
    ang[1] = NormalizeAngle(rot[1]);
    ang[2] = NormalizeAngle(rot[2]);
    ang[3] = 0.0f;
    sceVu0UnitMatrix(unit);             // the ROM rotates the unit matrix at data 0x3293c0
    sceVu0RotMatrix(m, unit, ang);

    up[0] = 0.0f;                       // data 0x329450 (0, 1, 0, 0), y negated
    up[1] = -1.0f;
    up[2] = 0.0f;
    up[3] = 0.0f;
    sceVu0ApplyMatrix(up, m, up);
}

// Onto the peeping camera: the game camera is saved, the record's camera
// applied, the local eye / target, pivot, rotation and up built, the look
// cleared, and Rei put at the record's spot. FF3 0x00226478.
static void PeepViewStart(void)
{
    PEEP_VIEW *v = peep_cur;
    float     *pos;
    float     *target;
    float      len;

    if (v == nullptr)                   // PC: no record selected (the ROM reads EE address 0)
        return;

    pos    = gra3dcamGetPosition();
    target = gra3dcamGetTarget();
    sceVu0CopyVector(peep_wrk.save_pos, pos);
    sceVu0CopyVector(peep_wrk.save_target, target);
    peep_wrk.save_fov = gra3dcamGetFov();

    gra3dcamSetPosition(v->cam_pos);
    gra3dcamSetTarget(v->cam_target, 1);
    gra3dcamSetFov(v->fov);
    gra3dApplyCamera(nullptr, 1, 1);

    len = sqrtf((v->cam_target[0] - v->cam_pos[0]) * (v->cam_target[0] - v->cam_pos[0])
              + (v->cam_target[1] - v->cam_pos[1]) * (v->cam_target[1] - v->cam_pos[1])
              + (v->cam_target[2] - v->cam_pos[2]) * (v->cam_target[2] - v->cam_pos[2]));
    peep_wrk.eye[0]    = 0.0f;
    peep_wrk.eye[1]    = 0.0f;
    peep_wrk.eye[2]    = v->look_dist;
    peep_wrk.eye[3]    = 1.0f;          // sdata 0x37180c
    peep_wrk.target[0] = 0.0f;
    peep_wrk.target[1] = 0.0f;
    peep_wrk.target[2] = v->look_dist - len;
    peep_wrk.target[3] = 1.0f;

    PeepCalcPivot(peep_wrk.pivot, peep_wrk.rot, peep_wrk.up);
    peep_wrk.rot[1] = -peep_wrk.rot[1];

    sceVu0SubVector(peep_wrk.look, peep_wrk.look, peep_wrk.look);
    sceVu0SubVector(peep_wrk.slide, peep_wrk.slide, peep_wrk.slide);
    sceVu0SubVector(peep_wrk.push, peep_wrk.push, peep_wrk.push);

    PlyrSetPos(v->plyr_pos);
    PlyrSetRot(v->plyr_rot);
}

// The look while peeping (PEEP_STATE_LOOK; PlyrNormalCtrl / PlyrFinderCtrl).
// The look stick turns the view toward its pitch / yaw limits (slower near
// them) and, past half of a limit, also slides it up to the slide limit; the
// second stick slides it directly. FF3 0x002265f0.
void PeepLookCtrl(void)
{
    PEEP_VIEW *v = peep_cur;
    u_char     lx, ly, rx, ry;
    int        look_y, look_x, slide_x, slide_y;
    float      sp;

    if (IsLockTblOn(3))
        return;
    if (v == nullptr)                   // PC: no record selected
        return;

    GetStickRaw(5, &lx, &ly);
    GetStickRaw(6, &rx, &ry);
    look_y  = (s_char)(ly - 0x80);
    look_x  = (s_char)(lx - 0x80);
    slide_x = (s_char)(rx - 0x80);
    slide_y = (s_char)(ry - 0x80);

    // Up / down: the pitch (look.x) and the vertical push.
    if ((float)abs(look_y) > PEEP_STICK_DEAD)
    {
        if (look_y < 1)
        {
            sp = PeepLookSpeed(peep_wrk.look[0], v->look_x_max, v->look_dist);
            PeepStepUp(v->look_x_max, sp, &peep_wrk.look[0]);
            if (PeepSpeedIsMoving(sp))
            {
                sp = PeepSlideSpeed(peep_wrk.look[0], v->look_x_max);
                PeepStepDown(-v->slide_y_max, sp, &peep_wrk.push[1]);
                PeepStepDown(-v->slide_y_max - peep_wrk.push[1], 0.0f, &peep_wrk.slide[1]);
            }
        }
        else
        {
            sp = PeepLookSpeed(peep_wrk.look[0], -v->look_x_max, v->look_dist);
            PeepStepDown(-v->look_x_max, sp, &peep_wrk.look[0]);
            if (PeepSpeedIsMoving(sp))
            {
                sp = PeepSlideSpeed(peep_wrk.look[0], -v->look_x_max);
                PeepStepUp(v->slide_y_max, sp, &peep_wrk.push[1]);
                PeepStepUp(v->slide_y_max - peep_wrk.push[1], 0.0f, &peep_wrk.slide[1]);
            }
        }
    }

    // Left / right: the yaw (look.y) and the horizontal push.
    if ((float)abs(look_x) > PEEP_STICK_DEAD)
    {
        if (look_x < 1)
        {
            sp = PeepLookSpeed(peep_wrk.look[1], v->look_y_max, v->look_dist);
            PeepStepUp(v->look_y_max, sp, &peep_wrk.look[1]);
            if (PeepSpeedIsMoving(sp))
            {
                sp = PeepSlideSpeed(peep_wrk.look[1], v->look_y_max);
                PeepStepUp(v->slide_x_max, sp, &peep_wrk.push[0]);
                PeepStepUp(v->slide_x_max - peep_wrk.push[0], 0.0f, &peep_wrk.slide[0]);
            }
        }
        else
        {
            sp = PeepLookSpeed(peep_wrk.look[1], -v->look_y_max, v->look_dist);
            PeepStepDown(-v->look_y_max, sp, &peep_wrk.look[1]);
            if (PeepSpeedIsMoving(sp))
            {
                sp = PeepSlideSpeed(peep_wrk.look[1], -v->look_y_max);
                PeepStepDown(-v->slide_x_max, sp, &peep_wrk.push[0]);
                PeepStepDown(-v->slide_x_max - peep_wrk.push[0], 0.0f, &peep_wrk.slide[0]);
            }
        }
    }

    // The second stick slides within what the push leaves.
    if ((float)abs(slide_y) > PEEP_STICK_DEAD)
    {
        if (slide_y < 0)
            PeepStepUp(v->slide_y_max - peep_wrk.push[1], PEEP_STICK_SLIDE, &peep_wrk.slide[1]);
        else
            PeepStepDown(-v->slide_y_max - peep_wrk.push[1], PEEP_STICK_SLIDE, &peep_wrk.slide[1]);
    }
    if ((float)abs(slide_x) > PEEP_STICK_DEAD)
    {
        if (slide_x < 0)
            PeepStepDown(-v->slide_x_max - peep_wrk.push[0], PEEP_STICK_SLIDE, &peep_wrk.slide[0]);
        else
            PeepStepUp(v->slide_x_max - peep_wrk.push[0], PEEP_STICK_SLIDE, &peep_wrk.slide[0]);
    }
}

// The peeping camera, each frame while PLYR_WRK._0e0 bit 0 is set (CameraMain,
// FinderViewCameraCtrl): (RotMatrix(rot) + pivot) * (RotMatrix(look) + push
// + slide) applied to the local eye and target, with the view's up.
// FF3 0x002269c8.
void CameraSetFromRotPair(void)
{
    sceVu0FMATRIX unit;
    sceVu0FMATRIX look;
    sceVu0FMATRIX view;
    sceVu0FVECTOR ang;
    sceVu0FVECTOR eye;
    sceVu0FVECTOR target;

    sceVu0UnitMatrix(unit);             // data 0x3293c0

    ang[0] = NormalizeAngle(peep_wrk.look[0]);
    ang[1] = NormalizeAngle(peep_wrk.look[1]);
    ang[2] = NormalizeAngle(peep_wrk.look[2]);
    ang[3] = 0.0f;
    sceVu0RotMatrix(look, unit, ang);
    look[3][0] += peep_wrk.push[0];     // vadd.xyz: w kept
    look[3][1] += peep_wrk.push[1];
    look[3][2] += peep_wrk.push[2];
    look[3][0] += peep_wrk.slide[0];
    look[3][1] += peep_wrk.slide[1];
    look[3][2] += peep_wrk.slide[2];

    ang[0] = NormalizeAngle(peep_wrk.rot[0]);
    ang[1] = NormalizeAngle(peep_wrk.rot[1]);
    ang[2] = NormalizeAngle(peep_wrk.rot[2]);
    sceVu0RotMatrix(view, unit, ang);
    view[3][0] += peep_wrk.pivot[0];
    view[3][1] += peep_wrk.pivot[1];
    view[3][2] += peep_wrk.pivot[2];

    sceVu0MulMatrix(look, view, look);
    sceVu0ApplyMatrix(eye, look, peep_wrk.eye);
    sceVu0ApplyMatrix(target, look, peep_wrk.target);

    gra3dcamSetPosition(eye);
    gra3dcamSetTargetUp(target, peep_wrk.up);
    gra3dApplyCamera(nullptr, 0, 0);
}

// Back onto the camera PeepViewStart saved. FF3 0x00226c00.
static void PeepViewEnd(void)
{
    gra3dcamSetPosition(peep_wrk.save_pos);
    gra3dcamSetTarget(peep_wrk.save_target, 1);
    gra3dcamSetFov(peep_wrk.save_fov);
    gra3dApplyCamera(nullptr, 1, 1);
}

// The pulse while peeping. FF3 sets it by the two data points, then again
// without them; the second request wins (a fixed beat). FF3 0x00226c50.
static void PeepPulseStart(void)
{
    VibPulseReqDist(1200.0f, 120.0f, peep_pulse_p0, peep_pulse_p1);    // sdata 0x371818 / 0x37181c
    VibPulseReqDist(1200.0f, 120.0f, nullptr, nullptr);
}

// FF3 0x00226cb8.
static void PeepPulseStop(void)
{
    VibPulseClear();
}

// FF3 0x00226df0.
void PeepStateClear(void)
{
    peep_state = PEEP_STATE_NONE;
}

// FF3 0x00226df8.
void PeepStateSet(int state)
{
    peep_state = state;
}

// FF3 0x00226e00.
int PeepStateGet(void)
{
    return peep_state;
}

// FF3 0x00226e08.
int PeepStateIsStart(void)
{
    return peep_state == PEEP_STATE_START;
}

// FF3 0x00226e18.
int PeepStateIsView(void)
{
    return (u_int)(peep_state - PEEP_STATE_IN) < 3;
}

// FF3 0x00226e28.
int PeepStateIsLook(void)
{
    return peep_state == PEEP_STATE_LOOK;
}

// FF3 0x00226e68.
static void PeepPhaseRequest(void)
{
    peep_step  = 0;
    peep_eff   = nullptr;
    peep_alpha = 0;
    VibPulseClear();
    GetGPhaseCtrl()->SetNext(0x2d, 0);
}

// PEEPING_REQUEST: record no (negative keeps the last one), then phase 45.
// FF3 0x00226e38.
int PeepingRequest(int no)
{
    peep_state = PEEP_STATE_NONE;
    PeepDatSelect(no);
    PeepPhaseRequest();
    return 1;
}

// FF3 0x00226eb0.
static void PeepFadeLayerReset(void)
{
    GetCmnFadeLayer()->base_alpha = peep_alpha;
    peep_help_shown = 0;
}

// Each frame: the fade layer runs; the first frame shows its label for the
// button layout (0x10 for layouts 0 / 1, 0x18 for 2); its base alpha follows
// the screen fade, 0 while the player is in a finder hold. FF3 0x00226ed8.
static int PeepFadeLayerUpdate(void)
{
    s_char cfg;

    GetCmnFadeLayer()->Update();
    if (peep_help_shown == 0)
    {
        peep_help_shown = 1;
        GetCmnFadeLayer()->base_alpha = peep_alpha;
        cfg = GetOptionWrk()->now.pad_cfg;
        if (cfg >= 0)
        {
            if (cfg < 2)
                GetCmnFadeLayer()->Show(0x10);
            else if (cfg == 2)
                GetCmnFadeLayer()->Show(0x18);
        }
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_HOLD) == 0)
        GetCmnFadeLayer()->base_alpha = peep_alpha;
    else
        GetCmnFadeLayer()->base_alpha = 0;
    return 1;
}

// FF3 0x00226fc8.
static void PeepFadeLayerTerm(void)
{
    GetCmnFadeLayer()->base_alpha = 0;
    GetCmnFadeLayer()->Term();
}

// FF3 0x00226ff0.
int init_Story_Peeping(void)
{
    PlyrClear234();
    PlyrTurnFlagClear();
    PeepFadeLayerReset();
    GetPhotoModeWrk()->SetViewFade(8);
    if (GetGPhaseCtrl()->from == 0x20)  // from Story_Normal
    {
        PeepStateSet(PEEP_STATE_START);
        LockTblInc(3);
        LockTblInc(9);
        LockTblInc(0x17);
    }
    return 1;
}

// Steps (peep_step): 0 fade out; 1 when black, onto the peeping camera
// (PeepViewStart, the pulse, effect 7) and fade in; 2 when in (or the player
// left mode 0), looking; 3 until pad_key[OK] (then 4) or a player mode (then
// 7 at once); 4/5 fade out, back on the game camera, fade in; 6 until in;
// 7 back to Story_Normal. FF3 0x00227080.
int one_Story_Peeping(void)
{
    PLYR_WRK *plyr;

    if (PeepFadeLayerUpdate() == 0)
        return 0;

    VibPulseMain();
    if (peep_step == 0)
    {
        FadeOutReq(0, 0, 0, 8);
        peep_step = 1;
    }

    if (IngameMain(0) != 0)
        return 1;

    if (peep_step == 1 && GetFadeState() != 0)
    {
        plyr = ingame_wrk.plyr_wrk;
        if ((plyr->flags & PLYR_FLG_HOLD) == 0 && plyr->mode != PLYR_MODE_10)
            SetPlyrAnime(0, 0xff, 0xff, 0xff, 1, 0.0f);
        PeepViewStart();
        PeepPulseStart();
        FadeInReq(0, 0, 0, 8);
        ingame_wrk.plyr_wrk->_0e0 |= 1;
        peep_eff = SetEffects(7, 2, 0x40, 0xa0);
        PeepStateSet(PEEP_STATE_IN);
        peep_step = 2;
    }

    if (peep_step == 2)
    {
        peep_alpha = GetFadeAlpha(1);
        GetCmnFadeLayer()->base_alpha = peep_alpha;
        if (GetFadeState() != 0 || ingame_wrk.plyr_wrk->mode != PLYR_MODE_NORMAL)
        {
            PeepStateSet(PEEP_STATE_LOOK);
            peep_step = 3;
            LockTblDec(3);
        }
    }

    if (peep_step == 3 && !IsLockTblOn(3))
    {
        ReiPan_MenuMouseConfirm();      // PC: a click steps back from the peephole
        if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0))
        {
            if (!PlyrIsFinderModeOff())
                PlyrFinderModeEnd();
            PeepStateSet(PEEP_STATE_OUT);
            peep_step = 4;
            LockTblInc(3);
        }
        else if (ingame_wrk.plyr_wrk->mode != PLYR_MODE_NORMAL)
        {
            // Something took the player (damage, an event): out at once.
            FadeInReq(0, 0, 0, 0);
            ingame_wrk.plyr_wrk->_0e0 &= ~1;
            if (peep_eff != nullptr)
            {
                EffectRelease(peep_eff);
                peep_eff = nullptr;
            }
            peep_step = 7;
            LockTblInc(3);
        }
    }

    if (peep_step == 4)
    {
        FadeOutReq(0, 0, 0, 8);
        peep_step = 5;
    }

    if (peep_step == 5)
    {
        peep_alpha = GetFadeAlpha(1);
        GetCmnFadeLayer()->base_alpha = peep_alpha;
        if (GetFadeState() != 0)
        {
            PeepViewEnd();
            PeepPulseStop();
            FadeInReq(0, 0, 0, 8);
            ingame_wrk.plyr_wrk->_0e0 &= ~1;
            if (peep_eff != nullptr)
            {
                EffectRelease(peep_eff);
                peep_eff = nullptr;
            }
            PeepStateSet(PEEP_STATE_BACK);
            peep_step = 6;
        }
    }

    if (peep_step == 6 && GetFadeState() != 0)
        peep_step = 7;

    if (peep_step == 7)
    {
        GetGPhaseCtrl()->SetNext(0x20, 0);
        return 1;
    }

    IngameDrawSub(0);
    return 0;
}

// FF3 0x00227430.
int end_Story_Peeping(void)
{
    PeepFadeLayerTerm();
    GetPhotoModeWrk()->SetViewFade(0);
    if (GetGPhaseCtrl()->now == 0x20)   // back into Story_Normal
    {
        PeepStateSet(PEEP_STATE_NONE);
        LockTblDec(3);
        LockTblDec(9);
        LockTblDec(0x17);
    }
    IngameDrawSub(0);
    return 1;
}
