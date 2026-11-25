// ingame/plyr/plyr_shot.c -- the shot judging, FF3 0x23c028..0x23e208
// (pinned 2026-10-19; see plyr_shot.h). FF3-only code: the FF2 names the
// matcher gave this run (SixPuzzle*, CostumeSetSave) were wrong, and FF2's
// own shot code (player.c PhotoDmgChk2 / PhotoPointCulcEne2 /
// PlayerTakePictJob) works by other rules.
//
// A shot runs PlyrShotJudge then, for a shutter shot, PlyrShotRecord. The
// judge walks the 16 enemies: an acting one on screen (flags 0x80) is hit
// (0x400000000); one that was charging the camera (0x10, set each frame by
// PlyrShotChargeCtrl) takes damage, its reaction and the hit effects, and
// counts towards the bonuses. PlyrShotScore then scores every hit ghost
// into the score panel (PHOTO_WRK_288). The record files the picture: the
// subjects (the ghosts' soul-list entries, else the photo object in view),
// the album record (AddPhotoData) and the photo sequence's definition.
//
// FF3 0x0023e1c8 / 0x0023e1e8 are compiler output: the atexit thunks of the
// GPhaseCtrl (0x41e468) and CandleControl (0x41e540) singletons this file's
// inline getters instantiate; implicit for the C++ statics on the host.

#include <math.h>                       // ceil, sqrtf
#include <string.h>                     // memset

#include "common/reipan_stub.h"
#include "ingame/plyr/plyr_shot.h"

#include "common/vu0math.h"             // RotLimitChk
#include "graphics/effect/effect_ene.h" // EneHitEffectReq, ENE_DMG_PARTICLE_REQ, EneDmgParticleEffectReq
#include "ingame/camera/finder_camera.h"    // FINDER_MODE_WRK, GetFinderModeWrk
#include "ingame/candle_ctrl.h"         // GetCandleControl
#include "ingame/enemy/ene_main.h"      // ene_wrk, ENE_WRK, ENE_CMN, IsActEnemy, EneMahi*, ShutterChanceChk, ...
#include "ingame/enemy/enemy.h"         // EneIdFlgSet, EneIdFlgCheck
#include "ingame/enemy/random_ghost.h"  // RandGhostCntReset
#include "ingame/ingame_effect.h"       // IgEffectSubFuncPDeformClear, IgEffectParticleEndPosFinderGet
#include "ingame/ingame_wrk.h"          // ingame_wrk, title_start_flg, IngameWrkAddPoint, IngameWrkSetBestScore
#include "ingame/item/prg/item.h"       // ItemGetCnt
#include "ingame/item/prg/soul_list.h"  // SoulListSetHave
#include "ingame/photo/finder_photo.h"  // GetPhotoModeWrk and its parts, PhotoModeSePlay
#include "ingame/photo/finder_view.h"   // FinderViewSetTurnTime, FinderViewTurnReq
#include "ingame/photo/fndr_effect.h"   // PHOTO_WRK_1E8, PHOTO_WRK_288, PhotoWrk288Req, PhotoWrk180SetBit4
#include "ingame/photo/fndr_parts.h"    // LENS_WRK, DamageDispReq, FilmGetType, PhotoChargerIsReady, ...
#include "ingame/photo/photo_dat.h"     // PHOTO_WRK_DEF, PhotoSeqReset, PhotoSeqStart
#include "ingame/photo/photo_file.h"    // GetSavePhotoNo, AddPhotoData
#include "ingame/photo/photo_obj.h"     // PhotoObjGetTarget
#include "ingame/photo/photo_obj_dat.h" // PHOTO_OBJ_DAT, PhotoObjGetDat, PhotoObjIsPhotographed, the ene subject
#include "ingame/plyr/player.h"         // PLYR_WRK, PLYR_FLG_*, PlyrGetCurNo, PlyrSePlay, PlyrGetEquipLv, PlyrIsHeldBy
#include "ingame/plyr/plyr_gauge.h"     // PlyrGaugeAdd
#include "ingame/plyr/plyr_param.h"     // PlyrGetChargeMax, PlyrGetShotRange, PlyrGetShotRate, PlyrGetCamLvCharge
#include "ingame/plyr/unit_ctl.h"       // UnitGetTrgtRot
#include "reipan/gameplay/reipan_haptic_cues.h"     // PC: the Camera Obscura in the triggers
#include "sdk/libcdvd.h"                // sceCdCLOCK, sceCdReadClock
#include "sdk/libscf.h"                 // sceScfGetLocalTimefromRTC
#include "system/os/system.h"           // GetPALMode

static void PlyrShotScore(char lens);
static void PlyrShotScoreEne(ENE_WRK *ene, char lens);
static int  PlyrShotSubjectAdd(SUBJECT_WRK *list, short type, short no, int n);
static int  PlyrShotSubjectAddEne(SUBJECT_WRK *list, short soul_no, int n);
static int  PlyrShotSubjectsEne(SUBJECT_WRK *list, int n);
static int  PlyrShotSubjectObj(PHOTO_WRK_DEF *def, SUBJECT_WRK *list, int *score, int n);
static void PlyrShotDamage(ENE_WRK *ene, char lens);
static void PlyrShotHitEffect(ENE_WRK *ene, char lens);
static void PlyrShotParticle(ENE_WRK *ene);
static void FinderViewTurnToEneSet(float time, int no);

