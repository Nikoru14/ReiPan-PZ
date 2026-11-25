/* ==========================================================================
 *  ingame/photo/photo_obj.h
 *
 *  The photo objects of the current area (FF3-only, FF3 0x2289a8..0x229990;
 *  file name chosen by content and slot: photo_make < photo_obj < player.
 *  The FF2 link order put it in ingame/menu/play_data.c's slot, whose five
 *  "matches" were wrong). Up to four objects of the room (photo_obj_dat.c's
 *  table) hold a glow slot: a kind 0x1e effect and one-frame parts deforms
 *  at the object, faded by the finder. Every frame PhotoObjMain finds the
 *  object nearest the finder centre (the target), the nearest one in reach
 *  (the filament's hint and photoSe's hum), and fades the glow.
 * ======================================================================== */
#ifndef _INGAME_PHOTO_PHOTO_OBJ_H
#define _INGAME_PHOTO_PHOTO_OBJ_H

#include <stddef.h>                     // offsetof
#include "sdk/scetypes.h"
#include "sdk/libvu0.h"                 // sceVu0FVECTOR
#include "ingame/photo/fndr_parts.h"    // CWRK_VAR_SHORT_0_128

#define PHOTO_OBJ_SLOT_NUM  4           // bss 0x3f66e0, 4 x 0x50

// One glow slot (bss 0x3f66e0 + i * 0x50, save block 30: FF3 saves the
// whole 0x140 bytes, the effect pointer included; PhotoObjLoadRestore drops
// it after a load).
// layout: host -- eff is a pointer (EE 4 bytes at +0x04); later fields
// shift by 4 / 16 on the host. EE offsets in the comments.
struct PHOTO_OBJ_SLOT                   /* 0x50 */
{
    /* 0x00 */ int                  no;         // photo object, -1: free
    /* 0x04 */ void                *eff;        // EffKind1EReq's record, null: none
    /* 0x08 */ CWRK_VAR_SHORT_0_128 alpha;      // kind 0x1e's alpha 0..128 (/ 128); delta -+3 PAL, -+4 NTSC
    /* 0x0c */ int                  _0c;
    /* 0x10 */ sceVu0FVECTOR        pos;        // the object's position, w 1
    /* 0x20 */ sceVu0FVECTOR        eff_pos;    // EffKind1EMatrix's point: the deforms' position
    /* 0x30 */ float                fw[2][2];   // per deform: Fw[0] / Fw[1] of its EFF_PDEFORM_PRM (pSpeed / pWave)
    /* 0x40 */ float                rate;       // the deforms' rate, steps 0.1 a frame toward rate_to
    /* 0x44 */ float                rate_to;    // 1 while the glow mode is on, else 0
    /* 0x48 */ int                  _48[2];
};
static_assert(offsetof(PHOTO_OBJ_SLOT, no) == 0x0, "PHOTO_OBJ_SLOT.no");

// The per-frame result (bss 0x3f6820, 0x30 bytes cleared by PhotoObjInit).
struct PHOTO_OBJ_STATE                  /* 0x30 */
{
    /* 0x00 */ int           target;    // object nearest the finder centre in its circle, -1: none
    /* 0x04 */ u_char        hint;      // hint_pos is valid (written as a byte)
    /* 0x05 */ u_char        _05[0xb];
    /* 0x10 */ sceVu0FVECTOR hint_pos;  // the nearest object in reach (the filament's hint)
    /* 0x20 */ int           hold;      // PhotoObjHoldAlpha: no alpha steps until the finder closes
    /* 0x24 */ int           _24[3];
};
static_assert(sizeof(PHOTO_OBJ_STATE) == 0x30, "PHOTO_OBJ_STATE: PhotoObjInit's memset");
static_assert(offsetof(PHOTO_OBJ_STATE, hint) == 0x04, "PHOTO_OBJ_STATE.hint");
static_assert(offsetof(PHOTO_OBJ_STATE, hint_pos) == 0x10, "PHOTO_OBJ_STATE.hint_pos");
static_assert(offsetof(PHOTO_OBJ_STATE, hold) == 0x20, "PHOTO_OBJ_STATE.hold");

#define PHOTO_SE_NONE       0x300000    // photoSeHdl::id when nothing plays

// A looping SE voice (no RTTI; name invented). Same layout on every compiler.
class photoSeHdl                        /* 0x4 */
{
public:
    photoSeHdl();                       // FF3 0x00229618 (photoSeHdlCtor); copy 0x00229608 unreferenced
    void Play(int vol, int type);       // FF3 0x00229628: start for the object type, or set the volume
    void Stop(int time);                // FF3 0x002296c8: fade out

    /* 0x0 */ int id;                   // SndSePlay's id, PHOTO_SE_NONE
};
static_assert(sizeof(photoSeHdl) == 0x4, "photoSeHdl is one word");

// The hum near a photo object (RTTI photoSe, vtable 0x3423d8, bss 0x3f6850).
// 0x10 bytes on the EE.
// layout: host -- the vptr is 4 bytes on the EE: se at EE +0x04, range +0x0c.
class photoSe
{
public:
    photoSe();                          // FF3 0x00229760 (photoSeCtor); copy 0x00229710 unreferenced
    virtual ~photoSe();                 // EE vtable +0x00 0x002297d8 (base 0x002297b0), deleting +0x04 0x00229800: Stop

    void Stop();                        // FF3 0x00229838: both voices fade out over 30
    void Update(int no, float *pos);    // FF3 0x00229880: hum by the XZ distance to object no

    photoSeHdl se[2];                   // EE +0x04: two voices of the same SE
    float      range;                   // EE +0x0c: 200, the hum's reach
};
static_assert(sizeof(photoSeHdl[2]) == 0x8, "photoSe.se is two words");

int    PhotoObjModeIsOn(u_int mode);                // FF3 0x002289a8
void   PhotoObjHoldAlpha(void);                     // FF3 0x002289e8: photo_dat.c 0x227eb0
int    PhotoObjGetTarget(void);                     // FF3 0x002289f8: roku_pzl.c 0x23c868
int    PhotoObjTargetIsOn(void);                    // FF3 0x00228a08: unreferenced
int    PhotoObjHintInView(float dmax, float deg);   // FF3 0x00228a58: unreferenced
float *PhotoObjGetHintPos(void);                    // FF3 0x00228aa0: unreferenced
int    PhotoObjSlotSaveGet(void **addr);            // FF3 0x00228ab8: save block 30 {0x3f66e0, 0x140}
void   PhotoObjLoadRestore(void);                   // PC: IngameLoadRestore, the saved effect pointers dropped
void   PhotoObjMain(void);                          // FF3 0x00228de8: IngameMain, every frame
void   PhotoObjAreaOn(int area);                    // FF3 0x00229448: AREA_CHANGE, SET_PHOTOABLE_, doors, reload
void   PhotoObjAreaOff(int area);                   // FF3 0x00229460: the room buffer being left
void   PhotoObjSeStop(void);                        // FF3 0x00229478: unreferenced
void   PhotoObjInit(void);                          // FF3 0x00229498: PhotoDatInit
void   PhotoObjSlotCut(int no);                     // FF3 0x00229380: also PhotoObjSetPhotographed

#endif /* _INGAME_PHOTO_PHOTO_OBJ_H */
