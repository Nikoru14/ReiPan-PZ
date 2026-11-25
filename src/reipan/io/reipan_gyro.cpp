/* ==========================================================================
 *  reipan_gyro.cpp  (aiming the Camera Obscura with a gamepad's gyro)
 *
 *  See reipan_gyro.h.  From MioPan's input layer.  Events arrive on the game
 *  thread (the renderer's pump) and the poll runs there too, from the pad
 *  read, so nothing here needs a lock.
 * ======================================================================== */

#include "reipan_gyro.h"

#include "reipan_input.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_stdinc.h>

#include <math.h>
#include <string.h>

namespace
{

constexpr float kDegToRad = SDL_PI_F / 180.0f;

/* Rotation slower than this is eased toward zero (out = rate * rate / this):
 * the fraction of a degree a second a gyro reads after calibration cannot
 * creep the view, and a deliberate turn passes untouched. */
constexpr float kTightenRad = 2.0f * kDegToRad;

/* "Still", for calibration: every axis within the noise of its own short
 * average, and that average under the cap -- no gyro's bias is anywhere near
 * ten degrees a second, so anything faster is the pad moving, however
 * smoothly.  There is deliberately no test against the current bias: a wrong
 * one would then refuse the very samples that correct it. */
constexpr float kStillNoiseRad = 2.0f * kDegToRad;
constexpr float kStillCapRad = 10.0f * kDegToRad;
constexpr float kFastTau = 0.2f;        /* the short average, seconds */
constexpr float kSettle = 0.5f;         /* still this long before learning */
constexpr float kFirstTau = 0.25f;      /* learning rate until calibrated... */
constexpr float kBiasTau = 1.0f;        /* ...and after */
constexpr float kCalibrated = 1.0f;     /* learning this long counts as done */

/* A gap longer than this between two samples is a pad waking up or the window
 * coming back, not a turn; the sample is weighted at the nominal rate. */
constexpr float kMaxDt = 0.05f;
constexpr float kFallbackHz = 250.0f;

constexpr ReiPan_GyroSettings kDefaults = {
    1, REIPAN_GYRO_SENSITIVITY_DEFAULT, REIPAN_GYRO_TURN_YAW, 0, 0,
    REIPAN_GYRO_RECENTER_ZERO,
};

/* Fork: the touch pad's right half as last read, for its press edge. */
bool g_recenter_was_held;

ReiPan_GyroSettings g_set = kDefaults;

/* The open pad: whether it has a gyro, whether it is reporting, its rate. */
SDL_Gamepad   *g_gamepad;
SDL_JoystickID g_gamepad_id;
int            g_has;
int            g_active;
float          g_rate_hz;
int            g_enable_failure_logged;

/* Sensor timestamp of the last sample, 0 for none since the pad (re)started. */
Uint64 g_last_ns;

/* Calibration, all in radians a second, SDL's axes (pitch, yaw, roll). */
float g_fast[3];
int   g_fast_valid;
float g_bias[3];
float g_still_s;
float g_calibrated_s;

/* The last sample, bias removed, in the view's signs -- for the readout. */
float g_rate_yaw;
float g_rate_pitch;

/* Turn gathered since the last poll, and the last poll's share of it, in
 * radians in the view's signs (yaw right, pitch up), before sensitivity. */
float g_motion_yaw;
float g_motion_pitch;
float g_look_yaw;
float g_look_pitch;

/* Play's requests, for the frame now running and the one before.  Both count,
 * so the samples in the gap between a poll and the game asking again are not
 * lost, and aiming does not outlive the finder by more than a frame. */
int g_request;
int g_request_prev;

int Aiming()
{
    return g_request != 0 || g_request_prev != 0;
}

/* Fork: the touch pad clicked on its right half -- SDL's touchpad button,
 * taken as the right half when the first finger is there or the pad says
 * nothing of its fingers. */
bool TouchpadRightHeld()
{
    if (g_gamepad == nullptr ||
        !SDL_GetGamepadButton(g_gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD))
    {
        return false;
    }
    if (SDL_GetNumGamepadTouchpads(g_gamepad) > 0 &&
        SDL_GetNumGamepadTouchpadFingers(g_gamepad, 0) > 0)
    {
        bool down = false;
        float x = 0.0f;
        float y = 0.0f;
        float pressure = 0.0f;
        if (SDL_GetGamepadTouchpadFinger(g_gamepad, 0, 0, &down, &x, &y, &pressure) &&
            down)
        {
            return x >= 0.5f;
        }
    }
    return true;
}

/* Drop motion in flight and the sample clock, keeping the calibration.  For
 * anything that breaks the run of samples: focus lost, the gyro switched. */
void ClearMotion()
{
    g_last_ns = 0;
    g_motion_yaw = 0.0f;
    g_motion_pitch = 0.0f;
    g_look_yaw = 0.0f;
    g_look_pitch = 0.0f;
    g_rate_yaw = 0.0f;
    g_rate_pitch = 0.0f;
    g_still_s = 0.0f;
    g_fast_valid = 0;
}

/* Forget the bias as well.  A different pad has a different one. */
void ResetCalibration()
{
    ClearMotion();
    memset(g_bias, 0, sizeof(g_bias));
    g_calibrated_s = 0.0f;
}

/* Bring the open pad's gyro in line with the setting: on when there is a pad,
 * it has a gyro and gyro aiming is on, off otherwise -- a pad whose gyro is
 * not wanted is not streaming 250 reports a second for nothing.  Called when
 * the pad or the setting changes; harmless to call again. */
void SyncSensor()
{
    if (g_gamepad == nullptr)
    {
        g_has = 0;
        g_active = 0;
        g_rate_hz = 0.0f;
        ClearMotion();
        return;
    }

    g_has = SDL_GamepadHasSensor(g_gamepad, SDL_SENSOR_GYRO) ? 1 : 0;
    int want = g_has && g_set.look;

    if (g_has &&
        !SDL_SetGamepadSensorEnabled(g_gamepad, SDL_SENSOR_GYRO, want != 0))
    {
        if (!g_enable_failure_logged)
        {
            g_enable_failure_logged = 1;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "ReiPan: could not %s the gyro on \"%s\": %s",
                        want ? "enable" : "disable",
                        SDL_GetGamepadName(g_gamepad), SDL_GetError());
        }
        want = 0;
    }

