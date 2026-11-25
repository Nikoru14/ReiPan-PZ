/* ==========================================================================
 *  ingame/photo/photo_obj.c
 *
 *  The photo objects of the current area: glow slots, the finder target,
 *  the filament's hint and the hum (FF3-only; see photo_obj.h).
 *  FF3 0x2289a8..0x229990. The slots are filled by area (PhotoObjAreaOn /
 *  Off from the room changes and SET_PHOTOABLE_) and run by PhotoObjMain
 *  every in-game frame.
 * ======================================================================== */
#include "ingame/photo/photo_obj.h"

#include <math.h>
#include <string.h>

#include "graphics/effect/effect.h"            // SetEffects
#include "graphics/effect/effect_obj.h"        // EffKind1E*, EffPDeformPrmGet
#include "graphics/graph3d/gra3d.h"            // gra3dcamGetPosition
#include "ingame/ingame_wrk.h"                 // ingame_wrk.plyr_wrk
#include "ingame/photo/finder_photo.h"         // GetPhotoModeWrk180 / 244
#include "ingame/photo/fndr_effect.h"          // PhotoWrk180SetBit0
#include "ingame/photo/fndr_parts.h"           // FilamentSetHint, CWrkVariable<short, 0, 128>
#include "ingame/photo/photo_obj_dat.h"        // the table and its bit sets
#include "ingame/plyr/player.h"                // PLYR_WRK, PlyrFinderTargetCheck, PlyrViewDist*
#include "ingame/plyr/plyr_mdl.h"              // PlyrAreaLineHitCheck
#include "ingame/plyr/plyr_param.h"            // PlyrGetFinderRadius
#include "ingame/plyr/plyr_sub.h"              // PlyrGetJointPos
#include "ingame/sound/snd_ctrl.h"             // SndSePlay / SndSeVolFade / SndSeFadeStop
#include "system/os/system.h"                  // GetPALMode

// The player's distance to pos on the ground plane: FF3 inlines the VU0
// sequence (vsub.xyz, vmul.xyz, vaddz.x, vsqrt) at both uses; y is ignored.

static float PhotoObjPlyrDistXZ(const float *pos)
{
    float *p  = ingame_wrk.plyr_wrk->pos;
    float  dx = p[0] - pos[0];
    float  dz = p[2] - pos[2];

    return sqrtf(dx * dx + dz * dz);
}

// ── state ──────────────────────────────────────────────────────────────

static PHOTO_OBJ_SLOT  photo_obj_slot[PHOTO_OBJ_SLOT_NUM];   /* bss 0x3f66e0 */
static PHOTO_OBJ_STATE photo_obj_state;                      /* bss 0x3f6820 */

// The hum, bss 0x3f6850. FF3 0x00229510 (the file's
// __static_initialization_and_destruction_0, PhotoObjStaticInit) zeroes the
// four slot alphas (CWrkVariable) and constructs it at start-up (FF3
// 0x002295c8, _GLOBAL__I_, .ctors 0x2e9174) and destroys it at exit (FF3
// 0x002295e8, _GLOBAL__D_, .dtors 0x2e91b8); the C++ statics do both here.

static photoSe photo_se;

// ──────────────────────────────────────────────────────────────────────
// A 2-bit mode of a record (PHOTO_OBJ_SH_*): 1 on, 2 on while the
// viewfinder is up, else off. FF3's callers pass the field sign-extended
// ((bits << n) >> 30); only its low two bits are compared.
// FF3 0x002289a8.