// ENE_WRK.flags bits this file sets or tests (besides those ene_main.h
// lists): 0x10 charging the camera, 0x400000000 hit by this shot,
// 0x20000000000 -> 0x40000000000 finished by a damaging shot,
// 0x10000000000 a finder fatal-frame hit, 0x2000000000 cleared with the
// shutter-chance bits when a reaction is spent.
#define ENE_FLG_ON_SCREEN   0x80ULL
#define ENE_FLG_CHARGE      0x10ULL
#define ENE_FLG_SC          0x800ULL            // hit in a shutter chance
#define ENE_FLG_FF          0x1000ULL           // hit in a fatal frame
#define ENE_FLG_NO_SHOT     0x10000ULL
#define ENE_FLG_NO_TARGET   0x100000ULL
#define ENE_FLG_HIT         0x400000000ULL
#define ENE_FLG_FINISH_REQ  0x20000000000ULL
#define ENE_FLG_FINISHED    0x40000000000ULL
#define ENE_FLG_REACT_BITS  0x12000001800ULL    // cleared with dmg_react

// The lens numbers the shot code knows (LENS_WRK::GetSelEquip).
#define SHOT_LENS_RATE      3                   // its rate, full in a shutter chance / fatal frame
#define SHOT_LENS_REVEAL    4                   // ENE_WRK.alpha_a 0x80
#define SHOT_LENS_STOP      5                   // paralysis 0 (EneMahi0Req), frees a held player; no damage
#define SHOT_LENS_SLOW      6                   // paralysis 1 (EneMahi1Req); no damage
#define SHOT_LENS_MAHI2     7                   // paralysis 2 (EneMahi2Req)
#define SHOT_LENS_STONE     8                   // the stones' rate

// ──────────────────────────────────────────────────────────────────────
// The score

// Every on-screen ghost the shot hit: the subject check, its score (not
// for a lens shot of rate 0, nor for flags2 0x100), the panel's corner
// counts, the ghost's id flags 1 / 0x8000; then the clamped total to the
// panel, the points, the best score and the mission work. Nothing while
// PLYR_FLG_000004.
// FF3 0x0023c028

static void PlyrShotScore(char lens)
{
    PHOTO_WRK_288 *w288 = GetPhotoModeWrk288();

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_000004) != 0)
        return;

    PhotoObjClrEneSubject();
    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if ((ene->flags & (ENE_FLG_HIT | ENE_FLG_ON_SCREEN)) != (ENE_FLG_HIT | ENE_FLG_ON_SCREEN))
            continue;

        LENS_WRK *lw = GetLensWrk();

        PhotoObjCheckEne(ene->type, ene->id);
        if ((lens != 1 || lw->GetSelRate() != 0.0f) && (ene->flags2 & 0x100) == 0)
        {
            PlyrShotScoreEne(ene, lens);
            if (ene->type != 2)
            {
                if ((ene->flags & ENE_FLG_FF) != 0)
                    w288->corner_a++;
                else if ((ene->flags & ENE_FLG_SC) != 0)
                    w288->corner_b++;
            }
        }
        EneIdFlgSet(ene->type, ene->id, 1);
        EneIdFlgSet(ene->type, ene->id, 0x8000);
    }

    w288->score = w288->on[SHOT_SCORE_RAW];
    if ((u_int)w288->on[SHOT_SCORE_RAW] > 99999)
        w288->score = 99999;

    PhotoWrk288Req(w288, w288->score, 10);
    if (title_start_flg.mode != 2)
        IngameWrkAddPoint(w288->score);
    IngameWrkSetBestScore(w288->score);
    GetFinderModeWrk()->AddScore(w288->score);
}

// One ghost's score into the panel. A special ghost (type 2) gives its
// cmn->score once (id flag 0x8000 not yet set); an ordinary one scores
// off the damage: cmn->score * dmg * 5 / 1000 (plus cmn->score itself
// when it is finished), times 1 + 0.3 per level above 1 in a big reaction
// and + 0.15 (+ 0.3 for the triples) per bonus, each bonus counted in its
// on[] row. The sum is in doubles, as the ROM's soft-float calls.
// FF3 0x0023c250