    if (want != g_active)
    {
        ClearMotion();
    }
    g_active = want;
    g_rate_hz = g_has ? SDL_GetGamepadSensorDataRate(g_gamepad, SDL_SENSOR_GYRO)
                      : 0.0f;
}

/* One gyro sample: learn the bias when the pad is still and nobody is aiming,
 * and while play is aiming add the turn to this frame's motion. */
void Sample(const float *w, Uint64 sample_ns)
{
    if (!g_active)
    {
        return;
    }

    float dt = 0.0f;
    if (g_last_ns != 0 && sample_ns > g_last_ns)
    {
        dt = (float)((double)(sample_ns - g_last_ns) * 1.0e-9);
    }
    g_last_ns = sample_ns;
    if (!(dt > 0.0f) || dt > kMaxDt)
    {
        dt = 1.0f / (g_rate_hz > 0.0f ? g_rate_hz : kFallbackHz);
    }

    /* Calibration. */
    if (!g_fast_valid)
    {
        memcpy(g_fast, w, sizeof(g_fast));
        g_fast_valid = 1;
    }
    else
    {
        const float k = dt / (kFastTau + dt);
        for (int i = 0; i < 3; i++)
        {
            g_fast[i] += (w[i] - g_fast[i]) * k;
        }
    }

    int still = 1;
    for (int i = 0; i < 3; i++)
    {
        if (fabsf(w[i] - g_fast[i]) > kStillNoiseRad ||
            fabsf(g_fast[i]) > kStillCapRad)
        {
            still = 0;
        }
    }

    /* Never while aiming: a slow, steady turn through the finder is exactly
     * what a still pad with a bias looks like. */
    if (still && !Aiming())
    {
        g_still_s += dt;
        if (g_still_s >= kSettle)
        {
            const float tau = g_calibrated_s < kCalibrated ? kFirstTau : kBiasTau;
            const float k = dt / (tau + dt);
            for (int i = 0; i < 3; i++)
            {
                g_bias[i] += (g_fast[i] - g_bias[i]) * k;
            }
            if (g_calibrated_s < kCalibrated)
            {
                g_calibrated_s += dt;
            }
        }
    }
    else
    {
        g_still_s = 0.0f;
    }

    float r[3];
    for (int i = 0; i < 3; i++)
    {
        r[i] = w[i] - g_bias[i];
    }

    /* SDL's rotations are counter-clockwise positive seen from the axis's own
     * end: positive yaw points the pad left, positive roll leans its top left,
     * positive pitch lifts its far edge.  The view turns right on positive yaw
     * and looks up on positive pitch, so the turn axis is negated. */
    float yaw = -(g_set.turn == REIPAN_GYRO_TURN_ROLL ? r[2] : r[1]);
    float pitch = r[0];
    g_rate_yaw = yaw;
    g_rate_pitch = pitch;

    const float speed = sqrtf(yaw * yaw + pitch * pitch);
    if (speed < kTightenRad)
    {
        const float k = speed / kTightenRad;
        yaw *= k;
        pitch *= k;
    }

    if (Aiming() && !ReiPan_InputIsSuppressed())
    {
        g_motion_yaw += yaw * dt;
        g_motion_pitch += pitch * dt;
    }
}

} // namespace

