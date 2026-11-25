/* ==========================================================================
 *  graphics/scene/scene_effect.c
 *
 *  The scene-effect controller (FF2 scene_effect.c, from MioPan
 *  graphics/scene/scene_effect.c; FF3 0x254018..0x256da8): the interpreter
 *  of a scene's effect script, the scene's third file (scene_no * 3 +
 *  SCENE0010M_PSS + 2). Every cutscene post-process -- dither, blur,
 *  deform, focus, contrast, nega, frame fade, cross fade, screen fades,
 *  lights, heat haze, model fades, particle deforms, pad vibration and
 *  FF3's texture-frame ramps, room glow and enemy auras -- arrives as a
 *  record and is either fired at once or parked in a small control that
 *  re-feeds the effect layer every frame.
 *
 *  The script:
 *    SCENE_EFFECT_HEADER { TableNum, TimingTableOffset, VibrationDataOffset }
 *    at TimingTableOffset: SCENE_EFFECT_TABLE[TableNum] of { Frame, Offset }
 *    at each Offset: a run of records, each opening with a short FuncNo,
 *                    dispatched through SceneEffectFunc until FuncNo 0
 *    at VibrationDataOffset: SCENE_EFFECT_VIBRATION2[] while FuncNo == 15
 *
 *  FF3 against FF2: the screen effect ids are renumbered (effect.h), so the
 *  parked ids are dither 2, blur 3, deform 4, focus 5, overlap 6, frame
 *  fade 7, nega 10, contrast 0xb..0xd; the debug gates (SceneTestEffectFlgGet)
 *  and the printf are gone; models are found by (obj_no, index) pairs; the
 *  enemy aura no longer asks for an effect (the man model draw reads
 *  SCN_MDL's aura fields instead); torches are gone (the list is still
 *  initialised); the lens flare moved into the man model draw
 *  (SceneEffectLenzFlareExec); FuncNo 27..29 are new.
 * ======================================================================== */
#include "graphics/scene/scene_effect.h"

#include <stddef.h>                     // offsetof

#include "common/SingleLinkList.h"      // SINGLE_LINK_LIST, SingleLinkList*
#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "common/vu0math.h"             // Vu0Sinf
#include "graphics/effect/effect.h"     // SetEffects, ResetEffects, CutEffects
#include "graphics/effect/effect_obj.h" // EffKind1DReq4 / Release, EffRenzFlareDraw, EffRoomGlow*
#include "graphics/effect/effect_oth.h" // ScreenFlash*, ScreenFadeTo*, EffectMonochromeSet
#include "graphics/effect/effect_sub.h" // Get2PDist3D2, Get2PosRot
#include "graphics/graph3d/gra3dMisc.h"     // gra3dPrelightScene
#include "graphics/scene/scene.h"       // SCENE_CTRL, SCN_MDL, SceneManModel*, SceneChange*LightParameter
#include "graphics/scene/scene_dat.h"   // ScenePlayCtrlGet
#include "ingame/ingame_effect.h"       // IgEffectHandLightFlarePosRot
#include "ingame/plyr/acs_model.h"      // AcsModel (SCN_MDL.efct)
#include "outgame/option.h"             // GetOptionWrk
#include "sdk/libvu0.h"                 // sceVu0AddVector
#include "sdk/scetypes.h"
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetPALMode
#include "system/pad/pad.h"             // pad_wrk
#include "reipan/gameplay/reipan_haptic_cues.h"     // PC: what the rumble is for

// ──────────────────────────────────────────────────────────────────────
// The records (FF2's types; FF3's changes noted). Each opens with its
// FuncNo, so a handler steps the cursor past itself.
// ──────────────────────────────────────────────────────────────────────

struct SCENE_EFFECT_HEADER              /* 0xc */
{
    /* 0x0 */ int   TableNum;
    /* 0x4 */ u_int TimingTableOffset;
    /* 0x8 */ u_int VibrationDataOffset;
};
static_assert(sizeof(SCENE_EFFECT_HEADER) == 0xc, "SCENE_EFFECT_HEADER");
static_assert(offsetof(SCENE_EFFECT_HEADER, VibrationDataOffset) == 0x8, "SCENE_EFFECT_HEADER.VibrationDataOffset");

struct SCENE_EFFECT_TABLE               /* 0x8 */
{
    /* 0x0 */ int Frame;
    /* 0x4 */ int Offset;
};
static_assert(sizeof(SCENE_EFFECT_TABLE) == 0x8, "SCENE_EFFECT_TABLE");
static_assert(offsetof(SCENE_EFFECT_TABLE, Offset) == 0x4, "SCENE_EFFECT_TABLE.Offset");

struct SCENE_EFFECT_OFF                 /* 0x4: FuncNo 1 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short EffectId;           // the OFF selector, not the parked id
};
static_assert(sizeof(SCENE_EFFECT_OFF) == 0x4, "SCENE_EFFECT_OFF");
static_assert(offsetof(SCENE_EFFECT_OFF, EffectId) == 0x2, "SCENE_EFFECT_OFF.EffectId");

struct SCENE_EFFECT_FOCUSDEPTH          /* 0x4: FuncNo 2 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short Type;
};
static_assert(sizeof(SCENE_EFFECT_FOCUSDEPTH) == 0x4, "SCENE_EFFECT_FOCUSDEPTH");
static_assert(offsetof(SCENE_EFFECT_FOCUSDEPTH, Type) == 0x2, "SCENE_EFFECT_FOCUSDEPTH.Type");

struct SCENE_EFFECT_DITHER              /* 0x14: FuncNo 3 */
{
    /* 0x00 */ short FuncNo;
    /* 0x02 */ short Type;
    /* 0x04 */ short StartSpeed;
    /* 0x06 */ short EndSpeed;
    /* 0x08 */ short StartAlpha;
    /* 0x0a */ short EndAlpha;
    /* 0x0c */ short AlphaMax;
    /* 0x0e */ short ColorMax;
    /* 0x10 */ short Time;
    /* 0x12 */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_DITHER) == 0x14, "SCENE_EFFECT_DITHER");
static_assert(offsetof(SCENE_EFFECT_DITHER, StartSpeed) == 0x4, "SCENE_EFFECT_DITHER.StartSpeed");
static_assert(offsetof(SCENE_EFFECT_DITHER, StartAlpha) == 0x8, "SCENE_EFFECT_DITHER.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_DITHER, AlphaMax) == 0xc, "SCENE_EFFECT_DITHER.AlphaMax");
static_assert(offsetof(SCENE_EFFECT_DITHER, Time) == 0x10, "SCENE_EFFECT_DITHER.Time");

struct SCENE_EFFECT_BLUR                /* 0x14: FuncNo 4 */
{
    /* 0x00 */ short FuncNo;
    /* 0x02 */ short Type;              // FF3: not read (always blur type 1)
    /* 0x04 */ short StartAlpha;
    /* 0x06 */ short EndAlpha;
    /* 0x08 */ short StartScale;
    /* 0x0a */ short EndScale;
    /* 0x0c */ short StartRot;
    /* 0x0e */ short EndRot;
    /* 0x10 */ short Time;
    /* 0x12 */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_BLUR) == 0x14, "SCENE_EFFECT_BLUR");
static_assert(offsetof(SCENE_EFFECT_BLUR, StartAlpha) == 0x4, "SCENE_EFFECT_BLUR.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_BLUR, StartScale) == 0x8, "SCENE_EFFECT_BLUR.StartScale");
static_assert(offsetof(SCENE_EFFECT_BLUR, StartRot) == 0xc, "SCENE_EFFECT_BLUR.StartRot");
static_assert(offsetof(SCENE_EFFECT_BLUR, Time) == 0x10, "SCENE_EFFECT_BLUR.Time");

struct SCENE_EFFECT_DEFORM              /* 0xc: FuncNo 5 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short Type;
    /* 0x4 */ short StartRate;
    /* 0x6 */ short EndRate;
    /* 0x8 */ short Time;
    /* 0xa */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_DEFORM) == 0xc, "SCENE_EFFECT_DEFORM");
static_assert(offsetof(SCENE_EFFECT_DEFORM, StartRate) == 0x4, "SCENE_EFFECT_DEFORM.StartRate");
static_assert(offsetof(SCENE_EFFECT_DEFORM, Time) == 0x8, "SCENE_EFFECT_DEFORM.Time");

struct SCENE_EFFECT_FOCUS               /* 0x8: FuncNo 6 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short StartRate;
    /* 0x4 */ short EndRate;
    /* 0x6 */ short Time;
};
static_assert(sizeof(SCENE_EFFECT_FOCUS) == 0x8, "SCENE_EFFECT_FOCUS");
static_assert(offsetof(SCENE_EFFECT_FOCUS, Time) == 0x6, "SCENE_EFFECT_FOCUS.Time");

struct SCENE_EFFECT_CONTRAST            /* 0x10: FuncNo 7 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short Type;
    /* 0x4 */ short StartColor;
    /* 0x6 */ short EndColor;
    /* 0x8 */ short StartAlpha;
    /* 0xa */ short EndAlpha;
    /* 0xc */ short Time;
    /* 0xe */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_CONTRAST) == 0x10, "SCENE_EFFECT_CONTRAST");
static_assert(offsetof(SCENE_EFFECT_CONTRAST, StartColor) == 0x4, "SCENE_EFFECT_CONTRAST.StartColor");
static_assert(offsetof(SCENE_EFFECT_CONTRAST, StartAlpha) == 0x8, "SCENE_EFFECT_CONTRAST.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_CONTRAST, Time) == 0xc, "SCENE_EFFECT_CONTRAST.Time");

struct SCENE_EFFECT_NEGA                /* 0x10: FuncNo 8 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short StartColor;
    /* 0x4 */ short EndColor;
    /* 0x6 */ short StartAlpha;
    /* 0x8 */ short EndAlpha;
    /* 0xa */ short StartAlpha2;
    /* 0xc */ short EndAlpha2;
    /* 0xe */ short Time;
};
static_assert(sizeof(SCENE_EFFECT_NEGA) == 0x10, "SCENE_EFFECT_NEGA");
static_assert(offsetof(SCENE_EFFECT_NEGA, StartAlpha) == 0x6, "SCENE_EFFECT_NEGA.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_NEGA, StartAlpha2) == 0xa, "SCENE_EFFECT_NEGA.StartAlpha2");
static_assert(offsetof(SCENE_EFFECT_NEGA, Time) == 0xe, "SCENE_EFFECT_NEGA.Time");

struct SCENE_EFFECT_FADEFRAME           /* 0x8: FuncNo 9 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short StartAlpha;
    /* 0x4 */ short EndAlpha;
    /* 0x6 */ short Time;
};
static_assert(sizeof(SCENE_EFFECT_FADEFRAME) == 0x8, "SCENE_EFFECT_FADEFRAME");
static_assert(offsetof(SCENE_EFFECT_FADEFRAME, Time) == 0x6, "SCENE_EFFECT_FADEFRAME.Time");

struct SCENE_EFFECT_OVERLAP             /* 0x4: FuncNo 10 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short Frame;
};
static_assert(sizeof(SCENE_EFFECT_OVERLAP) == 0x4, "SCENE_EFFECT_OVERLAP");
static_assert(offsetof(SCENE_EFFECT_OVERLAP, Frame) == 0x2, "SCENE_EFFECT_OVERLAP.Frame");

// FF3 grew FF2's 0xc record by an Index (the n-th model of that number).
struct SCENE_EFFECT_MODELFADE           /* 0x10: FuncNo 11 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelType;          // 0 man, 1 furniture, 2 door, 3 item
    /* 0x4 */ short ModelId;            // SCN_MDL.obj_no
    /* 0x6 */ short Index;              // < 0: every model of that number
    /* 0x8 */ short StartAlpha;
    /* 0xa */ short EndAlpha;
    /* 0xc */ short Frame;
    /* 0xe */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_MODELFADE) == 0x10, "SCENE_EFFECT_MODELFADE");
static_assert(offsetof(SCENE_EFFECT_MODELFADE, Index) == 0x6, "SCENE_EFFECT_MODELFADE.Index");
static_assert(offsetof(SCENE_EFFECT_MODELFADE, StartAlpha) == 0x8, "SCENE_EFFECT_MODELFADE.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_MODELFADE, Frame) == 0xc, "SCENE_EFFECT_MODELFADE.Frame");

// FF3: FF2's Pad after ModelId became the Index, Type moved up one.
struct SCENE_EFFECT_PDEFORM             /* 0x24: FuncNo 12 */
{
    /* 0x00 */ short FuncNo;
    /* 0x02 */ short ModelId;
    /* 0x04 */ short Index;
    /* 0x06 */ short Type;              // read as a byte (lbu)
    /* 0x08 */ short StartScaleX;
    /* 0x0a */ short EndScaleX;
    /* 0x0c */ short StartScaleY;
    /* 0x0e */ short EndScaleY;
    /* 0x10 */ short StartAlpha;
    /* 0x12 */ short EndAlpha;
    /* 0x14 */ short StartWaveRate;
    /* 0x16 */ short EndWaveRate;
    /* 0x18 */ short StartTexRate;
    /* 0x1a */ short EndTexRate;
    /* 0x1c */ short StartWaveSpeed;
    /* 0x1e */ short EndWaveSpeed;
    /* 0x20 */ short Distance;
    /* 0x22 */ short Time;
};
static_assert(sizeof(SCENE_EFFECT_PDEFORM) == 0x24, "SCENE_EFFECT_PDEFORM");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, Type) == 0x6, "SCENE_EFFECT_PDEFORM.Type");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, StartScaleX) == 0x8, "SCENE_EFFECT_PDEFORM.StartScaleX");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, StartAlpha) == 0x10, "SCENE_EFFECT_PDEFORM.StartAlpha");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, StartWaveRate) == 0x14, "SCENE_EFFECT_PDEFORM.StartWaveRate");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, StartTexRate) == 0x18, "SCENE_EFFECT_PDEFORM.StartTexRate");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, StartWaveSpeed) == 0x1c, "SCENE_EFFECT_PDEFORM.StartWaveSpeed");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, Distance) == 0x20, "SCENE_EFFECT_PDEFORM.Distance");
static_assert(offsetof(SCENE_EFFECT_PDEFORM, Time) == 0x22, "SCENE_EFFECT_PDEFORM.Time");

