/* ==========================================================================
 *  system/pad/vib_pulse.c
 *
 *  The pulse (see vib_pulse.h). FF3-only, FF3 0x1e75f0..0x1e7878.
 * ======================================================================== */
#include "system/pad/vib_pulse.h"

#include <math.h>                       // sqrtf

#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "outgame/option.h"             // GetOptionWrk
#include "reipan/gameplay/reipan_haptic_cues.h"     // PC: what the rumble is for
#include "system/pad/pad.h"             // pad_wrk

struct VIB_PULSE;

typedef int (*VIB_PULSE_FUNC)(VIB_PULSE *vp);

// The beat. Off: gap counts down to 0, then reloads gap, power and on.
// On: on counts down, the SE plays on its first frame and the motor runs
// every frame of it. FF3 bss 0x3ef150; layout: host -- func is a pointer
// (EE offsets in the comments).
struct VIB_PULSE
{
    /* 0x00 */ VIB_PULSE_FUNC func;     // per frame; 0 stops the beat
    /* 0x04 */ u_short gap_cnt;         // frames left of the gap
    /* 0x06 */ u_short gap_time;
    /* 0x08 */ u_short on_cnt;          // frames left of the beat
    /* 0x0a */ u_short on_time;
    /* 0x0c */ u_char  power;           // this beat's motor power
    /* 0x0d */ u_char  power_req;       // the next beat's
};

// VibPulseDistFunc's parameters. FF3 bss 0x3ef160; layout: host (pointers).
struct VIB_PULSE_DIST
{
    /* 0x00 */ float *p0;
    /* 0x04 */ float *p1;
    /* 0x08 */ float  dist0;            // t = 0 here
    /* 0x0c */ float  dist1;            // t = 1 here
};

#define VIB_PULSE_SE        0x1a9b0009
#define VIB_PULSE_GAP       90          // frames between beats at t = 1 (rodata 0x370d20)
#define VIB_PULSE_ON        18          // frames of a beat
#define VIB_PULSE_POWER     0x7f        // the fixed beat's power

static VIB_PULSE      vib_pulse;        // bss 0x3ef150
static VIB_PULSE_DIST vib_pulse_dist;   // bss 0x3ef160

// FF3 0x001e75f0
void VibPulseClear(void)
{
    vib_pulse.func = nullptr;
    vib_pulse.gap_time = 0;
    vib_pulse.gap_cnt = 0;
    vib_pulse.power_req = 0;
    vib_pulse.on_cnt = 0;
    vib_pulse.on_time = 0;
    vib_pulse.power = 0;
}

// FF3 0x001e7618
static void VibPulseSetFunc(VIB_PULSE_FUNC func)
{
    VibPulseClear();
    vib_pulse.func = func;
}

// FF3 reads the vibration switch through player.c's cached option pointer
// (sdata 0x372a04, filled by GetOptionWrk on first use); the port asks
// GetOptionWrk, the same pointer.
// FF3 0x001e7648
void VibPulseMain(void)
{
    VIB_PULSE *vp = &vib_pulse;

    if (vp->func == nullptr || vp->func(vp) == 0)
        return;

    if (vp->on_cnt == 0)
    {
        if (vp->gap_cnt == 0)
        {
            vp->gap_cnt = vp->gap_time;
            vp->power = vp->power_req;
            vp->on_cnt = vp->on_time;
        }
        else
        {
            vp->gap_cnt--;
        }
        return;
    }

    if (vp->on_cnt-- == vp->on_time)
    {
        SndSePlay(VIB_PULSE_SE, 100, 0x1000, 0, nullptr, 0);
        ReiPan_HapticCue(REIPAN_CUE_HEARTBEAT, (float)vp->power / 255.0f);  // PC
    }

    if (GetOptionWrk()->now.vibration != 0)
    {
        pad_wrk[0].vib_big_pow = vp->power;
        pad_wrk[0].vib_big_time = 1;
    }
}

// The fixed beat, then, with both points, gap and power by their xyz
// distance. The clamp tests dist1 first, so it only works for dist0 <
// dist1; peeping passes (1200, 120) and no points. Returns 1.
// FF3 0x001e7730 (Ghidra had it inside VibPulseMain's body)
static int VibPulseDistFunc(VIB_PULSE *vp)
{
    VIB_PULSE_DIST *pd = &vib_pulse_dist;
    float dx, dy, dz, d, t;

    vp->gap_time = VIB_PULSE_GAP;
    vp->on_time = VIB_PULSE_ON;
    vp->power_req = VIB_PULSE_POWER;

    if (pd->p0 != nullptr && pd->p1 != nullptr)
    {
        dx = pd->p0[0] - pd->p1[0];
        dy = pd->p0[1] - pd->p1[1];
        dz = pd->p0[2] - pd->p1[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);

        if (d > pd->dist1)
            d = pd->dist1;
        else if (d < pd->dist0)
            d = pd->dist0;
        t = (d - pd->dist0) / (pd->dist1 - pd->dist0);

        vp->gap_time = (int)(t * 90.0f);
        vp->power_req = (int)((1.0f - t) * 128.0f + 127.0f);
    }
    return 1;
}

// FF3 0x001e7838
void VibPulseReqDist(float dist0, float dist1, float *p0, float *p1)
{
    vib_pulse_dist.p0 = p0;
    vib_pulse_dist.p1 = p1;
    vib_pulse_dist.dist1 = dist1;
    vib_pulse_dist.dist0 = dist0;
    VibPulseSetFunc(VibPulseDistFunc);
}

// FF3 0x1e7870 is an empty void function with no reference (no jal, data
// word or address pair); not reconstructed.