static void PlyrShotScoreEne(ENE_WRK *ene, char lens)
{
    PHOTO_WRK_288 *w288 = GetPhotoModeWrk288();

    if (ene->type > 1)
    {
        if (ene->type != 2)
            return;
        if (EneIdFlgCheck(2, ene->id, 0x8000) != 0)
            return;
        w288->on[SHOT_SCORE_RAW] += ene->cmn->score;
        return;
    }

    int     dmg  = ene->dmg;
    double  mag  = 1.0;
    u_short base = ene->cmn->score;
    int     left = ene->hp - dmg;           // <= 0: finished

    if (EneIsDmgReactBig(ene))
    {
        s_char level = ene->level;

        if (level > 1)
        {
            mag = 1.0 + (double)(level - 1) * 0.3;
            w288->level = level;
        }
    }

    if (GetPhotoModeWrk1e8()->GetNum() == PlyrGetChargeMax(0xff, 0xff))
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_CHARGE]++;
        if ((ene->flags & (ENE_FLG_SC | ENE_FLG_FF)) != 0)
        {
            mag += 0.15;
            w288->on[SHOT_BONUS_CHARGE_SC]++;
            if (left < 1)
            {
                mag += 0.15;
                w288->on[SHOT_BONUS_CHARGE_KILL]++;
            }
        }
    }

    if (ene->dist < 20.0f)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_CLOSE]++;
    }
    if (ene->fdr_dist <= 20.0f)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_CORE]++;
    }
    if (lens == 1 || GetPhotoModeWrk1e8()->GetNumSub() > 0)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_LENS]++;
    }
    if ((u_int)(left + 49) < 50)            // -49 .. 0
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_JUST]++;
    }
    if (left < -199)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_OVER]++;
    }

    if (w288->hit_cnt >= 3)
    {
        mag += 0.3;
        w288->on[SHOT_BONUS_TRIPLE]++;
    }
    else if (w288->hit_cnt >= 2)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_DOUBLE]++;
    }

    if (w288->kill_cnt >= 3)
    {
        mag += 0.3;
        w288->on[SHOT_BONUS_TRIPLE_KILL]++;
    }
    else if (w288->kill_cnt >= 2)
    {
        mag += 0.15;
        w288->on[SHOT_BONUS_DOUBLE_KILL]++;
    }

    double score;

    if (left < 1)
        score = ((double)base + (double)base * (double)dmg * 5.0 / 1000.0) * mag;
    else
        score = (double)(int)(base * dmg) * 5.0 * mag / 1000.0;

    w288->on[SHOT_SCORE_RAW] += (int)ceil(score);
}

// ──────────────────────────────────────────────────────────────────────
// The photo definition (PHOTO_WRK_DEF's methods, photo_dat.h)

// FF3 0x0023c688

int PHOTO_WRK_DEF::GetSpecial()
{
    return -1;
}

// FF3 0x0023c690

void PHOTO_WRK_DEF::Init()
{
    memset(this, 0, sizeof(*this));
    unlock_ghost = 0;
    msg_type     = -1;
    type         = 0;
    hint_cnt     = 0;
    msg_name     = -1;
}

// FF3 0x0023c6d8

void PHOTO_WRK_DEF::AddHint(int no)
{
    hint_pict[hint_cnt] = no;
    hint_cnt++;
}

// ──────────────────────────────────────────────────────────────────────
// The subjects

// list[n] = (type, no); the count stops at 14, so further subjects keep
// overwriting the last entry.
// FF3 0x0023c6f8

static int PlyrShotSubjectAdd(SUBJECT_WRK *list, short type, short no, int n)
{
    list[n].type = type;
    list[n].no   = (u_short)no;
    return n + (n != SUBJECT_MAX - 1);
}

// A ghost with a soul-list entry: the entry is unlocked and the ghost is
// a subject.
// FF3 0x0023c720

static int PlyrShotSubjectAddEne(SUBJECT_WRK *list, short soul_no, int n)
{
    if (soul_no < 0)
        return n;
    SoulListSetHave(soul_no);
    return PlyrShotSubjectAdd(list, SUBJECT_TYPE_SOUL, soul_no, n);
}

// Every acting ghost on screen; a fatal-frame hit on an ordinary one also
// unlocks its second soul entry.
// FF3 0x0023c798

static int PlyrShotSubjectsEne(SUBJECT_WRK *list, int n)
{
    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if (!IsActEnemy(i) || (ene->flags & ENE_FLG_ON_SCREEN) == 0)
            continue;

        n = PlyrShotSubjectAddEne(list, ene->cmn->soul_no, n);
        if (ene->type != 2 && ene->cmn->soul_sp_no >= 0 && (ene->flags & ENE_FLG_FF) != 0)
            SoulListSetHave(ene->cmn->soul_sp_no);
    }
    return n;
}

// No ghost in the picture: the photo object in view (not the enemy-borne
// types 4 / 5) is its hint and subject; its points count once (the low
// half of bits, its first photograph only), f34 bit 0 asks for the
// seal-door burn, object type 6 makes a scenery shot.
// FF3 0x0023c868

static int PlyrShotSubjectObj(PHOTO_WRK_DEF *def, SUBJECT_WRK *list, int *score, int n)
{
    int no = PhotoObjGetTarget();

    if (no <= 0)
        return n;

    PHOTO_OBJ_DAT *dat = PhotoObjGetDat(no);

    switch (dat->type)
    {
    case 4:
    case 5:
        return n;
    case 6:
        def->type = PHOTO_TYPE_FURN;
        // fall through
    default:
        break;
    }

    def->AddHint(no);
    if ((dat->f34 & 1) != 0)
        def->unlock_ghost = 1;

    if (PhotoObjIsPhotographed(no) == 0)
    {
        *score = (short)dat->bits;
        IngameWrkAddPoint(*score);
        IngameWrkSetBestScore(*score);
        GetFinderModeWrk()->AddScore(*score);
    }
    else
    {
        *score = 0;
    }

    if (def->GetSpecial() >= 0)
        def->type = PHOTO_TYPE_SPECIAL;
    return PlyrShotSubjectAdd(list, SUBJECT_TYPE_NONE, 0, n);
}

