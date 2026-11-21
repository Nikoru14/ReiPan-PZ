/* ==========================================================================
 *  reipan_over_shoulder.cpp  (the over-the-shoulder camera)
 *
 *  See reipan_over_shoulder.h.  Ported from MikuPan's (MikuPanOTS,
 *  src/mikupan/gameplay/mikupan_over_shoulder.cpp) to FF3: world units about
 *  1/28 of FF1's (Rei stands about 27 tall), a 30 Hz tick instead of 60,
 *  the GRA3D camera instead of FF1's, and the game's own camera-relative
 *  walking given this camera's heading instead of a movement replacement.
 * ======================================================================== */

#include "reipan/gameplay/reipan_over_shoulder.h"

#include "graphics/graph3d/gra3d.h"             // gra3dGetCamera, gra3dcamSet*, gra3dApplyCamera
#include "ingame/ingame_wrk.h"                  // ingame_wrk.plyr_wrk
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_MODE_*, PLYR_FLG_*
#include "ingame/plyr/unit_ctl.h"               // GetStickRaw
#include "outgame/option.h"                     // GetOptionWrk: b09, the look stick's y
#include "reipan/gameplay/reipan_ots_geometry.h"
#include "system/os/system.h"                    // GetSysWrk: the game tick
#include "reipan/io/reipan_log.h"                // ReiPan_LogPrintf
#include "system/pad/pad.h"                     // pad_wrk
#include "system/pad/pad_ctrl.h"                // IsPadLocked

#include <math.h>
#include <string.h>