extern "C" {

void ReiPan_GyroDefaults(ReiPan_GyroSettings *out)
{
    *out = kDefaults;
}

void ReiPan_GyroGetSettings(ReiPan_GyroSettings *out)
{
    *out = g_set;
}

void ReiPan_GyroSetSettings(const ReiPan_GyroSettings *in)
{
    ReiPan_GyroSettings s = *in;
    s.look = s.look ? 1 : 0;
    s.invert_x = s.invert_x ? 1 : 0;
    s.invert_y = s.invert_y ? 1 : 0;
    s.turn = s.turn == REIPAN_GYRO_TURN_ROLL ? REIPAN_GYRO_TURN_ROLL
                                             : REIPAN_GYRO_TURN_YAW;
    if (s.recenter < REIPAN_GYRO_RECENTER_ZERO || s.recenter > REIPAN_GYRO_RECENTER_START)
    {
        s.recenter = REIPAN_GYRO_RECENTER_ZERO;
    }
    /* Written so a NaN from a hand-edited file lands on the floor too. */
    if (!(s.sensitivity >= REIPAN_GYRO_SENSITIVITY_MIN))
    {
        s.sensitivity = REIPAN_GYRO_SENSITIVITY_MIN;
    }
    else if (s.sensitivity > REIPAN_GYRO_SENSITIVITY_MAX)
    {
        s.sensitivity = REIPAN_GYRO_SENSITIVITY_MAX;
    }

    const bool switched = s.look != g_set.look;
    g_set = s;
    if (switched)
    {
        SyncSensor();
    }
}

int ReiPan_GyroSettingsAreDefault(void)
{
    return g_set.look == kDefaults.look &&
           g_set.sensitivity == kDefaults.sensitivity &&
           g_set.turn == kDefaults.turn &&
           g_set.invert_x == kDefaults.invert_x &&
           g_set.invert_y == kDefaults.invert_y &&
           g_set.recenter == kDefaults.recenter;
}

void ReiPan_GyroRecalibrate(void)
{
    ResetCalibration();
    SDL_Log("ReiPan: gyro calibration restarted");
}

int ReiPan_GyroRecenterPressed(void)
{
    const bool held = g_active && Aiming() && TouchpadRightHeld();
    const bool pressed = held && !g_recenter_was_held;
    g_recenter_was_held = held;
    return pressed ? g_set.recenter : -1;
}

int ReiPan_GyroRegripHeld(void)
{
    return g_active && Aiming() && TouchpadRightHeld() ? 1 : 0;
}

void ReiPan_GyroRequestAim(void)
{
    g_request = 1;
}

int ReiPan_GyroLook(float *yaw, float *pitch)
{
    float x = 0.0f;
    float y = 0.0f;

    if (g_active && !ReiPan_InputIsSuppressed())
    {
        x = g_look_yaw * g_set.sensitivity;
        y = g_look_pitch * g_set.sensitivity;
        x = g_set.invert_x ? -x : x;
        y = g_set.invert_y ? -y : y;
    }

    if (yaw != nullptr)
    {
        *yaw = x;
    }
    if (pitch != nullptr)
    {
        *pitch = y;
    }
    return x != 0.0f || y != 0.0f;
}

void ReiPan_GyroGetStatus(ReiPan_GyroStatus *out)
{
    const float to_deg = 180.0f / SDL_PI_F;

    out->has_gamepad = g_gamepad != nullptr;
    out->has_gyro = g_has;
    out->active = g_active;
    out->aiming = Aiming();
    out->rate_hz = g_rate_hz;
    out->calibration = g_calibrated_s >= kCalibrated ? 1.0f
                                                     : g_calibrated_s / kCalibrated;
    for (int i = 0; i < 3; i++)
    {
        out->bias_deg[i] = g_bias[i] * to_deg;
    }
    out->rate_yaw_deg = g_rate_yaw * to_deg;
    out->rate_pitch_deg = g_rate_pitch * to_deg;
}

void ReiPan_GyroPoll(void)
{
    g_request_prev = g_request;
    g_request = 0;

    /* Fork: a re-grip lets this frame's motion go. */
    const bool regrip = g_request_prev && TouchpadRightHeld();
    g_look_yaw = g_request_prev && !regrip ? g_motion_yaw : 0.0f;
    g_look_pitch = g_request_prev && !regrip ? g_motion_pitch : 0.0f;
    g_motion_yaw = 0.0f;
    g_motion_pitch = 0.0f;
}

}

