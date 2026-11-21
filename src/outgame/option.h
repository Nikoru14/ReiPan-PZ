/* ==========================================================================
 *  outgame/option.h
 *
 *  The option work (FF3 OptionWrk singleton, 0x41e580), the option helpers
 *  other files call (layout text positioning, cursor stepping, fade-layer
 *  wrappers) and phase 7 (Option_Main, the container of the option screens
 *  19..23). FF3 0x224618..0x2254b8.
 * ======================================================================== */
#ifndef _OUTGAME_OPTION_H
#define _OUTGAME_OPTION_H

#include <stddef.h>                 // offsetof
#include "sdk/scetypes.h"

class CWindowMsg;                      // common/WindowMsg.h

// One set of option settings, 0x14 bytes (OptionInit writes it; option
// screens compare and copy it as five words). Fields without a known role
// keep their offset as name.
struct OPTION_DAT                   /* 0x14 */
{
    /* 0x00 */ u_char      b00;         // the brightness level 0..11, 6 by default (phase 21) (BrightnessAdjustmentFilterDraw)
    /* 0x01 */ signed char vol_ch1;     // 0..19 (default 10): sound channel 1 (OptionGetSndVolume1), "BG music" on phase 20
    /* 0x02 */ signed char vol_ch0;     // 0..19 (default 10): sound channel 0 (OptionGetSndVolume0), "Sound effects" on phase 20
    /* 0x03 */ u_char      _03;
    /* 0x04 */ int         vibration;   // 1 by default; 0: VibrateRequest1/2 do nothing
    /* 0x08 */ signed char pad_cfg;     // button layout 0..2 = Type A..C (PadKeyUpdate; phase 23)
    /* 0x09 */ u_char      b09;         // 1: the look stick's y is inverted (GetStickRaw mode 5); phase 22 row "Point of view"
    /* 0x0a */ u_char      _0a[2];
    /* 0x0c */ int         subtitle;    // passed to SubTitleMain as its on switch; default 1
    /* 0x10 */ u_char      mono;        // SndSetMonoMode (FF2's stereo/mono setting)
    /* 0x11 */ u_char      b11;         // 1: d-pad mashing counts (PlyrGetMashInput); a change calls FUN_00242d50 (PlyrCtrlOptCheck); phase 22 row "Player Controls" (0 3rd person, 1 1st person)
    /* 0x12 */ u_char      b12;         // stick / d-pad pressure choice of GetStickRaw and GetDirTrg (unit_ctl.c); phase 22 row "Change film"
    /* 0x13 */ u_char      b13;         // non-zero: in the viewfinder the right stick aims and the left steps (GetStickRaw 5 / 6; walking is always the left); phase 22 row "Analog stick"
};
static_assert(sizeof(OPTION_DAT) == 0x14, "OPTION_DAT is 0x14 bytes");
static_assert(offsetof(OPTION_DAT, vol_ch1) == 0x01, "OPTION_DAT.vol_ch1");
static_assert(offsetof(OPTION_DAT, vol_ch0) == 0x02, "OPTION_DAT.vol_ch0");
static_assert(offsetof(OPTION_DAT, vibration) == 0x04, "OPTION_DAT.vibration");
static_assert(offsetof(OPTION_DAT, pad_cfg) == 0x08, "OPTION_DAT.pad_cfg");
static_assert(offsetof(OPTION_DAT, subtitle) == 0x0c, "OPTION_DAT.subtitle");
static_assert(offsetof(OPTION_DAT, mono) == 0x10, "OPTION_DAT.mono");
static_assert(offsetof(OPTION_DAT, b13) == 0x13, "OPTION_DAT.b13");

// The option work: the live settings and two saved copies. Slot 0 is taken
// when an option screen opens (OptionSubInitStep) and restored when it is
// cancelled; slot 1 when phase 7 starts (init_Option_Main), restored by the
// top menu's "discard changes" choice. Nothing past 0x3c has been seen read;
// the singleton may be bigger.
struct OPTION_WRK                   /* 0x3c (at least) */
{
    /* 0x00 */ OPTION_DAT now;
    /* 0x14 */ OPTION_DAT saved[2];
};
static_assert(sizeof(OPTION_WRK) == 0x3c, "OPTION_WRK is 0x3c bytes");
static_assert(offsetof(OPTION_WRK, now.subtitle) == 0x0c, "OPTION_WRK.now.subtitle");
static_assert(offsetof(OPTION_WRK, saved) == 0x14, "OPTION_WRK.saved");

// FF3 0x002b0310 (getter, emitted with the singletons).
OPTION_WRK *GetOptionWrk(void);

int  OptionWrkSaveGet(void **addr);                    // save block 17: {GetOptionWrk(), 0x14}
void OptionInit(OPTION_WRK *opt);                       // defaults of opt->now
int  OptionIsSameAsSaved(OPTION_WRK *opt, int slot);    // now == saved[slot]
void OptionLoadSaved(OPTION_WRK *opt, int slot);        // now = saved[slot]

void OptionApplySound(void);            // both volumes and mono mode to the sound layer
int  OptionGetSndVolume0(void);         // 0..256 (vol_ch0 * 256 / 19)
int  OptionGetSndVolume1(void);         // 0..256 (vol_ch1 * 256 / 19)

void OptionMenuCsrInit(void);           // menu cursors 0x13, 0x14, 0x16, 0x17
void OptionMenuCsrRelease(void);
void OptionBgStart(void);
int  OptionBgLoadWait(void);
int  OptionBgIsReady(void);
void OptionBgStop(void);
int  OptionBgIsStopped(void);
void OptionRelease(void);               // background + the option layout

// The option layout: Bp file OPTION_LAYOUT_FILE + GetLanguage().
#define OPTION_LAYOUT_FILE 0x1255

// Option-screen phase helpers (the screens 19..23 run them from init/end).
// Both return 1 when done.
int OptionSubInitStep(void (*on_ready)(void), void (*on_wait)(void), int file, const char *label);
int OptionSubEndStep(void (*on_wait)(void), int file, const char *label);

// Shared by many menus (30 / 25 / 5 / 13 callers).
void LayoutItemAddPos(float *pos, const char *label, int file, int *rect);
void PrintMsgPos(int msg0, int msg1, float *pos, int a3, int a4, int a5, int a6);
int  CsrMoveUpDownSe(int pos, int min, int count, u_short mask_inc, u_short mask_dec,
                     int wrap, int se);
void CmnFadeLayerShowItem(int label, int handle, int id);
void CmnFadeLayerSwitchItem(int label);
void CmnFadeLayerTermItem(void);

// Phase 7, Option_Main (state shared with the option screens' files).
extern int      option_cancel;          // sdata 0x3717c8: 1 = restore saved[0] when a screen ends
extern int      option_from_title;      // sdata 0x3717cc: phase 7 was entered from Title_Menu (17)
extern int      option_from_phase;      // sdata 0x3717d0: the phase that opened phase 7
extern CWindowMsg *option_sel_win;         // sdata 0x3717d8

int  init_Option_Main(void);
int  one_Option_Main(void);
int  end_Option_Main(void);
void pre_Option_Main(void);
void after_Option_Main(void);

#endif /* _OUTGAME_OPTION_H */
