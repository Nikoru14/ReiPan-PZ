/* ==========================================================================
 *  reipan_game_mouse.cpp  (the mouse in play)
 *
 *  See reipan_game_mouse.h.  Events arrive on the game thread (the renderer's
 *  pump) and the poll runs there too, from the pad read, so nothing here
 *  needs a lock; only switching the pointer capture is done on the window's
 *  thread.
 * ======================================================================== */

#include "reipan_game_mouse.h"

#include "reipan_input.h"
#include "reipan_menu_mouse.h"
#include "../os/reipan_host.h"
#include "../reipan_ui/reipan_ui.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <math.h>
#include <string.h>

namespace
{

/* Radians a pixel of movement turns at sensitivity 1, MikuPan's figure. */
constexpr float kRadPerPixel = 0.0035f;

/* Wheel steps waiting to be played out, at most: a fast flick changes a few
 * films, not a dozen over the next half second. */
constexpr int kWheelQueueMax = 3;

ReiPan_GameMouseSettings g_set;
bool                     g_set_init = false;

/* From the events, between two polls. */
int   g_down[REIPAN_MOUSE_SRC_COUNT];       /* held now */
int   g_pending_press[REIPAN_MOUSE_SRC_COUNT];
int   g_pending_release[REIPAN_MOUSE_SRC_COUNT];
int   g_wheel_up;
int   g_wheel_down;
float g_wheel_travel;
float g_move_x;
float g_move_y;
SDL_WindowID g_window_id;
int   g_focused = 1;

/* Latched by the poll for this frame. */
int   g_pressed[REIPAN_MOUSE_SRC_COUNT];
int   g_released[REIPAN_MOUSE_SRC_COUNT];
int   g_held[REIPAN_MOUSE_SRC_COUNT];
float g_look_x;
float g_look_y;
int   g_available;

/* Pointer capture. */
int g_look_requested;           /* play asked this frame */
int g_captured;

void EnsureSettings()
{
    if (!g_set_init)
    {
        ReiPan_GameMouseDefaults(&g_set);
        g_set_init = true;
    }
}

int SourceOfButton(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:   return REIPAN_MOUSE_SRC_LEFT;
    case SDL_BUTTON_RIGHT:  return REIPAN_MOUSE_SRC_RIGHT;
    case SDL_BUTTON_MIDDLE: return REIPAN_MOUSE_SRC_MIDDLE;
    case SDL_BUTTON_X1:     return REIPAN_MOUSE_SRC_X1;
    case SDL_BUTTON_X2:     return REIPAN_MOUSE_SRC_X2;
    default:                return REIPAN_MOUSE_SRC_NONE;
    }
}

/* Play may take the mouse: no menu has claimed it, the overlay is not using
 * it, the window has the focus. */
int PlayMayTake(int free_pointer)
{
    return free_pointer && ReiPan_MenuMouseClaimed() == 0 && g_focused;
}

struct CaptureRequest
{
    SDL_WindowID id;
    bool         on;
};

void SetCaptureOnMainThread(void *arg)
{
    const CaptureRequest *req = static_cast<const CaptureRequest *>(arg);
    SDL_Window *window = SDL_GetWindowFromID(req->id);
    if (window != nullptr)
    {
        SDL_SetWindowRelativeMouseMode(window, req->on);
    }
}

void SetCapture(int on)
{
    if (on == g_captured || g_window_id == 0)
    {
        return;
    }
    CaptureRequest req{g_window_id, on != 0};
    ReiPan_HostRunOnMainThread(SetCaptureOnMainThread, &req);
    g_captured = on;
    /* What moved while the pointer was free is not a look. */
    g_move_x = 0.0f;
    g_move_y = 0.0f;
}

} // namespace

