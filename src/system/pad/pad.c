/* ==========================================================================
 *  system/pad/pad.c
 *
 *  The pad driver (see pad.h). FF3 0x1f0138..0x1f0d78, FF2 system/pad/pad.c's
 *  role rewritten: one PAD_WRK per port with its own connection state machine
 *  (Sony's sample sequence: actuators, analog mode locked, pressure mode),
 *  button/pressure/stick reading and vibration timers. Before it: the C++
 *  heap and a run of chunk-table accessors; after it: Get1Byte..Get4Byte.
 * ======================================================================== */
#include "system/pad/pad.h"

#include <string.h>                     // memset
#include <stdlib.h>                     // abs
#include <libpad.h>
#include "reipan/gameplay/reipan_mouse_actions.h"   // PC: the mouse in play

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@001ef370 (math helpers)
float g3dAtan2f(float fX, float fY);    // FF2 g3dAtan2f(fX, fY)

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

PAD_WRK pad_wrk[2];                     // bss 0x3295c0

// data 0x35b640: the button bit behind each counter / pressure index; the
// first 12 are the pressure-sensitive buttons in report order (right, left,
// up, down, triangle, circle, cross, square, L1, R1, L2, R2), then
// R3, L3, start, select... (FF2 had the same bits in another order).
u_short sce_pad[16] =
{
    0x2000, 0x8000, 0x1000, 0x4000, 0x0010, 0x0020, 0x0040, 0x0080,
    0x0004, 0x0008, 0x0001, 0x0002, 0x0200, 0x0400, 0x0800, 0x0100,
};

#define PAD_ANALOG_DEAD   60            // |stick - 0x80| below this is centred
#define PAD_PRESS_MIN     8             // weaker presses do not count as held

// libpad states and request states (Sony libpad.h values)
#define PAD_STATE_DISCONN   0
#define PAD_STATE_FINDPAD   1
#define PAD_STATE_FINDCTP1  2
#define PAD_STATE_EXECCMD   5
#define PAD_STATE_STABLE    6
#define PAD_REQ_COMPLETE    0
#define PAD_REQ_FAILED      1

static void PadSyncOne(PAD_WRK *pw);
static void PadVibrateUpdate(PAD_WRK *pw);
static void PadReadFunc(PAD_WRK *pw);
static void SetAnlgInfo(PAD_WRK *pw);
static void PadClearCount(PAD_WRK *pw);
static u_short PadStickToDpad(u_short btn, int pad);

// ──────────────────────────────────────────────────────────────────────
// Clear both works (port 0 and 1, slot 0), then open the ports.
// FF3 0x001f0138; FF2 InitPad (pressure, stick and vibration state is new).

void PadOpenAll(void)
{
    PAD_WRK *pw;
    int      i;
    int      j;

    scePadInit(0);

    for (i = 0, pw = pad_wrk; i < 2; i++, pw++)
    {
        memset(pw, 0, sizeof(PAD_WRK));
        pw->port       = i;
        pw->connected  = 1;
        pw->slot       = 0;
        pw->held       = 0;
        pw->old        = 0;
        pw->trg        = 0;
        pw->rpt        = 0;
        pw->rpt_time   = 0;
        pw->stick_dpad = 0;
        pw->step       = PAD_STEP_CHECK_MODE;
        for (j = 0; j < 6; j++)
            pw->act_direct[j] = 0;
        pw->vib_small_time = 0;
        pw->vib_big_time   = 0;
        pw->vib_big_pow    = 0;
        for (j = 0; j < 16; j++)
        {
            pw->cnt[j]     = 0;
            pw->cnt_bak[j] = 0;
        }
        for (j = 0; j < 2; j++)
        {
            pw->an_cnt[1][j] = 0;
            pw->an_cnt[0][j] = 0;
            pw->an_dir[1][j] = 0xff;
            pw->an_dir[0][j] = 0xff;
            pw->an_rot[1][j] = 0.0f;
            pw->an_rot[0][j] = 0.0f;
        }
        for (j = 0; j < 4; j++)
            pw->analog[j] = 0x80;
    }

    for (i = 0, pw = pad_wrk; i < 2; i++, pw++)
        scePadPortOpen(pw->port, pw->slot, pw->dma_buf);
}

// ──────────────────────────────────────────────────────────────────────
// Per frame: step and read both pads, then their vibration.
// FF3 0x001f02a0; FF2 PadSyncCallback's loop.