struct SCENE_EFFECT_ENE_AURA            /* 0x14: FuncNo 14 */
{
    /* 0x00 */ short FuncNo;
    /* 0x02 */ short ModelId;
    /* 0x04 */ short Size;
    /* 0x06 */ short AlphaRate;
    /* 0x08 */ short R;
    /* 0x0a */ short G;
    /* 0x0c */ short B;
    /* 0x0e */ short A;
    /* 0x10 */ short Adjust;
    /* 0x12 */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_ENE_AURA) == 0x14, "SCENE_EFFECT_ENE_AURA");
static_assert(offsetof(SCENE_EFFECT_ENE_AURA, R) == 0x8, "SCENE_EFFECT_ENE_AURA.R");
static_assert(offsetof(SCENE_EFFECT_ENE_AURA, A) == 0xe, "SCENE_EFFECT_ENE_AURA.A");
static_assert(offsetof(SCENE_EFFECT_ENE_AURA, Adjust) == 0x10, "SCENE_EFFECT_ENE_AURA.Adjust");

struct SCENE_EFFECT_POINTLIGHT          /* 0x10: FuncNo 16 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelType;
    /* 0x4 */ short ModelId;
    /* 0x6 */ short LightNo;
    /* 0x8 */ short R;                  // thousandths
    /* 0xa */ short G;
    /* 0xc */ short B;
    /* 0xe */ short Power;
};
static_assert(sizeof(SCENE_EFFECT_POINTLIGHT) == 0x10, "SCENE_EFFECT_POINTLIGHT");
static_assert(offsetof(SCENE_EFFECT_POINTLIGHT, LightNo) == 0x6, "SCENE_EFFECT_POINTLIGHT.LightNo");
static_assert(offsetof(SCENE_EFFECT_POINTLIGHT, Power) == 0xe, "SCENE_EFFECT_POINTLIGHT.Power");

struct SCENE_EFFECT_SPOTLIGHT           /* 0x14: FuncNo 17 */
{
    /* 0x00 */ short FuncNo;
    /* 0x02 */ short ModelType;
    /* 0x04 */ short ModelId;
    /* 0x06 */ short LightNo;
    /* 0x08 */ short R;
    /* 0x0a */ short G;
    /* 0x0c */ short B;
    /* 0x0e */ short Power;
    /* 0x10 */ short Cone;              // tenths of a degree
    /* 0x12 */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_SPOTLIGHT) == 0x14, "SCENE_EFFECT_SPOTLIGHT");
static_assert(offsetof(SCENE_EFFECT_SPOTLIGHT, Cone) == 0x10, "SCENE_EFFECT_SPOTLIGHT.Cone");

struct SCENE_EFFECT_HANDSPOTLIGHT       /* 0xc: FuncNo 18 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short R;
    /* 0x4 */ short G;
    /* 0x6 */ short B;
    /* 0x8 */ short Power;
    /* 0xa */ short Cone;
};
static_assert(sizeof(SCENE_EFFECT_HANDSPOTLIGHT) == 0xc, "SCENE_EFFECT_HANDSPOTLIGHT");
static_assert(offsetof(SCENE_EFFECT_HANDSPOTLIGHT, Power) == 0x8, "SCENE_EFFECT_HANDSPOTLIGHT.Power");

// FF3 grew FF2's 4-byte record by the Index.
struct SCENE_EFFECT_PDEFORM_OFF         /* 0x8: FuncNo 20 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelId;
    /* 0x4 */ short Index;
    /* 0x6 */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_PDEFORM_OFF) == 0x8, "SCENE_EFFECT_PDEFORM_OFF");
static_assert(offsetof(SCENE_EFFECT_PDEFORM_OFF, Index) == 0x4, "SCENE_EFFECT_PDEFORM_OFF.Index");

struct SCENE_EFFECT_MONOCHRO_ONOFF      /* 0x4: FuncNo 21 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short OnFlg;
};
static_assert(sizeof(SCENE_EFFECT_MONOCHRO_ONOFF) == 0x4, "SCENE_EFFECT_MONOCHRO_ONOFF");
static_assert(offsetof(SCENE_EFFECT_MONOCHRO_ONOFF, OnFlg) == 0x2, "SCENE_EFFECT_MONOCHRO_ONOFF.OnFlg");

struct SCENE_EFFECT_HAZE                /* 0x4: FuncNo 22 and 23 (off) */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelId;
};
static_assert(sizeof(SCENE_EFFECT_HAZE) == 0x4, "SCENE_EFFECT_HAZE");
static_assert(offsetof(SCENE_EFFECT_HAZE, ModelId) == 0x2, "SCENE_EFFECT_HAZE.ModelId");

struct SCENE_EFFECT_FADE_IN_OUT         /* 0x8: FuncNo 24 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short InFlg;
    /* 0x4 */ short ColorType;          // 1 white, else black
    /* 0x6 */ short Time;
};
static_assert(sizeof(SCENE_EFFECT_FADE_IN_OUT) == 0x8, "SCENE_EFFECT_FADE_IN_OUT");
static_assert(offsetof(SCENE_EFFECT_FADE_IN_OUT, Time) == 0x6, "SCENE_EFFECT_FADE_IN_OUT.Time");

struct SCENE_EFFECT_FADE_SCREEN         /* 0xc: FuncNo 25 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ColorR;
    /* 0x4 */ short ColorG;
    /* 0x6 */ short ColorB;
    /* 0x8 */ short Alpha;
    /* 0xa */ short Pad;
};
static_assert(sizeof(SCENE_EFFECT_FADE_SCREEN) == 0xc, "SCENE_EFFECT_FADE_SCREEN");
static_assert(offsetof(SCENE_EFFECT_FADE_SCREEN, Alpha) == 0x8, "SCENE_EFFECT_FADE_SCREEN.Alpha");

struct SCENE_EFFECT_VIBRATION2          /* 0xe: the vibration block */
{
    /* 0x0 */ short FuncNo;             // 15 while the block runs
    /* 0x2 */ short StartTime;
    /* 0x4 */ short EndTime;
    /* 0x6 */ short BlankTime;
    /* 0x8 */ short VibrateTime;
    /* 0xa */ short Power;
    /* 0xc */ short ActuaterNo;         // 0 the small motor, else the big one at Power
};
static_assert(sizeof(SCENE_EFFECT_VIBRATION2) == 0xe, "SCENE_EFFECT_VIBRATION2");
static_assert(offsetof(SCENE_EFFECT_VIBRATION2, ActuaterNo) == 0xc, "SCENE_EFFECT_VIBRATION2.ActuaterNo");

// FF3-only: a texture frame ramp on a man model (sine eased).
struct SCENE_EFFECT_TEX_FRAME           /* 0xc: FuncNo 27 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelId;
    /* 0x4 */ short Index;
    /* 0x6 */ short StartFrame;         // texture frames
    /* 0x8 */ short EndFrame;
    /* 0xa */ short Time;               // < 1: EndFrame at once
};
static_assert(sizeof(SCENE_EFFECT_TEX_FRAME) == 0xc, "SCENE_EFFECT_TEX_FRAME");
static_assert(offsetof(SCENE_EFFECT_TEX_FRAME, StartFrame) == 0x6, "SCENE_EFFECT_TEX_FRAME.StartFrame");
static_assert(offsetof(SCENE_EFFECT_TEX_FRAME, Time) == 0xa, "SCENE_EFFECT_TEX_FRAME.Time");

// FF3-only: the fixed room glow colour (EffRoomGlowSet; each value read as a byte).
struct SCENE_EFFECT_ROOM_GLOW           /* 0xc: FuncNo 28 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short R;
    /* 0x4 */ short G;
    /* 0x6 */ short B;
    /* 0x8 */ short Alpha;
    /* 0xa */ short Boost;
};
static_assert(sizeof(SCENE_EFFECT_ROOM_GLOW) == 0xc, "SCENE_EFFECT_ROOM_GLOW");
static_assert(offsetof(SCENE_EFFECT_ROOM_GLOW, Boost) == 0xa, "SCENE_EFFECT_ROOM_GLOW.Boost");

// FF3-only: a man model's aura parameters (SCN_MDL.aura_*; bytes read as bytes).
struct SCENE_EFFECT_MODEL_AURA          /* 0x10: FuncNo 29 */
{
    /* 0x0 */ short FuncNo;
    /* 0x2 */ short ModelId;
    /* 0x4 */ short Index;
    /* 0x6 */ short Type;               // SCN_MDL.aura_type (0: no aura)
    /* 0x8 */ short Speed;              // hundredths
    /* 0xa */ short Alpha;
    /* 0xc */ short Amp;
    /* 0xe */ short F3c;                // hundredths
};
static_assert(sizeof(SCENE_EFFECT_MODEL_AURA) == 0x10, "SCENE_EFFECT_MODEL_AURA");
static_assert(offsetof(SCENE_EFFECT_MODEL_AURA, Speed) == 0x8, "SCENE_EFFECT_MODEL_AURA.Speed");
static_assert(offsetof(SCENE_EFFECT_MODEL_AURA, F3c) == 0xe, "SCENE_EFFECT_MODEL_AURA.F3c");

// ──────────────────────────────────────────────────────────────────────
// The controls.
// ──────────────────────────────────────────────────────────────────────

// One parked long-running effect; the record is copied into ChangeData.
// layout: host -- pRet and pNext are pointers (EE pRet 0, EffectId 4,
// StartFrame 8, EndFrame 0xc, pNext 0x10, ChangeData 0x20, 0x30 bytes);
// the host lands ChangeData at 0x20 as well and SceneEffectCtrlRegist
// sizes the cell with offsetof.
struct SCENE_EFFECT_CTRL_DATA
{
    void                   *pRet;       // the effect the record fired at once (overlap), else nullptr
    int                     EffectId;   // the parked id
    int                     StartFrame;
    int                     EndFrame;
    SCENE_EFFECT_CTRL_DATA *pNext;
    char                    ChangeData; // the request record
};
static_assert(offsetof(SCENE_EFFECT_CTRL_DATA, EffectId) == sizeof(void *), "SCENE_EFFECT_CTRL_DATA.EffectId (host)");
static_assert(offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData) == 0x20, "SCENE_EFFECT_CTRL_DATA.ChangeData (EE +0x20)");

// layout: host -- the two list pointers (EE 4 bytes each at 0 / 4; the
// flag at 8, the vectors at 0x10 / 0x20, ExecutedFrame at 0x30, bss 0x3fce90).
struct SCENE_EFFECT_CTRL
{
    SCENE_EFFECT_CTRL_DATA *pDataTop;
    SCENE_EFFECT_CTRL_DATA *pDataLast;
    int                     LenzFlareFlg;
    sceVu0FVECTOR           SpotLightPos;   // the lens flare's point (SceneEffectLenzFlareExec)
    sceVu0FVECTOR           SpotLightRot;
    int                     ExecutedFrame;  // the last script frame run
};
static_assert(offsetof(SCENE_EFFECT_CTRL, LenzFlareFlg) == 2 * sizeof(void *), "SCENE_EFFECT_CTRL.LenzFlareFlg (host)");
static_assert(offsetof(SCENE_EFFECT_CTRL, SpotLightRot) == offsetof(SCENE_EFFECT_CTRL, SpotLightPos) + 0x10, "SCENE_EFFECT_CTRL.SpotLightRot");

// A model fade (EE 0x20: FF3 added Index after ModelId).
// layout: host -- pNext is a pointer (EE +0x1c).
struct SCENE_FADE_MODEL_DATA
{
    /* 0x00 */ int ModelType;
    /* 0x04 */ int ModelId;
    /* 0x08 */ int Index;
    /* 0x0c */ int StartFrame;
    /* 0x10 */ int EndFrame;
    /* 0x14 */ int StartAlpha;
    /* 0x18 */ int EndAlpha;
    /* 0x1c */ SCENE_FADE_MODEL_DATA *pNext;
};
static_assert(offsetof(SCENE_FADE_MODEL_DATA, Index) == 0x8, "SCENE_FADE_MODEL_DATA.Index");
static_assert(offsetof(SCENE_FADE_MODEL_DATA, EndAlpha) == 0x18, "SCENE_FADE_MODEL_DATA.EndAlpha");
static_assert(offsetof(SCENE_FADE_MODEL_DATA, pNext) == 0x1c + (sizeof(void *) - 4), "SCENE_FADE_MODEL_DATA.pNext (host)");