// The picture of a shutter shot (PlyrFinderShot): the definition (type 1,
// the panel's score), the subjects -- the ghosts', else the special
// subject a lens shot latched, else the photo object -- the clock, the
// album record and the photo sequence.
// FF3 0x0023c9b8

void PlyrShotRecord(void)
{
    SUBJECT_WRK   list[SUBJECT_MAX + 1];    // 0x80 on the stack, 0x78 cleared
    PHOTO_WRK_DEF def;
    sceCdCLOCK    rtc;
    int           score;
    int           n;

    memset(list, 0, sizeof(SUBJECT_WRK) * SUBJECT_MAX);
    def.Init();
    PhotoSeqReset();
    def.type = 1;
    score = GetPhotoModeWrk288()->score;

    n = PlyrShotSubjectsEne(list, 0);
    PHOTO_OBJ_DAT *sp = PhotoObjGetEneSubject();
    if (sp == nullptr)
    {
        if (n == 0)
            n = PlyrShotSubjectObj(&def, list, &score, 0);
    }
    else
    {
        def.AddHint(sp->no);
        PhotoObjClrEneSubject();
    }

    sceCdReadClock(&rtc);
    sceScfGetLocalTimefromRTC(&rtc);
    def.adr_no = GetSavePhotoNo();
    // ingame_wrk._28 is FF2's room number in this call (GetPlyrRoomID).
    AddPhotoData(def.adr_no, score, (u_short)ingame_wrk._28, ingame_wrk.b02,
                 GetPhotoModeWrk()->GetMode(), rtc, list, n);
    PhotoSeqStart(&def);
}

// ──────────────────────────────────────────────────────────────────────
// The damage

// One charged ghost hit by the shot. A lens shot applies its lens (the
// paralysis lenses do no damage); a running paralysis is ended by the
// shot (the stop lens' one with its level rate) unless the lens renews
// it, paralysis 2 multiplies the damage. The damage is the charge steps
// (+ 1) x the film's power x 1.5 in a fatal frame / 1.25 in a shutter
// chance x the player's shot rate x the lens rate / ene->_1d0, rounded up.
// A finished ghost counts in the panel, sets PLYR_FLG_SHOT_KILL and id
// flag 4; a plain shot on an unparalysed ghost sends the particles home.
// (fptodp / __fixdfsi are the ROM's soft-double calls: plain casts here.)
// FF3 0x0023cad0