namespace
{

constexpr float kPi = 3.14159265f;

float Deg(float d)
{
    return d * kPi / 180.0f;
}

/* ---- tuning (FF3 world units, per 30 Hz tick) ---------------------------- */

/* Pitch limits: further down lifts the camera into low ceilings, further up
 * drops it to the floor. */
constexpr float kPitchMinDeg = -40.0f;
constexpr float kPitchMaxDeg = 35.0f;
constexpr float kDefaultPitchDeg = -8.0f;

/* Right-stick speed at full tilt, degrees a tick. */
constexpr float kStickYawDeg = 6.4f;
constexpr float kStickPitchDeg = 4.4f;
constexpr int   kStickDeadzone = 24;

/* The camera as a sphere against the drawn scenery: its radius, how far in
 * front of a hit it stops, and the size below which a piece is a rope, a
 * pole or a candle and is looked past. */
constexpr float kCameraRadius = 1.0f;
constexpr float kGeomMargin = 1.1f;
constexpr float kShoulderMargin = 1.3f;
constexpr float kThinPiece = 2.2f;

/* Walls clamp at once so the camera never sees through them; after any pull
 * in it holds its distance a moment before easing back out, so narrow
 * corridors do not pump. */
constexpr float kBoomEaseOut = 0.12f;
constexpr int   kBoomHoldTicks = 10;
constexpr float kShoulderPullRate = 0.51f;
constexpr float kShoulderEaseOut = 0.10f;

/* Keep the camera between these heights above the feet (Y grows down). */
constexpr float kMinHeight = 2.2f;
constexpr float kMaxHeight = 48.0f;

/* Below this boom the camera sits inside the shoulder; the gap between the
 * two stops the model flickering at the threshold. */
constexpr float kHideModelBoom = 5.0f;
constexpr float kShowModelBoom = 7.0f;

/* Blend from the previous camera when it was near enough to be the same
 * shot (lowering the viewfinder, an event camera that ends where we are). */
constexpr int   kBlendTicks = 6;
constexpr float kBlendMaxDist = 64.0f;

/* Ticks of right-stick silence before the camera drifts back behind the
 * player, and the drift's and R3's rates. */
constexpr int   kAutoFollowDelay = 23;
constexpr float kAutoFollowRate = 0.06f;
constexpr float kRecenterRate = 0.39f;

/* After another camera has had the screen, keep the player's camera angle if
 * she is still standing where she was and facing the same way. */
constexpr float kResumeMaxMove = 11.0f;
constexpr float kResumeMaxTurnDeg = 25.0f;

/* The pivot snaps rather than eases when the player moves this far in one
 * tick (a door into the next room, a warp). */
constexpr float kPivotSnap = 14.0f;

/* Looking from the camera this far ahead keeps the target stable however
 * short the boom gets. */
constexpr float kLookDistance = 40.0f;

constexpr unsigned short kPadR3 = 0x0400;   // FF2-internal bit (sdk/libpad.cpp)

/* ---- state --------------------------------------------------------------- */

ReiPan_OverShoulderSettings g_set;
bool                        g_set_init = false;

int   g_active = 0;          // on screen this frame
int   g_was_active = 0;      // and last frame
int   g_game_camera = 0;     // REIPAN_OTS_CAM_*: what CameraMain ran
int   g_logged_camera = -1;
u_int g_ctrl_tick = 0;
float g_yaw = 0.0f;
float g_pitch = 0.0f;
float g_boom = 0.0f;
int   g_boom_hold = 0;
float g_shoulder_scale = 1.0f;
float g_pivot[3];
int   g_pivot_valid = 0;
const PLYR_WRK *g_pivot_owner = nullptr;
int   g_hide_model = 0;
int   g_blend_hide = 0;

int   g_look_idle = 0;
int   g_recenter = 0;

/* The game's own camera, kept while this one is on screen, so its cameras
 * that ease from where they were keep easing from their shot. */
GRA3DCAMERA        g_stock_cam;
int                g_stock_valid = 0;
int                g_overrode = 0;
u_int              g_overrode_tick = 0;

/* What was on screen before the stock camera ran this frame, for blends. */
float g_last_p[4];
float g_last_i[4];
int   g_last_valid = 0;

int   g_blend = 0;
float g_blend_p[3];
float g_blend_i[3];

/* The heading the left stick is relative to; held across a cut until the
 * stick is let go. */
float g_move_ref = 0.0f;
int   g_move_latched = 0;
int   g_move_src_prev = -1;

/* Where the player was when this camera last had the screen. */
int   g_resume_valid = 0;
float g_resume_pos[3];
float g_resume_facing = 0.0f;

ReiPan_OverShoulderDebug g_debug;

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float Wrap(float a)
{
    while (a > kPi)
    {
        a -= 2.0f * kPi;
    }
    while (a < -kPi)
    {
        a += 2.0f * kPi;
    }
    return a;
}

float Dist(const float *a, const float *b)
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

float StickAxis(u_char value)
{
    const int v = (int)value - 128;
    const int av = v < 0 ? -v : v;
    if (av <= kStickDeadzone)
    {
        return 0.0f;
    }
    const float f = (float)(av - kStickDeadzone) / (float)(127 - kStickDeadzone);
    return Clamp(f, 0.0f, 1.0f) * (v < 0 ? -1.0f : 1.0f);
}

void EnsureSettings()
{
    if (!g_set_init)
    {
        ReiPan_OverShoulderDefaults(&g_set);
        g_set_init = true;
    }
}

PLYR_WRK *Player()
{
    return ingame_wrk.plyr_wrk;
}

void ClampPitch()
{
    g_pitch = Clamp(g_pitch, Deg(kPitchMinDeg), Deg(kPitchMaxDeg));
}

/* ---- ownership ----------------------------------------------------------- */

/* The player is in ordinary control: walking, standing, examining.  Not the
 * viewfinder, crawling, a ladder, a door, a hold, the death scene. */
int PlayerInOrdinaryControl(const PLYR_WRK *plyr)
{
    if (plyr == nullptr)
    {
        return 0;
    }
    if (plyr->mode != PLYR_MODE_NORMAL && plyr->mode != PLYR_MODE_11)
    {
        return 0;
    }
    if ((plyr->flags & (PLYR_FLG_HOLD | PLYR_FLG_PZL)) != 0)
    {
        return 0;
    }
    if ((plyr->_0e0 & 3) != 0)
    {
        return 0;
    }
    return 1;
}

/* ---- camera geometry ----------------------------------------------------- */

void TargetPivot(float out[3])
{
    const PLYR_WRK *plyr = Player();
    out[0] = plyr->pos[0];
    out[1] = plyr->pos[1] - g_set.height;
    out[2] = plyr->pos[2];
}

/* Positive pitch looks up; Y grows down; yaw as GetTrgtRot's atan2(x, z). */
void Forward(float out[3])
{
    const float cp = cosf(g_pitch);
    out[0] = sinf(g_yaw) * cp;
    out[1] = -sinf(g_pitch);
    out[2] = cosf(g_yaw) * cp;
}

void Shoulder(float out[3])
{
    const float s = (g_set.side_left ? -1.0f : 1.0f) * g_set.side;
    /* Right of the heading: forward (sin, 0, cos) turned a quarter. */
    out[0] = cosf(g_yaw) * s;
    out[1] = 0.0f;
    out[2] = -sinf(g_yaw) * s;
}

void IdealCamera(float out[3])
{
    float pivot[3];
    float shoulder[3];
    float forward[3];

    TargetPivot(pivot);
    Shoulder(shoulder);
    Forward(forward);
    for (int i = 0; i < 3; i++)
    {
        out[i] = pivot[i] + shoulder[i] - forward[i] * g_set.distance;
    }
}

/* How far along `dir` the drawn scenery lets the camera go. */
float FreeAlong(const float from[3], const float dir[3], float length,
                ReiPan_OtsGeomHit *hit)
{
    ReiPan_OtsGeomCast(from, dir, length, kCameraRadius, kThinPiece, hit);
    return hit->distance;
}

/* Fraction of the shoulder offset that stays clear of walls. */
float FreeShoulder(const float from[3], const float offset[3])
{
    const float length = sqrtf(offset[0] * offset[0] + offset[2] * offset[2]);
    if (length <= 0.08f)
    {
        return 1.0f;
    }
    const float dir[3] = {offset[0] / length, 0.0f, offset[2] / length};
    ReiPan_OtsGeomHit hit;
    float free_len = FreeAlong(from, dir, length + kShoulderMargin, &hit);
    if (free_len >= length + kShoulderMargin)
    {
        return 1.0f;
    }
    free_len -= kShoulderMargin;
    return Clamp(free_len / length, 0.0f, 1.0f);
}

/* ---- orbit --------------------------------------------------------------- */

int CanResume(const PLYR_WRK *plyr)
{
    if (!g_resume_valid || plyr != g_pivot_owner)
    {
        return 0;
    }
    if (Dist(plyr->pos, g_resume_pos) > kResumeMaxMove)
    {
        return 0;
    }
    return fabsf(Wrap(plyr->rot[1] - g_resume_facing)) <= Deg(kResumeMaxTurnDeg);
}

void RememberOrbit(const PLYR_WRK *plyr)
{
    memcpy(g_resume_pos, plyr->pos, sizeof(g_resume_pos));
    g_resume_facing = plyr->rot[1];
    g_resume_valid = 1;
}

void Activate()
{
    PLYR_WRK *plyr = Player();
    float     ideal[3];

    if (CanResume(plyr))
    {
        /* Same spot, same facing: carry on with the player's own angle. */
        g_boom_hold = 0;
    }
    else
    {
        g_yaw = plyr->rot[1];
        g_pitch = Deg(kDefaultPitchDeg);
        ClampPitch();
        g_boom = g_set.distance;
        g_boom_hold = 0;
        g_shoulder_scale = 1.0f;
        g_hide_model = 0;
    }

    TargetPivot(g_pivot);
    g_pivot_valid = 1;
    g_pivot_owner = plyr;
    g_look_idle = kAutoFollowDelay;
    g_recenter = 0;
    g_blend = 0;

    IdealCamera(ideal);
    if (g_last_valid && Dist(g_last_p, ideal) < kBlendMaxDist &&
        Dist(g_last_p, plyr->pos) < kBlendMaxDist)
    {
        g_blend = kBlendTicks;
        memcpy(g_blend_p, g_last_p, sizeof(g_blend_p));
        memcpy(g_blend_i, g_last_i, sizeof(g_blend_i));
    }
}

int UpdateLook()
{
    u_char x = 0x80;
    u_char y = 0x80;
    int    moved = 0;

    if (!IsPadLocked(0))
    {
        /* The right stick, always.  Walking is the left stick whatever the
         * options say (GetStickRaw 2); the "Analog stick" option only swaps
         * which stick aims and which steps inside the viewfinder (modes 5
         * and 6), so following it here would put the camera on the walking
         * stick. */
        GetStickRaw(4, &x, &y);
    }

    const float yaw_in = StickAxis(x);
    float       pitch_in = StickAxis(y);

    if (yaw_in != 0.0f)
    {
        g_yaw += yaw_in * Deg(kStickYawDeg) * g_set.sensitivity;
        moved = 1;
    }
    if (pitch_in != 0.0f)
    {
        /* Stick up is below centre: look up, unless either the game's
         * "Point of view" inversion or this camera's is on. */
        pitch_in = -pitch_in;
        if ((GetOptionWrk()->now.b09 == 1) != (g_set.invert_y != 0))
        {
            pitch_in = -pitch_in;
        }
        g_pitch += pitch_in * Deg(kStickPitchDeg) * g_set.sensitivity;
        moved = 1;
    }

    g_yaw = Wrap(g_yaw);
    ClampPitch();

    if (moved)
    {
        g_look_idle = 0;
        g_recenter = 0;
    }
    else if (g_look_idle < 100000)
    {
        g_look_idle++;
    }
    return moved;
}

void UpdateRecenter(const PLYR_WRK *plyr)
{
    if (!IsPadLocked(0) && (pad_wrk[0].trg & kPadR3) != 0)
    {
        g_recenter = 1;
    }
    if (!g_recenter)
    {
        return;
    }
    const float delta = Wrap(plyr->rot[1] - g_yaw);
    g_yaw = Wrap(g_yaw + delta * kRecenterRate);
    g_pitch += (Deg(kDefaultPitchDeg) - g_pitch) * kRecenterRate;
    ClampPitch();
    if (fabsf(delta) < Deg(0.5f))
    {
        g_recenter = 0;
    }
}

void AutoFollow(const PLYR_WRK *plyr)
{
    if (!g_set.auto_follow || g_recenter || g_look_idle < kAutoFollowDelay)
    {
        return;
    }
    /* Only while she actually walks, and not while she turns round, or the
     * camera would chase the turn. */
    const float moved = sqrtf(plyr->move_last[0] * plyr->move_last[0] +
                              plyr->move_last[2] * plyr->move_last[2]);
    if (moved < 0.03f || (plyr->flags2 & 0x3f800) != 0)
    {
        return;
    }
    const float delta = Wrap(plyr->rot[1] - g_yaw);
    /* Walking towards the camera is a choice; leave it alone. */
    if (fabsf(delta) > Deg(135.0f))
    {
        return;
    }
    g_yaw = Wrap(g_yaw + delta * kAutoFollowRate);
}

/* ---- build --------------------------------------------------------------- */

void BuildCamera(float out_p[3], float out_i[3])
{
    const PLYR_WRK *plyr = Player();
    float           target[3];
    float           shoulder[3];
    float           forward[3];
    float           back[3];
    float           start[3];

    TargetPivot(target);

    /* Ease the pivot so the camera lags a touch behind, but snap when the
     * player is moved a long way at once. */
    const float follow = 1.0f - Clamp(g_set.smoothing, 0.0f, 0.8f);
    if (!g_pivot_valid || g_pivot_owner != plyr ||
        Dist(g_pivot, target) > kPivotSnap)
    {
        memcpy(g_pivot, target, sizeof(g_pivot));
        g_pivot_valid = 1;
        g_pivot_owner = plyr;
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            g_pivot[i] += (target[i] - g_pivot[i]) * follow;
        }
    }

