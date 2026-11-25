#ifndef REIPAN_GYRO_H
#define REIPAN_GYRO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gyro aiming, as MioPan has it: while the Camera Obscura is raised, a gamepad
 * with a gyroscope aims it by being turned, alongside the stick and the mouse.
 * The motion goes straight onto the view's yaw and pitch, not through the
 * stick.  Nothing else uses the gyro -- walking, the over-the-shoulder camera
 * and the menus ignore it entirely.
 *
 * Every sample SDL reports is integrated over its own sensor timestamp, so the
 * turn is an angle, not a rate: tilting the pad 10 degrees turns the view 10
 * degrees times the sensitivity however the samples fell between frames.
 *
 * A gyro reads a little above zero when still, and that bias walks with
 * temperature.  It is learned continuously whenever the pad lies still with
 * the Camera Obscura down -- never while aiming, where a slow, steady turn
 * would look the same -- and ReiPan_GyroRecalibrate() starts it over.  What
 * bias is left is damped by easing rotation slower than a couple of degrees a
 * second toward zero, which is what stops the view creeping.
 *
 * Settings are section [gyro] of reipan.ini.
 */

/* Multiplies the pad's own rotation: 1.0 turns the view as far as the pad
 * turned. */
#define REIPAN_GYRO_SENSITIVITY_DEFAULT 1.5f
#define REIPAN_GYRO_SENSITIVITY_MIN     0.1f
#define REIPAN_GYRO_SENSITIVITY_MAX     8.0f

/* Which rotation turns the view left and right.  Pitch -- tipping the pad's
 * far edge up or down -- always looks up and down. */
enum
{
    REIPAN_GYRO_TURN_YAW = 0,       /* point the pad left and right, like a torch */
    REIPAN_GYRO_TURN_ROLL           /* tip it sideways, like a steering wheel */
};

/* Fork: what the touch pad's right half does with the Camera Obscura up.
 * Each holds the gyro still while it is pressed, so the controller can be
 * brought back to a comfortable angle; the last two also move the view. */
enum
{
    REIPAN_GYRO_RECENTER_ZERO = 0,  /* the view stays put: re-grip only */
    REIPAN_GYRO_RECENTER_LEVEL,     /* the view tips back to level */
    REIPAN_GYRO_RECENTER_START      /* level, and turned back to where she faced
                                     * when the Camera Obscura went up */
};

typedef struct ReiPan_GyroSettings
{
    int   look;                     /* aim the Camera Obscura with it */
    float sensitivity;              /* REIPAN_GYRO_SENSITIVITY_MIN.._MAX */
    int   turn;                     /* REIPAN_GYRO_TURN_* */
    int   invert_x;
    int   invert_y;
    int   recenter;                 /* fork: REIPAN_GYRO_RECENTER_* */
} ReiPan_GyroSettings;

void ReiPan_GyroDefaults(ReiPan_GyroSettings *out);
void ReiPan_GyroGetSettings(ReiPan_GyroSettings *out);
void ReiPan_GyroSetSettings(const ReiPan_GyroSettings *in);
int  ReiPan_GyroSettingsAreDefault(void);

/* Forget the learned bias; it is learned again the next time the pad lies
 * still with the Camera Obscura down. */
void ReiPan_GyroRecalibrate(void);

/* Play is aiming the Camera Obscura this frame.  To be repeated every frame it
 * does; aiming lapses one pad read after the requests stop. */
void ReiPan_GyroRequestAim(void);

/* Fork: the right half of the touch pad, held while the Camera Obscura is up,
 * re-grips the controller: the gyro's motion is let go while it is held, so
 * the pad can be brought back to a comfortable angle, and whatever way it
 * points when let go is where the view carries on from.  On the PS5 a click
 * on the touch pad is also Select (its left half stays Select while aiming);
 * the input layer leaves Select out while this is held.  1 while held. */
int ReiPan_GyroRegripHeld(void);

/* Fork: the frame the touch pad's right half went down with the Camera
 * Obscura up and the gyro on, whichever REIPAN_GYRO_RECENTER_* is chosen
 * (the finder levels or turns the view back for the last two); -1 on every
 * other frame.  Read once a frame, by the finder. */
int ReiPan_GyroRecenterPressed(void);

/* This frame's turn, in radians with the sensitivity and the inversions
 * already applied: positive yaw turns right, positive pitch looks up.  Writes
 * 0 and returns 0 when there is none. */
int ReiPan_GyroLook(float *yaw, float *pitch);

/* What the settings show.  `rate_*` is the pad's motion now, bias removed, in
 * degrees a second and in the view's own signs (yaw positive to the right,
 * pitch up), before sensitivity -- so it reads the same whatever the setting.
 * `calibration` runs 0..1 while the first bias is being learned. */
typedef struct ReiPan_GyroStatus
{
    int   has_gamepad;
    int   has_gyro;                 /* the open pad reports one */
    int   active;                   /* reporting: on in the settings and on the pad */
    int   aiming;                   /* play is asking for it */
    float rate_hz;                  /* the pad's own sample rate, 0 if it does not say */
    float calibration;
    float bias_deg[3];              /* pitch, yaw, roll, degrees a second */
    float rate_yaw_deg;
    float rate_pitch_deg;
} ReiPan_GyroStatus;

void ReiPan_GyroGetStatus(ReiPan_GyroStatus *out);

/* Once a frame, from the pad read (reipan_input.cpp): hands over the turn
 * gathered since the last read and rolls the aim request. */
void ReiPan_GyroPoll(void);

#ifdef __cplusplus
}

struct SDL_Gamepad;
union SDL_Event;

/* The pad play uses, from reipan_input.cpp: just opened, or null once it has
 * been closed.  Its gyro is switched to match the settings. */
void ReiPan_GyroSetGamepad(SDL_Gamepad *gamepad);

/* Every window event, from the renderer's pump.  Only the pad's sensor and the
 * loss of the focus are looked at. */
void ReiPan_GyroEvent(const SDL_Event *event);
#endif

#endif /* REIPAN_GYRO_H */