static void PlyrShotDamage(ENE_WRK *ene, char lens)
{
    PLYR_WRK *plyr     = ingame_wrk.plyr_wrk;
    float     rate     = 1.0f;
    int       dmg_ok   = 1;
    int       particle = 0;
    LENS_WRK *lw       = GetLensWrk();

    if (lens == 1 && (lw->GetSelEquip(0xff) == SHOT_LENS_SLOW || lw->GetSelEquip(0xff) == SHOT_LENS_STOP))
        dmg_ok = 0;
    if (dmg_ok && (ene->flags & ENE_FLG_FINISH_REQ) != 0)
        ene->flags |= ENE_FLG_FINISHED;

    if (lens == 1)
    {
        rate = lw->GetSelRate();
        if (lw->GetSelEquip(0xff) == SHOT_LENS_RATE)
        {
            if ((ene->flags & (ENE_FLG_SC | ENE_FLG_FF)) != 0)
                rate = 1.0f;
        }
        else if (lw->GetSelEquip(0xff) == SHOT_LENS_SLOW)
        {
            float val = lw->GetSelVal();

            EneMahi1Req(ene, (short)((int)val * (GetPALMode() == 0 ? 30 : 25)), 0.3f);
        }
        else if (lw->GetSelEquip(0xff) == SHOT_LENS_STOP)
        {
            if (PlyrIsHeldBy(ene))
            {
                ene->atk_type = 0;
                ene->hold_pow = 0.0f;
                plyr->status_time = 0.0f;
                ene->flags &= ~0x4000ULL;
            }
            float val = lw->GetSelVal();

            EneMahi0Req(ene, val * (GetPALMode() == 0 ? 30.0f : 25.0f));
        }
        else if (lw->GetSelEquip(0xff) == SHOT_LENS_MAHI2)
        {
            float val = lw->GetSelVal();

            EneMahi2Req(ene, val * (GetPALMode() == 0 ? 30.0f : 25.0f));
        }
        else if (lw->GetSelEquip(0xff) == SHOT_LENS_STONE)
        {
            rate *= lw->GetStoneRate((u_char)lw->GetStoneNum(0xff));
        }
        else if (lw->GetSelEquip(0xff) == SHOT_LENS_REVEAL)
        {
            ene->alpha_a = 0x80;
        }
    }
    else if ((ene->flags & 1) == 0)
    {
        particle = (ene->flags & 6) == 0;
    }

    if (lens != 1 || lw->GetSelEquip(0xff) != SHOT_LENS_MAHI2)
    {
        if ((ene->flags & 4) != 0)
            rate *= lw->GetRate(SHOT_LENS_MAHI2);
    }
    if (lens != 1 || lw->GetSelEquip(0xff) != SHOT_LENS_SLOW)
    {
        if ((ene->flags & 2) != 0)
            EneMahi1End(ene, 1);
    }
    if (lens != 1 || lw->GetSelEquip(0xff) != SHOT_LENS_STOP)
    {
        if ((ene->flags & 1) != 0)
        {
            rate *= lw->GetLevelRate(lw->GetLevel(SHOT_LENS_STOP, 0xff));
            EneMahi0End(ene, 1);
        }
    }

    int add;

    if (rate == 0.0f)
    {
        ene->dmg = 0;
        add = 0;
    }
    else
    {
        PHOTO_WRK_1E8 *w1e8 = GetPhotoModeWrk1e8();
        float k    = 1.5f;
        float num  = (float)(w1e8->GetNum() + GetPhotoModeWrk1e8()->GetNumSub() + 1);
        float pow  = (float)GetPhotoModeWrk()->GetFilmPower();
        float shot;

        if ((ene->flags & ENE_FLG_FF) == 0)
            k = ((ene->flags & ENE_FLG_SC) == 0) ? 1.0f : 1.25f;

        if (PlyrGetCurNo() == 2)
            shot = (float)PlyrGetCamLvCharge(2) * 0.25f + 1.0f;
        else
            shot = PlyrGetShotRate();

        (void)GetCandleControl()->IsOut();  // FF3 drops the result

        ene->dmg = (short)(int)ceil((double)((num * pow * k * shot * rate) / ene->_1d0));

        if (GetPhotoModeWrk()->GetMode() == PHOTO_MODE_FINDER
            && (ene->flags2 & 0x200) == 0
            && (ene->flags & 0x2000001000ULL) == ENE_FLG_FF)
        {
            ene->flags |= 0x10000000000ULL;
        }

        DamageDispReq(&ene->dmg_disp, (short)ene->dmg, 5);
        add = ene->dmg;
        if (add > 0)
            EneIdFlgSet(ene->type, ene->id, 2);
    }

    plyr->_130 += (u_long)(int64_t)add;
    PlyrGaugeAdd((float)ene->dmg * 0.7f, GetAlbumSaveDisp());

    if (ene->type == 1 && ene->dmg >= 1)
        RandGhostCntReset(ene);

    if (ene->hp - ene->dmg < 1 || (ene->flags & ENE_FLG_FINISHED) != 0)
    {
        GetPhotoModeWrk288()->kill_cnt++;
        plyr->flags |= PLYR_FLG_SHOT_KILL;
        EneIdFlgSet(ene->type, ene->id, 4);
    }

    if (particle)
        PlyrShotParticle(ene);
}

// Each viewfinder frame. Ghosts whose reaction is spent (acting, alive, a
// reaction set, not in hit action 3) lose it, and the charge restarts.
// With a film loaded (or the real camera) and the shutter charged, every
// acting ghost that may be shot charges the camera (ENE_FLG_CHARGE) while
// the nearest ordinary ghost is within the shot range and it is on screen;
// without one the charge drains. The shutter chance of a charging ghost is
// kept in sc_state and lights the finder point's glow (PHOTO180_FLG_10).
// FF3 0x0023d178

