#ifndef REIPAN_OVER_SHOULDER_H
#define REIPAN_OVER_SHOULDER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The over-the-shoulder camera: an optional modern third-person camera in
 * place of the game's fixed room angles, ported from MikuPan's.
 *
 * It follows the player from behind and beside her; the right stick orbits
 * it, R3 brings it back behind her, and the left stick walks the way it is
 * pushed as seen through it (the game's own camera-relative control, given
 * this camera's heading).  It collides with the rooms and furniture as they
 * are drawn (reipan_ots_geometry.h), so it looks over a low table and stops at
 * a wall or a closed door.
 *
 * It only replaces the ordinary map camera.  Scripted event cameras, the
 * viewfinder, crawling, ladders, the death and damage shots and the other
 * special cameras run as the game made them, and the camera picks up again
 * where it was when they end.
 *
 * Distances are world units: Y grows downwards and Rei is about seventeen
 * tall.
 */

typedef struct ReiPan_OverShoulderSettings
{
    int   enabled;          /* 0: the game's fixed cameras */
    int   side_left;        /* over the left shoulder instead of the right */
    int   collision;        /* pull in against the drawn scenery */
    int   auto_follow;      /* drift back behind the player while she walks */
    int   invert_y;         /* stick up looks down */
    float distance;         /* how far behind the shoulder, 6..30 */
    float height;           /* pivot height above the feet, 6..20 */
    float side;             /* shoulder offset, 0..8 */
    float fov_deg;          /* vertical field of view, 35..80 */
    float sensitivity;      /* right-stick speed multiplier, 0.25..3 */
    float smoothing;        /* how much the pivot lags the player, 0..0.8 */
} ReiPan_OverShoulderSettings;

void ReiPan_OverShoulderDefaults(ReiPan_OverShoulderSettings *out);
void ReiPan_OverShoulderGetSettings(ReiPan_OverShoulderSettings *out);
/* Clamped to the ranges above. */
void ReiPan_OverShoulderSetSettings(const ReiPan_OverShoulderSettings *in);

int  ReiPan_OverShoulderEnabled(void);
void ReiPan_OverShoulderSetEnabled(int enabled);

/* ---- the camera (ingame/camera/map_camera.c) --------------------------- */

/* Start of CameraMain(): puts back the game's own camera, so its cameras
 * that ease from where they were keep easing from their own shot. */
void ReiPan_OverShoulderBeginFrame(void);

/* CameraMain() ran the ordinary map camera this frame (no event, special
 * or player camera): the shot this camera may replace. */
void ReiPan_OverShoulderNoteMapCamera(void);

/* End of CameraMain()'s camera choice: replaces the shot when it may. */
void ReiPan_OverShoulderCameraCtrl(void);

/* This camera is on screen (this frame, once CameraMain() has run). */
int ReiPan_OverShoulderActive(void);

/* ---- the player (ingame/plyr) ------------------------------------------ */

/* The player is walking under this camera: the left stick is relative to
 * it, the right stick is the camera's, the tank control is not used. */
int ReiPan_OverShoulderOwnsMovement(void);

/* PlyrMoveDirCtrl(): the heading the left stick is relative to.  `stock_yaw`
 * is the game's own (camera towards the player), `stick_held` whether the
 * stick is pushed.  Returns non-zero and sets *yaw when this camera decides
 * it; across a cut to or from another camera the old heading holds until
 * the stick is let go, so a cut never turns the player round. */
int ReiPan_OverShoulderMoveYaw(float stock_yaw, int stick_held, float *yaw);

/* PlyrFinderModeStart(): the viewfinder opens where this camera looked. */
void ReiPan_OverShoulderFinderStart(float *facing, float *pitch);

/* The camera is inside the player's shoulder: leave her model out. */
int ReiPan_OverShoulderHidePlayer(void);

/* ---- readout ------------------------------------------------------------ */

typedef struct ReiPan_OverShoulderDebug
{
    int   active;
    float yaw_deg;
    float pitch_deg;
    float boom;             /* how far the camera hangs back now */
    float allowed;          /* how far the scenery lets it */
    float requested;        /* the distance setting */
    int   pieces;           /* scenery pieces known this frame */
    int   pieces_tested;
    int   triangles_tested;
    int   hit;
    float hit_size[3];      /* world bounds of what stopped it */
} ReiPan_OverShoulderDebug;

void ReiPan_OverShoulderGetDebug(ReiPan_OverShoulderDebug *out);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_OVER_SHOULDER_H */