extern "C" {

void ReiPan_GameMouseDefaults(ReiPan_GameMouseSettings *out)
{
    out->look = 1;
    out->sensitivity = 1.0f;
    out->camera_toggle = 0;
    out->bind[REIPAN_MOUSE_ACT_CAMERA] = REIPAN_MOUSE_SRC_RIGHT;
    out->bind[REIPAN_MOUSE_ACT_SHUTTER] = REIPAN_MOUSE_SRC_LEFT;
    out->bind[REIPAN_MOUSE_ACT_LENS_SHOT] = REIPAN_MOUSE_SRC_MIDDLE;
    out->bind[REIPAN_MOUSE_ACT_FILM_PREV] = REIPAN_MOUSE_SRC_WHEEL_UP;
    out->bind[REIPAN_MOUSE_ACT_FILM_NEXT] = REIPAN_MOUSE_SRC_WHEEL_DOWN;
}

void ReiPan_GameMouseGetSettings(ReiPan_GameMouseSettings *out)
{
    EnsureSettings();
    *out = g_set;
}

void ReiPan_GameMouseSetSettings(const ReiPan_GameMouseSettings *in)
{
    EnsureSettings();
    ReiPan_GameMouseSettings s = *in;
    s.look = s.look ? 1 : 0;
    s.camera_toggle = s.camera_toggle ? 1 : 0;
    if (!isfinite(s.sensitivity))
    {
        s.sensitivity = 1.0f;
    }
    s.sensitivity = s.sensitivity < 0.25f ? 0.25f
                    : (s.sensitivity > 4.0f ? 4.0f : s.sensitivity);
    for (int &b : s.bind)
    {
        if (b < 0 || b >= REIPAN_MOUSE_SRC_COUNT)
        {
            b = REIPAN_MOUSE_SRC_NONE;
        }
    }
    g_set = s;
}

const char *ReiPan_GameMouseSourceName(int src)
{
    static const char *const kNames[REIPAN_MOUSE_SRC_COUNT] = {
        "none", "left", "right", "middle", "x1", "x2", "wheel_up", "wheel_down",
    };
    return src >= 0 && src < REIPAN_MOUSE_SRC_COUNT ? kNames[src] : "none";
}

const char *ReiPan_GameMouseSourceLabel(int src)
{
    static const char *const kLabels[REIPAN_MOUSE_SRC_COUNT] = {
        "Nothing",     "Left button", "Right button", "Middle button",
        "Back button", "Forward button", "Wheel up", "Wheel down",
    };
    return src >= 0 && src < REIPAN_MOUSE_SRC_COUNT ? kLabels[src] : "Nothing";
}

int ReiPan_GameMouseSourceFromName(const char *name)
{
    if (name == nullptr)
    {
        return -1;
    }
    for (int i = 0; i < REIPAN_MOUSE_SRC_COUNT; i++)
    {
        if (SDL_strcasecmp(name, ReiPan_GameMouseSourceName(i)) == 0)
        {
            return i;
        }
    }
    return -1;
}

const char *ReiPan_GameMouseActionLabel(int act)
{
    static const char *const kLabels[REIPAN_MOUSE_ACT_COUNT] = {
        "Raise the Camera Obscura", "Shutter", "Lens shot", "Previous film",
        "Next film",
    };
    return act >= 0 && act < REIPAN_MOUSE_ACT_COUNT ? kLabels[act] : "";
}

void ReiPan_GameMousePoll(void)
{
    EnsureSettings();

    const int free_pointer = ReiPan_InputMenuPointerFree();
    g_available = PlayMayTake(free_pointer);

    /* The capture follows what play asked for last frame, and lets go the
     * moment anything else wants the pointer -- the overlay's bar included,
     * which F1 shows and hides. */
    SetCapture(g_look_requested && g_set.look && g_available &&
               !ReiPanUi::MainMenuBarVisible());
    g_look_requested = 0;

    for (int i = 0; i < REIPAN_MOUSE_SRC_COUNT; i++)
    {
        g_pressed[i] = g_pending_press[i] > 0;
        g_released[i] = g_pending_release[i] > 0;
        g_held[i] = g_down[i];
        g_pending_press[i] = 0;
        g_pending_release[i] = 0;
    }

    /* One wheel step a frame each way, so each is its own press. */
    g_pressed[REIPAN_MOUSE_SRC_WHEEL_UP] = g_wheel_up > 0;
    g_pressed[REIPAN_MOUSE_SRC_WHEEL_DOWN] = g_wheel_down > 0;
    if (g_wheel_up > 0)
    {
        g_wheel_up--;
    }
    if (g_wheel_down > 0)
    {
        g_wheel_down--;
    }

    if (!g_available)
    {
        memset(g_pressed, 0, sizeof(g_pressed));
        memset(g_released, 0, sizeof(g_released));
        memset(g_held, 0, sizeof(g_held));
        g_wheel_up = g_wheel_down = 0;
    }

    const float scale = kRadPerPixel * g_set.sensitivity;
    g_look_x = g_captured ? g_move_x * scale : 0.0f;
    g_look_y = g_captured ? -g_move_y * scale : 0.0f;
    g_move_x = 0.0f;
    g_move_y = 0.0f;
}

void ReiPan_GameMouseRequestLook(void)
{
    g_look_requested = 1;
}

int ReiPan_GameMouseLook(float *yaw, float *pitch)
{
    if (yaw != nullptr)
    {
        *yaw = g_look_x;
    }
    if (pitch != nullptr)
    {
        *pitch = g_look_y;
    }
    return g_captured && (g_look_x != 0.0f || g_look_y != 0.0f);
}

int ReiPan_GameMouseAvailable(void)
{
    return g_available;
}

int ReiPan_GameMousePressed(int act)
{
    EnsureSettings();
    if (act < 0 || act >= REIPAN_MOUSE_ACT_COUNT)
    {
        return 0;
    }
    const int src = g_set.bind[act];
    return src != REIPAN_MOUSE_SRC_NONE && g_pressed[src];
}

int ReiPan_GameMouseHeld(int act)
{
    EnsureSettings();
    if (act < 0 || act >= REIPAN_MOUSE_ACT_COUNT)
    {
        return 0;
    }
    const int src = g_set.bind[act];
    return src != REIPAN_MOUSE_SRC_NONE && g_held[src];
}

int ReiPan_GameMouseReleased(int act)
{
    EnsureSettings();
    if (act < 0 || act >= REIPAN_MOUSE_ACT_COUNT)
    {
        return 0;
    }
    const int src = g_set.bind[act];
    return src != REIPAN_MOUSE_SRC_NONE && g_released[src];
}

}