void PlyrShotChargeCtrl(void)
{
    PLYR_WRK       *plyr    = ingame_wrk.plyr_wrk;
    int             charged = 0;
    int             sc      = ShutterChanceChk();
    float           range   = PlyrGetShotRange(0xff, 0xff);
    PHOTO_WRK_1E8  *w1e8    = GetPhotoModeWrk1e8();
    PHOTO_MODE_WRK *pm      = GetPhotoModeWrk();
    u_char          cur     = PlyrGetCurNo();
    u_short         film    = (u_short)FilmGetType(GetFilmWrk());
    int             reset   = 0;

    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if (ene->state == ENE_STATE_ACT && ene->hp != 0 && ene->dmg_react != 0 && ene->act_no != 3)
        {
            reset++;
            ene->level = 0;
            ene->dmg_react = 0;
            ene->flags &= ~ENE_FLG_REACT_BITS;
        }
    }
    if (reset != 0)
    {
        if (GetPhotoModeWrk1e8()->mode != 0)
            GetPhotoModeWrk1e8()->mode = 0;
        w1e8->Init();
    }

    int run;

    if (pm->GetMode() == PHOTO_MODE_FINDER)
    {
        short num = 0;

        if (film < 0x45)
            num = *ItemGetCnt(film);
        run = num >= 1 || pm->GetMode() != PHOTO_MODE_FINDER;
    }
    else
    {
        run = pm->GetMode() != PHOTO_MODE_FINDER;
    }

    if (run)
    {
        if (PhotoChargerIsReady(&pm->charger))
        {
            for (int i = 0; i < ENE_WRK_MAX; i++)
            {
                ENE_WRK *ene = &ene_wrk[i];

                if (!IsActEnemy(i) || (ene->flags2 & 0x800) != 0
                    || (ene->flags & ENE_FLG_NO_SHOT) != 0 || (ene->flags2 & 0x100) != 0)
                {
                    continue;
                }

                u_long f = ene->flags;

                if (range <= plyr->near_dist || ene->type == 2 || (f & ENE_FLG_ON_SCREEN) == 0)
                {
                    f &= ~ENE_FLG_CHARGE;
                }
                else
                {
                    int speed;

                    if (GetPhotoModeWrk1e8()->mode == 0)
                        speed = w1e8->GetSpeed(cur, plyr->equip[1]);
                    else
                        speed = w1e8->GetSpeedEquip(PlyrGetEquipLv(6));

                    float dr = w1e8->DistRate(ene->dist, cur, plyr->equip[1]);
                    float rr = w1e8->RadiusRate(ene->fdr_dist, cur, plyr->equip[1]);
                    float sr = w1e8->StepRate(cur, w1e8->GetMax(cur, plyr->equip[0]));

                    if (GetPhotoModeWrk()->GetMode() == PHOTO_MODE_FINDER)
                        w1e8->Add(dr, rr, sr, 1.0f, speed);
                    f = ene->flags | ENE_FLG_CHARGE;
                    charged = 1;
                }
                ene->flags = f;
            }
        }

        int steps = w1e8->GetNum() + w1e8->GetNumSub();     // PC

        if (!charged)
            w1e8->Drain(cur);
        w1e8->Main(w1e8->GetMax(cur, plyr->equip[0]));

        // PC: each new step of the charge, harder as it fills
        if (charged && w1e8->GetNum() + w1e8->GetNumSub() > steps)
            ReiPan_HapticCue(REIPAN_CUE_CHARGE, (float)w1e8->pct / 100.0f);
    }

    plyr->sc_state_old = plyr->sc_state;
    plyr->sc_state = charged ? sc : 0;
    if (plyr->sc_state > plyr->sc_state_old)    // PC: a shutter chance, or a fatal frame
        ReiPan_HapticCue(REIPAN_CUE_CHANCE, plyr->sc_state == 2 ? 1.0f : 0.7f);

    if (plyr->sc_state != 0 && PhotoChargerIsReady(&pm->charger))
        PhotoWrk180SetBit4(GetPhotoModeWrk180(), 1);
    else if (plyr->sc_state_old != 0 && plyr->sc_state == 0)
        PhotoWrk180SetBit4(GetPhotoModeWrk180(), 0);
}

// The shot over the 16 ghosts. Every acting one on screen is hit; a
// special one (type 2) is caught by a shutter shot (and no longer a
// target); an ordinary charged one takes the damage (or, while
// PLYR_FLG_000004, a 0.2 slow-down for 75 / 90 frames), its reaction and
// level (not when a paralysis lens hits it mid-reaction), the mission's
// level record and the hit effect. Then the score, the charge reset when
// no ghost is left in a big reaction or alive, and the shot SE: 4 for a
// strong reaction, 3 for a plain one, 2 for none (the real camera: 2).
// FF3 0x0023d5f0

void PlyrShotJudge(char lens)
{
    PLYR_WRK      *plyr   = ingame_wrk.plyr_wrk;
    PHOTO_WRK_288 *w288   = GetPhotoModeWrk288();
    LENS_WRK      *lw     = GetLensWrk();
    int            hit    = 0;
    int            strong = 0;
    int            se0    = 0;
    int            alive  = 0;

    plyr->flags &= ~(u_long)PLYR_FLG_SHOT_KILL;

    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if (!IsActEnemy(i))
            continue;

        u_long f       = ene->flags;
        int    handled = 0;

        if ((f & ENE_FLG_ON_SCREEN) != 0)
        {
            if ((ene->flags2 & 0x100) == 0 && (f & ENE_FLG_NO_SHOT) == 0)
            {
                if (lens == 1)
                {
                    u_int se;

                    if (lw->GetSelEquip(0xff) == SHOT_LENS_STOP || lw->GetSelEquip(0xff) == SHOT_LENS_SLOW)
                        se = 7;
                    else
                        se = (lw->GetSelEquip(0xff) == SHOT_LENS_MAHI2) ? 7 : 10;
                    PlyrSePlay(plyr, se, 100, 0x1000);
                }

                if (ene->type == 2)
                {
                    if (lens != 1)
                    {
                        ene->flags |= ENE_FLG_HIT | ENE_FLG_NO_TARGET;
                        w288->sp_cnt++;
                    }
                }
                else
                {
                    f = ene->flags;
                    ene->flags = f | ENE_FLG_HIT;
                    if ((f & ENE_FLG_CHARGE) != 0)
                    {
                        w288->hit_cnt++;
                        if ((plyr->flags & PLYR_FLG_000004) == 0)
                        {
                            PlyrShotDamage(ene, lens);
                            if (!EneIsDmgReactBig(ene) || lens != 1
                                || (lw->GetSelEquip(0xff) != SHOT_LENS_SLOW && lw->GetSelEquip(0xff) != SHOT_LENS_STOP))
                            {
                                EneDmgReactSet(ene, (lens == 1) ? lw->GetSelEquip(0xff) : (s_char)-1);
                                EneLevelByDmgReact(ene);
                            }
                            GetFinderModeWrk()->SetMaxLevel(ene->level);
                            PlyrShotHitEffect(ene, lens);
                            if (ene->hp - ene->dmg > 0 && (ene->flags & ENE_FLG_FINISHED) == 0)
                                alive++;
                        }
                        else
                        {
                            EneSetStepReq(ene, (GetPALMode() == 0) ? 90 : 75, 0.2f);
                        }

                        handled = 1;
                        switch (ene->dmg_react)
                        {
                        case 0:
                        case 5:
                            hit = 1;
                            se0 = 1;
                            break;
                        case 1:
                        case 2:
                            hit = 1;
                            break;
                        case 3:
                        case 4:
                            strong = 1;
                            break;
                        }
                    }
                }
            }
            else if (ene->type == 2)
            {
                if (lens != 1)
                    ene->flags = f | ENE_FLG_HIT | ENE_FLG_NO_TARGET;
            }
            else
            {
                ene->flags = f | ENE_FLG_HIT;
            }
        }

        if (!handled && EneIsDmgReactBig(ene))
        {
            ene->dmg_react = 0;
            ene->flags &= ~ENE_FLG_REACT_BITS;
        }
    }

    plyr->_250 = 0;
    PlyrShotScore(lens);

    if (lens != 1)                      // PC: the shutter, harder for a hit
        ReiPan_HapticCue(REIPAN_CUE_SHUTTER, strong ? 1.0f : (hit ? 0.7f : 0.4f));

    if (!EneAnyDmgReactBig() || alive == 0)
    {
        PHOTO_WRK_1E8 *w1e8 = GetPhotoModeWrk1e8();

        w1e8->Init();
        GetPhotoModeWrk()->SetSub220Rate();
        if (w1e8->mode != 0)
            w1e8->mode = 0;
    }

    if (GetPhotoModeWrk()->GetMode() == PHOTO_MODE_FINDER)
    {
        if (lens != 1)
        {
            PhotoModeSePlay(strong ? 4 : (hit ? 3 : 2), 100, 0x1000);
            if (se0)
                PhotoChargerSetSe0Wait(GetPhotoModeWrk220(), 2);
        }
    }
    else
    {
        PhotoModeSePlay(2, 100, 0x1000);
    }

    if (w288->sp_cnt != 0)
        GetPhotoModeWrk220()->se7_wait = 2;
}

