/* ==========================================================================
 *  reipan_haptic_cues.cpp  (what the game's rumble is for)
 *
 *  See reipan_haptic_cues.h.
 * ======================================================================== */

#include "reipan/gameplay/reipan_haptic_cues.h"

#include "outgame/option.h"                     // GetOptionWrk

extern "C" {

void ReiPan_HapticCue(int cue, float strength)
{
    if (GetOptionWrk()->now.vibration != 0)
    {
        ReiPan_HapticsCue(cue, strength);
    }
}

}