    Shoulder(shoulder);
    Forward(forward);
    for (int i = 0; i < 3; i++)
    {
        back[i] = -forward[i];
    }

    const int collide = g_set.collision && ReiPan_OtsGeomPieceCount() > 0;

    /* The shoulder offset eases towards the clear fraction, quickly in and
     * slowly out; the camera uses whichever is smaller, so it is clear of
     * the wall yet never pops back out. */
    {
        const float scale_target =
            collide ? FreeShoulder(g_pivot, shoulder) : 1.0f;
        g_shoulder_scale += (scale_target - g_shoulder_scale) *
                            (scale_target < g_shoulder_scale ? kShoulderPullRate
                                                             : kShoulderEaseOut);
        g_shoulder_scale = Clamp(g_shoulder_scale, 0.0f, 1.0f);
        const float scale =
            g_shoulder_scale < scale_target ? g_shoulder_scale : scale_target;
        for (int i = 0; i < 3; i++)
        {
            start[i] = g_pivot[i] + shoulder[i] * scale;
        }
    }

    float             allowed = g_set.distance;
    ReiPan_OtsGeomHit hit{};
    if (collide)
    {
        allowed = FreeAlong(start, back, allowed, &hit);
        if (allowed < g_set.distance)
        {
            allowed -= kGeomMargin;
        }
        allowed = allowed < 0.0f ? 0.0f : allowed;
    }