// layout: host -- two pointers (EE 8 bytes).
struct SCENE_FADE_MODEL_CTRL            /* sbss gp+0x430 (0x373920) */
{
    SCENE_FADE_MODEL_DATA *pDataTop;
    SCENE_FADE_MODEL_DATA *pDataLast;
};
static_assert(offsetof(SCENE_FADE_MODEL_CTRL, pDataLast) == sizeof(void *), "SCENE_FADE_MODEL_CTRL.pDataLast (host)");

// A particle deform (a list cell body, EE 0x50).
struct SCENE_PDEFORM_DATA
{
    /* 0x00 */ sceVu0FVECTOR        Position;
    /* 0x10 */ SCENE_EFFECT_PDEFORM Param;
    /* 0x34 */ float                WaveSpeed;
    /* 0x38 */ float                WaveRate;
    /* 0x3c */ float                TexRate;
    /* 0x40 */ int                  StartFrame;
    /* 0x44 */ int                  EndFrame;
};
static_assert(sizeof(SCENE_PDEFORM_DATA) == 0x50, "SCENE_PDEFORM_DATA (SingleLinkListInit 0x50)");
static_assert(offsetof(SCENE_PDEFORM_DATA, Param) == 0x10, "SCENE_PDEFORM_DATA.Param");
static_assert(offsetof(SCENE_PDEFORM_DATA, WaveSpeed) == 0x34, "SCENE_PDEFORM_DATA.WaveSpeed");
static_assert(offsetof(SCENE_PDEFORM_DATA, TexRate) == 0x3c, "SCENE_PDEFORM_DATA.TexRate");
static_assert(offsetof(SCENE_PDEFORM_DATA, EndFrame) == 0x44, "SCENE_PDEFORM_DATA.EndFrame");

// An enemy aura (EE 0x30). FF3 keeps only the anchor here; nothing sets
// pRet any more (see SceneEffectEneAuraRegist).
// layout: host -- pRet and pNext are pointers (EE +0x24 / +0x28).
struct SCENE_ENE_AURA_DATA
{
    /* 0x00 */ sceVu0FVECTOR        Position;
    /* 0x10 */ int                  ModelId;
    /* 0x14 */ u_int                Rgba;
    /* 0x18 */ float                Size;
    /* 0x1c */ float                AlphaRate;
    /* 0x20 */ float                Adjust;
    /* 0x24 */ void                *pRet;
    /* 0x28 */ SCENE_ENE_AURA_DATA *pNext;
};
static_assert(offsetof(SCENE_ENE_AURA_DATA, ModelId) == 0x10, "SCENE_ENE_AURA_DATA.ModelId");
static_assert(offsetof(SCENE_ENE_AURA_DATA, Adjust) == 0x20, "SCENE_ENE_AURA_DATA.Adjust");
static_assert(offsetof(SCENE_ENE_AURA_DATA, pRet) >= 0x24, "SCENE_ENE_AURA_DATA.pRet (host)");

// layout: host -- two pointers (EE 8 bytes).
struct SCENE_ENE_AURA_CTRL              /* sbss gp+0x438 (0x373928) */
{
    SCENE_ENE_AURA_DATA *pDataTop;
    SCENE_ENE_AURA_DATA *pDataLast;
};
static_assert(offsetof(SCENE_ENE_AURA_CTRL, pDataLast) == sizeof(void *), "SCENE_ENE_AURA_CTRL.pDataLast (host)");

// A heat haze column (a list cell body, EE 0x50).
// layout: host -- pEffRet is a pointer (EE +0x44).
struct SCENE_HAZE_DATA
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ sceVu0FVECTOR HipPosition;
    /* 0x20 */ sceVu0FVECTOR OldHipPosition;
    /* 0x30 */ sceVu0FVECTOR Rot;
    /* 0x40 */ int           ModelId;
    /* 0x44 */ void         *pEffRet;
};
static_assert(offsetof(SCENE_HAZE_DATA, Rot) == 0x30, "SCENE_HAZE_DATA.Rot");
static_assert(offsetof(SCENE_HAZE_DATA, ModelId) == 0x40, "SCENE_HAZE_DATA.ModelId");

// A pad vibration (EE 0x18).
// layout: host -- pNext is a pointer (EE +0x14).
struct SCENE_VIBRATION_DATA
{
    /* 0x00 */ short StartFrame;
    /* 0x02 */ short EndFrame;
    /* 0x04 */ short BlankTime;
    /* 0x06 */ short VibrateTime;
    /* 0x08 */ short Power;
    /* 0x0a */ short ActuaterNo;
    /* 0x0c */ int   BlankCount;
    /* 0x10 */ int   VibrateCount;
    /* 0x14 */ SCENE_VIBRATION_DATA *pNext;
};
static_assert(offsetof(SCENE_VIBRATION_DATA, ActuaterNo) == 0xa, "SCENE_VIBRATION_DATA.ActuaterNo");
static_assert(offsetof(SCENE_VIBRATION_DATA, VibrateCount) == 0x10, "SCENE_VIBRATION_DATA.VibrateCount");

// layout: host -- two pointers (EE 8 bytes).
struct SCENE_VIBRATION_CTRL             /* sbss gp+0x440 (0x373930) */
{
    SCENE_VIBRATION_DATA *pDataTop;
    SCENE_VIBRATION_DATA *pDataLast;
};
static_assert(offsetof(SCENE_VIBRATION_CTRL, pDataLast) == sizeof(void *), "SCENE_VIBRATION_CTRL.pDataLast (host)");

// FF2's torch cell (EE 0x20). FF3 still sets the list up but never adds
// to it (SceneEffectReqTorch skips the record).
// layout: host -- the two pointers (EE +0x10 / +0x14).
struct SCENE_EFFECT_TORCH_DATA
{
    /* 0x00 */ sceVu0FVECTOR Position;
    /* 0x10 */ void         *pEffectRet;
    /* 0x14 */ void         *pMdlAnm;
};
static_assert(offsetof(SCENE_EFFECT_TORCH_DATA, pEffectRet) == 0x10, "SCENE_EFFECT_TORCH_DATA.pEffectRet");

// FF3-only: a running texture frame ramp (a list cell body, 0x10).
struct SCENE_TEX_FRAME_DATA
{
    /* 0x00 */ int   ModelId;
    /* 0x04 */ int   Index;
    /* 0x08 */ short Start;             // texture frames
    /* 0x0a */ short End;
    /* 0x0c */ short StartFrame;        // script frames
    /* 0x0e */ short EndFrame;
};
static_assert(sizeof(SCENE_TEX_FRAME_DATA) == 0x10, "SCENE_TEX_FRAME_DATA (SingleLinkListInit 0x10)");
static_assert(offsetof(SCENE_TEX_FRAME_DATA, Start) == 0x8, "SCENE_TEX_FRAME_DATA.Start");
static_assert(offsetof(SCENE_TEX_FRAME_DATA, EndFrame) == 0xe, "SCENE_TEX_FRAME_DATA.EndFrame");

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

static SCENE_EFFECT_CTRL     SceneEffectCtrl;       // bss 0x3fce90
static SCENE_FADE_MODEL_CTRL SceneFadeModelCtrl;    // sbss 0x373920
static SINGLE_LINK_LIST      ScenePDeformCtrl;      // bss 0x3fced0
static SCENE_ENE_AURA_CTRL   SceneEneAuraCtrl;      // sbss 0x373928
static SCENE_VIBRATION_CTRL  SceneVibrationCtrl;    // sbss 0x373930
static SINGLE_LINK_LIST      SceneTorchCtrl;        // bss 0x3fcee0
static SINGLE_LINK_LIST      SceneHazeCtrl;         // bss 0x3fcef0
static SINGLE_LINK_LIST      SceneTexFrameCtrl;     // bss 0x3fcf00

// The model -> haze anchor offset (FF2 450 units, FF3 world units).
static const sceVu0FVECTOR SaeHazeOffset = { 0.0f, 18.0f, 0.0f, 0.0f };    // rodata 0x362b20

// CallBlur hands the effect layer &BlurAlpha in the blur's alpha slot,
// which FF3's unpacker reads as a byte (FF2's took the pointer): the
// blur's alpha is the low byte of the static's address, sdata 0x3723bc.
#define SCENE_BLUR_ALPHA_BYTE   0xbc

static short *SceneEffectReqDataEnd(short *pData, int Frame);
static short *SceneEffectReqOff(short *pData, int Frame);
static short *SceneEffectReqFocusDepth(short *pData, int Frame);
static short *SceneEffectReqDither(short *pData, int Frame);
static short *SceneEffectReqBlur(short *pData, int Frame);
static short *SceneEffectReqDeform(short *pData, int Frame);
static short *SceneEffectReqFocus(short *pData, int Frame);
static short *SceneEffectReqContrast(short *pData, int Frame);
static short *SceneEffectReqNega(short *pData, int Frame);
static short *SceneEffectReqFadeFrame(short *pData, int Frame);
static short *SceneEffectReqOverlap(short *pData, int Frame);
static short *SceneEffectReqModelFade(short *pData, int Frame);
static short *SceneEffectReqPDeform(short *pData, int Frame);
static short *SceneEffectReqLenzFlare(short *pData, int Frame);
static short *SceneEffectReqEneAura(short *pData, int Frame);
static short *SceneEffectReqVibration(short *pData, int Frame);
static short *SceneEffectReqPointLight(short *pData, int Frame);
static short *SceneEffectReqSpotLight(short *pData, int Frame);
static short *SceneEffectReqHandSpotLight(short *pData, int Frame);
static short *SceneEffectReqTorch(short *pData, int Frame);
static short *SceneEffectReqPDeformOff(short *pData, int Frame);
static short *SceneEffectMonochroModeOnOff(short *pData, int Frame);
static short *SceneEffectReqHaze(short *pData, int Frame);
static short *SceneEffectHazeOff(short *pData, int Frame);
static short *SceneEffectFadeInOut(short *pData, int Frame);
static short *SceneEffectReqFadeScreen(short *pData, int Frame);
static short *SceneEffectReqFadeScreenOff(short *pData, int Frame);
static short *SceneEffectReqTexFrame(short *pData, int Frame);
static short *SceneEffectReqRoomGlow(short *pData, int Frame);
static short *SceneEffectReqModelAura(short *pData, int Frame);

// The FuncNo dispatch (rodata 0x362aa8, 30 slots; 27..29 FF3's). Each
// handler returns the cursor for the next record; slot 0 ends the run.
static short *(*const SceneEffectFunc[])(short *pData, int Frame) =
{
    SceneEffectReqDataEnd,              //  0
    SceneEffectReqOff,                  //  1
    SceneEffectReqFocusDepth,           //  2
    SceneEffectReqDither,               //  3
    SceneEffectReqBlur,                 //  4
    SceneEffectReqDeform,               //  5
    SceneEffectReqFocus,                //  6
    SceneEffectReqContrast,             //  7
    SceneEffectReqNega,                 //  8
    SceneEffectReqFadeFrame,            //  9
    SceneEffectReqOverlap,              // 10
    SceneEffectReqModelFade,            // 11
    SceneEffectReqPDeform,              // 12
    SceneEffectReqLenzFlare,            // 13
    SceneEffectReqEneAura,              // 14
    SceneEffectReqVibration,            // 15
    SceneEffectReqPointLight,           // 16
    SceneEffectReqSpotLight,            // 17
    SceneEffectReqHandSpotLight,        // 18
    SceneEffectReqTorch,                // 19
    SceneEffectReqPDeformOff,           // 20
    SceneEffectMonochroModeOnOff,       // 21
    SceneEffectReqHaze,                 // 22
    SceneEffectHazeOff,                 // 23
    SceneEffectFadeInOut,               // 24
    SceneEffectReqFadeScreen,           // 25
    SceneEffectReqFadeScreenOff,        // 26
    SceneEffectReqTexFrame,             // 27
    SceneEffectReqRoomGlow,             // 28
    SceneEffectReqModelAura,            // 29
};