void ReiPan_GyroSetGamepad(SDL_Gamepad *gamepad)
{
    g_gamepad = gamepad;
    g_gamepad_id = gamepad != nullptr ? SDL_GetGamepadID(gamepad) : 0;

    /* A pad that only comes back (Bluetooth waking) is calibrated again: its
     * bias has had time to walk. */
    ResetCalibration();
    g_enable_failure_logged = 0;
    SyncSensor();

    if (gamepad == nullptr)
    {
        return;
    }
    if (g_has)
    {
        SDL_Log("ReiPan: it has a gyro (%.0f Hz); gyro aiming is %s",
                (double)g_rate_hz, g_active ? "on" : "off");
    }
    else
    {
        SDL_Log("ReiPan: it reports no gyro");
    }
}

void ReiPan_GyroEvent(const SDL_Event *event)
{
    if (event == nullptr)
    {
        return;
    }
    switch (event->type)
    {
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        if (event->gsensor.sensor == SDL_SENSOR_GYRO && g_gamepad != nullptr &&
            event->gsensor.which == g_gamepad_id)
        {
            /* The pad's own clock when it has one; SDL's arrival time is
             * jittered by USB and by how events were batched. */
            Sample(event->gsensor.data, event->gsensor.sensor_timestamp != 0
                                            ? event->gsensor.sensor_timestamp
                                            : event->gsensor.timestamp);
        }
        return;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* SDL stops delivering pad events to a window in the background, so
         * the next sample after it comes back is no continuation of this one. */
        ClearMotion();
        return;

    default:
        return;
    }
}