// The finder mode's hit effect: a plain shot's screen effect (label 1 for
// a shutter-chance slow-down, 2 for a strong hit, else 0), a lens shot's
// deform jobs cleared and the lens' own effect.
// FF3 0x0023dad0

static void PlyrShotHitEffect(ENE_WRK *ene, char lens)
{
    LENS_WRK *lw = GetLensWrk();

    if (GetPhotoModeWrk()->GetMode() != PHOTO_MODE_FINDER)
        return;

    if (lens == 0)
    {
        int label = 1;

        if (ene->dmg_react != 4)
            label = (ene->dmg_react == 3) ? 2 : 0;
        EneHitEffectReq(ene->alg.idx, ene->tpos, label);
        return;
    }

    IgEffectSubFuncPDeformClear(ene->alg.idx);
    lw->HitEffectReq(ene, (u_char)lw->GetSelEquip(0xff));
}

// A tenth of the damage (at most 20) in particles from the camera's aim
// point on the ghost, flying home to the finder (not with the real camera).
// FF3 0x0023db98

static void PlyrShotParticle(ENE_WRK *ene)
{
    if (GetPhotoModeWrk()->GetMode() == PHOTO_MODE_REAL)
        return;

    int num = ene->dmg / 10;

    if (num > 20)
        num = 20;
    if (num <= 0)
        return;

    ENE_DMG_PARTICLE_REQ req = {};      // FF3 leaves EffectType as stack garbage

    req.StartPos[0] = ene->tpos[0];
    req.StartPos[1] = ene->tpos[1];
    req.StartPos[2] = ene->tpos[2];
    req.StartPos[3] = ene->tpos[3];
    req.pEndPos     = IgEffectParticleEndPosFinderGet();
    req.ParticleNum = num;
    req.DistPE      = ene->dist;
    req.SuctionFlg  = 1;
    EneDmgParticleEffectReq(&req);
}

// ──────────────────────────────────────────────────────────────────────
// The view turns

// The point the view turns from: the player's position at eye height.
static void PlyrShotEyePos(PLYR_WRK *plyr, float *eye)
{
    eye[0] = plyr->pos[0];
    eye[1] = plyr->pos[1] + plyr->crawl_h;
    eye[2] = plyr->pos[2];
    eye[3] = plyr->pos[3];
}

// The XZ distance from the player to a ghost's body (vsub / vmul .xyz,
// vaddz.x: y is left out).
static float PlyrShotEneDistXZ(PLYR_WRK *plyr, ENE_WRK *ene)
{
    float dx = plyr->pos[0] - ene->pos[0];
    float dz = plyr->pos[2] - ene->pos[2];

    return sqrtf(dx * dx + dz * dz);
}

// Going into the viewfinder with lens function 3 (auto aim): when exactly
// one acting, visible ghost is in view (flags 0x100, a target, flags2
// without 0x900) and within 200, the view turns to it over 10 frames;
// otherwise the turn is cleared (and still requested).
// FF3 0x0023dc38

