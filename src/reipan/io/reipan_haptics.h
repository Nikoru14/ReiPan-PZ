#ifndef REIPAN_HAPTICS_H
#define REIPAN_HAPTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The controller's motors.
 *
 * The game drives a DualShock 2: every frame pad.c hands the pad a small motor
 * that is on or off and a big one with 256 strengths, and those are the two
 * motors in a modern pad's grips (ReiPan_HapticsMotors).  By then nothing says
 * why the pad shakes, and that is what a pad with more motors needs to know.
 * So the places in the game that ask for a shake also raise a cue
 * (gameplay/reipan_haptic_cues.h), and a pad with motors behind its triggers
 * -- Xbox One and later -- plays the cue there: the heartbeat as two beats, a
 * ghost's grip as a struggle from side to side.  The grips stay the game's, and
 * a pad without trigger motors behaves as it always did.
 *
 * Cues play on the clock, not on the game's frames, from a timer that only runs
 * while something is playing.
 *
 * Settings are section [rumble] of reipan.ini.  The game's own Vibration option
 * stays the switch for all of it.
 */

/* What can be felt.  A kept cue lasts for as long as it is raised every frame,
 * any other plays once each time. */
enum
{
    REIPAN_CUE_HEARTBEAT = 0,   /* one beat, harder the closer the ghost */
    REIPAN_CUE_HIT,             /* a ghost's blow */
    REIPAN_CUE_HELD,            /* kept: in a ghost's grip, being drained */
    REIPAN_CUE_STRICKEN,        /* kept: shaking off what a ghost left on her */
    REIPAN_CUE_SHOT,            /* a photograph that hurt a ghost badly */
    REIPAN_CUE_THUNDER,         /* kept */
    REIPAN_CUE_SCENE,           /* kept: a cutscene or movie's own rumble */

    /* Not the original's, only played with the camera setting on. */
    REIPAN_CUE_SHUTTER,         /* the shutter, harder when it hit */
    REIPAN_CUE_CHARGE,          /* one more step of the spirit charge */
    REIPAN_CUE_CHANCE,          /* a shutter chance opened; 1.0 a fatal frame */

    REIPAN_CUE_COUNT
};

const char *ReiPan_HapticsCueName(int cue);

typedef struct ReiPan_HapticsSettings
{
    float strength;             /* every motor, 0..1; 1 is what the game asks */
    int   triggers;             /* play the cues on trigger motors */
    float trigger_strength;     /* those only, 0..1, on top of strength */
    int   camera;               /* the Camera Obscura's cues too */
} ReiPan_HapticsSettings;

void ReiPan_HapticsDefaults(ReiPan_HapticsSettings *out);
void ReiPan_HapticsGetSettings(ReiPan_HapticsSettings *out);
void ReiPan_HapticsSetSettings(const ReiPan_HapticsSettings *in);
int  ReiPan_HapticsSettingsAreDefault(void);

/* The game's two motors for this frame, as the PS2 has them.  They stop by
 * themselves when the frames do.  Returns non-zero with a pad to shake. */
int ReiPan_HapticsMotors(unsigned char small_on, unsigned char big_power);

/* Raise a cue; `strength` is 0..1. */
void ReiPan_HapticsCue(int cue, float strength);

/* For the settings: every motor the pad has in turn, and one cue by itself
 * whatever the camera setting. */
void ReiPan_HapticsTest(void);
void ReiPan_HapticsPreview(int cue);

/* How the trigger motors are reached. */
enum
{
    REIPAN_TRIGGERS_NONE = 0,   /* the pad has none, or they are off */
    REIPAN_TRIGGERS_SEARCHING,  /* an Xbox pad Windows has not shown us yet */
    REIPAN_TRIGGERS_SDL,
    REIPAN_TRIGGERS_WINDOWS     /* io/reipan_xbox_motors.h, all four motors */
};

/* What the settings show.  The motors are what the pad was last told, 0..1. */
typedef struct ReiPan_HapticsStatus
{
    int   has_gamepad;
    int   has_rumble;           /* the pad can shake at all */
    int   triggers;             /* REIPAN_TRIGGERS_* */
    float low;
    float high;
    float left_trigger;
    float right_trigger;
} ReiPan_HapticsStatus;

void ReiPan_HapticsGetStatus(ReiPan_HapticsStatus *out);

#ifdef __cplusplus
}

struct SDL_Gamepad;

/* The pad the input layer has open, or null -- before it closes one, so the
 * motors can be stopped. */
void ReiPan_HapticsSetGamepad(SDL_Gamepad *gamepad);
#endif

#endif /* REIPAN_HAPTICS_H */
