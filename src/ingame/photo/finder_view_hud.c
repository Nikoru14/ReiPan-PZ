/* ==========================================================================
 *  ingame/photo/finder_view_hud.c
 *
 *  The camera HUD parts: FF3-only code, FF3 0x1bb4c8..0x1bf108 between
 *  finder_view.c and the FlyModel methods, plus four out-of-line helpers at
 *  0x2b9610..0x2b9cb8 (HudPartDraw, HudNumberDraw, HudFilamentCoreDraw,
 *  HudFilamentGlowDraw) whose float constants sit in this file's sdata run
 *  0x370504..0x3706bc. Every sprite goes through finder_view.c's helpers on
 *  its table hud_sprt; the ring gauges place HUD_PART records around the
 *  screen centre (320, 224) and move / scale them as a whole. Roles and
 *  arguments: finder_view_hud.h.
 * ======================================================================== */
#include "ingame/photo/finder_view_hud.h"

#include <math.h>                           // sqrtf
#include <string.h>                         // memset

#include "common/vu0math.h"                 // NormalizeAngle, Vu0Sinf, Vu0Cosf
#include "graphics/graph2d/g2d_draw.h"      // DISP_SPRT, DispSprD
#include "ingame/ingame_wrk.h"              // ingame_wrk
#include "ingame/menu/anim_2d.h"            // LangTexSend
#include "ingame/photo/finder_photo.h"      // GetLensWrk
#include "ingame/photo/finder_view.h"       // hud_sprt, HudSprt*, HudGetCamType, HudIsViewOn
#include "ingame/photo/fndr_effect.h"       // PHOTO_CHARGE_DISP
#include "ingame/photo/fndr_parts.h"        // LensGetFunc, LensGetSel, LensGetEquip, LENS_FUNC_*
#include "ingame/plyr/plyr_param.h"         // PlyrGetFinderScale

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

#define HUD_PI          3.1415927f
#define HUD_CX          320.0f              // screen centre
#define HUD_CY          224.0f
#define HUD_CHR_CUR     0xff                // chr argument: the current player

// data 0x327190 / 0x3271b8 / 0x3271e0: the ring gauges' parts.
static HUD_PART hud_steps_part;             // HudChargeStepsDrawSub
static HUD_PART hud_gauge_part;             // HudChargeGaugeDrawSub
static HUD_PART hud_charge2_part;           // HudCharge2Draw

// data 0x359708: the +0x288 panel's icon sprites (< 0: none).
static const int hud_panel_icon[14] =
{
    -1, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, -1,
};

// data 0x359740: a colour per lens (sprite 0xd4 behind the shown lens).
static const u_char hud_lens_col[9][4] =
{
    { 0xf2, 0xb2, 0x65, 0x80 }, { 0x06, 0x43, 0xff, 0x80 }, { 0xf2, 0x14, 0x34, 0x80 },
    { 0x4a, 0xbc, 0x82, 0x80 }, { 0xfd, 0x6d, 0x33, 0x80 }, { 0x9e, 0xd2, 0x4e, 0x80 },
    { 0xf2, 0x14, 0x9b, 0x80 }, { 0x00, 0x95, 0xd0, 0x80 }, { 0x6f, 0x39, 0xff, 0x80 },
};

// data 0x359768: a colour per lens for its turning marks (HudLensMarkDraw).
static const u_char hud_lens_mark_col[9][4] =
{
    { 0xc6, 0xa4, 0x5e, 0x80 }, { 0x34, 0x57, 0xed, 0x80 }, { 0xdb, 0x3e, 0x2b, 0x80 },
    { 0x15, 0x9f, 0x4e, 0x80 }, { 0xff, 0x81, 0x3a, 0x80 }, { 0xb2, 0xd9, 0x47, 0x80 },
    { 0xf2, 0x14, 0xb9, 0x80 }, { 0x00, 0x91, 0xbc, 0x80 }, { 0x90, 0x39, 0xff, 0x80 },
};

// data 0x359790: the slow-mode gauge's colour rows (HudSlowGaugeDraw col).
static const u_char hud_slow_col[3][3] =
{
    { 0x80, 0x80, 0x80 }, { 0xff, 0xff, 0xff }, { 0x40, 0x40, 0x40 },
};

// rodata 0x368298: player 1's second charge ring radius by camera level.
static const float hud_level_radius[4] = { 114.0f, 116.0f, 118.0f, 119.0f };

// ──────────────────────────────────────────────────────────────────────
// Draws part p with its centre moved by (ox, oy) and, when sx / sy is not
// 1, scaled about (cx, cy); alpha a * p->alpha, scale p's times (sx, sy).
// FF3 0x002b9610 (out of line, after the template instances).

static void HudPartDraw(float ox, float oy, float a, float sx, float sy, float cx, float cy,
                        DISP_SPRT *ds, HUD_PART *p)
{
    float x;
    float y;

    if (p == NULL || a <= 0.0f || p->no == HUD_PART_NONE)
    {
        return;
    }

    x = p->x + ox;
    y = p->y + oy;
    if (sx != 1.0f)
    {
        x = (x - cx) * sx + cx;
    }
    if (sy != 1.0f)
    {
        y = (y - cy) * sy + cy;
    }

    HudSprtSetCenter(x, y, a * p->alpha, p->sx * sx, p->sy * sy, p->rot, ds, p->no);
    ds->r = p->r;
    ds->g = p->g;
    ds->b = p->b;
    ds->att |= p->att;
    DispSprD(ds);
}

// ──────────────────────────────────────────────────────────────────────
// num in decimal, left to right from (x, y): digit d is sprite digit0 + d,
// each advancing x by its width; then the sprite 'tail' unless < 0.
// FF3 0x002b9710 (out of line).

static void HudNumberDraw(float x, float y, float a, int num, int digit0, int tail)
{
    DISP_SPRT ds;
    s_char    digit[8];
    s_char    n;
    int       i;
    int       no;

    if (a <= 0.0f || num < 0 || digit0 < 0)
    {
        return;
    }

    n = 0;
    for (i = 0; i < 8; i++)
    {
        digit[i] = num % 10;
        if (num / 10 > 0 || num % 10 > 0)
        {
            n++;
        }
        num /= 10;
    }
    if (n == 0)
    {
        n = 1;
    }

    for (i = n - 1; i >= 0; i--)
    {
        no = digit0 + digit[i];
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, no, 0, 0x80, 0x80, 0x80);
        x += (float)hud_sprt[no].w;
    }

    if (tail >= 0)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, tail, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The filament's two core sprites (type 1: 0x21, 0x22; type 2: 0x24, 0x25)