int PhotoObjModeIsOn(u_int mode)
{
    switch (mode & 3)
    {
    case 1:
        return 1;
    case 2:
        return (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0;
    default:
        return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The seal door's burn (photo_dat.c 0x227eb0): the glow alphas stay where
// they are until the viewfinder closes.
// FF3 0x002289e8.

void PhotoObjHoldAlpha(void)
{
    photo_obj_state.hold = 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002289f8.

int PhotoObjGetTarget(void)
{
    return photo_obj_state.target;
}

// ──────────────────────────────────────────────────────────────────────
// No caller in the ROM.
// FF3 0x00228a08.

int PhotoObjTargetIsOn(void)
{
    PHOTO_OBJ_DAT *dat;

    if (photo_obj_state.target <= 0)
        return 0;

    dat = PhotoObjGetDat(photo_obj_state.target);
    if (dat == nullptr)
        return 0;

    return PhotoObjModeIsOn(PHOTO_OBJ_MODE(dat->bits, PHOTO_OBJ_SH_FINDER)) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// No caller in the ROM (Ghidra had no function here). 1.0 is .lit4 0x371874.
// FF3 0x00228a58.

int PhotoObjHintInView(float dmax, float deg)
{
    if (photo_obj_state.hint == 0)
        return 0;

    return PlyrViewDistCheck(1.0f, dmax, deg, photo_obj_state.hint_pos);
}

// ──────────────────────────────────────────────────────────────────────
// No caller in the ROM (Ghidra had no function here).
// FF3 0x00228aa0.

float *PhotoObjGetHintPos(void)
{
    return photo_obj_state.hint != 0 ? photo_obj_state.hint_pos : nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// Save block 30. FF3 saves 0x140 bytes; the host table is larger (pointer).
// FF3 0x00228ab8.

int PhotoObjSlotSaveGet(void **addr)
{
    *addr = photo_obj_slot;
    return sizeof(photo_obj_slot);
}

// ──────────────────────────────────────────────────────────────────────
// PC: not in FF3. Block 30 carries each slot's effect pointer. On the EE the
// effect banks never move, so a loaded pointer still names a record (a free
// one: IngameInit's InitEffects ran before the load); on the host it belongs
// to the process that saved. Drop it -- the object is kept, and the room
// load's PhotoObjAreaOn starts the current area's glows again.

void PhotoObjLoadRestore(void)
{
    int i;

    for (i = 0; i < PHOTO_OBJ_SLOT_NUM; i++)
        photo_obj_slot[i].eff = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// One parts deform at the slot's glow point for this frame (SetEffects 0x16
// fl 1), its speed / wave values copied into the slot. FF3 writes this out
// three times in PhotoObjSlotEffStep.

static void PhotoObjDeformReq(PHOTO_OBJ_SLOT *slot, int i, EFF_PDEFORM_PRM *prm, int alpha)
{
    slot->fw[i][0] = prm->Fw[0];
    slot->fw[i][1] = prm->Fw[1];
    SetEffects(0x16, 1, prm->Type, alpha, (double)prm->Prm[0], (double)prm->Prm[1], slot->eff_pos,
               0, 0, 0, (void *)nullptr, (void *)&slot->fw[i][0], (void *)&slot->fw[i][1], (void *)nullptr,
               prm->Col[0], prm->Col[1], prm->Col[2]);
}

// ──────────────────────────────────────────────────────────────────────
// The rate steps 0.1 toward rate_to (.lit4 0x371878); while above 0 the
// glow point is placed toward the camera and the deforms run: rows 0 and 1
// for type 2, row 2 for types 1 and 3..6, each at rate * its alpha.
// FF3 0x00228ad0.
// callcheck: SetEffects, fptodp (in PhotoObjDeformReq)

static void PhotoObjSlotEffStep(int type, PHOTO_OBJ_SLOT *slot)
{
    sceVu0FMATRIX    mat;
    EFF_PDEFORM_PRM *prm;
    EFF_PDEFORM_PRM *prm1;
    float            rate = slot->rate;
    float            to   = slot->rate_to;
    int              alpha;
    int              alpha1;

    if (rate < to)
    {
        rate += 0.1f;
        slot->rate = rate;
        if (to < rate)
        {
            slot->rate = to;
            rate = to;
        }
    }
    else if (to < rate)
    {
        rate -= 0.1f;
        slot->rate = rate;
        if (rate < to)
        {
            slot->rate = to;
            rate = to;
        }
    }

    if (rate <= 0.0f)
    {
        slot->rate = 0.0f;
        return;
    }

    EffKind1EMatrix(slot->eff_pos, mat, slot->pos);

    if (type == 2)
    {
        prm  = EffPDeformPrmGet(0);
        prm1 = EffPDeformPrmGet(1);
        alpha  = (int)(slot->rate * (float)prm->Alpha);
        alpha1 = (int)(slot->rate * (float)prm1->Alpha);

        if (prm->Alpha > 0)
            PhotoObjDeformReq(slot, 0, prm, alpha);
        if (prm1->Alpha > 0)
            PhotoObjDeformReq(slot, 1, prm1, alpha1);
    }
    else if (type == 1 || (type >= 3 && type <= 6))
    {
        prm   = EffPDeformPrmGet(2);
        alpha = (int)(slot->rate * (float)prm->Alpha);

        if (prm->Alpha > 0)
            PhotoObjDeformReq(slot, 0, prm, alpha);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Every in-game frame (IngameMain). Per filled slot: the nearest object
// with its near mode on (photoSe, the hint); with the viewfinder up and
// its finder mode on, the finder test -- inside the capture circle the
// alpha rises (the nearest to the centre becomes the target and the
// finder point glows), else it falls -- and an object behind a wall (f34
// bit 1) is skipped for this frame; then the glow's rate. Constants .lit4
// 0x37187c (9999), 0x371880 (100000), 0x371884 (128), 0x371888 (PI),
// 0x37188c (1), 0x371890..98 (10, 200, 179).
// FF3 0x00228de8.

void PhotoObjMain(void)
{
    PLYR_WRK       *pw;
    PHOTO_OBJ_SLOT *slot;
    PHOTO_OBJ_DAT  *dat;
    sceVu0FVECTOR   near_pos = { 0.0f, 0.0f, 0.0f, 0.0f };  // PC: FF3 leaves it unset when no object is near
    sceVu0FVECTOR   eye;
    float           near_dist   = 100000.0f;
    float           target_dist = 9999.0f;
    float           finder_dist;
    float           dist;
    float           rate;
    int             near_no = -1;
    int             in_frame;
    int             delta;
    int             i;

    photo_obj_state.target = -1;
    photo_obj_state.hint = 0;
    FilamentSetHint(GetPhotoModeWrk244(), 0.0f);
    PhotoWrk180SetBit0(GetPhotoModeWrk180(), 0);

    for (i = 0; i < PHOTO_OBJ_SLOT_NUM; i++)
    {
        slot = &photo_obj_slot[i];
        if (slot->no == -1)
            continue;

        dat  = PhotoObjGetDat(slot->no);
        dist = PhotoObjPlyrDistXZ(slot->pos);

        if (dist < near_dist && PhotoObjModeIsOn(PHOTO_OBJ_MODE(dat->bits, PHOTO_OBJ_SH_NEAR)))
        {
            near_dist = dist;
            sceVu0CopyVector(near_pos, slot->pos);
            near_no = slot->no;
        }

        if (slot->eff != nullptr)
            EffKind1ESetRate(0.0f, slot->eff);

        pw = ingame_wrk.plyr_wrk;
        if ((pw->flags & PLYR_FLG_800000) != 0 && (s_char)pw->_260 < 2
            && PhotoObjModeIsOn(PHOTO_OBJ_MODE(dat->bits, PHOTO_OBJ_SH_FINDER)))
        {
            delta = GetPALMode() != 0 ? -3 : -4;
            in_frame = PlyrFinderTargetCheck(3.1415927f, dat->range, slot->pos, &finder_dist);

            if (dat->f34 & PHOTO_OBJ_F34_LINE)
            {
                if (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000)
                    PlyrGetJointPos(2, eye, nullptr);
                else
                    sceVu0CopyVector(eye, gra3dcamGetPosition());

                if (PlyrAreaLineHitCheck(eye, slot->pos) != 0)
                    continue;
            }

            if (in_frame != 0 && finder_dist <= PlyrGetFinderRadius(0xff, 0xff))
            {
                if (finder_dist < target_dist)
                {
                    photo_obj_state.target = slot->no;
                    target_dist = finder_dist;
                }
                delta = -delta;
                PhotoWrk180SetBit0(GetPhotoModeWrk180(), 1);
            }

            slot->alpha.delta = (short)delta;
            if (slot->eff != nullptr && photo_obj_state.hold == 0 && dat->type == 2)
            {
                slot->alpha.Work();
                EffKind1ESetRate((float)slot->alpha.val / 128.0f, slot->eff);
            }
        }

        slot->rate_to = PhotoObjModeIsOn(PHOTO_OBJ_MODE(dat->bits, PHOTO_OBJ_SH_GLOW)) ? 1.0f : 0.0f;
        PhotoObjSlotEffStep(dat->type, slot);
    }

    photo_se.Update(near_no, near_pos);

    if (photo_obj_state.target >= 0 || near_no >= 0)
    {
        pw = ingame_wrk.plyr_wrk;
        if ((pw->flags & PLYR_FLG_800000) == 0 || (s_char)pw->_260 < 2)
            rate = PlyrViewDistRate(10.0f, 200.0f, 179.0f, near_pos);
        else
            rate = 0.0f;

        FilamentSetHint(GetPhotoModeWrk244(), rate);
        photo_obj_state.hint = 1;
        sceVu0CopyVector(photo_obj_state.hint_pos, near_pos);
    }

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) == 0)
        photo_obj_state.hold = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Photoable, and not a once-only object already photographed.
// FF3 0x00229258.

static int PhotoObjIsActive(int no)
{
    PHOTO_OBJ_DAT *dat = PhotoObjGetDat(no);

    if ((dat->flags & PHOTO_OBJ_FLG_ONCE) && PhotoObjIsPhotographed((u_int)no) != 0)
        return 0;

    return PhotoObjIsPhotoable((u_int)no) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// The slot holding object no (-1: a free slot), null when none.
// FF3 0x002292b0.

static PHOTO_OBJ_SLOT *PhotoObjSlotFind(int no)
{
    int i;

    for (i = 0; i < PHOTO_OBJ_SLOT_NUM; i++)
    {
        if (photo_obj_slot[i].no == no)
            return &photo_obj_slot[i];
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// An active object takes a free slot and starts its kind 0x1e glow at
// rate 0 (.lit4 0x37189c: w 1).
// FF3 0x002292e8.

static void PhotoObjSlotSet(int no)
{
    PHOTO_OBJ_SLOT *slot;
    PHOTO_OBJ_DAT  *dat;

    if (PhotoObjIsActive(no) == 0)
    {
        return;
    }

    slot = PhotoObjSlotFind(-1);
    if (slot == nullptr)
        return;                         // PC: FF3 writes through the null pointer (a fifth object in one area)

    slot->no = no;
    slot->alpha.val = 0;
    slot->alpha.delta = 0;
    dat = PhotoObjGetDat(no);
    slot->pos[0] = dat->pos[0];
    slot->pos[1] = dat->pos[1];
    slot->pos[2] = dat->pos[2];
    slot->pos[3] = 1.0f;
    slot->rate = 0.0f;
    slot->rate_to = 0.0f;
    slot->eff = EffKind1EReq(slot->rate, slot->pos);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00229380.

void PhotoObjSlotCut(int no)
{
    PHOTO_OBJ_SLOT *slot = PhotoObjSlotFind(no);

    if (slot == nullptr)
        return;

    if (slot->eff != nullptr)
    {
        EffKind1ECut(slot->eff);
        slot->eff = nullptr;
    }
    slot->no = -1;
}

// ──────────────────────────────────────────────────────────────────────
// Objects 1..65 of the area drop their slot; with on, the active ones take
// one again.
// FF3 0x002293d0.

static int PhotoObjAreaSet(short area, int on)
{
    int no;

    for (no = 1; no < PHOTO_OBJ_NUM; no++)
    {
        if (PhotoObjGetDat(no)->area == area)
        {
            PhotoObjSlotCut(no);
            if (on != 0)
                PhotoObjSlotSet(no);
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00229448.

void PhotoObjAreaOn(int area)
{
    PhotoObjAreaSet((short)area, 1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00229460.

void PhotoObjAreaOff(int area)
{
    PhotoObjAreaSet((short)area, 0);
}

// ──────────────────────────────────────────────────────────────────────
// No caller in the ROM.
// FF3 0x00229478.

void PhotoObjSeStop(void)
{
    photo_se.Stop();
}

// ──────────────────────────────────────────────────────────────────────
// PhotoDatInit: every slot free, no target.
// FF3 0x00229498.

void PhotoObjInit(void)
{
    int i;

    memset(photo_obj_slot, 0, sizeof(photo_obj_slot));
    for (i = 0; i < PHOTO_OBJ_SLOT_NUM; i++)
        photo_obj_slot[i].no = -1;

    memset(&photo_obj_state, 0, sizeof(photo_obj_state));
    photo_obj_state.target = -1;
}

// ── photoSeHdl ─────────────────────────────────────────────────────────

// FF3 0x00229618 (FF3 0x00229608 is the same body, unreferenced).

photoSeHdl::photoSeHdl()
{
    id = PHOTO_SE_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// Playing: the volume fades to vol over a frame. Else the object type's
// SE starts: type 3 0x1a9b000a, types 1 / 2 / 6 0x1a9b0008, others SE 0.
// Always 0 in FF3.
// FF3 0x00229628.

void photoSeHdl::Play(int vol, int type)
{
    u_int se;

    if (id != PHOTO_SE_NONE)
    {
        SndSeVolFade(id, vol, 1);
        return;
    }

    se = 0;
    if (type == 3)
        se = 0x1a9b000a;
    else if (type == 1 || type == 2 || type == 6)
        se = 0x1a9b0008;

    id = SndSePlay(se, vol, 0x1000, 0, nullptr, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002296c8.

void photoSeHdl::Stop(int time)
{
    if (id != PHOTO_SE_NONE)
    {
        SndSeFadeStop(id, time);
        id = PHOTO_SE_NONE;
    }
}

// ── photoSe ────────────────────────────────────────────────────────────

// Range: .lit4 0x3718a4 (200; the unreferenced copy 0x00229710 reads the
// same value from 0x3718a0).
// FF3 0x00229760.
// callcheck: photoSeHdlCtor (the members' constructors)

photoSe::photoSe()
{
    range = 200.0f;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002297d8 (complete), 0x002297b0 (base), 0x00229800 (deleting).

photoSe::~photoSe()
{
    Stop();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00229838.

void photoSe::Stop()
{
    int i;

    for (i = 0; i < 2; i++)
        se[i].Stop(30);
}

// ──────────────────────────────────────────────────────────────────────
// Object no (from PhotoObjMain: the nearest with its near mode set) hums
// at (range - distance) * 127 / range (.lit4 0x3718a8) within range on
// the ground plane; otherwise, or with no object, the hum fades out.
// FF3 0x00229880.

void photoSe::Update(int no, float *pos)
{
    PHOTO_OBJ_DAT *dat;
    float          dist;
    int            vol;
    int            i;

    if (no >= 0)
    {
        dat = PhotoObjGetDat(no);
        if (dat != nullptr && (dat->bits & (3u << PHOTO_OBJ_SH_NEAR)) != 0)
        {
            dist = PhotoObjPlyrDistXZ(pos);
            if (dist <= range)
            {
                vol = (int)((range - dist) * 127.0f / range);
                for (i = 0; i < 2; i++)
                    se[i].Play(vol, dat->type);
                return;
            }
        }
    }

    Stop();
}