    if (g_boom > allowed)
    {
        /* Never let the camera end up behind a wall. */
        g_boom = allowed;
        g_boom_hold = kBoomHoldTicks;
    }
    else if (g_boom_hold > 0)
    {
        g_boom_hold--;
    }
    else
    {
        g_boom += (allowed - g_boom) * kBoomEaseOut;
    }

    for (int i = 0; i < 3; i++)
    {
        out_p[i] = start[i] + back[i] * g_boom;
    }

    /* Above the floor and under the ceiling whatever the pitch. */
    out_p[1] = Clamp(out_p[1], plyr->pos[1] - kMaxHeight,
                     plyr->pos[1] - kMinHeight);

    for (int i = 0; i < 3; i++)
    {
        out_i[i] = out_p[i] + forward[i] * kLookDistance;
    }

    if (g_boom < kHideModelBoom)
    {
        g_hide_model = 1;
    }
    else if (g_boom > kShowModelBoom)
    {
        g_hide_model = 0;
    }

    g_debug.boom = g_boom;
    g_debug.allowed = allowed;
    g_debug.requested = g_set.distance;
    g_debug.pieces = ReiPan_OtsGeomPieceCount();
    g_debug.pieces_tested = hit.pieces_tested;
    g_debug.triangles_tested = hit.triangles_tested;
    g_debug.hit = hit.hit;
    memcpy(g_debug.hit_size, hit.hit_size, sizeof(g_debug.hit_size));
}