// at a (1 - a) and a^2, additive; turned 90 degrees (moved right by their
// height) unless the viewfinder is up, which uses the set 9 further on.
// FF3 0x002b98c8 (out of line).

static void HudFilamentCoreDraw(float x, float y, float a, DISP_SPRT *ds, int type)
{
    int no;

    if (a <= 0.0f)
    {
        return;
    }
    if (1.0f < a)
    {
        a = 1.0f;
    }

    if (type == 1)
    {
        no = 0x21;
    }
    else if (type == 2)
    {
        no = 0x24;
    }
    else
    {
        return;
    }

    if (HudIsViewOn())
    {
        no += 9;
        HudSprtDrawOfs(x, y, a * (1.0f - a), 1.0f, 1.0f, 0.0f, ds, no, 1, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a * a, 1.0f, 1.0f, 0.0f, ds, no + 1, 1, 0x80, 0x80, 0x80);
    }
    else
    {
        HudSprtDrawOfs(x + (float)hud_sprt[no].h, y, a * (1.0f - a), 1.0f, 1.0f, 90.0f,
                       ds, no, 1, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x + (float)hud_sprt[no + 1].h, y, a * a, 1.0f, 1.0f, 90.0f,
                       ds, no + 1, 1, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The filament's glow (type 1: 0x23, type 2: 0x26; + 9 with the viewfinder
// up, else turned 90 degrees) mirrored four ways at scale s, additive. The
// quadrant offset uses the unshifted sprite's size in both cases.
// FF3 0x002b9ae0 (out of line).

static void HudFilamentGlowDraw(float x, float y, float a, float s, DISP_SPRT *ds, int type)
{
    const SPRT_DAT *spr;
    int no;

    if (a <= 0.0f)
    {
        return;
    }
    if (1.0f < a)
    {
        a = 1.0f;
    }
    if (s == 0.0f)
    {
        return;
    }

    if (type == 1)
    {
        no = 0x23;
    }
    else if (type == 2)
    {
        no = 0x26;
    }
    else
    {
        return;
    }

    spr = &hud_sprt[no];
    if (HudIsViewOn())
    {
        HudSprtSetOfs(x, y, a, 1.0f, 1.0f, 0.0f, ds, no + 9);
    }
    else
    {
        HudSprtSetOfs(x, y, a, 1.0f, 1.0f, 90.0f, ds, no);
    }
    ds->x += (float)-(int)spr->w;
    ds->y += (float)-(int)spr->h;
    ds->alphar = 0x48;

    ds->scw = s;
    ds->sch = s;
    DispSprD(ds);
    ds->scw = -s;
    ds->sch = s;
    DispSprD(ds);
    ds->scw = s;
    ds->sch = -s;
    DispSprD(ds);
    ds->sch = -s;
    ds->scw = -s;
    DispSprD(ds);
}

// ──────────────────────────────────────────────────────────────────────
// The +0x180 glow: one mirrored sprite (no + 0x49, real camera no + 0x8c)
// at alpha a / 128, scaled by the camera level's rate. a 0: nothing.
// FF3 0x001bb4c8.

void HudPointGlowDraw(float x, float y, float rot, float sx, float sy, int no,
                      u_char r, u_char g, u_char b, u_char a, int flag, int real)
{
    DISP_SPRT ds;
    float rate;

    if (a == 0)
    {
        return;
    }

    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    HudSprtDrawMirror4(x, y, (float)a * 0.0078125f, sx * rate, sy * rate, rot,
                       &ds, real ? no + 0x8c : no + 0x49, flag, r, g, b);
}

// ──────────────────────────────────────────────────────────────────────
// 'rings' copies of a mirrored sprite, phases t + k / rings (wrapped into
// 0..1): each grows to amp * sqrt(phase) + 1 and fades as (a / 128)^2 times
// (1 - phase). Nothing for a 0, no rings or t outside 0..1.
// FF3 0x001bb5d8.

void HudPointRingsDraw(float x, float y, float amp, float t, float rot, float sx, float sy, int no, int rings,
                       u_char r, u_char g, u_char b, u_char a, int flag, int real)
{
    DISP_SPRT ds;
    float rate;
    float step;
    float al;
    float s;

    if (a == 0 || rings <= 0 || t < 0.0f || 1.0f < t)
    {
        return;
    }

    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    sy *= rate;
    sx *= rate;
    no = real ? no + 0x8c : no + 0x49;
    step = 1.0f / (float)rings;
    al = (float)a * 0.0078125f;

    while (rings > 0)
    {
        t += step;
        if (1.0f < t)
        {
            t -= 1.0f;
        }
        s = amp * sqrtf(t) + 1.0f;        // VU0 vsqrt
        rings--;
        HudSprtDrawMirror4(x, y, al * (1.0f - t) * al, sx * s, sy * s, rot, &ds, no, flag, r, g, b);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The charge gauge's steps: 'max' of them clockwise around the screen
// centre (player 1: one slot more, starting at the bottom; player 2: from
// 90 degrees), each a marker 0x4e at radius 125 turned along the ring and
// its number sprite 0x57 + i at radius 132. The ring moves with (x, y) and
// grows with ramp^2.
// FF3 0x001bb7c8.

static void HudChargeStepsDrawSub(float x, float y, float a, float ramp, float sx, float sy,
                                  int max, int chr)
{
    DISP_SPRT ds;
    HUD_PART *p = &hud_steps_part;
    float ox = x - HUD_CX;
    float oy = y - HUD_CY;
    float step;
    float start;
    float k;
    float ang;
    float rad;
    float c;
    float s;
    int   i;

    memset(p, 0, sizeof(*p));
    step = -360.0f / (float)(chr == 1 ? max + 1 : max);
    if (chr == 1)
    {
        start = step + 180.0f;
    }
    else
    {
        start = (chr == 2) ? 90.0f : 0.0f;
    }

    p->att = 0;
    p->sx = 1.0f;
    p->_20 = 1;
    p->sy = 1.0f;
    p->alpha = 1.0f;

    k = ramp * ramp * 0.08f + 1.0f;
    sy *= k;
    sx *= k;

    for (i = 0; i < max; i++)
    {
        ang = step * (float)i + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);

        p->no = 0x4e;
        p->r = 0xa7;
        p->g = 0x88;
        p->b = 0x45;
        p->y = c * 125.0f + HUD_CY;
        p->rot = -ang;
        p->x = s * 125.0f + HUD_CX;
        HudPartDraw(ox, oy, a, sx, sy, x, y, &ds, p);

        p->no = i + 0x57;
        p->r = 0x76;
        p->g = 0x6a;
        p->b = 0x4f;
        p->x = s * 132.0f + HUD_CX;
        p->y = c * 132.0f + HUD_CY;
        p->rot = 0.0f;
        HudPartDraw(ox, oy, a, sx, sy, x, y, &ds, p);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The charge gauge, laid out like HudChargeStepsDrawSub: per step a cell
// (0x4c, charging 0x4d) at radius 123, its light 0x62 + i at 131, its
// number 0x6d + i at 140 and a marker 0x4e at 125. Steps below num_lit are
// lit; the filling one (and every step for player 2) lights by frac and
// glows above 0.95. While the flash runs the cells and numbers get a
// second, growing copy at 0.8 (1 - flash).
// FF3 0x001bbb18.

static void HudChargeGaugeDrawSub(float x, float y, float a, float sx, float sy, PHOTO_CHARGE_DISP *disp)
{
    DISP_SPRT ds;
    HUD_PART *p = &hud_gauge_part;
    float ox = x - HUD_CX;
    float oy = y - HUD_CY;
    float fl_a;
    float fl_sx;
    float fl_sy;
    float step;
    float start;
    float k;
    float lit;
    float glow;
    float fl_lit;
    float ang;
    float rad;
    float c;
    float s;
    int   i;

    if (disp->charging)
    {
        LangTexSend(0xf);
        LangTexSend(0x11);
    }

    fl_sx = 0.0f;
    fl_sy = 0.0f;
    fl_a = 0.0f;
    if (0.0f < disp->flash && disp->flash <= 1.0f)
    {
        k = disp->flash * 0.2f + 1.0f;
        fl_a = a * 0.8f * (1.0f - disp->flash);
        fl_sy = k * sy;
        fl_sx = k * sx;
    }

    memset(p, 0, sizeof(*p));
    step = -360.0f / (float)(disp->chr == 1 ? disp->max + 1 : disp->max);
    if (disp->chr == 1)
    {
        start = step + 180.0f;
    }
    else
    {
        start = (disp->chr == 2) ? 90.0f : 0.0f;
    }

    p->att = 0;
    k = disp->ramp * disp->ramp * 0.08f + 1.0f;
    sy *= k;
    sx *= k;

    for (i = 0; i < disp->max; i++)
    {
        lit = (i < disp->num_lit) ? a : 0.0f;
        glow = lit;
        if ((i == disp->num_lit || disp->chr == 2)
            && 0.0f < disp->frac && disp->frac <= 1.0f)
        {
            lit = disp->frac * a;
            if (0.95f < disp->frac)
            {
                glow = (disp->frac - 0.95f) * 20.0f * a;
            }
        }
        fl_lit = lit * fl_a;

        ang = step * (float)i + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);

        // the cell, and its flash copy
        p->no = disp->charging ? 0x4d : 0x4c;
        p->y = c * 123.0f + HUD_CY;
        p->r = 0x80;
        p->sy = 0.95f;
        p->x = s * 123.0f + HUD_CX;
        p->g = 0x80;
        p->sx = 0.95f;
        p->b = 0x80;
        p->alpha = 1.0f;
        p->_20 = 0;
        p->rot = -ang;
        HudPartDraw(ox, oy, lit, sx, sy, x, y, &ds, p);
        HudPartDraw(ox, oy, fl_lit, fl_sx, fl_sy, x, y, &ds, p);

        // the light
        p->r = 0x80;
        p->no = i + 0x62;
        p->g = 0x80;
        p->b = 0x80;
        p->_20 = 0;
        p->x = s * 131.0f + HUD_CX;
        p->y = c * 131.0f + HUD_CY;
        p->sx = 1.0f;
        p->sy = 1.0f;
        p->rot = 0.0f;
        p->alpha = 1.0f;
        HudPartDraw(ox, oy, glow, sx, sy, x, y, &ds, p);

        // the number: flash copy, then (all lit) itself
        p->no = i + 0x6d;
        p->_20 = 1;
        p->x = s * 140.0f + HUD_CX;
        p->y = c * 140.0f + HUD_CY;
        HudPartDraw(ox, oy, fl_lit, fl_sx, fl_sy, x, y, &ds, p);
        if (disp->num_lit == disp->max)
        {
            HudPartDraw(ox, oy, lit, sx, sy, x, y, &ds, p);
        }

        // the marker
        p->no = 0x4e;
        p->r = 0xa7;
        p->_20 = 1;
        p->x = s * 125.0f + HUD_CX;
        p->g = 0x88;
        p->y = c * 125.0f + HUD_CY;
        p->b = 0x45;
        p->alpha = 1.0f;
        p->sx = 1.0f;
        p->sy = 1.0f;
        p->rot = -ang;
        HudPartDraw(ox, oy, lit, sx, sy, x, y, &ds, p);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bc108.

void HudChargeStepsDraw(float x, float y, float a, float ramp, float sx, float sy, int max, int chr)
{
    float rate;

    if (a <= 0.0f || max <= 0)
    {
        return;
    }
    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    HudChargeStepsDrawSub(x, y, a, ramp, sx * rate, sy * rate, max, chr);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bc1c0.

void HudChargeGaugeDraw(float x, float y, float a, float sx, float sy, PHOTO_CHARGE_DISP *disp)
{
    float rate;

    if (a <= 0.0f || disp->max <= 0)
    {
        return;
    }
    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    HudChargeGaugeDrawSub(x, y, a, sx * rate, sy * rate, disp);
}

// ──────────────────────────────────────────────────────────────────────
// The full-charge glow: sprite 0x4b mirrored four ways, growing with
// ramp^2, in colour rgb.
// FF3 0x001bc260.

void HudChargeFullDraw(float x, float y, float a, float ramp, float sx, float sy, u_char r, u_char g, u_char b)
{
    DISP_SPRT ds;
    float rate;
    float k;

    if (a <= 0.0f)
    {
        return;
    }
    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    k = ramp * ramp * 0.08f + 1.0f;
    HudSprtDrawMirror4(x, y, a, sx * rate * k, sy * rate * k, 0.0f, &ds, 0x4b, 0, r, g, b);
}

// ──────────────────────────────────────────────────────────────────────
// One enemy marker: 0x56 (real camera 0x8f) mirrored four ways at a / 2,
// additive, in (0xa7, 0x94, 0x69).
// FF3 0x001bc370.

void HudEneMarkDraw(float x, float y, float a, float sx, float sy, int real)
{
    DISP_SPRT ds;

    a *= 0.5f;
    if (a <= 0.0f)
    {
        return;
    }
    HudSprtDrawMirror4(x, y, a, sx, sy, 0.0f, &ds, real ? 0x8f : 0x56, 1, 0xa7, 0x94, 0x69);
}

// ──────────────────────────────────────────────────────────────────────
// The marker centre: sprites 0x53..0x55 (real camera 0x8e) at a / 2,
// additive, in (0xa7, 0x94, 0x69).
// FF3 0x001bc3d0.

void HudEneMarkCenterDraw(float x, float y, float a, float sx, float sy, int real)
{
    DISP_SPRT ds;
    int no;

    a *= 0.5f;
    if (a <= 0.0f)
    {
        return;
    }

    if (real == 0)
    {
        for (no = 0x53; no < 0x56; no++)
        {
            HudSprtDrawOfs(x, y, a, sx, sy, 0.0f, &ds, no, 1, 0xa7, 0x94, 0x69);
        }
    }
    else
    {
        HudSprtDrawOfs(x, y, a, sx, sy, 0.0f, &ds, 0x8e, 1, 0xa7, 0x94, 0x69);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Player 1's second charge ring (sprites by camera level, 4 per level from
// 0xd9): num2 marks 0xd9 at radius 113 (the filling one at frac2), then at
// the level's radius num_round dots 0xda at a / 2 and num2 marks 0xdb, and
// the filling step as five ticks 0xdc, each fading in over a fifth of
// frac2. Charging: red (0xe6, 0x50, 0x80) instead of grey.
// FF3 0x001bc4b8.

void HudCharge2Draw(float x, float y, float a, float sx, float sy, PHOTO_CHARGE_DISP *disp)
{
    DISP_SPRT ds;
    HUD_PART *p = &hud_charge2_part;
    float rate;
    float ox;
    float oy;
    float step;
    float start;
    float rad_lv;
    float sub;
    float base;
    float t;
    float ang;
    float rad;
    float c;
    float s;
    int   r;
    int   g;
    int   lv4;
    int   i;

    if (a <= 0.0f || disp->max <= 0 || disp->num <= 0)
    {
        return;
    }

    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    sy *= rate;
    sx *= rate;
    r = 0x80;
    g = 0x80;
    ox = x - HUD_CX;
    oy = y - HUD_CY;
    if (disp->charging)
    {
        r = 0xe6;
        g = 0x50;
    }

    memset(p, 0, sizeof(*p));
    step = -360.0f / (float)(disp->chr == 1 ? disp->max + 1 : disp->max);
    if (disp->chr == 1)
    {
        start = step + 180.0f;
    }
    else
    {
        start = (disp->chr == 2) ? 90.0f : 0.0f;
    }

    p->_20 = 1;
    p->alpha = 1.0f;
    p->sx = 1.0f;
    p->sy = 1.0f;
    p->att = 0;
    lv4 = disp->level * 4;
    p->r = r;
    p->_20 = 0;
    p->no = lv4 + 0xd9;
    p->g = g;
    p->b = 0x80;
    rad_lv = hud_level_radius[disp->level];

    // the full steps' marks, then the filling one's at frac2
    for (i = 0; i < disp->num2; i++)
    {
        ang = step * (float)i + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);
        p->rot = -ang;
        p->y = c * 113.0f + HUD_CY;
        p->x = s * 113.0f + HUD_CX;
        HudPartDraw(ox, oy, a, sx, sy, x, y, &ds, p);
    }
    if (0.0f < disp->frac2)
    {
        ang = step * (float)disp->num2 + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);
        p->rot = -ang;
        p->y = c * 113.0f + HUD_CY;
        p->x = s * 113.0f + HUD_CX;
        HudPartDraw(ox, oy, disp->frac2 * a, sx, sy, x, y, &ds, p);
    }

    // the rounded charge's dots
    p->no = lv4 + 0xda;
    p->b = 0x80;
    p->_20 = 1;
    p->r = r;
    p->g = g;
    for (i = 0; i < disp->num_round; i++)
    {
        ang = step * (float)i + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);
        p->rot = -ang;
        p->y = rad_lv * c + HUD_CY;
        p->x = rad_lv * s + HUD_CX;
        HudPartDraw(ox, oy, a * 0.5f, sx, sy, x, y, &ds, p);
    }

    // the full steps on the level ring
    p->no = lv4 + 0xdb;
    p->b = 0x80;
    p->g = g;
    p->r = r;
    p->_20 = 1;
    for (i = 0; i < disp->num2; i++)
    {
        ang = step * (float)i + start;
        rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
        c = Vu0Cosf(rad);
        s = Vu0Sinf(rad);
        p->rot = -ang;
        p->y = rad_lv * c + HUD_CY;
        p->x = rad_lv * s + HUD_CX;
        HudPartDraw(ox, oy, a, sx, sy, x, y, &ds, p);
    }

    // the filling step as five ticks
    if (0.0f < disp->frac2)
    {
        p->no = lv4 + 0xdc;
        sub = (step + 5.0f) / 5.0f;
        base = (step * (float)disp->num2 + start) - (sub + sub);
        for (i = 0; i < 5; i++)
        {
            t = disp->frac2 - (float)i * 0.2f;
            if (0.0f < t)
            {
                ang = sub * (float)i + base;
                rad = NormalizeAngle((ang * HUD_PI) / 180.0f - HUD_PI);
                c = Vu0Cosf(rad);
                s = Vu0Sinf(rad);
                p->x = rad_lv * s + HUD_CX;
                p->y = rad_lv * c + HUD_CY;
                p->rot = -ang;
                HudPartDraw(ox, oy, (0.2f <= t) ? a : (a * t) / 0.2f, sx, sy, x, y, &ds, p);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// A sprite turned about the screen centre by t degrees: its table centre
// (scaled by sx, sy and the camera rate) is rotated, and the sprite drawn
// centred there with rotation t + 180.
// FF3 0x001bcc78.

static void HudSprtDrawRot(float x, float y, float a, float t, float sx, float sy,
                           int no, int blend, u_char r, u_char g, u_char b)
{
    DISP_SPRT ds;
    const SPRT_DAT *spr = &hud_sprt[no];
    float rate;
    float rot;
    float rad;
    float c;
    float s;
    float dx;
    float dy;

    rate = PlyrGetFinderScale(HUD_CHR_CUR, 0xff);
    sy *= rate;
    sx *= rate;
    rot = t + 180.0f;
    rad = NormalizeAngle((t * HUD_PI) / 180.0f - HUD_PI);
    c = Vu0Cosf(rad);
    s = Vu0Sinf(rad);

    dx = (float)(spr->x + (spr->w >> 1) - 320) * sx;
    dy = (float)(spr->y + (spr->h >> 1) - 224) * sy;
    HudSprtSetCenter((dx * c - dy * s) + HUD_CX + x, (dx * s + dy * c) + HUD_CY + y,
                     a, sx, sy, rot, &ds, no);
    HudSprtSetBlend(&ds, blend);
    ds.r = r;
    ds.g = g;
    ds.b = b;
    DispSprD(&ds);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bce58.

void HudDamageNumDraw(float x, float y, float a, short damage)
{
    HudNumberDraw(x, y, a, damage, 0x90, 0x9a);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bce78.

void HudPanelNumDraw(float x, float y, float a, int num)
{
    HudNumberDraw(x, y, a, num, 0xb6, 0xc0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bce98.

void HudPanelLevelDraw(float x, float y, float a, int level)
{
    HudNumberDraw(x, y, a, level, 0x9f, 0xa9);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bceb8.

void HudPanelIconDraw(float x, float y, float a, int no)
{
    DISP_SPRT ds;

    if (hud_panel_icon[no] >= 0)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, hud_panel_icon[no], 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bcf18.

void HudPanelMark1Draw(float x, float y, float a)
{
    DISP_SPRT ds;
    int no;

    for (no = 0x9b; no < 0x9d; no++)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, no, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bcfb8.

void HudPanelMark2Draw(float x, float y, float a)
{
    DISP_SPRT ds;
    int no;

    for (no = 0x9d; no < 0x9f; no++)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, no, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Viewfinder down only: the gauge frame sprites 0x20, 0x1e, 0x1f turned 90
// degrees, moved right by 0x20's height (12 px less for player 1's camera).
// FF3 0x001bd058.

void HudGageFrameDraw(float x, float y, float a)
{
    DISP_SPRT ds;

    if (HudIsViewOn())
    {
        return;
    }
    if (HudGetCamType() == 2)
    {
        x -= 12.0f;
    }
    x += (float)hud_sprt[0x20].h;
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 90.0f, &ds, 0x20, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 90.0f, &ds, 0x1e, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 90.0f, &ds, 0x1f, 0, 0x80, 0x80, 0x80);
}

// ──────────────────────────────────────────────────────────────────────
// The filament: its core at a * core and its glow at a * whole * 0.8,
// scale a * whole * 0.2 + 0.8 (12 px left for player 1's camera with the
// viewfinder down).
// FF3 0x001bd1a0.

void HudFilamentDraw(float x, float y, float a, float core, float whole, int type)
{
    DISP_SPRT ds;
    float w = a * whole;
    float glow_a = w * 0.8f;
    float glow_s = w * 0.2f + 0.8f;
    float core_a = a * core;

    if (!HudIsViewOn() && HudGetCamType() == 2)
    {
        x -= 12.0f;
    }
    HudFilamentCoreDraw(x, y, core_a, &ds, type);
    HudFilamentGlowDraw(x, y, glow_a, glow_s, &ds, type);
}

// ──────────────────────────────────────────────────────────────────────
// The filament's glow alone at a * 0.6 + 0.2, scale a * 0.2 + 1.
// FF3 0x001bd290.

void HudFilament2Draw(float x, float y, float a, int type)
{
    DISP_SPRT ds;
    float glow_a = a * 0.6f + 0.2f;
    float glow_s = a * 0.2f + 1.0f;

    if (!HudIsViewOn() && HudGetCamType() == 2)
    {
        x -= 12.0f;
    }
    HudFilamentGlowDraw(x, y, glow_a, glow_s, &ds, type);
}

// ──────────────────────────────────────────────────────────────────────
// The viewfinder frame: sprites 0x30..0x39, 0x29, 0x27, 0x28, then 0x3a
// with lens function 4 and 0x3c with lens function 1 on; the real camera's
// frame is 0x78..0x8b.
// FF3 0x001bd368.

void HudFinderFrameDraw(float x, float y, float a, int real)
{
    DISP_SPRT ds;
    int no;

    if (real != 0)
    {
        for (no = 0x78; no < 0x8c; no++)
        {
            HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, no, 0, 0x80, 0x80, 0x80);
        }
        return;
    }

    for (no = 0x30; no < 0x3a; no++)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, no, 0, 0x80, 0x80, 0x80);
    }
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x29, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x27, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x28, 0, 0x80, 0x80, 0x80);
    if (GetLensWrk()->GetFunc(LENS_FUNC_4BC))
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x3a, 0, 0x80, 0x80, 0x80);
    }
    if (GetLensWrk()->GetFunc(LENS_FUNC_274))
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x3c, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The +0x4bc gauge: sprite 0x3b stretched to max(disp, tail) in red at
// a / 2, then to disp in grey; both additive.
// FF3 0x001bd5a8.

void HudDrawGage4bc(float x, float y, float a, float disp, float tail)
{
    DISP_SPRT ds;

    if (0.0f < tail)
    {
        HudSprtDrawOfs(x, y, a * 0.5f, (tail < disp) ? disp : tail, 1.0f, 0.0f,
                       &ds, 0x3b, 1, 0xff, 0, 0);
    }
    if (0.0f < disp)
    {
        HudSprtDrawOfs(x, y, a, disp, 1.0f, 0.0f, &ds, 0x3b, 1, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001bd688.

void HudWrk274Draw(float x, float y, float a, float rate)
{
    DISP_SPRT ds;

    if (0.0f < rate)
    {
        HudSprtDrawOfs(x, y, a * rate, 1.0f, 1.0f, 0.0f, &ds, 0x3d, 1, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The lens slots: the frame 0xc3 (one slot: with 0xc4), else a pair of
// 0xc2 every 23 px up per further slot (46 per slot), a rail of 0xc5 over
// 72 px and the top pieces 0xc6, 0xc1 above the last slot.
// FF3 0x001bd6e0.

void HudLensSlotsDraw(float x, float y, float a, int slot_num)
{
    DISP_SPRT ds;
    float base;
    float top;
    int   h;
    int   n;
    int   i;
    int   j;

    if (a < 0.0f || slot_num <= 0)
    {
        return;
    }

    if (slot_num == 1)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xc3, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xc4, 0, 0x80, 0x80, 0x80);
        return;
    }

    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xc3, 0, 0x80, 0x80, 0x80);
    for (i = 0; i < slot_num - 1; i++)
    {
        for (j = 0; j < 2; j++)
        {
            HudSprtDrawOfs(x, y + ((float)(i * -46) + (float)(j * -23)), a, 1.0f, 1.0f, 0.0f,
                           &ds, 0xc2, 0, 0x80, 0x80, 0x80);
        }
    }

    // FF3 divides by the rail sprite's height without a check: hud_sprt has
    // to be filled before this runs (finder_view.c's static initializer).
    base = (float)(slot_num * -46 + 46);
    h = hud_sprt[0xc5].h;
    n = 0x48 / h;
    for (j = 0; j <= n; j++)
    {
        HudSprtDrawOfs(x, y + ((float)(hud_sprt[0xc5].h * j) + base), a, 1.0f, 1.0f, 0.0f,
                       &ds, 0xc5, 0, 0x80, 0x80, 0x80);
    }

    top = y + base;
    HudSprtDrawOfs(x, top, a, 1.0f, 1.0f, 0.0f, &ds, 0xc6, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, top, a, 1.0f, 1.0f, 0.0f, &ds, 0xc1, 0, 0x80, 0x80, 0x80);
}

// ──────────────────────────────────────────────────────────────────────
// The slots' lenses (slot frame 0xc9 and the lens icon 0xca + n, 9: none,
// in 0x40 grey), the selected slot's at the bottom (hidden unless
// switching: HudLensDraw shows it) and each next one 46 px up. While
// switching (sw_state 1 forwards, 2 back) they slide by t and the ends
// fade.
// FF3 0x001bd998.

void HudLensSlotLensDraw(float x, float y, float a, float t, int slot_num, int sw_state)
{
    DISP_SPRT ds;
    float inv;
    float off;
    float yy;
    float al;
    s_char lens;
    int   cur;
    int   slot;
    int   icon;
    int   i;

    if (a < 0.0f || slot_num <= 0)
    {
        return;
    }

    inv = 1.0f - t;
    off = 0.0f;
    if (sw_state == 1)
    {
        off = t * -46.0f;
    }
    else if (sw_state == 2)
    {
        off = inv * -46.0f;
    }

    cur = GetLensWrk()->GetSel(HUD_CHR_CUR);
    if (sw_state == 1)
    {
        cur++;
    }

    for (i = 0; i < slot_num; i++)
    {
        slot = (i + cur) % slot_num;
        yy = (float)(i * -46) + off;
        al = a;
        if (i == 0)
        {
            if (sw_state == 0)
            {
                continue;
            }
            if (sw_state == 1)
            {
                al *= t;
            }
            else if (sw_state == 2)
            {
                al *= inv;
            }
        }
        else if (i == slot_num - 1)
        {
            if (sw_state == 1)
            {
                al *= inv;
            }
            else if (sw_state == 2)
            {
                al *= t;
            }
        }

        yy = y + yy;
        lens = GetLensWrk()->GetEquip((u_char)slot, HUD_CHR_CUR);
        icon = ((u_char)lens < 9) ? lens : 9;
        HudSprtDrawOfs(x, yy, al, 1.0f, 1.0f, 0.0f, &ds, 0xc9, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, yy, al, 1.0f, 1.0f, 0.0f, &ds, icon + 0xca, 0, 0x40, 0x40, 0x40);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The shown lens: its icon 0xca + n (9: none) 6 px down, fading out while
// switching, and behind it sprite 0xd4 at a * usable in the lens's colour.
// FF3 0x001bdbf0.

void HudLensDraw(float x, float y, float a, float t, float usable, int equip, int sw_state)
{
    DISP_SPRT ds;
    s_char lens = (s_char)equip;
    int    valid = (u_char)lens < 9;

    if (a < 0.0f)
    {
        return;
    }

    if ((u_int)(sw_state - 1) < 2)
    {
        a = a * (1.0f - t);
    }
    HudSprtDrawOfs(x, y + 6.0f, a, 1.0f, 1.0f, 0.0f, &ds, (valid ? lens : 9) + 0xca, 0, 0x80, 0x80, 0x80);
    if (valid)
    {
        HudSprtDrawOfs(x, y, a * usable, 1.0f, 1.0f, 0.0f, &ds, 0xd4, 1,
                       hud_lens_col[lens][0], hud_lens_col[lens][1], hud_lens_col[lens][2]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The lens's marks: sprites 0xe9..0xec turned by (1 - label) * 50 degrees
// and 0xed..0xf0 by the opposite, at a * label / 2, additive, in the
// lens's mark colour.
// FF3 0x001bdd28.

void HudLensMarkDraw(float x, float y, float a, float label, int equip)
{
    s_char lens = (s_char)equip;
    const u_char *col;
    float al;
    float rot;
    int   i;

    if (a < 0.0f || (u_char)lens >= 9)
    {
        return;
    }

    col = hud_lens_mark_col[lens];
    al = a * (label * 0.5f);
    rot = (1.0f - label) * 50.0f;
    for (i = 0; i < 4; i++)
    {
        HudSprtDrawRot(x, y, al, rot, 1.0f, 1.0f, 0xe9 + i, 1, col[0], col[1], col[2]);
        HudSprtDrawRot(x, y, al, -rot, 1.0f, 1.0f, 0xed + i, 1, col[0], col[1], col[2]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The spirit-stone sockets, 20 px apart: 0xc7, the last 0xc8.
// FF3 0x001bde68.

void HudStoneSocketsDraw(float x, float y, float a, int stone_max)
{
    DISP_SPRT ds;
    int i;

    if (stone_max <= 0)
    {
        return;
    }
    for (i = 0; i < stone_max; i++)
    {
        HudSprtDrawOfs(x + (float)(i * 20), y, a, 1.0f, 1.0f, 0.0f, &ds,
                       (i != stone_max - 1) ? 0xc7 : 0xc8, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Spirit stone i, 20 px apart: states 2 and 3 show the turning stone 0xd5
// (state 4 fading it in by t, else 0xd6 while filled); then the socket light
// 0xd7 and, blinking in (state 1) or out (state 2) by t, 0xd8 additive.
// The turning stone's table position (132, 384) is the socket's centre, and
// HudSprtSet pivots the rotation there; the sprite (18x18) is then moved back
// 10 px so it turns in place.  Moved forward, it swung round the socket in a
// 27 px circle as the charge built.
// FF3 0x001bdf60.

void HudStoneDraw(float x, float y, float a, float fill, float t, float rot, int i, int state)
{
    DISP_SPRT ds;

    if (i < 0)
    {
        return;
    }
    x += (float)(i * 20);

    if ((u_int)(state - 2) < 2)
    {
        HudSprtSetOfs(x, y, a, 1.0f, 1.0f, rot, &ds, 0xd5);
        ds.x -= 10.0f;
        ds.y -= 10.0f;
        DispSprD(&ds);
    }
    else
    {
        if (0.0f < fill)
        {
            HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xd6, 0, 0x80, 0x80, 0x80);
        }
        if (state == 4)
        {
            HudSprtSetOfs(x, y, a * t, 1.0f, 1.0f, rot, &ds, 0xd5);
            ds.x -= 10.0f;
            ds.y -= 10.0f;
            DispSprD(&ds);
        }
    }

    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xd7, 0, 0x80, 0x80, 0x80);
    if (state == 1)
    {
        HudSprtDrawOfs(x, y, a * t, 1.0f, 1.0f, 0.0f, &ds, 0xd8, 1, 0x80, 0x80, 0x80);
    }
    else if (state == 2)
    {
        HudSprtDrawOfs(x, y, a * (1.0f - t), 1.0f, 1.0f, 0.0f, &ds, 0xd8, 1, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The HP gauge: sprite 5 stretched upwards (-40 px per unit), the tail in
// red at a / 2, the value at a * pulse; 5 px left and 45 up with the
// viewfinder up.
// FF3 0x001be160.

void HudHpGaugeDraw(float x, float y, float a, float disp, float tail, float pulse)
{
    DISP_SPRT ds;

    if (HudIsViewOn())
    {
        x += -5.0f;
        y += -45.0f;
    }
    if (0.0f < tail)
    {
        HudSprtDrawOfs(x, y, a * 0.5f, 1.0f, tail * -40.0f, 0.0f, &ds, 5, 0, 0xff, 0, 0);
    }
    if (0.0f < disp)
    {
        HudSprtDrawOfs(x, y, a * pulse, 1.0f, disp * -40.0f, 0.0f, &ds, 5, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Player 1's camera only: the slow-mode gauge, sprite 0xb stretched
// upwards by rate in the colour row col.
// FF3 0x001be2a0.

void HudSlowGaugeDraw(int col, float x, float y, float a, float rate)
{
    DISP_SPRT ds;
    const u_char *c;

    if (HudGetCamType() != 2)
    {
        return;
    }
    c = hud_slow_col[col];
    if (HudIsViewOn())
    {
        x += -5.0f;
        y += -45.0f;
    }
    if (0.0f < rate)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, rate * -40.0f, 0.0f, &ds, 0xb, 0, c[0], c[1], c[2]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The HP frame: sprite 4 ten times as tall, then 0..3 (player 1's camera:
// 10, then 6..9); none while ingame_wrk.b03 is set.
// FF3 0x001be390.

void HudHpFrameDraw(float x, float y, float a)
{
    DISP_SPRT ds;
    int base;

    if (HudIsViewOn())
    {
        x += -5.0f;
        y += -45.0f;
    }
    if ((s_char)ingame_wrk.b03 != 0)
    {
        return;
    }

    if (HudGetCamType() == 2)
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 10.0f, 0.0f, &ds, 0xa, 0, 0x80, 0x80, 0x80);
        base = 6;
    }
    else
    {
        HudSprtDrawOfs(x, y, a, 1.0f, 10.0f, 0.0f, &ds, 4, 0, 0x80, 0x80, 0x80);
        base = 0;
    }
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, base + 0, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, base + 1, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, base + 2, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, base + 3, 0, 0x80, 0x80, 0x80);
}

// ──────────────────────────────────────────────────────────────────────
// The viewfinder's counters, placed absolutely from (x, y): with the flash
// (HudGetCamType 1) a long panel 0x48 / 0x46 / 0x47, the flash counter
// (icon 0x12, grey 0x20 when empty or not ready, separator 0x13, two
// digits 0x14..) at +556 and the film counter at +441; otherwise the panel
// 0x45 / 0x43 / 0x44 and the film counter at +471. The film counter: the
// film's icon 0x3e + type (grey when empty or unable to shoot), 0xf1, the
// film's name 0xf2 + type, 0x13 and two digits.
// FF3 0x001be630.

void HudFinderCountDraw(float x, float y, float a, int film_type, int film_num, int can_shoot,
                        int flash_num, int flash_ready)
{
    DISP_SPRT ds;
    float fx;
    float fy;
    float nx;
    float ny;
    float ny2;
    int   lit;
    u_char col;

    if (HudGetCamType() == 1)
    {
        HudSprtDrawOfs(x, y, a, 7.0f, 1.0f, 0.0f, &ds, 0x48, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x46, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x47, 0, 0x80, 0x80, 0x80);

        // the flash counter
        lit = (0 < flash_num) ? flash_ready : 0;
        col = lit ? 0x80 : 0x20;
        fx = x + 556.0f;
        fy = y + 400.0f;
        HudSprtDraw(fx, fy, a, 1.0f, 1.0f, 0.0f, &ds, 0x12, 0, col, col, col);
        ny = fy + 6.0f;
        nx = fx + 15.0f;
        HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, 0x13, 0, 0x80, 0x80, 0x80);
        ny2 = ny - 5.0f;
        HudSprtDraw(nx + 12.0f, ny2, a, 1.0f, 1.0f, 0.0f, &ds, flash_num / 10 + 0x14, 0, 0x80, 0x80, 0x80);
        nx += 22.0f;
        HudSprtDraw(nx, ny2, a, 1.0f, 1.0f, 0.0f, &ds, flash_num % 10 + 0x14, 0, 0x80, 0x80, 0x80);

        // the film counter
        lit = (0 < film_num) ? can_shoot : 0;
        col = lit ? 0x80 : 0x20;
        x += 441.0f;
        HudSprtDraw(x, fy, a, 1.0f, 1.0f, 0.0f, &ds, film_type + 0x3e, 0, col, col, col);
        HudSprtDraw(x + 31.0f, fy + 2.0f, a, 1.0f, 1.0f, 0.0f, &ds, 0xf1, 0, 0x80, 0x80, 0x80);
        HudSprtDraw(x + 62.0f, fy + 2.0f, a, 1.0f, 1.0f, 0.0f, &ds, film_type + 0xf2, 0, 0x80, 0x80, 0x80);
        nx = x + 79.0f;
        HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, 0x13, 0, 0x80, 0x80, 0x80);
        HudSprtDraw(nx + 12.0f, ny2, a, 1.0f, 1.0f, 0.0f, &ds, film_num / 10 + 0x14, 0, 0x80, 0x80, 0x80);
        nx += 22.0f;
        HudSprtDraw(nx, ny2, a, 1.0f, 1.0f, 0.0f, &ds, film_num % 10 + 0x14, 0, 0x80, 0x80, 0x80);
    }
    else
    {
        HudSprtDrawOfs(x, y, a, 5.0f, 1.0f, 0.0f, &ds, 0x45, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x43, 0, 0x80, 0x80, 0x80);
        HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x44, 0, 0x80, 0x80, 0x80);

        // the film counter
        lit = (0 < film_num) ? can_shoot : 0;
        col = lit ? 0x80 : 0x20;
        fx = x + 471.0f;
        fy = y + 400.0f;
        HudSprtDraw(fx, fy, a, 1.0f, 1.0f, 0.0f, &ds, film_type + 0x3e, 0, col, col, col);
        HudSprtDraw(fx + 31.0f, fy + 2.0f, a, 1.0f, 1.0f, 0.0f, &ds, 0xf1, 0, 0x80, 0x80, 0x80);
        HudSprtDraw(fx + 62.0f, fy + 2.0f, a, 1.0f, 1.0f, 0.0f, &ds, film_type + 0xf2, 0, 0x80, 0x80, 0x80);
        nx = fx + 79.0f;
        ny = fy + 6.0f;
        HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, 0x13, 0, 0x80, 0x80, 0x80);
        ny -= 5.0f;
        HudSprtDraw(nx + 12.0f, ny, a, 1.0f, 1.0f, 0.0f, &ds, film_num / 10 + 0x14, 0, 0x80, 0x80, 0x80);
        nx += 22.0f;
        HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, film_num % 10 + 0x14, 0, 0x80, 0x80, 0x80);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The flash counter on the HUD (viewfinder down, flash player only): the
// panel 0xd (x4) / 0xc / 0xe / 0x10 (x4) / 0xf / 0x11, then at +511, +404
// the flash icon 0x12 (grey 0x20 when empty or not ready), 0x13 and two
// digits 0x14...
// FF3 0x001beda0.

void HudFlashCountDraw(float x, float y, float a, int flash_num, int flash_ready)
{
    DISP_SPRT ds;
    float nx;
    float ny;
    int   lit;
    u_char col;

    if (HudIsViewOn())
    {
        return;
    }
    if (HudGetCamType() != 1)
    {
        return;
    }

    HudSprtDrawOfs(x, y, a, 4.0f, 1.0f, 0.0f, &ds, 0xd, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xc, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xe, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 4.0f, 1.0f, 0.0f, &ds, 0x10, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0xf, 0, 0x80, 0x80, 0x80);
    HudSprtDrawOfs(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x11, 0, 0x80, 0x80, 0x80);

    lit = (0 < flash_num) ? flash_ready : 0;
    col = lit ? 0x80 : 0x20;
    x += 511.0f;
    y += 404.0f;
    HudSprtDraw(x, y, a, 1.0f, 1.0f, 0.0f, &ds, 0x12, 0, col, col, col);
    nx = x + 15.0f;
    ny = y + 6.0f;
    HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, 0x13, 0, 0x80, 0x80, 0x80);
    ny -= 5.0f;
    HudSprtDraw(nx + 12.0f, ny, a, 1.0f, 1.0f, 0.0f, &ds, flash_num / 10 + 0x14, 0, 0x80, 0x80, 0x80);
    nx += 22.0f;
    HudSprtDraw(nx, ny, a, 1.0f, 1.0f, 0.0f, &ds, flash_num % 10 + 0x14, 0, 0x80, 0x80, 0x80);
}