void PlyrFinderAutoTurn(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (GetLensWrk()->GetFunc(3) == 0)
        return;

    float best = 0.0f;
    int   sel  = 0xff;
    int   cnt  = 0;

    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK *ene = &ene_wrk[i];

        if (ene->state != ENE_STATE_ACT || ene->hp == 0
            || (ene->flags & (ENE_FLG_NO_TARGET | 0x100ULL)) != 0x100ULL
            || ene->alpha == 0 || (ene->flags2 & 0x900) != 0)
        {
            continue;
        }

        cnt++;
        float d = PlyrShotEneDistXZ(plyr, ene);
        if (d <= 200.0f && (best == 0.0f || d < best))
        {
            best = d;
            sel  = i;
        }
    }

    float eye[4];

    PlyrShotEyePos(plyr, eye);
    if (sel == 0xff || cnt != 1)
    {
        plyr->view_turn[0] = 0.0f;
        plyr->view_turn[1] = 0.0f;
        plyr->view_turn[2] = 0.0f;
        plyr->view_turn[3] = 0.0f;
    }
    else
    {
        UnitGetTrgtRot(eye, ene_wrk[sel].mpos, plyr->view_turn, 3);
        plyr->view_turn[1] -= plyr->rot[1];
        RotLimitChk(&plyr->view_turn[1]);
        plyr->finder_pitch = plyr->view_turn[0];
        plyr->view_turn[0] = 0.0f;
    }

    FinderViewSetTurnTime(10.0f);
    FinderViewTurnReq();
}

// FF3 0x0023de58

int PlyrGetFinderState(void)
{
    u_long flags = ingame_wrk.plyr_wrk->flags;

    if ((flags & PLYR_FLG_800000) != 0)
        return 1;
    if ((flags & (PLYR_FLG_400000 | PLYR_FLG_1000000)) != 0)
        return 2;
    return 0;
}

// The view's turn to ghost no. Time 0 snaps the pitch and the facing to it
// (_000 = 1); else the eased turn (PLYR_FLG_VIEW_EASE): view_turn holds
// the accelerations 2 x angle / time^2 from the current pitch / facing,
// view_from the start (FinderViewTurnCtrl runs it).
// FF3 0x0023de90

static void FinderViewTurnToEneSet(float time, int no)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    float     eye[4];

    if (time == 0.0f)
    {
        PlyrShotEyePos(plyr, eye);
        UnitGetTrgtRot(eye, ene_wrk[no].mpos, plyr->view_turn, 3);
        plyr->finder_pitch = plyr->view_turn[0];
        RotLimitChk(&plyr->finder_pitch);
        plyr->rot[1] = plyr->view_turn[1];
        RotLimitChk(&plyr->rot[1]);
        plyr->view_turn[0] = 0.0f;
        plyr->view_turn[1] = 0.0f;
        plyr->_000 = 1.0f;
        return;
    }

    plyr->flags |= PLYR_FLG_VIEW_EASE;
    PlyrShotEyePos(plyr, eye);
    UnitGetTrgtRot(eye, ene_wrk[no].mpos, plyr->view_turn, 3);
    plyr->view_turn[0] -= plyr->finder_pitch;
    RotLimitChk(&plyr->view_turn[0]);
    plyr->view_turn[0] = (plyr->view_turn[0] + plyr->view_turn[0]) / (time * time);
    plyr->view_turn[1] -= plyr->rot[1];
    RotLimitChk(&plyr->view_turn[1]);
    plyr->view_turn[1] = (plyr->view_turn[1] + plyr->view_turn[1]) / (time * time);
    plyr->view_from[0] = plyr->rot[0];
    plyr->view_from[1] = plyr->rot[1];
    plyr->view_from[2] = plyr->rot[2];
    plyr->view_from[3] = plyr->rot[3];
    plyr->view_from[0] = plyr->finder_pitch;
    plyr->_000 = 0.0f;
}

// The enemy script's turn (EJobC41): to ene, or with nullptr to the
// nearest acting, visible target ghost within 200 (XZ); none: the turn is
// cleared.
// FF3 0x0023e078

void FinderViewTurnToEne(float time, ENE_WRK *ene)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       sel  = 0xff;

    if (ene == nullptr)
    {
        float best = 0.0f;

        for (int i = 0; i < ENE_WRK_MAX; i++)
        {
            ENE_WRK *e = &ene_wrk[i];

            if (e->state != ENE_STATE_ACT || e->hp == 0 || (e->flags & ENE_FLG_NO_TARGET) != 0
                || e->alpha == 0)
            {
                continue;
            }

            float d = PlyrShotEneDistXZ(plyr, e);
            if (d <= 200.0f && (best == 0.0f || d < best))
            {
                best = d;
                sel  = i;
            }
        }
    }
    else
    {
        sel = ene->alg.idx;
    }

    if (sel == 0xff)
    {
        plyr->view_turn[0] = 0.0f;
        plyr->view_turn[1] = 0.0f;
        plyr->view_turn[2] = 0.0f;
        plyr->view_turn[3] = 0.0f;
        return;
    }

    FinderViewSetTurnTime(time);
    FinderViewTurnToEneSet(time, sel);
}