void ApplyBlend(float p[3], float t_i[3])
{
    g_blend_hide = 0;
    if (g_blend <= 0)
    {
        return;
    }
    float t = (float)g_blend / (float)kBlendTicks;
    t = t * t;
    for (int i = 0; i < 3; i++)
    {
        p[i] += (g_blend_p[i] - p[i]) * t;
        t_i[i] += (g_blend_i[i] - t_i[i]) * t;
    }
    g_blend_hide = Dist(p, g_pivot) < 8.0f;
    g_blend--;
}

void UpdateMoveReference(float stock_yaw, int stick_held)
{
    const int   source = g_active ? 0 : 1;
    const float reference = g_active ? g_yaw : stock_yaw;

    if (g_move_src_prev >= 0 && source != g_move_src_prev && stick_held)
    {
        g_move_latched = 1;
    }
    if (!stick_held)
    {
        g_move_latched = 0;
    }
    if (!g_move_latched)
    {
        g_move_ref = reference;
    }
    g_move_src_prev = source;
}

} // namespace

extern "C" {

void ReiPan_OverShoulderDefaults(ReiPan_OverShoulderSettings *out)
{
    out->enabled = 0;
    out->side_left = 0;
    out->collision = 1;
    out->auto_follow = 1;
    out->invert_y = 0;
    out->distance = 25.0f;
    out->height = 26.0f;
    out->side = 3.5f;
    out->fov_deg = 60.0f;
    out->sensitivity = 1.0f;
    out->smoothing = 0.3f;
}

void ReiPan_OverShoulderGetSettings(ReiPan_OverShoulderSettings *out)
{
    EnsureSettings();
    *out = g_set;
}

void ReiPan_OverShoulderSetSettings(const ReiPan_OverShoulderSettings *in)
{
    EnsureSettings();
    ReiPan_OverShoulderSettings s = *in;
    s.enabled = s.enabled ? 1 : 0;
    s.side_left = s.side_left ? 1 : 0;
    s.collision = s.collision ? 1 : 0;
    s.auto_follow = s.auto_follow ? 1 : 0;
    s.invert_y = s.invert_y ? 1 : 0;
    s.distance = Clamp(isfinite(s.distance) ? s.distance : 25.0f, 10.0f, 45.0f);
    s.height = Clamp(isfinite(s.height) ? s.height : 26.0f, 10.0f, 32.0f);
    s.side = Clamp(isfinite(s.side) ? s.side : 3.5f, 0.0f, 12.0f);
    s.fov_deg = Clamp(isfinite(s.fov_deg) ? s.fov_deg : 60.0f, 35.0f, 80.0f);
    s.sensitivity =
        Clamp(isfinite(s.sensitivity) ? s.sensitivity : 1.0f, 0.25f, 3.0f);
    s.smoothing = Clamp(isfinite(s.smoothing) ? s.smoothing : 0.3f, 0.0f, 0.8f);
    ReiPan_OverShoulderSetEnabled(s.enabled);
    g_set = s;
}

int ReiPan_OverShoulderEnabled(void)
{
    EnsureSettings();
    return g_set.enabled;
}

void ReiPan_OverShoulderSetEnabled(int enabled)
{
    EnsureSettings();
    enabled = enabled ? 1 : 0;
    if (enabled && !g_set.enabled)
    {
        g_was_active = 0;
        g_resume_valid = 0;
        g_move_latched = 0;
        g_move_src_prev = -1;
    }
    g_set.enabled = enabled;
    if (!enabled)
    {
        g_active = 0;
        g_was_active = 0;
        g_hide_model = 0;
        g_blend_hide = 0;
    }
}

void ReiPan_OverShoulderBeginFrame(void)
{
    EnsureSettings();

    /* The scenery drawn last frame is what the camera collides with now. */
    ReiPan_OtsGeomNextFrame();
    ReiPan_OtsGeomSetEnabled(g_set.enabled && g_set.collision);

    GRA3DCAMERA *cam = gra3dGetCamera();

    /* What is on screen now, for a blend into this camera. */
    memcpy(g_last_p, cam->matCoord[3], sizeof(g_last_p));
    memcpy(g_last_i, cam->vTarget, sizeof(g_last_i));
    g_last_valid = 1;

    /* Put the game's own camera back, so its cameras that ease from where
     * they were ease from their own shot and not from ours.  Only straight
     * after a frame this camera replaced: if anything else drove the camera
     * in between (a cutscene, a menu's model view), that is the state to
     * keep. */
    const u_int tick = GetSysWrk()->count;
    if (g_overrode && g_stock_valid && tick - g_overrode_tick <= 1u)
    {
        const float keep_old[4] = {cam->vPositionOld[0], cam->vPositionOld[1],
                                   cam->vPositionOld[2], cam->vPositionOld[3]};
        *cam = g_stock_cam;
        memcpy(cam->vPositionOld, keep_old, sizeof(keep_old));
        gra3dApplyCamera(nullptr, 0, 0);
    }
    g_overrode = 0;

    g_was_active = g_active;
    g_active = 0;
    g_game_camera = REIPAN_OTS_CAM_NONE;
}

void ReiPan_OverShoulderNoteCamera(int which)
{
    g_game_camera = which;
}

const char *ReiPan_OverShoulderCameraName(int which)
{
    static const char *const kNames[REIPAN_OTS_CAM_COUNT] = {
        "none", "room camera", "event camera", "pinned target", "crawl view",
        "peep view", "Camera Obscura", "stance camera", "damage/death shot",
        "tattoo event", "ladder",
    };
    return which >= 0 && which < REIPAN_OTS_CAM_COUNT ? kNames[which] : "?";
}

void ReiPan_OverShoulderCameraCtrl(void)
{
    EnsureSettings();

    GRA3DCAMERA *cam = gra3dGetCamera();
    PLYR_WRK    *plyr = Player();

    /* The game's own shot of this frame, to put back next frame. */
    g_stock_cam = *cam;
    g_stock_valid = 1;

    g_debug.active = 0;
    g_debug.game_camera = g_game_camera;
    g_ctrl_tick = GetSysWrk()->count;

    /* Each change of the game's camera while this one is on, in the log, so
     * a place where it does not take over can be told apart from the rest. */
    if (g_set.enabled && g_game_camera != g_logged_camera)
    {
        ReiPan_LogPrintf("[camera] over-the-shoulder: the game's camera is now the %s%s\n",
                         ReiPan_OverShoulderCameraName(g_game_camera),
                         g_game_camera == REIPAN_OTS_CAM_MAP ? " (replaced)" : "");
        g_logged_camera = g_game_camera;
    }

    if (!g_set.enabled || g_game_camera != REIPAN_OTS_CAM_MAP || plyr == nullptr)
    {
        g_was_active = 0;
        return;
    }

    if (!g_was_active || g_pivot_owner != plyr)
    {
        Activate();
    }
    g_active = 1;

    if (PlayerInOrdinaryControl(plyr))
    {
        UpdateLook();
        UpdateRecenter(plyr);
        AutoFollow(plyr);
    }
    else if (g_look_idle < 100000)
    {
        g_look_idle++;
    }

    float p[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float t[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    BuildCamera(p, t);
    ApplyBlend(p, t);

    /* The sound listener's velocity is taken from the camera's last
     * position; that is ours, not the stock shot put back this frame. */
    const float old[4] = {g_last_p[0], g_last_p[1], g_last_p[2], 1.0f};
    gra3dcamSetPosition(p);
    memcpy(cam->vPositionOld, old, sizeof(old));
    gra3dcamSetTarget(t, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(Deg(g_set.fov_deg));
    gra3dApplyCamera(nullptr, 1, 1);

    RememberOrbit(plyr);
    g_overrode = 1;
    g_overrode_tick = GetSysWrk()->count;

    g_debug.active = 1;
    g_debug.yaw_deg = g_yaw * 180.0f / kPi;
    g_debug.pitch_deg = g_pitch * 180.0f / kPi;
}

int ReiPan_OverShoulderActive(void)
{
    return g_active;
}

int ReiPan_OverShoulderOwnsMovement(void)
{
    EnsureSettings();
    return g_set.enabled && g_active && PlayerInOrdinaryControl(Player());
}

int ReiPan_OverShoulderMoveYaw(float stock_yaw, int stick_held, float *yaw)
{
    EnsureSettings();
    if (!g_set.enabled)
    {
        g_move_latched = 0;
        g_move_src_prev = -1;
        return 0;
    }
    UpdateMoveReference(stock_yaw, stick_held);
    if (!g_active && !g_move_latched)
    {
        return 0;
    }
    *yaw = g_move_ref;
    return 1;
}

void ReiPan_OverShoulderFinderStart(float *facing, float *pitch)
{
    if (!g_set.enabled || !g_active || facing == nullptr || pitch == nullptr)
    {
        return;
    }
    *facing = g_yaw;
    /* The viewfinder's own range is -60..60 degrees. */
    *pitch = Clamp(g_pitch, Deg(-45.0f), Deg(45.0f));
}

int ReiPan_OverShoulderHidePlayer(void)
{
    return g_active && (g_hide_model || g_blend_hide);
}

void ReiPan_OverShoulderGetDebug(ReiPan_OverShoulderDebug *out)
{
    *out = g_debug;
    out->ticks_since = (int)(GetSysWrk()->count - g_ctrl_tick);
}

}