void ReiPan_GameMouseEvent(const SDL_Event *event, int free_pointer)
{
    if (event == nullptr)
    {
        return;
    }
    switch (event->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.which == SDL_TOUCH_MOUSEID)
        {
            return;
        }
        g_window_id = event->motion.windowID;
        if (g_captured)
        {
            g_move_x += event->motion.xrel;
            g_move_y += event->motion.yrel;
        }
        return;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (event->button.which == SDL_TOUCH_MOUSEID)
        {
            return;
        }
        g_window_id = event->button.windowID;
        const int src = SourceOfButton(event->button.button);
        if (src == REIPAN_MOUSE_SRC_NONE)
        {
            return;
        }
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            /* A click a menu or the overlay took is not play's. */
            if (!PlayMayTake(free_pointer))
            {
                return;
            }
            g_down[src] = 1;
            g_pending_press[src]++;
        }
        else if (g_down[src])
        {
            g_down[src] = 0;
            g_pending_release[src]++;
        }
        return;
    }

    case SDL_EVENT_MOUSE_WHEEL:
    {
        if (event->wheel.which == SDL_TOUCH_MOUSEID || !PlayMayTake(free_pointer))
        {
            return;
        }
        g_window_id = event->wheel.windowID;
        float y = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
            y = -y;
        }
        g_wheel_travel += y;
        while (g_wheel_travel >= 1.0f)
        {
            g_wheel_up = g_wheel_up < kWheelQueueMax ? g_wheel_up + 1 : g_wheel_up;
            g_wheel_travel -= 1.0f;
        }
        while (g_wheel_travel <= -1.0f)
        {
            g_wheel_down = g_wheel_down < kWheelQueueMax ? g_wheel_down + 1 : g_wheel_down;
            g_wheel_travel += 1.0f;
        }
        return;
    }

    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        g_focused = 1;
        g_window_id = event->window.windowID;
        return;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        g_focused = 0;
        /* Let go of everything: the button-up happens in another window. */
        for (int i = 0; i < REIPAN_MOUSE_SRC_COUNT; i++)
        {
            if (g_down[i])
            {
                g_down[i] = 0;
                g_pending_release[i]++;
            }
        }
        g_wheel_up = g_wheel_down = 0;
        g_wheel_travel = 0.0f;
        return;

    default:
        return;
    }
}
