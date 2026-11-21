#ifndef REIPAN_GAME_MOUSE_H
#define REIPAN_GAME_MOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The mouse in play, as MikuPan has it: it looks around (the over-the-shoulder
 * camera, the Camera Obscura's aim) and its buttons are the Camera Obscura's --
 * right raises it (held, or a click each way), left is the shutter, the wheel
 * changes film and middle is the lens shot.
 *
 * The game's menus have the mouse first (reipan_menu_mouse.h): whenever one
 * claims it, and while the ImGui overlay or an RmlUi screen holds it, play does
 * not see it at all.  Looking captures the pointer (SDL relative mode) only on
 * the frames play asks for it, so it is free again the moment a menu opens.
 *
 * This module is the host side: the events, the capture and each frame's
 * clicks, wheel steps and movement.  What they do to the game is in
 * gameplay/reipan_mouse_actions.h.
 */

/* Physical controls an action can be bound to. */
enum
{
    REIPAN_MOUSE_SRC_NONE = 0,
    REIPAN_MOUSE_SRC_LEFT,
    REIPAN_MOUSE_SRC_RIGHT,
    REIPAN_MOUSE_SRC_MIDDLE,
    REIPAN_MOUSE_SRC_X1,            /* "back" side button */
    REIPAN_MOUSE_SRC_X2,            /* "forward" side button */
    REIPAN_MOUSE_SRC_WHEEL_UP,
    REIPAN_MOUSE_SRC_WHEEL_DOWN,
    REIPAN_MOUSE_SRC_COUNT
};

/* What the buttons do in play. */
enum
{
    REIPAN_MOUSE_ACT_CAMERA = 0,    /* raise / lower the Camera Obscura */
    REIPAN_MOUSE_ACT_SHUTTER,       /* take the picture (held charges) */
    REIPAN_MOUSE_ACT_LENS_SHOT,     /* the selected lens's shot */
    REIPAN_MOUSE_ACT_FILM_PREV,
    REIPAN_MOUSE_ACT_FILM_NEXT,
    REIPAN_MOUSE_ACT_COUNT
};

typedef struct ReiPan_GameMouseSettings
{
    int   look;                         /* move the cameras with the mouse */
    float sensitivity;                  /* 0.25..4 */
    int   camera_toggle;                /* 0 hold to keep it raised, 1 click */
    int   bind[REIPAN_MOUSE_ACT_COUNT]; /* REIPAN_MOUSE_SRC_* */
} ReiPan_GameMouseSettings;

void ReiPan_GameMouseDefaults(ReiPan_GameMouseSettings *out);
void ReiPan_GameMouseGetSettings(ReiPan_GameMouseSettings *out);
void ReiPan_GameMouseSetSettings(const ReiPan_GameMouseSettings *in);

/* Names for the file and the menus: "left", "wheel_up", ... and "Left
 * button", "Wheel up", ...  FromName returns -1 for an unknown name. */
const char *ReiPan_GameMouseSourceName(int src);
const char *ReiPan_GameMouseSourceLabel(int src);
int         ReiPan_GameMouseSourceFromName(const char *name);
const char *ReiPan_GameMouseActionLabel(int act);

/* Once a frame, from the pad read (reipan_input.cpp): latches the clicks and
 * wheel steps since the last read and turns the pointer capture on or off for
 * what play asked last frame. */
void ReiPan_GameMousePoll(void);

/* Play wants to look with the mouse this frame. */
void ReiPan_GameMouseRequestLook(void);

/* This frame's look, in radians already scaled by the sensitivity: positive
 * x turns right, positive y looks up.  0 when the pointer is not captured. */
int ReiPan_GameMouseLook(float *yaw, float *pitch);

/* Play has the mouse this frame: no menu, overlay or screen holds it. */
int ReiPan_GameMouseAvailable(void);

/* An action's control this frame: went down, is down, came up.  The wheel is
 * only ever "went down" (one step per frame at most; the rest wait). */
int ReiPan_GameMousePressed(int act);
int ReiPan_GameMouseHeld(int act);
int ReiPan_GameMouseReleased(int act);

#ifdef __cplusplus
}

union SDL_Event;
/* Every window event, from the renderer's pump, after the menu mouse has seen
 * it.  `free_pointer` is ReiPan_InputMenuPointerFree(). */
void ReiPan_GameMouseEvent(const SDL_Event *event, int free_pointer);
#endif

#endif /* REIPAN_GAME_MOUSE_H */