void PadReadAll(void)
{
    int i;

    // PC: FF3 runs this with interrupts disabled (EIntr's old state, DIntr
    // after); nothing interrupts the host thread here.
    // callcheck: EIntr, DIntr
    for (i = 0; i < 2; i++)
    {
        PadSyncOne(&pad_wrk[i]);
        PadVibrateUpdate(&pad_wrk[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// One pad: track the connection and walk it to analog + pressure mode, then
// read it every frame (step 99). Not ready (or the state machine restarted)
// = idle buttons and no vibration.
// FF3 0x001f0318; per-pad part of FF2 PadSyncCallback, steps from Sony's
// sample code.

static void PadSyncOne(PAD_WRK *pw)
{
    int state;
    int id;
    int ext;
    int req;
    int j;

    state = scePadGetState(pw->port, pw->slot);
    if (state == PAD_STATE_DISCONN)
    {
        pw->connected = 0;
        pw->step      = PAD_STEP_CHECK_MODE;
        PadClearCount(pw);
    }
    else
    {
        pw->connected = 1;
    }
    if (state == PAD_STATE_FINDPAD)
    {
        pw->step          = PAD_STEP_CHECK_MODE;
        pw->act_direct[0] = 0;
        pw->act_direct[1] = 0;
        PadClearCount(pw);
    }
    if (pw->step != PAD_STEP_READY)
    {
        pw->act_direct[0] = 0;
        pw->act_direct[1] = 0;
        PadClearCount(pw);
    }

    switch (pw->step)
    {
    case PAD_STEP_CHECK_MODE:
        if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
            return;
        id = scePadInfoMode(pw->port, pw->slot, InfoModeCurID, 0);
        if (id == 0)
            return;
        ext = scePadInfoMode(pw->port, pw->slot, InfoModeCurExID, 0);
        if (ext > 0)
            id = ext;
        pw->id = 0;
        if (id != 4 && id != 7)                 // not analog / DualShock 2
        {
            pw->step = PAD_STEP_READY;
            return;
        }
        if (scePadInfoAct(pw->port, pw->slot, -1, 0) == 0)
        {
            pw->step = PAD_STEP_READY;
            return;
        }
        pw->act_align[0] = 0;
        pw->act_align[1] = 1;
        for (j = 2; j < 6; j++)
            pw->act_align[j] = 0xff;
        for (j = 0; j < 6; j++)
            pw->act_direct[j] = 0;
        if (scePadSetActAlign(pw->port, pw->slot, pw->act_align) == 1)
            pw->step = PAD_STEP_SET_MAIN;
        return;

    case PAD_STEP_SET_MAIN:
        if (scePadGetState(pw->port, pw->slot) == PAD_STATE_EXECCMD)
            return;
        if (scePadSetMainMode(pw->port, pw->slot, 1, 3) == 1)
            pw->step = PAD_STEP_WAIT_MAIN;
        else
            pw->step = PAD_STEP_CHECK_MODE;
        return;

    case PAD_STEP_WAIT_MAIN:
        req = scePadGetReqState(pw->port, pw->slot);
        if (req == PAD_REQ_FAILED)
            pw->step = PAD_STEP_CHECK_MODE;
        else if (req == PAD_REQ_COMPLETE)
            pw->step = PAD_STEP_CHECK_ID;
        return;

    case PAD_STEP_CHECK_ID:
        id = scePadInfoMode(pw->port, pw->slot, InfoModeCurID, 0);
        if (id == 0)
            return;
        ext = scePadInfoMode(pw->port, pw->slot, InfoModeCurExID, 0);
        if (ext > 0)
            id = ext;
        pw->step = (id == 7) ? PAD_STEP_PRESS_MODE : PAD_STEP_READY;
        return;

    case PAD_STEP_PRESS_MODE:
        if (scePadInfoPressMode(pw->port, pw->slot) != 1)
        {
            pw->step = PAD_STEP_READY;
            return;
        }
        if (scePadEnterPressMode(pw->port, pw->slot) == 1)
            pw->step = PAD_STEP_WAIT_PRESS;
        return;

    case PAD_STEP_WAIT_PRESS:
        // Reads in the same frame whatever the request state says.
        req = scePadGetReqState(pw->port, pw->slot);
        if (req == PAD_REQ_COMPLETE)
            pw->step = PAD_STEP_READY;
        else if (req == PAD_REQ_FAILED)
            pw->step = PAD_STEP_PRESS_MODE;
        break;

    case PAD_STEP_READY:
        break;

    default:
        return;
    }

    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
    {
        pw->act_direct[0] = 0;
        pw->act_direct[1] = 0;
        PadClearCount(pw);
        return;
    }

    PadReadFunc(pw);
    if ((pw->act_direct[0] & 0x80) == 0)
        return;

    // A vibration request: send it, then keep a stop request pending while
    // either motor was on.
    pw->act_direct[0] &= 1;
    scePadSetActDirect(pw->port, pw->slot, pw->act_direct);
    if (pw->act_direct[0] != 0 || pw->act_direct[1] != 0)
    {
        pad_wrk[pw->port].act_direct[0] = 0x80;
        pad_wrk[pw->port].act_direct[1] = 0;
    }
    else
    {
        pw->act_direct[1] = 0;
        pw->act_direct[0] = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Count the vibration timers down into the next scePadSetActDirect data:
// act_direct[0] = small motor on/off, [1] = big motor power, bit 7 of [0] =
// send. A time of -1 runs until changed.
// FF3 0x001f06f0; new in FF3 (FF2 VibrateRequest* wrote act_direct directly).

static void PadVibrateUpdate(PAD_WRK *pw)
{
    PAD_WRK *dst = &pad_wrk[pw->port];

    if (pw->vib_small_time == -1)
    {
        dst->act_direct[0] = 0x81;
    }
    else if (pw->vib_small_time > 0)
    {
        pw->vib_small_time--;
        dst->act_direct[0] = 0x81;
    }
    else
    {
        pw->vib_small_time = 0;
        dst->act_direct[0] = 0x80;
    }

    if (pw->vib_big_time == -1)
    {
        dst->act_direct[1] = (u_char)pw->vib_big_pow;
    }
    else if (pw->vib_big_time < 1)
    {
        pw->vib_big_time   = 0;
        pw->vib_big_pow    = 0;
        dst->act_direct[1] = 0;
    }
    else
    {
        dst->act_direct[1] = (u_char)pw->vib_big_pow;
        pw->vib_big_time--;
    }
    dst->act_direct[0] |= 0x80;
}

// ──────────────────────────────────────────────────────────────────────
// Read one report: buttons (a pressure button under PAD_PRESS_MIN does not
// count), sticks, then trigger / repeat / per-button hold counters.
// FF3 0x001f07e8; FF2 PadReadFunc (pressure filter, stick-as-d-pad and the
// DualShock 2 id lock are new; counters do not saturate at 0xffff).

static void PadReadFunc(PAD_WRK *pw)
{
    u_char  rdata[32];
    u_short btn;
    u_short trg;
    int     i;

    pw->old = pw->held;

    if (scePadRead(pw->port, pw->slot, rdata) == 0 || rdata[0] != 0)
    {
        pw->act_direct[0] = 0;
    }
    else if (pw->id == 0 || pw->id == (char)rdata[1])
    {
        pw->id   = (char)rdata[1];
        pw->held = (u_short)~((rdata[2] << 8) | rdata[3]);

        if ((rdata[1] & 0xf0) == 0x70)              // analog
        {
            for (i = 0; i < 4; i++)
                pw->analog[i] = rdata[4 + i];
        }
        if (pw->id == 0x79)                         // DualShock 2: pressure
        {
            for (i = 0; i < 12; i++)
            {
                pw->press[i] = rdata[8 + i];
                if (pw->press[i] < PAD_PRESS_MIN)
                {
                    pw->press[i] = 0;
                    pw->held &= ~sce_pad[i];
                }
            }
            SetAnlgInfo(pw);
        }

        if (pad_wrk[pw->port].stick_dpad != 0)
            pw->held = PadStickToDpad(pw->held, pw->port);

        // PC: the mouse's buttons in play press the game's own buttons
        // (raise the Camera Obscura, the shutter, the lens shot), before
        // the edges are taken so they make presses like the pad's.
        if (pw->port == 0)
            pw->held |= ReiPan_MouseActionPadBits();

        btn     = pw->held;
        trg     = btn & (pw->old ^ btn);
        pw->rpt = trg;
        pw->trg = trg;
        if (btn == pw->old)
        {
            if (++pw->rpt_time > 9)
            {
                pw->rpt      = btn;
                pw->rpt_time = 5;
            }
        }
        else
        {
            pw->rpt_time = 0;
        }

        for (i = 0; i < 16; i++)
        {
            pw->cnt_bak[i] = pw->cnt[i];
            if ((sce_pad[i] & btn) == 0)
                pw->cnt[i] = 0;
            else
                pw->cnt[i]++;
        }
        return;
    }
    else
    {
        // Another controller type was plugged in: renegotiate.
        pw->step          = PAD_STEP_CHECK_MODE;
        pw->act_direct[0] = 0;
    }

    pw->act_direct[1] = 0;
    PadClearCount(pw);
}

// ──────────────────────────────────────────────────────────────────────
// Per stick: keep last frame's values, then angle and 8-way direction (0..7,
// 0xff in the dead zone) and how many frames it has stayed the same.
// FF3 0x001f09e8; FF2 SetAnlgInfo (g3dAtan2f instead of atan2f).

static void SetAnlgInfo(PAD_WRK *pw)
{
    float  rot;
    short  pad_x;
    int    pad_y;
    int    dir;
    u_char dir_old;
    u_char x;
    u_char y;
    u_int  i;

    for (i = 0; i < 2; i++)
    {
        dir_old          = pw->an_dir[0][i];
        pw->an_cnt[1][i] = pw->an_cnt[0][i];
        pw->an_dir[1][i] = dir_old;
        pw->an_rot[1][i] = pw->an_rot[0][i];

        if (i == 0)
        {
            x = pw->analog[2];
            y = pw->analog[3];
        }
        else
        {
            x = pw->analog[0];
            y = pw->analog[1];
        }

        pad_x = (short)(x - 0x80);
        pad_y = 0x80 - y;
        if (abs(pad_x) < PAD_ANALOG_DEAD && abs(pad_y) < PAD_ANALOG_DEAD)
        {
            pw->an_dir[0][i] = 0xff;
            pw->an_cnt[0][i] = 0;
            continue;
        }

        rot = g3dAtan2f((float)pad_x, (float)pad_y);
        dir = (int)((rot + 3.1415927f + 0.39269909f) / 0.78539819f) % 8;  // data 0x370eb4..
        pw->an_dir[0][i] = (u_char)(dir + 4);
        if (pw->an_dir[0][i] > 7)
            pw->an_dir[0][i] = (u_char)(dir - 4);
        pw->an_rot[0][i] = rot;

        if (pw->an_dir[0][i] == dir_old)
            pw->an_cnt[0][i]++;
        else
            pw->an_cnt[0][i] = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Idle state: no buttons, counters cleared, sticks centred.
// FF3 0x001f0bb8; FF2 PadClearCount (on the work, not an index; sticks rest
// at 0x7f here but at 0x80 after PadOpenAll).

static void PadClearCount(PAD_WRK *pw)
{
    int i;

    pw->held     = 0;
    pw->old      = 0;
    pw->trg      = 0;
    pw->rpt      = 0;
    pw->rpt_time = 0;
    for (i = 0; i < 16; i++)
    {
        pw->cnt_bak[i] = 0;
        pw->cnt[i]     = 0;
    }
    for (i = 0; i < 2; i++)
    {
        pw->an_cnt[1][i] = 0;
        pw->an_cnt[0][i] = 0;
        pw->an_dir[1][i] = 0xff;
        pw->an_dir[0][i] = 0xff;
        pw->an_rot[1][i] = 0.0f;
        pw->an_rot[0][i] = 0.0f;
    }
    for (i = 0; i < 4; i++)
        pw->analog[i] = 0x7f;
}

// ──────────────────────────────────────────────────────────────────────
// Left stick pushed past 0x3b / 0xbb also counts as the d-pad.
// FF3 0x001f0c60; new in FF3 (FF2 PadAnalogMain had a separate repeat flag).

static u_short PadStickToDpad(u_short btn, int pad)
{
    if (pad_wrk[pad].analog[3] < 0x3b)
        btn |= 0x1000;                          // up
    if (pad_wrk[pad].analog[3] > 0xbb)
        btn |= 0x4000;                          // down
    if (pad_wrk[pad].analog[2] > 0xbb)
        btn |= 0x2000;                          // right
    if (pad_wrk[pad].analog[2] < 0x3b)
        btn |= 0x8000;                          // left
    return btn;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f0cc8; new in FF3 (used by common/ol_load.c).

u_short PadGetSceButton(int i)
{
    return sce_pad[i];
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the controller reports pressure mode (DualShock 2 ready); the
// boot pad check, IngameMain and LayoutMsg treat 0 as a controller error.
// FF3 0x001f0ce0; new in FF3.

int PadIsPressMode(int pad)
{
    return scePadInfoPressMode(pad_wrk[pad].port, pad_wrk[pad].slot) == 1;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when libpad reports the pad stable (6) or FindCTP1 (2).
// FF3 0x001f0d18; FF2 GetPadStateStable.

int PadIsStable(int pad)
{
    if (scePadGetState(pad_wrk[pad].port, pad_wrk[pad].slot) == PAD_STATE_STABLE)
        return 1;
    return scePadGetState(pad_wrk[pad].port, pad_wrk[pad].slot) == PAD_STATE_FINDCTP1;
}