static void SceneEffectCtrlInit(void);
static void SceneEffectCtrlAllDelete(void);
static SCENE_EFFECT_CTRL_DATA *SceneEffectCtrlRegist(void *pRet, int EffectId, int ChangeDataSize);
static int  SceneEffectCtrlDelete(int EffectId);
static void SceneEffectReq(short *pDataTop, int Frame);
static void SceneEffectRegistEffectReq(int NowFrame);
static void SceneEffectVibrationReq(short *pDataTop, int Frame);
static void SceneEffectReqOneData(short *pData, int NowFrame);
static void *SceneEffectChangeDataPtr(SCENE_EFFECT_CTRL_DATA *pCtrl);
static void SceneEffectCallDither(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallBlur(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallDeform(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallFocus(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallContrast(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallNega(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectCallFadeFrame(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static void SceneEffectFadeModelCtrlInit(void);
static int  SceneEffectFadeModelRegist(SCENE_EFFECT_MODELFADE *pModelFade, int Frame);
static void SceneEffectFadeModelAllDelete(void);
static void SceneEffectFadeModelCtrlExec(SCENE_CTRL *pSceneCtrl);
static void SceneEffectPDeformCtrlInit(void);
static void SceneEffectPDeformCtrlAllDelete(void);
static void SceneEffectPDeformCtrlDelete(short ModelId, short Index);
static int  SceneEffectPDeformRegist(SCENE_EFFECT_PDEFORM *pPDeform, int NowFrame);
static void SceneEffectPDeformCtrlExec(int NowFrame);
static void SceneEffectEneAuraCtrlInit(void);
static void SceneEffectEneAuraCtrlAllDelete(void);
static int  SceneEffectEneAuraRegist(SCENE_EFFECT_ENE_AURA *pEneAura);
static void SceneEffectEneAuraCtrlExec(void);
static void SceneEffectHazeCtrlInit(void);
static void SceneEffectHazeCtrlDelete(int ModelId);
static void SceneEffectHazeCtrlAllDelete(void);
static int  SceneEffectHazeRegist(SCENE_EFFECT_HAZE *pHaze);
static void SceneEffectHazeCtrlExec(void);
static void SceneEffectVibrationCtrlInit(void);
static void SceneEffectVibrationCtrlAllDelete(void);
static int  SceneEffectVibrationRegist(SCENE_EFFECT_VIBRATION2 *pVibration);
static void SceneEffectVibrationCtrlExec(int NowFrame);
static void SceneEffectVibrationExecPAL(int NowFrame);
static void SceneEffectVibrationCountPAL(int NowFrame);
static void SceneTorchCtrlInit(void);
static void SceneTorchCtrlAllDelete(void);
static void SceneTorchCtrlExec(void);
static void SceneEffectTexFrameCtrlInit(void);
static void SceneEffectTexFrameCtrlAllDelete(void);
static void SceneEffectTexFrameCtrlExec(int NowFrame);

// ──────────────────────────────────────────────────────────────────────
// Every control cleared; FF3 also resets the room glow to the scene's
// fixed block (cleared when glow_clear) and requests screen effect 0x10.
// FF3 0x00254018; FF2 SceneEffectInit 0x0024d0a8 (+ the room glow, the
// texture frame list).

void SceneEffectInit(int glow_clear)
{
    SceneEffectCtrlInit();
    SceneEffectFadeModelCtrlInit();
    SceneEffectPDeformCtrlInit();
    SceneEffectEneAuraCtrlInit();
    SceneEffectVibrationCtrlInit();
    SceneTorchCtrlInit();
    SceneEffectHazeCtrlInit();
    SceneEffectTexFrameCtrlInit();
    if (glow_clear != 0)
        EffRoomGlowClear();
    EffRoomGlowSetFixed(1);
    SetEffects(0x10, 2);
}

// ──────────────────────────────────────────────────────────────────────
// Runs the script for every scene frame since the last call (a PAL scene
// can step more than one), then the per-frame controls. FF2's lens flare
// call moved into the man model draw (SceneEffectLenzFlareExec).
// FF3 0x00254098; FF2 SceneEffectMain 0x0024d0f8.

void SceneEffectMain(SCENE_CTRL *sc, void *data)
{
    if (data != nullptr)
    {
        int NowFrame = (int)sc->fod_ctrl.now_frame;
        int ExecutedFrame = SceneEffectCtrl.ExecutedFrame;
        int LoopNum = NowFrame - ExecutedFrame;
        int i;

        for (i = ExecutedFrame + 1; i <= ExecutedFrame + LoopNum; i++)
        {
            SceneEffectReq((short *)data, i);
            SceneEffectRegistEffectReq(i);
            SceneEffectVibrationReq((short *)data, i);
            SceneEffectFadeModelCtrlExec(sc);
            SceneEffectEneAuraCtrlExec();
            SceneEffectTexFrameCtrlExec(i);
            SceneEffectVibrationCtrlExec(i);
            SceneTorchCtrlExec();
            SceneEffectHazeCtrlExec();
        }

        if (LoopNum != 0)
            SceneEffectPDeformCtrlExec(NowFrame);

        SceneEffectCtrl.ExecutedFrame = NowFrame;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Every control emptied, the screen tint off, the room glow back to the
// area's and screen effect 0x10 cut.
// FF3 0x00254180; FF2 SceneEffectEnd 0x0024d1f8 (SetParam is ScreenFlashReq).

void SceneEffectEnd(void)
{
    SceneEffectCtrlAllDelete();
    SceneEffectFadeModelAllDelete();
    SceneEffectPDeformCtrlAllDelete();
    SceneEffectEneAuraCtrlAllDelete();
    SceneEffectVibrationCtrlAllDelete();
    SceneTorchCtrlAllDelete();
    SceneEffectHazeCtrlAllDelete();
    ScreenFlashReq(0, 0, 0, 0, 0, 0);
    SceneEffectTexFrameCtrlAllDelete();
    EffRoomGlowSetFixed(0);
    CutEffects(0x10);
}

// ──────────────────────────────────────────────────────────────────────
// The parked-effect list.
// FF3 0x00254200; FF2 SceneEffectCtrlInit 0x0024d260 (from MioPan).

static void SceneEffectCtrlInit(void)
{
    SceneEffectCtrl.pDataTop = nullptr;
    SceneEffectCtrl.ExecutedFrame = 0;
    SceneEffectCtrl.pDataLast = nullptr;
    SceneEffectCtrl.LenzFlareFlg = 0;
    SceneEffectCtrl.SpotLightPos[0] = 0.0f;
    SceneEffectCtrl.SpotLightPos[1] = 0.0f;
    SceneEffectCtrl.SpotLightPos[2] = 0.0f;
    SceneEffectCtrl.SpotLightPos[3] = 1.0f;
    SceneEffectCtrl.SpotLightRot[0] = 0.0f;
    SceneEffectCtrl.SpotLightRot[1] = 0.0f;
    SceneEffectCtrl.SpotLightRot[2] = 0.0f;
    SceneEffectCtrl.SpotLightRot[3] = 0.0f;
}

// FF3 0x00254248; FF2 SceneEffectCtrlAllDelete 0x0024d2a8 (from MioPan).

static void SceneEffectCtrlAllDelete(void)
{
    SCENE_EFFECT_CTRL_DATA *pData = SceneEffectCtrl.pDataTop;
    SCENE_EFFECT_CTRL_DATA *pDataNext;

    while (pData != nullptr)
    {
        if (pData->pRet != nullptr)
            ResetEffects(pData->pRet);

        pDataNext = pData->pNext;
        heapCtrlFree(GetSystemHeapWrkP(), pData, 0, 0, 0);
        pData = pDataNext;
    }

    SceneEffectCtrlInit();
}

// Appends a parked effect with room for a ChangeDataSize-byte record.
// FF3 0x002542c0; FF2 SceneEffectCtrlRegist 0x0024d318 (from MioPan; the
// ROM's 0x20 is the EE offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData)).

static SCENE_EFFECT_CTRL_DATA *SceneEffectCtrlRegist(void *pRet, int EffectId, int ChangeDataSize)
{
    SCENE_EFFECT_CTRL *pCtrl = &SceneEffectCtrl;
    SCENE_EFFECT_CTRL_DATA *pMalloc;
    int MallocSize = (int)offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData) + ChangeDataSize;

    pMalloc = (SCENE_EFFECT_CTRL_DATA *)heapCtrlMalloc(GetSystemHeapWrkP(), (u_int)MallocSize, 0, 0, 0);
    if (pMalloc != nullptr)
    {
        if (pCtrl->pDataLast == nullptr)
        {
            pCtrl->pDataTop = pMalloc;
        }
        else
        {
            pCtrl->pDataLast->pNext = pMalloc;
        }
        pCtrl->pDataLast = pMalloc;

        pCtrl->pDataLast->pRet = pRet;
        pCtrl->pDataLast->EffectId = EffectId;
        pCtrl->pDataLast->StartFrame = 0;
        pCtrl->pDataLast->EndFrame = 0;
        pCtrl->pDataLast->pNext = nullptr;
    }

    return pMalloc;
}

// Unlinks and frees the first entry with the id (its effect reset), then
// rebuilds pDataLast. 1 on a hit, -1 if none.
// FF3 0x00254370; FF2 SceneEffectCtrlDelete 0x0024d3c0 (from MioPan).

static int SceneEffectCtrlDelete(int EffectId)
{
    SCENE_EFFECT_CTRL *pCtrl = &SceneEffectCtrl;
    SCENE_EFFECT_CTRL_DATA *pData = pCtrl->pDataTop;
    SCENE_EFFECT_CTRL_DATA *pPreData = pData;
    int RetVal = -1;

    while (pData != nullptr)
    {
        if (pData->EffectId == EffectId)
        {
            if (pPreData != pData)
                pPreData->pNext = pData->pNext;
            else
                pCtrl->pDataTop = pData->pNext;
            ResetEffects(pData->pRet);
            heapCtrlFree(GetSystemHeapWrkP(), pData, 0, 0, 0);
            RetVal = 1;
            break;
        }
        pPreData = pData;
        pData = pData->pNext;
    }

    pData = pCtrl->pDataTop;
    pCtrl->pDataLast = nullptr;
    while (pData != nullptr)
    {
        pCtrl->pDataLast = pData;
        pData = pData->pNext;
    }

    return RetVal;
}

// ──────────────────────────────────────────────────────────────────────
// The script. Finds the frame in the timing table and runs its run.
// FF3 0x00254448; FF2 SceneEffectReq 0x0024d498 (from MioPan).

static void SceneEffectReq(short *pDataTop, int Frame)
{
    SCENE_EFFECT_HEADER *pHeader = (SCENE_EFFECT_HEADER *)pDataTop;
    SCENE_EFFECT_TABLE *pTable;
    int i;

    if (pDataTop != nullptr)
    {
        pTable = (SCENE_EFFECT_TABLE *)((char *)pDataTop + pHeader->TimingTableOffset);
        for (i = 0; i < pHeader->TableNum; i++)
        {
            if (pTable[i].Frame == Frame)
            {
                SceneEffectReqOneData((short *)((char *)pDataTop + pTable[i].Offset), Frame);
                return;
            }
        }
    }
}

// Feeds every parked interpolating effect for this frame (FF3's ids).
// FF3 0x002544b0; FF2 SceneEffectRegistEffectReq 0x0024d4f8.

static void SceneEffectRegistEffectReq(int NowFrame)
{
    SCENE_EFFECT_CTRL_DATA *pData;

    for (pData = SceneEffectCtrl.pDataTop; pData != nullptr; pData = pData->pNext)
    {
        if (pData->pRet != nullptr)
            continue;

        if (pData->EffectId == 2)
            SceneEffectCallDither(pData, NowFrame);
        else if (pData->EffectId == 3)
            SceneEffectCallBlur(pData, NowFrame);
        else if (pData->EffectId == 4)
            SceneEffectCallDeform(pData, NowFrame);
        else if (pData->EffectId == 5)
            SceneEffectCallFocus(pData, NowFrame);
        else if ((u_int)(pData->EffectId - 0xb) < 3)
            SceneEffectCallContrast(pData, NowFrame);
        else if (pData->EffectId == 10)
            SceneEffectCallNega(pData, NowFrame);
        else if (pData->EffectId == 7)
            SceneEffectCallFadeFrame(pData, NowFrame);
    }
}

// Walks the vibration block; records starting this frame go onto the
// vibration control (the frame compared as a short).
// FF3 0x002545f8; FF2 SceneEffectVibrationReq 0x0024d648 (from MioPan).

static void SceneEffectVibrationReq(short *pDataTop, int Frame)
{
    SCENE_EFFECT_VIBRATION2 *pVibData;

    if (pDataTop != nullptr)
    {
        pVibData = (SCENE_EFFECT_VIBRATION2 *)((char *)pDataTop
                                               + ((SCENE_EFFECT_HEADER *)pDataTop)->VibrationDataOffset);
        while (pVibData->FuncNo == 0xf)
        {
            if (pVibData->StartTime == (short)Frame)
                SceneEffectVibrationRegist(pVibData);
            pVibData++;
        }
    }
}

// Runs one frame's run through the dispatch (no bounds check on FuncNo:
// the ROM trusts its data). FF2's printf is gone.
// FF3 0x00254670; FF2 SceneEffectReqOneData 0x0024d6c8.

static void SceneEffectReqOneData(short *pData, int NowFrame)
{
    while (pData != nullptr)
        pData = SceneEffectFunc[*pData](pData, NowFrame);
}

// FF3 0x002546d0; FF2 SceneEffectChangeDataPtr 0x0024d750.

static void *SceneEffectChangeDataPtr(SCENE_EFFECT_CTRL_DATA *pCtrl)
{
    return &pCtrl->ChangeData;
}

// FuncNo 0: the end of the run.
// FF3 0x002546e0; FF2 SceneEffectReqDataEnd 0x0024d758.

static short *SceneEffectReqDataEnd(short *pData, int Frame)
{
    (void)pData;
    (void)Frame;
    return nullptr;
}

// FuncNo 1: stops parked effects; the selector maps to FF3's parked ids.
// FF3 0x002546e8; FF2 SceneEffectReqOff 0x0024d760 (ids renumbered).

static short *SceneEffectReqOff(short *pData, int Frame)
{
    SCENE_EFFECT_OFF *pOff = (SCENE_EFFECT_OFF *)pData;
    int EffectId;

    (void)Frame;
    switch (pOff->EffectId)
    {
    case 2:  EffectId = 1;  break;      // the focus depth
    case 3:  EffectId = 2;  break;      // dither
    case 4:  EffectId = 3;  break;      // blur
    case 5:  EffectId = 4;  break;      // deform
    case 6:  EffectId = 5;  break;      // focus
    case 7:                             // contrast
        SceneEffectCtrlDelete(0xb);
        SceneEffectCtrlDelete(0xc);
        EffectId = 0xd;
        break;
    case 8:  EffectId = 10; break;      // nega
    case 9:  EffectId = 7;  break;      // frame fade
    case 10: EffectId = 6;  break;      // overlap
    case 13:
        SceneEffectCtrl.LenzFlareFlg = 0;
        return (short *)(pOff + 1);
    default:
        return (short *)(pOff + 1);
    }
    SceneEffectCtrlDelete(EffectId);

    return (short *)(pOff + 1);
}

// FuncNo 2: the depth-of-field base state.
// FF3 0x002547a0; FF2 SceneEffectReqFocusDepth 0x0024d870 (SetEffects id 1).

static short *SceneEffectReqFocusDepth(short *pData, int Frame)
{
    SCENE_EFFECT_FOCUSDEPTH *pDepth = (SCENE_EFFECT_FOCUSDEPTH *)pData;

    (void)Frame;
    if (pDepth->Type == 0)
        SceneEffectCtrlDelete(1);
    else if (pDepth->Type == 1)
        SetEffects(1, 2);

    return (short *)(pDepth + 1);
}

// FuncNo 3: parks a dither ramp (id 2).
// FF3 0x002547f8; FF2 SceneEffectReqDither 0x0024d8c8 (from MioPan).

static short *SceneEffectReqDither(short *pData, int Frame)
{
    SCENE_EFFECT_DITHER *pDither = (SCENE_EFFECT_DITHER *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(2);

    if (pDither->Type != 0)
    {
        pCtrlData = SceneEffectCtrlRegist(nullptr, 2, sizeof(SCENE_EFFECT_DITHER));
        if (pCtrlData != nullptr)
        {
            *(SCENE_EFFECT_DITHER *)SceneEffectChangeDataPtr(pCtrlData) = *pDither;
            pCtrlData->StartFrame = Frame;
            pCtrlData->EndFrame = Frame + pDither->Time;
        }
    }

    return (short *)(pDither + 1);
}

// FF3 0x002548b8; FF2 SceneEffectCallDither 0x0024d988 (no debug gate;
// SetEffects id 2 without FF2's envelope zeros).
// callcheck: fptodp (the (double) arguments)

static void SceneEffectCallDither(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_DITHER *pDither = (SCENE_EFFECT_DITHER *)SceneEffectChangeDataPtr(pData);
    float Alpha;
    float Speed;
    float Progress;

    if (pDither->Time <= 0 || pData->EndFrame < NowFrame)
    {
        Alpha = (float)pDither->EndAlpha;
        Speed = (float)pDither->EndSpeed;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pDither->Time;
        Alpha = (float)(pDither->EndAlpha - pDither->StartAlpha) * Progress + (float)pDither->StartAlpha;
        Speed = (float)(pDither->EndSpeed - pDither->StartSpeed) * Progress + (float)pDither->StartSpeed;
    }

    SetEffects(2, 1, (int)pDither->Type, (double)Alpha, (double)Speed,
               (int)pDither->AlphaMax, (int)pDither->ColorMax);
}

// FuncNo 4: parks a blur ramp (id 3; FF2 picked 3/4/5 by type).
// FF3 0x002549d0; FF2 SceneEffectReqBlur 0x0024dac0.

static short *SceneEffectReqBlur(short *pData, int Frame)
{
    SCENE_EFFECT_BLUR *pBlur = (SCENE_EFFECT_BLUR *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(3);

    if (pBlur->Type != 0)
    {
        pCtrlData = SceneEffectCtrlRegist(nullptr, 3, sizeof(SCENE_EFFECT_BLUR));
        if (pCtrlData != nullptr)
        {
            *(SCENE_EFFECT_BLUR *)SceneEffectChangeDataPtr(pCtrlData) = *pBlur;
            pCtrlData->StartFrame = Frame;
            pCtrlData->EndFrame = Frame + pBlur->Time;
        }
    }

    return (short *)(pBlur + 1);
}

// Blur type 1 round the screen centre (320, 224). The alpha ramp still
// lands in the BlurAlpha static, but the slot carries its address
// (SCENE_BLUR_ALPHA_BYTE above).
// FF3 0x00254a90; FF2 SceneEffectCallBlur 0x0024dba8.
// callcheck: fptodp (the (double) arguments)

static void SceneEffectCallBlur(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_BLUR *pBlur = (SCENE_EFFECT_BLUR *)SceneEffectChangeDataPtr(pData);
    static int BlurAlpha = 0;           // sdata 0x3723bc
    int Scale;
    int Rot;
    float Progress;

    if (pBlur->Time <= 0 || pData->EndFrame < NowFrame)
    {
        BlurAlpha = pBlur->EndAlpha;
        Scale = pBlur->EndScale;
        Rot = pBlur->EndRot;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pBlur->Time;
        BlurAlpha = (int)((float)(pBlur->EndAlpha - pBlur->StartAlpha) * Progress + (float)pBlur->StartAlpha);
        Scale = (int)((float)(pBlur->EndScale - pBlur->StartScale) * Progress + (float)pBlur->StartScale);
        Rot = (int)((float)(pBlur->EndRot - pBlur->StartRot) * Progress + (float)pBlur->StartRot);
    }

    SetEffects(pData->EffectId, 1, 1, SCENE_BLUR_ALPHA_BYTE, Scale, Rot, 320.0, 224.0);
}

// FuncNo 5: parks a screen deform (id 4).
// FF3 0x00254bb8; FF2 SceneEffectReqDeform 0x0024dd08.

static short *SceneEffectReqDeform(short *pData, int Frame)
{
    SCENE_EFFECT_DEFORM *pDeform = (SCENE_EFFECT_DEFORM *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(4);

    if (pDeform->Type != 0)
    {
        pCtrlData = SceneEffectCtrlRegist(nullptr, 4, sizeof(SCENE_EFFECT_DEFORM));
        if (pCtrlData != nullptr)
        {
            *(SCENE_EFFECT_DEFORM *)SceneEffectChangeDataPtr(pCtrlData) = *pDeform;
            pCtrlData->StartFrame = Frame;
            pCtrlData->EndFrame = Frame + pDeform->Time;
        }
    }

    return (short *)(pDeform + 1);
}

// FF3 0x00254c68; FF2 SceneEffectCallDeform 0x0024ddb8 (no gate, no envelope zeros).

static void SceneEffectCallDeform(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_DEFORM *pDeform = (SCENE_EFFECT_DEFORM *)SceneEffectChangeDataPtr(pData);
    int Rate;
    float Progress;

    if (pDeform->Time <= 0 || pData->EndFrame < NowFrame)
    {
        Rate = pDeform->EndRate;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pDeform->Time;
        Rate = (int)((float)(pDeform->EndRate - pDeform->StartRate) * Progress + (float)pDeform->StartRate);
    }

    SetEffects(4, 1, (int)pDeform->Type, Rate);
}

// FuncNo 6: parks a focus ramp (id 5, no type gate).
// FF3 0x00254d18; FF2 SceneEffectReqFocus 0x0024de88.

static short *SceneEffectReqFocus(short *pData, int Frame)
{
    SCENE_EFFECT_FOCUS *pFocus = (SCENE_EFFECT_FOCUS *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(5);

    pCtrlData = SceneEffectCtrlRegist(nullptr, 5, sizeof(SCENE_EFFECT_FOCUS));
    if (pCtrlData != nullptr)
    {
        *(SCENE_EFFECT_FOCUS *)SceneEffectChangeDataPtr(pCtrlData) = *pFocus;
        pCtrlData->StartFrame = Frame;
        pCtrlData->EndFrame = Frame + pFocus->Time;
    }

    return (short *)(pFocus + 1);
}

// FF3 0x00254da8; FF2 SceneEffectCallFocus 0x0024df18 (no gate).

static void SceneEffectCallFocus(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_FOCUS *pFocus = (SCENE_EFFECT_FOCUS *)SceneEffectChangeDataPtr(pData);
    int Rate;
    float Progress;

    if (pFocus->Time <= 0 || pData->EndFrame < NowFrame)
    {
        Rate = pFocus->EndRate;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pFocus->Time;
        Rate = (int)((float)(pFocus->EndRate - pFocus->StartRate) * Progress + (float)pFocus->StartRate);
    }

    SetEffects(5, 1, Rate);
}

// FuncNo 7: parks a contrast filter; type 2/3/4 -> id 0xb/0xc/0xd, others
// park nothing.
// FF3 0x00254e50; FF2 SceneEffectReqContrast 0x0024dfe8 (ids 0xd..0xf there).

static short *SceneEffectReqContrast(short *pData, int Frame)
{
    SCENE_EFFECT_CONTRAST *pContrast = (SCENE_EFFECT_CONTRAST *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;
    int Type;

    SceneEffectCtrlDelete(0xb);
    SceneEffectCtrlDelete(0xc);
    SceneEffectCtrlDelete(0xd);

    switch (pContrast->Type)
    {
    case 2:  Type = 0xb; break;
    case 3:  Type = 0xc; break;
    case 4:  Type = 0xd; break;
    default: Type = 0;   break;
    }

    if (Type != 0)
    {
        pCtrlData = SceneEffectCtrlRegist(nullptr, Type, sizeof(SCENE_EFFECT_CONTRAST));
        if (pCtrlData != nullptr)
        {
            *(SCENE_EFFECT_CONTRAST *)SceneEffectChangeDataPtr(pCtrlData) = *pContrast;
            pCtrlData->StartFrame = Frame;
            pCtrlData->EndFrame = Frame + pContrast->Time;
        }
    }

    return (short *)(pContrast + 1);
}

// FF3 0x00254f30; FF2 SceneEffectCallContrast 0x0024e0d8 (no gates).

static void SceneEffectCallContrast(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_CONTRAST *pContrast = (SCENE_EFFECT_CONTRAST *)SceneEffectChangeDataPtr(pData);
    int Color;
    int Alpha;
    float Progress;

    if (pContrast->Time <= 0 || pData->EndFrame < NowFrame)
    {
        Alpha = pContrast->EndAlpha;
        Color = pContrast->EndColor;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pContrast->Time;
        Alpha = (int)((float)(pContrast->EndAlpha - pContrast->StartAlpha) * Progress + (float)pContrast->StartAlpha);
        Color = (int)((float)(pContrast->EndColor - pContrast->StartColor) * Progress + (float)pContrast->StartColor);
    }

    SetEffects(pData->EffectId, 1, Color, Alpha);
}

// FuncNo 8: parks a nega filter (id 10, no gate).
// FF3 0x00255008; FF2 SceneEffectReqNega 0x0024e1f0 (id 0xc there).

static short *SceneEffectReqNega(short *pData, int Frame)
{
    SCENE_EFFECT_NEGA *pNega = (SCENE_EFFECT_NEGA *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(10);

    pCtrlData = SceneEffectCtrlRegist(nullptr, 10, sizeof(SCENE_EFFECT_NEGA));
    if (pCtrlData != nullptr)
    {
        *(SCENE_EFFECT_NEGA *)SceneEffectChangeDataPtr(pCtrlData) = *pNega;
        pCtrlData->StartFrame = Frame;
        pCtrlData->EndFrame = Frame + pNega->Time;
    }

    return (short *)(pNega + 1);
}

// The second alpha travels through a static byte the effect layer keeps a
// pointer to.
// FF3 0x002550a8; FF2 SceneEffectCallNega 0x0024e290 (no gate).

static void SceneEffectCallNega(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_NEGA *pNega = (SCENE_EFFECT_NEGA *)SceneEffectChangeDataPtr(pData);
    static u_char nalp = 0;             // sdata 0x3723c0
    int Color;
    int Alpha;
    float Progress;

    if (pNega->Time <= 0 || pData->EndFrame < NowFrame)
    {
        nalp = (u_char)pNega->EndAlpha2;
        Color = pNega->EndColor;
        Alpha = pNega->EndAlpha;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pNega->Time;
        nalp = (u_char)(int)((float)(pNega->EndAlpha2 - pNega->StartAlpha2) * Progress + (float)pNega->StartAlpha2);
        Color = (int)((float)(pNega->EndColor - pNega->StartColor) * Progress + (float)pNega->StartColor);
        Alpha = (int)((float)(pNega->EndAlpha - pNega->StartAlpha) * Progress + (float)pNega->StartAlpha);
    }

    SetEffects(10, 1, Color, Alpha, &nalp);
}

// FuncNo 9: parks a frame fade (id 7, no gate).
// FF3 0x002551b8; FF2 SceneEffectReqFadeFrame 0x0024e3c0 (id 9 there).

static short *SceneEffectReqFadeFrame(short *pData, int Frame)
{
    SCENE_EFFECT_FADEFRAME *pFadeFrame = (SCENE_EFFECT_FADEFRAME *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(7);

    pCtrlData = SceneEffectCtrlRegist(nullptr, 7, sizeof(SCENE_EFFECT_FADEFRAME));
    if (pCtrlData != nullptr)
    {
        *(SCENE_EFFECT_FADEFRAME *)SceneEffectChangeDataPtr(pCtrlData) = *pFadeFrame;
        pCtrlData->StartFrame = Frame;
        pCtrlData->EndFrame = Frame + pFadeFrame->Time;
    }

    return (short *)(pFadeFrame + 1);
}

// FF3 0x00255248; FF2 SceneEffectCallFadeFrame 0x0024e450 (no gate).

static void SceneEffectCallFadeFrame(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_FADEFRAME *pFadeFrame = (SCENE_EFFECT_FADEFRAME *)SceneEffectChangeDataPtr(pData);
    int Alpha;
    float Progress;

    if (pFadeFrame->Time <= 0 || pData->EndFrame < NowFrame)
    {
        Alpha = pFadeFrame->EndAlpha;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pFadeFrame->Time;
        Alpha = (int)((float)(pFadeFrame->EndAlpha - pFadeFrame->StartAlpha) * Progress + (float)pFadeFrame->StartAlpha);
    }

    SetEffects(7, 1, Alpha, 0);
}

// FuncNo 10: a cross fade fired at once, parked (id 6) only so an OFF
// record can reset it.
// FF3 0x002552f8; FF2 SceneEffectReqOverlap 0x0024e520 (ids 2 / 8 there).

static short *SceneEffectReqOverlap(short *pData, int Frame)
{
    (void)Frame;
    SceneEffectCtrlRegist(SetEffects(6, 2, (int)((SCENE_EFFECT_OVERLAP *)pData)->Frame), 6, 0);

    return (short *)((SCENE_EFFECT_OVERLAP *)pData + 1);
}

// FuncNo 11. FF3 0x00255340; FF2 SceneEffectReqModelFade 0x0024e568.

static short *SceneEffectReqModelFade(short *pData, int Frame)
{
    SceneEffectFadeModelRegist((SCENE_EFFECT_MODELFADE *)pData, Frame);

    return (short *)((SCENE_EFFECT_MODELFADE *)pData + 1);
}

// FuncNo 12. FF3 0x00255370; FF2 SceneEffectReqPDeform 0x0024e590.

static short *SceneEffectReqPDeform(short *pData, int Frame)
{
    SceneEffectPDeformRegist((SCENE_EFFECT_PDEFORM *)pData, Frame);

    return (short *)((SCENE_EFFECT_PDEFORM *)pData + 1);
}

// FuncNo 13. FF3 0x002553a0; FF2 SceneEffectReqLenzFlare 0x0024e5b8.

static short *SceneEffectReqLenzFlare(short *pData, int Frame)
{
    (void)Frame;
    SceneEffectCtrl.LenzFlareFlg = 1;

    return pData + 2;
}

// FuncNo 14. FF3 0x002553b8; FF2 SceneEffectReqEneAura 0x0024e5d0.

static short *SceneEffectReqEneAura(short *pData, int Frame)
{
    (void)Frame;
    SceneEffectEneAuraRegist((SCENE_EFFECT_ENE_AURA *)pData);

    return (short *)((SCENE_EFFECT_ENE_AURA *)pData + 1);
}

// FuncNo 15: nothing (vibration records live in their own block); the
// six-short record is skipped.
// FF3 0x002553e8; FF2 SceneEffectReqVibration 0x0024e5f8.

static short *SceneEffectReqVibration(short *pData, int Frame)
{
    (void)Frame;
    return pData + 6;
}

// FuncNo 16: a point light; colours in thousandths.
// FF3 0x002553f8; FF2 SceneEffectReqPointLight 0x0024e600.

static short *SceneEffectReqPointLight(short *pData, int Frame)
{
    SCENE_EFFECT_POINTLIGHT *pLight = (SCENE_EFFECT_POINTLIGHT *)pData;

    (void)Frame;
    SceneChangeLightParameter((float)pLight->R / 1000.0f, (float)pLight->G / 1000.0f,
                              (float)pLight->B / 1000.0f, (float)pLight->Power, 0.0f,
                              3, pLight->LightNo, pLight->ModelType, pLight->ModelId);

    return (short *)(pLight + 1);
}

// FuncNo 17: a spot light; the cone in tenths of a degree.
// FF3 0x00255498; FF2 SceneEffectReqSpotLight 0x0024e698.

static short *SceneEffectReqSpotLight(short *pData, int Frame)
{
    SCENE_EFFECT_SPOTLIGHT *pLight = (SCENE_EFFECT_SPOTLIGHT *)pData;

    (void)Frame;
    SceneChangeLightParameter((float)pLight->R / 1000.0f, (float)pLight->G / 1000.0f,
                              (float)pLight->B / 1000.0f, (float)pLight->Power,
                              (float)pLight->Cone / 10.0f,
                              2, pLight->LightNo, pLight->ModelType, pLight->ModelId);

    return (short *)(pLight + 1);
}

// FuncNo 18. FF3 0x00255558; FF2 SceneEffectReqHandSpotLight 0x0024e750.

static short *SceneEffectReqHandSpotLight(short *pData, int Frame)
{
    SCENE_EFFECT_HANDSPOTLIGHT *pLight = (SCENE_EFFECT_HANDSPOTLIGHT *)pData;

    (void)Frame;
    SceneChangeHandSpotLightParameter((float)pLight->R / 1000.0f, (float)pLight->G / 1000.0f,
                                      (float)pLight->B / 1000.0f, (float)pLight->Power,
                                      (float)pLight->Cone / 10.0f);

    return (short *)(pLight + 1);
}

// FuncNo 19: FF3 dropped the torch; the 8-byte record is skipped.
// FF3 0x00255608; FF2 SceneEffectReqTorch 0x0024e7f8.

static short *SceneEffectReqTorch(short *pData, int Frame)
{
    (void)Frame;
    return pData + 4;
}

// FuncNo 20. FF3 0x00255618; FF2 SceneEffectReqPDeformOff 0x0024e848 (+ Index).

static short *SceneEffectReqPDeformOff(short *pData, int Frame)
{
    SCENE_EFFECT_PDEFORM_OFF *pOff = (SCENE_EFFECT_PDEFORM_OFF *)pData;

    (void)Frame;
    SceneEffectPDeformCtrlDelete(pOff->ModelId, pOff->Index);

    return (short *)(pOff + 1);
}

// FuncNo 21: monochrome on / off and the playing scene's rooms
// prelit again so the baked colours match.
// FF3 0x00255650; FF2 SceneEffectMonochroModeOnOff 0x0024e878.

static short *SceneEffectMonochroModeOnOff(short *pData, int Frame)
{
    SCENE_EFFECT_MONOCHRO_ONOFF *pMonoOnOff = (SCENE_EFFECT_MONOCHRO_ONOFF *)pData;
    SCENE_CTRL *sc;
    int RoomNo;
    int SubRoomNo;

    (void)Frame;
    EffectMonochromeSet(pMonoOnOff->OnFlg != 0, 0);

    sc = ScenePlayCtrlGet();
    if (sc != nullptr)
    {
        RoomNo = SceneRoomNoGet(sc);
        SubRoomNo = SceneSubRoomNoGet(sc);
        if (RoomNo != -1)
            gra3dPrelightScene(RoomNo);
        if (SubRoomNo != -1)
            gra3dPrelightScene(SubRoomNo);
    }

    return (short *)(pMonoOnOff + 1);
}

// FuncNo 22. FF3 0x00255700; FF2 SceneEffectReqHaze 0x0024e918.

static short *SceneEffectReqHaze(short *pData, int Frame)
{
    (void)Frame;
    SceneEffectHazeRegist((SCENE_EFFECT_HAZE *)pData);

    return (short *)((SCENE_EFFECT_HAZE *)pData + 1);
}

// FuncNo 23. FF3 0x00255730; FF2 SceneEffectHazeOff 0x0024e940.

static short *SceneEffectHazeOff(short *pData, int Frame)
{
    (void)Frame;
    SceneEffectHazeCtrlDelete(((SCENE_EFFECT_HAZE *)pData)->ModelId);

    return (short *)((SCENE_EFFECT_HAZE *)pData + 1);
}

// FuncNo 24: the four screen fades.
// FF3 0x00255760; FF2 SceneEffectFadeInOut 0x0024e970 (SetWhiteIn2 ..).

static short *SceneEffectFadeInOut(short *pData, int Frame)
{
    SCENE_EFFECT_FADE_IN_OUT *pFade = (SCENE_EFFECT_FADE_IN_OUT *)pData;

    (void)Frame;
    if (pFade->InFlg == 0)
    {
        if (pFade->ColorType == 1)
            ScreenFadeToWhite(pFade->Time);
        else
            ScreenFadeToBlack(pFade->Time);
    }
    else
    {
        if (pFade->ColorType == 1)
            ScreenFlashWhite(pFade->Time);
        else
            ScreenFlashBlack(pFade->Time);
    }

    return (short *)(pFade + 1);
}

// FuncNo 25: an immediate flat screen tint.
// FF3 0x002557e8; FF2 SceneEffectReqFadeScreen 0x0024e9f8 (SetParam).

static short *SceneEffectReqFadeScreen(short *pData, int Frame)
{
    SCENE_EFFECT_FADE_SCREEN *pFade = (SCENE_EFFECT_FADE_SCREEN *)pData;

    (void)Frame;
    ScreenFlashReq((u_char)pFade->Alpha, 0, (u_char)pFade->ColorR, (u_char)pFade->ColorG,
                   (u_char)pFade->ColorB, 0);

    return (short *)(pFade + 1);
}

// FuncNo 26. FF3 0x00255830; FF2 SceneEffectReqFadeScreenOff 0x0024ea38.

static short *SceneEffectReqFadeScreenOff(short *pData, int Frame)
{
    (void)Frame;
    ScreenFlashReq(0, 0, 0, 0, 0, 0);

    return pData + 2;
}

// ──────────────────────────────────────────────────────────────────────
// Model fades: alpha ramps written onto the scene's SCN_MDL entries.
// FF3 0x00255878; FF2 SceneEffectFadeModelCtrlInit 0x0024ea78.

static void SceneEffectFadeModelCtrlInit(void)
{
    SceneFadeModelCtrl.pDataTop = nullptr;
    SceneFadeModelCtrl.pDataLast = nullptr;
}

// FF3 0x00255888; FF2 SceneEffectFadeModelRegist 0x0024ea88 (+ Index).

static int SceneEffectFadeModelRegist(SCENE_EFFECT_MODELFADE *pModelFade, int Frame)
{
    SCENE_FADE_MODEL_CTRL *pCtrl = &SceneFadeModelCtrl;
    SCENE_FADE_MODEL_DATA *pMalloc;

    pMalloc = (SCENE_FADE_MODEL_DATA *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(SCENE_FADE_MODEL_DATA), 0, 0, 0);
    if (pMalloc == nullptr)
        return -1;

    if (pCtrl->pDataLast != nullptr)
        pCtrl->pDataLast->pNext = pMalloc;
    else
        pCtrl->pDataTop = pMalloc;
    pCtrl->pDataLast = pMalloc;

    pMalloc->ModelType = pModelFade->ModelType;
    pMalloc->ModelId = pModelFade->ModelId;
    pMalloc->Index = pModelFade->Index;
    pMalloc->StartFrame = Frame;
    pMalloc->EndFrame = pModelFade->Frame + Frame;
    pMalloc->StartAlpha = pModelFade->StartAlpha;
    pMalloc->EndAlpha = pModelFade->EndAlpha;
    pMalloc->pNext = nullptr;

    return 0;
}

// FF3 0x00255960; FF2 SceneEffectFadeModelAllDelete 0x0024eb28.

static void SceneEffectFadeModelAllDelete(void)
{
    SCENE_FADE_MODEL_DATA *pData = SceneFadeModelCtrl.pDataTop;
    SCENE_FADE_MODEL_DATA *pDataNext;

    while (pData != nullptr)
    {
        pDataNext = pData->pNext;
        heapCtrlFree(GetSystemHeapWrkP(), pData, 0, 0, 0);
        pData = pDataNext;
    }

    SceneEffectFadeModelCtrlInit();
}

// Every live fade onto its models' mdl_alpha (bit 31 marks it
// fade-driven): of the models numbered ModelId in the group, the Index-th
// (all for Index < 0). The ROM does not step to the next fade after an
// unknown ModelType (it loops on it for ever); the port steps on.
// FF3 0x002559b8; FF2 SceneEffectFadeModelCtrlExec 0x0024eb88 (+ Index).

static void SceneEffectFadeModelCtrlExec(SCENE_CTRL *pSceneCtrl)
{
    SCENE_FADE_MODEL_DATA *pFadeData;
    SCN_MDL *pAnmMdl;
    int ModelNum;
    int NowFrame = (int)pSceneCtrl->fod_ctrl.now_frame;
    int Found;
    int DivFrame;
    int i;

    for (pFadeData = SceneFadeModelCtrl.pDataTop; pFadeData != nullptr; pFadeData = pFadeData->pNext)
    {
        switch (pFadeData->ModelType)
        {
        case 0:
            ModelNum = pSceneCtrl->man_mdl_num;
            pAnmMdl = &pSceneCtrl->man_mdl[0];
            break;
        case 1:
            ModelNum = pSceneCtrl->furn_num;
            pAnmMdl = &pSceneCtrl->furn_mdl[0];
            break;
        case 2:
            ModelNum = pSceneCtrl->door_num;
            pAnmMdl = &pSceneCtrl->door_mdl[0];
            break;
        case 3:
            ModelNum = pSceneCtrl->item_num;
            pAnmMdl = &pSceneCtrl->item_mdl[0];
            break;
        default:
            continue;                   // PC: the ROM hangs here (see above)
        }

        Found = -1;
        for (i = ModelNum; i > 0; i--, pAnmMdl++)
        {
            if (pFadeData->ModelId != pAnmMdl->obj_no)
                continue;
            Found++;
            if (pFadeData->Index >= 0 && Found != pFadeData->Index)
                continue;

            if (NowFrame < pFadeData->EndFrame)
            {
                DivFrame = pFadeData->EndFrame - pFadeData->StartFrame;
                if (DivFrame != 0)
                    pAnmMdl->mdl_alpha = (u_int)((pFadeData->EndAlpha - pFadeData->StartAlpha)
                                                 * (NowFrame - pFadeData->StartFrame) / DivFrame
                                                 + pFadeData->StartAlpha);
                else
                    pAnmMdl->mdl_alpha = (u_int)pFadeData->EndAlpha;
            }
            else
            {
                pAnmMdl->mdl_alpha = (u_int)pFadeData->EndAlpha;
            }
            pAnmMdl->mdl_alpha |= 0x80000000;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Particle deforms: heat ripples anchored on a man model.
// FF3 0x00255ae8; FF2 SceneEffectPDeformCtrlInit 0x0024ed10.

static void SceneEffectPDeformCtrlInit(void)
{
    SingleLinkListInit(&ScenePDeformCtrl, sizeof(SCENE_PDEFORM_DATA));
}

// FF3 0x00255b08; FF2 SceneEffectPDeformCtrlAllDelete 0x0024ed38.

static void SceneEffectPDeformCtrlAllDelete(void)
{
    SingleLinkListAllCellFree(&ScenePDeformCtrl);
}

// Every ripple on the model (ModelId, Index) removed.
// FF3 0x00255b28; FF2 SceneEffectPDeformCtrlDelete 0x0024ed58 (+ Index).

static void SceneEffectPDeformCtrlDelete(short ModelId, short Index)
{
    SINGLE_LINK_LIST *pSLL = &ScenePDeformCtrl;
    SLL_CELL *pCell = SingleLinkListBeginCell(pSLL);

    while (pCell != nullptr)
    {
        SLL_CELL *pCellNext = pCell->pNext;
        SCENE_PDEFORM_DATA *pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->Param.ModelId == ModelId && pData->Param.Index == Index)
            SingleLinkListRemove(pSLL, pCell);

        pCell = pCellNext;
    }
}

// The cell is reserved from an uninitialised stack copy and filled in place
// (the ROM's own way).
// FF3 0x00255bd0; FF2 SceneEffectPDeformRegist 0x0024edf8.

static int SceneEffectPDeformRegist(SCENE_EFFECT_PDEFORM *pPDeform, int NowFrame)
{
    SCENE_PDEFORM_DATA TmpData;
    SLL_CELL *pCell;
    SCENE_PDEFORM_DATA *pData;
    int RetVal = -1;

    pCell = SingleLinkListAddEnd(&ScenePDeformCtrl, &TmpData);
    if (pCell != nullptr)
    {
        pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);
        pData->Position[0] = 0.0f;
        pData->Position[1] = 0.0f;
        pData->Position[2] = 0.0f;
        pData->Position[3] = 1.0f;
        pData->Param = *pPDeform;
        RetVal = 0;
        pData->WaveSpeed = 0.0f;
        pData->WaveRate = 0.0f;
        pData->EndFrame = NowFrame + pData->Param.Time;
        pData->StartFrame = NowFrame;
        pData->TexRate = 1.0f;
    }

    return RetVal;
}

// Steps every ripple's ramps (hundredths) and feeds the PDEFORM effect.
// FF3 0x00255cf0; FF2 SceneEffectPDeformCtrlExec 0x0024eed8 (the anchor
// by (ModelId, Index)).
// callcheck: fptodp (the (double) arguments)

static void SceneEffectPDeformCtrlExec(int NowFrame)
{
    SINGLE_LINK_LIST *pSLL = &ScenePDeformCtrl;
    SLL_CELL *pCell;
    SCENE_PDEFORM_DATA *pData;
    int Alpha;
    float ScaleX;
    float ScaleY;
    float Progress;

    for (pCell = SingleLinkListBeginCell(pSLL); pCell != nullptr; pCell = SingleLinkListNextCell(pCell))
    {
        pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->Param.Time <= 0 || pData->EndFrame < NowFrame)
        {
            Alpha = pData->Param.EndAlpha;
            pData->WaveSpeed = (float)pData->Param.EndWaveSpeed / 100.0f;
            pData->WaveRate = (float)pData->Param.EndWaveRate / 100.0f;
            pData->TexRate = (float)pData->Param.EndTexRate / 100.0f;
            ScaleX = (float)pData->Param.EndScaleX / 100.0f;
            ScaleY = (float)pData->Param.EndScaleY / 100.0f;
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pData->Param.Time;
            pData->WaveSpeed = ((float)(pData->Param.EndWaveSpeed - pData->Param.StartWaveSpeed) * Progress
                                + (float)pData->Param.StartWaveSpeed) / 100.0f;
            Alpha = (int)((float)(pData->Param.EndAlpha - pData->Param.StartAlpha) * Progress
                          + (float)pData->Param.StartAlpha);
            pData->WaveRate = ((float)(pData->Param.EndWaveRate - pData->Param.StartWaveRate) * Progress
                               + (float)pData->Param.StartWaveRate) / 100.0f;
            ScaleX = ((float)(pData->Param.EndScaleX - pData->Param.StartScaleX) * Progress
                      + (float)pData->Param.StartScaleX) / 100.0f;
            ScaleY = ((float)(pData->Param.EndScaleY - pData->Param.StartScaleY) * Progress
                      + (float)pData->Param.StartScaleY) / 100.0f;
            pData->TexRate = ((float)(pData->Param.EndTexRate - pData->Param.StartTexRate) * Progress
                              + (float)pData->Param.StartTexRate) / 100.0f;
        }

        SceneGetModelPDeformPos((float)pData->Param.Distance, pData->Position,
                                pData->Param.ModelId, pData->Param.Index);

        // The ROM reads Type as a byte (lbu).
        SetEffects(0x16, 1, (int)(u_char)pData->Param.Type, Alpha, (double)ScaleX, (double)ScaleY,
                   (void *)pData->Position, 0, 0, 0, (void *)nullptr,
                   (void *)&pData->WaveSpeed, (void *)&pData->WaveRate, (void *)&pData->TexRate,
                   0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The lens flare record's flag, for the man model draw.
// FF3 0x00256030 (FF3-only).

int SceneEffectLenzFlareFlgGet(void)
{
    return SceneEffectCtrl.LenzFlareFlg;
}

// The flare on the lantern: efct[0]'s lens point and aim (x negated), the
// level the model's alpha / 128.
// FF3 0x00256040; FF2 SceneEffectLenzFlareExec 0x0024f218 (there run from
// SceneEffectMain off the player's light bone).

void SceneEffectLenzFlareExec(SCN_MDL *mdl)
{
    float RotX;
    float RotY;
    float Flare;

    IgEffectHandLightFlarePosRot(mdl->efct[0], SceneEffectCtrl.SpotLightPos, &RotX, &RotY);
    SceneEffectCtrl.SpotLightRot[3] = 0.0f;
    SceneEffectCtrl.SpotLightRot[0] = -RotX;
    SceneEffectCtrl.SpotLightRot[1] = RotY;
    SceneEffectCtrl.SpotLightRot[2] = 0.0f;

    Flare = (float)mdl->mdl_alpha * 0.0078125f;
    EffRenzFlareDraw(Flare, Flare, SceneEffectCtrl.SpotLightPos, SceneEffectCtrl.SpotLightRot, 4);
}

// ──────────────────────────────────────────────────────────────────────
// Enemy auras: FF3 keeps the anchor on the model up to date but no longer
// requests an ENEFIRE effect (the aura is drawn from SCN_MDL's aura
// fields). FF3 0x002560f8; FF2 SceneEffectEneAuraCtrlInit 0x0024f308.

static void SceneEffectEneAuraCtrlInit(void)
{
    SceneEneAuraCtrl.pDataTop = nullptr;
    SceneEneAuraCtrl.pDataLast = nullptr;
}

// FF3 0x00256108; FF2 SceneEffectEneAuraCtrlAllDelete 0x0024f318 (from MioPan).

static void SceneEffectEneAuraCtrlAllDelete(void)
{
    SCENE_ENE_AURA_DATA *pData = SceneEneAuraCtrl.pDataTop;
    SCENE_ENE_AURA_DATA *pDataNext;

    while (pData != nullptr)
    {
        if (pData->pRet != nullptr)
            ResetEffects(pData->pRet);

        pDataNext = pData->pNext;
        heapCtrlFree(GetSystemHeapWrkP(), pData, 0, 0, 0);
        pData = pDataNext;
    }

    SceneEffectEneAuraCtrlInit();
}

// FF3 0x00256178; FF2 SceneEffectEneAuraRegist 0x0024f390 (no SetEffects).

static int SceneEffectEneAuraRegist(SCENE_EFFECT_ENE_AURA *pEneAura)
{
    SCENE_ENE_AURA_CTRL *pCtrl = &SceneEneAuraCtrl;
    SCENE_ENE_AURA_DATA *pMalloc;

    pMalloc = (SCENE_ENE_AURA_DATA *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(SCENE_ENE_AURA_DATA), 0, 0, 0);
    if (pMalloc == nullptr)
        return -1;

    if (pCtrl->pDataLast != nullptr)
        pCtrl->pDataLast->pNext = pMalloc;
    else
        pCtrl->pDataTop = pMalloc;
    pCtrl->pDataLast = pMalloc;

    SceneGetModelPDeformPos(0.0f, pMalloc->Position, pEneAura->ModelId, 0);
    pMalloc->Position[1] += (float)pEneAura->Adjust;
    pMalloc->ModelId = pEneAura->ModelId;
    pMalloc->Rgba = ((u_int)(u_char)pEneAura->R << 24) | ((u_int)(u_char)pEneAura->G << 16)
                  | ((u_int)(u_char)pEneAura->B << 8) | (u_int)(u_char)pEneAura->A;
    pMalloc->Size = (float)pEneAura->Size;
    pMalloc->AlphaRate = (float)pEneAura->AlphaRate / 100.0f;
    pMalloc->Adjust = (float)pEneAura->Adjust;
    // PC: FF3 never writes pRet (heap garbage that AllDelete would hand to
    // ResetEffects); the port starts it null, as FF2's SetEffects did not fail.
    pMalloc->pRet = nullptr;
    pMalloc->pNext = nullptr;

    return 0;
}

// Re-anchors every aura on its model.
// FF3 0x002562b0; FF2 SceneEffectEneAuraCtrlExec 0x0024f4d0.

static void SceneEffectEneAuraCtrlExec(void)
{
    SCENE_ENE_AURA_DATA *pData;

    for (pData = SceneEneAuraCtrl.pDataTop; pData != nullptr; pData = pData->pNext)
    {
        SceneGetModelPDeformPos(0.0f, pData->Position, pData->ModelId, 0);
        pData->Position[1] += pData->Adjust;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Heat haze: a column above a man model (the 1D effect kind, record 4).
// FF3 0x00256308; FF2 SceneEffectHazeCtrlInit 0x0024f530.

static void SceneEffectHazeCtrlInit(void)
{
    SingleLinkListInit(&SceneHazeCtrl, sizeof(SCENE_HAZE_DATA));
}

// The first haze on the model released and removed.
// FF3 0x00256328; FF2 SceneEffectHazeCtrlDelete 0x0024f558 (EffectSaeHazeCut
// is EffKind1DRelease).

static void SceneEffectHazeCtrlDelete(int ModelId)
{
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;
    SLL_CELL *pCell = SingleLinkListBeginCell(pSLL);

    while (pCell != nullptr)
    {
        SLL_CELL *pCellNext = pCell->pNext;
        SCENE_HAZE_DATA *pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pHaze->ModelId == ModelId)
        {
            EffKind1DRelease(pHaze->pEffRet);
            SingleLinkListRemove(pSLL, pCell);
            return;
        }
        pCell = pCellNext;
    }
}

// FF3 0x002563d0; FF2 SceneEffectHazeCtrlAllDelete 0x0024f5e8.

static void SceneEffectHazeCtrlAllDelete(void)
{
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;
    SLL_CELL *pCell = SingleLinkListBeginCell(pSLL);

    while (pCell != nullptr)
    {
        SLL_CELL *pCellNext = pCell->pNext;
        SCENE_HAZE_DATA *pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);

        EffKind1DRelease(pHaze->pEffRet);
        SingleLinkListRemove(pSLL, pCell);
        pCell = pCellNext;
    }
}

// Anchors a haze column SaeHazeOffset above the model.
// FF3 0x00256450; FF2 SceneEffectHazeRegist 0x0024f678 (the old position
// copied, not fetched twice).

static int SceneEffectHazeRegist(SCENE_EFFECT_HAZE *pHaze)
{
    SCENE_HAZE_DATA TmpData;
    SLL_CELL *pCell;
    SCENE_HAZE_DATA *pData;
    int RetVal = -1;

    pCell = SingleLinkListAddEnd(&SceneHazeCtrl, &TmpData);
    if (pCell != nullptr)
    {
        pData = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pData->ModelId = pHaze->ModelId;
        SceneManModelPosGet(pData->HipPosition, pHaze->ModelId, 0);
        pData->OldHipPosition[0] = pData->HipPosition[0];
        pData->OldHipPosition[1] = pData->HipPosition[1];
        pData->OldHipPosition[2] = pData->HipPosition[2];
        pData->OldHipPosition[3] = pData->HipPosition[3];
        sceVu0AddVector(pData->Position, pData->HipPosition, SaeHazeOffset);
        RetVal = 0;
        pData->Rot[0] = 0.0f;
        pData->Rot[1] = 0.0f;
        pData->Rot[2] = 0.0f;
        pData->Rot[3] = 1.0f;
        pData->pEffRet = EffKind1DReq4(pData->Position, pData->Rot, nullptr);
    }

    return RetVal;
}

// Follows the model; when it moved more than 0.01 the column turns along
// the movement.
// FF3 0x00256518; FF2 SceneEffectHazeCtrlExec 0x0024f738.
// callcheck: fptodp, dpcmp (the double compare)

static void SceneEffectHazeCtrlExec(void)
{
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;
    SLL_CELL *pCell;
    SCENE_HAZE_DATA *pHaze;
    float RotX;

    for (pCell = SingleLinkListBeginCell(pSLL); pCell != nullptr; pCell = SingleLinkListNextCell(pCell))
    {
        pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);

        pHaze->OldHipPosition[0] = pHaze->HipPosition[0];
        pHaze->OldHipPosition[1] = pHaze->HipPosition[1];
        pHaze->OldHipPosition[2] = pHaze->HipPosition[2];
        pHaze->OldHipPosition[3] = pHaze->HipPosition[3];
        SceneManModelPosGet(pHaze->HipPosition, pHaze->ModelId, 0);
        sceVu0AddVector(pHaze->Position, pHaze->HipPosition, SaeHazeOffset);

        // The 0.01 is a double in the ROM (rodata 0x3723f0).
        if ((double)Get2PDist3D2(pHaze->OldHipPosition, pHaze->HipPosition) > 0.01)
            Get2PosRot(pHaze->OldHipPosition, pHaze->HipPosition, &RotX, &pHaze->Rot[1]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Pad vibration. FF2's VibrateRequest1 / 2 are inlined against pad 0's
// motor work, each behind the option's vibration switch (FF3 reads the
// option through player.c's cached pointer, sdata 0x372a04; the port asks
// GetOptionWrk, the same pointer).
// FF3 0x00256620; FF2 SceneEffectVibrationCtrlInit 0x0024f838.

static void SceneEffectVibrationCtrlInit(void)
{
    SceneVibrationCtrl.pDataTop = nullptr;
    SceneVibrationCtrl.pDataLast = nullptr;
}

// FF3 0x00256630; FF2 SceneEffectVibrationCtrlAllDelete 0x0024f848.

static void SceneEffectVibrationCtrlAllDelete(void)
{
    SCENE_VIBRATION_DATA *pData = SceneVibrationCtrl.pDataTop;
    SCENE_VIBRATION_DATA *pDataNext;

    while (pData != nullptr)
    {
        pDataNext = pData->pNext;
        heapCtrlFree(GetSystemHeapWrkP(), pData, 0, 0, 0);
        pData = pDataNext;
    }

    SceneEffectVibrationCtrlInit();
}

// FF3 0x00256688; FF2 SceneEffectVibrationRegist 0x0024f8a8 (from MioPan;
// the frame argument is gone).

static int SceneEffectVibrationRegist(SCENE_EFFECT_VIBRATION2 *pVibration)
{
    SCENE_VIBRATION_CTRL *pCtrl = &SceneVibrationCtrl;
    SCENE_VIBRATION_DATA *pMalloc;

    pMalloc = (SCENE_VIBRATION_DATA *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(SCENE_VIBRATION_DATA), 0, 0, 0);
    if (pMalloc == nullptr)
        return -1;

    if (pCtrl->pDataLast != nullptr)
        pCtrl->pDataLast->pNext = pMalloc;
    else
        pCtrl->pDataTop = pMalloc;
    pCtrl->pDataLast = pMalloc;

    pMalloc->StartFrame = pVibration->StartTime;
    pMalloc->EndFrame = pVibration->EndTime;
    pMalloc->BlankTime = pVibration->BlankTime;
    pMalloc->VibrateTime = pVibration->VibrateTime;
    pMalloc->Power = pVibration->Power;
    pMalloc->BlankCount = pVibration->BlankTime;
    pMalloc->ActuaterNo = pVibration->ActuaterNo;
    pMalloc->VibrateCount = pVibration->VibrateTime;
    pMalloc->pNext = nullptr;

    return 0;
}

// One frame's request to pad 0's motors (FF2 VibrateRequest1(0, 1) /
// VibrateRequest2(0, Power)), inlined in FF3.

static void SceneEffectVibrate(const SCENE_VIBRATION_DATA *pData)
{
    if (pData->ActuaterNo == 0)
    {
        if (GetOptionWrk()->now.vibration != 0)
            pad_wrk[0].vib_small_time = 1;
    }
    else
    {
        if (GetOptionWrk()->now.vibration != 0)
        {
            pad_wrk[0].vib_big_time = 1;
            pad_wrk[0].vib_big_pow = pData->Power;
        }
    }
    ReiPan_HapticCue(REIPAN_CUE_SCENE,          // PC: the small motor has no power
                     pData->ActuaterNo == 0 ? 0.6f : (float)pData->Power / 255.0f);
}

// Rumble while VibrateCount runs, rest while BlankCount runs, reload both
// when the rest ends (frames compared as shorts).
// FF3 0x00256760; FF2 SceneEffectVibrationCtrlExec 0x0024f950.
// callcheck: GetOptionWrk (SceneEffectVibrate, inlined in FF3)

static void SceneEffectVibrationCtrlExec(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;
    short Now = (short)NowFrame;

    for (pData = SceneVibrationCtrl.pDataTop; pData != nullptr; pData = pData->pNext)
    {
        if (Now < pData->StartFrame || Now >= pData->EndFrame)
            continue;

        if (pData->VibrateCount != 0)
        {
            SceneEffectVibrate(pData);
            pData->VibrateCount--;
        }
        else if (pData->BlankCount == 0)
        {
            pData->VibrateCount = pData->VibrateTime;
            pData->BlankCount = pData->BlankTime;
        }
        else
        {
            pData->BlankCount--;
        }
    }
}

// The movie half: the motors for the frame, the counters untouched.
// FF3 0x00256888; FF2 SceneEffectVibrationExecPAL 0x0024fa18.
// callcheck: GetOptionWrk (SceneEffectVibrate, inlined in FF3)

static void SceneEffectVibrationExecPAL(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;
    short Now = (short)NowFrame;

    for (pData = SceneVibrationCtrl.pDataTop; pData != nullptr; pData = pData->pNext)
    {
        if (Now < pData->StartFrame || Now >= pData->EndFrame)
            continue;

        if (pData->VibrateCount != 0)
            SceneEffectVibrate(pData);
    }
}

// ...and the counters for one movie frame, the motors untouched.
// FF3 0x00256980; FF2 SceneEffectVibrationCountPAL 0x0024faa8.

static void SceneEffectVibrationCountPAL(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;
    short Now = (short)NowFrame;

    for (pData = SceneVibrationCtrl.pDataTop; pData != nullptr; pData = pData->pNext)
    {
        if (Now < pData->StartFrame || Now >= pData->EndFrame)
            continue;

        if (pData->VibrateCount != 0)
        {
            pData->VibrateCount--;
        }
        else if (pData->BlankCount == 0)
        {
            pData->VibrateCount = pData->VibrateTime;
            pData->BlankCount = pData->BlankTime;
        }
        else
        {
            pData->BlankCount--;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Torches: FF2's list is still set up, the rest is empty in FF3.
// FF3 0x002569f0; FF2 SceneTorchCtrlInit 0x0024fb18.

static void SceneTorchCtrlInit(void)
{
    SingleLinkListInit(&SceneTorchCtrl, sizeof(SCENE_EFFECT_TORCH_DATA));
}

// FF3 0x00256a10; FF2 SceneTorchCtrlAllDelete 0x0024fba0 (empty in FF3).

static void SceneTorchCtrlAllDelete(void)
{
}

// FF3 0x00256a18; FF2 SceneTorchCtrlExec 0x0024fc30 (empty in FF3).

static void SceneTorchCtrlExec(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The movie-side entry: the script keyed on movie frames (x 1.2 on PAL);
// only the requests and the vibration run, the counters per movie frame,
// the motors once at the end.
// FF3 0x00256a20; FF2 SceneMovieEffectMain 0x0024fca8.

void SceneMovieEffectMain(int frame, void *data)
{
    int i;

    if (data != nullptr)
    {
        if (GetPALMode() != 0)
            frame = (int)((float)frame * 1.1999999f);

        for (i = SceneEffectCtrl.ExecutedFrame + 1; i <= frame; i++)
        {
            SceneEffectReq((short *)data, i);
            SceneEffectVibrationReq((short *)data, i);
            SceneEffectVibrationCountPAL(i);
        }

        SceneEffectVibrationExecPAL(frame);

        SceneEffectCtrl.ExecutedFrame = frame;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3-only: texture frame ramps on man models (record 27).
// FF3 0x00256ae8.

static void SceneEffectTexFrameCtrlInit(void)
{
    SingleLinkListInit(&SceneTexFrameCtrl, sizeof(SCENE_TEX_FRAME_DATA));
}

// FF3 0x00256b08.

static void SceneEffectTexFrameCtrlAllDelete(void)
{
    SingleLinkListAllCellFree(&SceneTexFrameCtrl);
}

// Inside a ramp's window: Start + (End - Start) * sin(t * pi / 2), set
// when it is a frame 0..0x80. Ramps stay on the list until the scene ends.
// FF3 0x00256b28.

static void SceneEffectTexFrameCtrlExec(int NowFrame)
{
    SINGLE_LINK_LIST *pSLL = &SceneTexFrameCtrl;
    SLL_CELL *pCell;
    SCENE_TEX_FRAME_DATA *pData;
    float Rate;
    u_int TexFrame;

    for (pCell = SingleLinkListBeginCell(pSLL); pCell != nullptr; pCell = SingleLinkListNextCell(pCell))
    {
        pData = (SCENE_TEX_FRAME_DATA *)SingleLinkListCellBodyPtr(pCell);

        if (pData->StartFrame < pData->EndFrame && pData->StartFrame <= NowFrame && NowFrame <= pData->EndFrame)
        {
            Rate = Vu0Sinf((float)(NowFrame - pData->StartFrame) / (float)(pData->EndFrame - pData->StartFrame)
                           * 1.5707964f);
            TexFrame = (u_int)((int)((float)(pData->End - pData->Start) * Rate) + pData->Start);
            if (TexFrame < 0x81)
                SceneManModelTexFrameSet(pData->ModelId, pData->Index, TexFrame);
        }
    }
}

// FuncNo 27: Time < 1 sets EndFrame now, else parks the ramp from this frame.
// FF3 0x00256c50.

static short *SceneEffectReqTexFrame(short *pData, int Frame)
{
    SCENE_EFFECT_TEX_FRAME *pTex = (SCENE_EFFECT_TEX_FRAME *)pData;
    SCENE_TEX_FRAME_DATA TmpData;

    if (pTex->Time < 1)
    {
        SceneManModelTexFrameSet(pTex->ModelId, pTex->Index, (u_int)(int)pTex->EndFrame);
    }
    else
    {
        TmpData.ModelId = pTex->ModelId;
        TmpData.Index = pTex->Index;
        TmpData.Start = pTex->StartFrame;
        TmpData.End = pTex->EndFrame;
        TmpData.StartFrame = (short)Frame;
        TmpData.EndFrame = (short)(pTex->Time + Frame);
        SingleLinkListAddEnd(&SceneTexFrameCtrl, &TmpData);
    }

    return (short *)(pTex + 1);
}

// FuncNo 28: the fixed room glow colour.
// FF3 0x00256ce0.

static short *SceneEffectReqRoomGlow(short *pData, int Frame)
{
    SCENE_EFFECT_ROOM_GLOW *pGlow = (SCENE_EFFECT_ROOM_GLOW *)pData;

    (void)Frame;
    EffRoomGlowSet((u_char)pGlow->R, (u_char)pGlow->G, (u_char)pGlow->B, (u_char)pGlow->Alpha,
                   (u_char)pGlow->Boost);

    return (short *)(pGlow + 1);
}

// FuncNo 29: a man model's aura parameters (speeds in hundredths).
// FF3 0x00256d20.

static short *SceneEffectReqModelAura(short *pData, int Frame)
{
    SCENE_EFFECT_MODEL_AURA *pAura = (SCENE_EFFECT_MODEL_AURA *)pData;

    (void)Frame;
    SceneManModelAuraSet((float)pAura->Speed / 100.0f, (float)pAura->F3c / 100.0f,
                         pAura->ModelId, pAura->Index, (u_char)pAura->Type,
                         (u_char)pAura->Alpha, (u_char)pAura->Amp);

    return (short *)(pAura + 1);
}
