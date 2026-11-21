/* ==========================================================================
 *  reipan_mouse_actions.cpp  (the mouse's buttons in play)
 *
 *  See reipan_mouse_actions.h.  Runs once a pad read, from PadReadFunc,
 *  after reipan_game_mouse.cpp has latched the frame's clicks.
 * ======================================================================== */

#include "reipan/gameplay/reipan_mouse_actions.h"

#include "ingame/ingame_wrk.h"                  // ingame_wrk.plyr_wrk
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_FLG_*
#include "ingame/story_menu.h"                  // IsLockTblOn
#include "main/gphase.h"                        // GetGPhaseCtrl
#include "reipan/io/reipan_game_mouse.h"
#include "system/pad/pad_ctrl.h"                // pad_key

namespace
{

/* The play phases (menu_mouse.c's): Story_Normal, where she walks, and
 * Story_Photo, the shot just taken. */
constexpr int kPhaseNormal = 32;
constexpr int kPhasePhoto = 33;

/* One-shot presses waiting, as pad bits: each is held for one read and then
 * left up for one, so two in a row are two edges. */
u_short g_queue[8];
int     g_queue_len = 0;
u_short g_last_shot = 0;      // held last read by a one-shot

int g_hold_engaged = 0;       // hold mode: raised by this press
int g_hold_release_pending = 0;
int g_shutter_engaged = 0;
int g_film_dir = -1;          // this frame's wheel film step

void Queue(u_int mask)
{
    const u_short bits = (u_short)(mask & 0xffff);
    if (bits != 0 && g_queue_len < (int)(sizeof(g_queue) / sizeof(g_queue[0])))
    {
        g_queue[g_queue_len++] = bits;
    }
}

u_short NextShot()
{
    /* Up for a read after each press. */
    if (g_last_shot != 0 || g_queue_len == 0)
    {
        g_last_shot = 0;
        return 0;
    }
    g_last_shot = g_queue[0];
    g_queue_len--;
    for (int i = 0; i < g_queue_len; i++)
    {
        g_queue[i] = g_queue[i + 1];
    }
    return g_last_shot;
}

int InPlay()
{
    const GPHASE_CTRL *ctrl = GetGPhaseCtrl();
    if (ctrl == nullptr || ctrl->leaf == nullptr)
    {
        return 0;
    }
    return ctrl->leaf->id == kPhaseNormal || ctrl->leaf->id == kPhasePhoto;
}

int FinderUp(const PLYR_WRK *plyr)
{
    return (plyr->flags & PLYR_FLG_800000) != 0;
}

int FinderRaising(const PLYR_WRK *plyr)
{
    return (plyr->flags & PLYR_FLG_400000) != 0;
}

/* Where the pad's raise would be read: ordinary control, nothing locking the
 * player. */
int CanRaise(const PLYR_WRK *plyr)
{
    return plyr->mode == PLYR_MODE_NORMAL &&
           (plyr->flags & (PLYR_FLG_HOLD | PLYR_FLG_1000000)) == 0 &&
           !IsLockTblOn(3) && !IsLockTblOn(4);
}

void Reset()
{
    g_queue_len = 0;
    g_hold_engaged = 0;
    g_hold_release_pending = 0;
    g_shutter_engaged = 0;
}

void CameraButton(const PLYR_WRK *plyr)
{
    ReiPan_GameMouseSettings s;
    ReiPan_GameMouseGetSettings(&s);

    const int pressed = ReiPan_GameMousePressed(REIPAN_MOUSE_ACT_CAMERA);
    const int released = ReiPan_GameMouseReleased(REIPAN_MOUSE_ACT_CAMERA);

    if (s.camera_toggle)
    {
        g_hold_engaged = 0;
        g_hold_release_pending = 0;
        if (!pressed)
        {
            return;
        }
        if (FinderUp(plyr))
        {
            Queue(pad_key[PAD_KEY_FINDER_OFF]);
        }
        else if (!FinderRaising(plyr) && CanRaise(plyr))
        {
            Queue(pad_key[PAD_KEY_FINDER]);
        }
        return;
    }

    /* Hold: up while held. */
    if (pressed && !FinderUp(plyr) && !FinderRaising(plyr) && CanRaise(plyr))
    {
        Queue(pad_key[PAD_KEY_FINDER]);
        g_hold_engaged = 1;
        g_hold_release_pending = 0;
    }
    if (released && g_hold_engaged)
    {
        g_hold_engaged = 0;
        g_hold_release_pending = 1;
    }
    if (g_hold_release_pending)
    {
        if (FinderUp(plyr))
        {
            /* Let go during the raise: down as soon as it is up. */
            Queue(pad_key[PAD_KEY_FINDER_OFF]);
            g_hold_release_pending = 0;
        }
        else if (!FinderRaising(plyr) && g_queue_len == 0 && g_last_shot == 0)
        {
            /* The raise never happened (or it is already down). */
            g_hold_release_pending = 0;
        }
    }
}

} // namespace

extern "C" {

u_short ReiPan_MouseActionPadBits(void)
{
    g_film_dir = -1;

    const PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    if (plyr == nullptr || !InPlay() || !ReiPan_GameMouseAvailable())
    {
        Reset();
        return NextShot();
    }

    CameraButton(plyr);

    u_short held = 0;
    if (FinderUp(plyr))
    {
        /* The shutter, held for as long as the button: a press while the
         * Camera Obscura is up, not one carried over from raising it. */
        if (ReiPan_GameMousePressed(REIPAN_MOUSE_ACT_SHUTTER))
        {
            g_shutter_engaged = 1;
        }
        if (!ReiPan_GameMouseHeld(REIPAN_MOUSE_ACT_SHUTTER))
        {
            g_shutter_engaged = 0;
        }
        if (g_shutter_engaged)
        {
            held |= (u_short)(pad_key[PAD_KEY_06] & 0xffff);
        }

        if (ReiPan_GameMousePressed(REIPAN_MOUSE_ACT_LENS_SHOT))
        {
            Queue(pad_key[PAD_KEY_LENS_SHOT]);
        }
        if (ReiPan_GameMousePressed(REIPAN_MOUSE_ACT_FILM_PREV))
        {
            g_film_dir = 0;
        }
        else if (ReiPan_GameMousePressed(REIPAN_MOUSE_ACT_FILM_NEXT))
        {
            g_film_dir = 1;
        }
    }
    else
    {
        g_shutter_engaged = 0;
    }

    return (u_short)(held | NextShot());
}

int ReiPan_MouseActionFilmTrg(int dir)
{
    return g_film_dir >= 0 && g_film_dir == dir;
}

}
