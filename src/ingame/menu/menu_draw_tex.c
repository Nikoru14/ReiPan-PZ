// ingame/menu/menu_draw_tex.c -- menu pictures and their cross-faders
// (FF3-only, 0x209610..0x20b148). See menu_draw_tex.h.

#include "ingame/menu/menu_draw_tex.h"

#include <new>                              // operator new / delete

#include "common/heapctrl.h"                // heapCtrlMalloc, heapCtrlFree
#include "graphics/graph2d/g2d_draw.h"      // SPRT_DAT, DISP_SPRT, CopySprDToSpr, DispSprD
#include "graphics/graph2d/message.h"       // SetCurColorRGB, MsgSetPage, MsgPrintPageZ
#include "graphics/graph2d/tim2.h"          // Tim2GetFirstPicture, MakeTim2*, MakeClutDirect
#include "system/eeiop/cddat.h"             // GetFileSize
#include "system/eeiop/file_cache.h"        // file_cache
#include "system/eeiop/fileload.h"          // FileLoadCancel
#include "system/os/eecdvd.h"               // LoadReq, IsLoadEnd
#include "system/os/sys_heap.h"             // GetSystemHeapWrkP
#include "reipan/rendering/reipan_renderer.h" // PC: ReiPan_RendererSet2dAlphaTest


// ══════════════════════════════════════════════════════════════════════
// Menu_DrawTex
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209670 (MenuDrawTexCtor; 0x00209610 MenuDrawTexCtorBase is the
// identical base-object copy the Menu_DrawTexMulti ctors call).

Menu_DrawTex::Menu_DrawTex()
{
    Clear();
    direct = 0;
    u      = 0;
    w      = 0xff;
    alphar = MENU_DRAW_TEX_ALPHA_BLEND;
    test   = MENU_DRAW_TEX_TEST_ALWAYS;
    v      = 0;
    h      = 0xff;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002096f8 (MenuDrawTexDtor, vtable slot 0; 0x002096d0 MenuDrawTexDtorBase
// is the base-object copy, 0x00209720 MenuDrawTexDtorDelete the deleting one).
// The release is the class's own, not the virtual.

Menu_DrawTex::~Menu_DrawTex()
{
    Menu_DrawTex::Release();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209758

void Menu_DrawTex::Clear()
{
    buf     = nullptr;
    load_id = -1;
    file    = MENU_DRAW_TEX_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// Loads file_no into a buffer of its size from the system heap; the blank
// file only marks the slot set.
// FF3 0x00209770 (MenuDrawTexLoad, vtable slot 2). FF3 calls GetFileSize
// three times (size, -1 test, 0 test); kept.

void Menu_DrawTex::Load(int file_no)
{
    u_int size;

    file = file_no;
    if (file_no == MENU_DRAW_TEX_BLANK)
        return;
    size = GetFileSize(file_no);
    if (GetFileSize(file_no) == (u_int)-1)
        return;
    if (GetFileSize(file_no) == 0)
        return;
    buf = heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    if (buf != nullptr)
        load_id = LoadReq(file_no, buf);
}

// ──────────────────────────────────────────────────────────────────────
// Cancels an unfinished load and frees the buffer.
// FF3 0x00209820 (MenuDrawTexRelease, vtable slot 3).

void Menu_DrawTex::Release()
{
    if (load_id != -1 && !IsLoadEnd(load_id))
        FileLoadCancel(load_id, nullptr, nullptr);
    if (buf != nullptr)
    {
        heapCtrlFree(GetSystemHeapWrkP(), buf, 0, 0, 0);
        buf = nullptr;
    }
    Clear();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002098a0 (MenuDrawTexIsReady, vtable slot 4).

int Menu_DrawTex::IsReady()
{
    if (file == MENU_DRAW_TEX_NONE)
        return 0;
    if (file == MENU_DRAW_TEX_BLANK)
        return 1;
    if (load_id == -1)
        return 0;
    return IsLoadEnd(load_id);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002098f0

int Menu_DrawTex::IsSet()
{
    return file != MENU_DRAW_TEX_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209900

int Menu_DrawTex::IsFile(int file_no)
{
    return file == file_no;
}

// ──────────────────────────────────────────────────────────────────────
// Puts a TIM2 at the fixed menu-picture place right away: TEX0 from the
// constant at 0x371210 (TBP0 0x2cc4, TBW 10, PSMT8, TW / TH 10 / 9, TCC 1,
// CBP 0x2bc0 PSMCT32, CLD 1). No callers.
// FF3 0x00209910

void Menu_DrawTex::SetVram(void *tim2)
{
    Tim2GetFirstPicture(tim2)->GsTex0 = 0x200578066932acc4ULL;     /* data 0x371210 */
    MakeTim2Direct(tim2, 0, MENU_DRAW_TEX_TBP);
    MakeClutDirect(tim2, 0, MENU_DRAW_TEX_CBP);
}

// ──────────────────────────────────────────────────────────────────────
// Rewrites the first picture's TEX0 for the fixed menu-picture place:
// TBP0 0x2cc4, CBP 0x2bc0, TBW from the width (2^TW rounded up to 64), TCC
// 1, CLD 1 (load the clut).
// FF3 0x00209960

void Menu_DrawTex::FixTex0(void *tim2)
{
    TIM2_PICTUREHEADER *ph = Tim2GetFirstPicture(tim2);
    u_long tex0;
    int    tw;

    tex0 = (ph->GsTex0 & ~0x3fffULL) | MENU_DRAW_TEX_TBP;                      // TBP0
    tex0 = (tex0 & ~(0x3fffULL << 37)) | ((u_long)MENU_DRAW_TEX_CBP << 37);     // CBP
    tw   = 1 << (int)((tex0 >> 26) & 0xf);                                     // 2^TW
    if (tw < 0)
        tw += 0x3f;
    tex0 = (tex0 & ~(0x3fULL << 14)) | ((u_long)((tw >> 6) & 0x3f) << 14);      // TBW
    tex0 |= 1ULL << 34;                                                         // TCC
    tex0 = (tex0 & ~(7ULL << 61)) | (1ULL << 61);                               // CLD
    ph->GsTex0 = tex0;
}

// ──────────────────────────────────────────────────────────────────────
// Draws the loaded picture as one sprite: its (u, v, w, h) texels at
// (x, y), z, priority pri, colour and alpha, with the slot's ALPHA / TEST;
// rot != 0 turns it about (x, y). Re-sends the TIM2 every draw, so the two
// slots of a MENU_PIC can share the fixed VRAM place.
// FF3 0x00209a10

void Menu_DrawTex::Draw(int x, int y, int z, int pri,
                     u_char r, u_char g, u_char b, u_char alpha, int rot)
{
    SPRT_DAT  dat;
    DISP_SPRT spr;

    if (file == MENU_DRAW_TEX_BLANK || !IsSet() || !IsReady())
        return;

    dat.tex0  = 0;
    dat.u     = (u_short)u;
    dat.v     = (u_short)v;
    dat.w     = (u_short)w;
    dat.h     = (u_short)h;
    dat.x     = x;
    dat.y     = y;
    dat.pri   = pri;
    dat.alpha = alpha;
    dat.flip  = 0;
    dat.bln   = 1;
    CopySprDToSpr(&spr, &dat);

    if (direct != 0)
        FixTex0(buf);
    MakeTim2SendPacket_3Dpkt(buf, 0, -1, -1);

    spr.test   = test;
    spr.tex0   = Tim2GetFirstPicture(buf)->GsTex0;
    spr.alphar = alphar;
    spr.r      = r;
    spr.g      = g;
    spr.b      = b;
    spr.z      = z;
    if (rot != 0)
    {
        spr.rot = (float)rot;
        spr.crx = (float)x;
        spr.cry = (float)y;
    }
    DispSprD(&spr);
}

// ══════════════════════════════════════════════════════════════════════
// Menu_DrawTexMulti
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209be0 (MenuDrawTexMultiCtor; 0x00209bb0 MenuDrawTexMultiCtorBase
// is the identical base-object copy).
// callcheck: MenuDrawTexCtorBase (the base-class ctor runs implicitly)

Menu_DrawTexMulti::Menu_DrawTexMulti()
{
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209c48 (MenuDrawTexMultiDtor, vtable slot 0; 0x00209c10
// MenuDrawTexMultiDtorBase is the base-object copy, 0x00209c80
// MenuDrawTexMultiDtorDelete the deleting one). Then ~Menu_DrawTex.
// callcheck: MenuDrawTexDtorBase (the base class destructor)

Menu_DrawTexMulti::~Menu_DrawTexMulti()
{
    Menu_DrawTexMulti::Release();
}

// ──────────────────────────────────────────────────────────────────────
// The buffer comes from the shared file cache, which loads each file once.
// FF3 0x00209cc0 (MenuDrawTexMultiLoad, vtable slot 2).

void Menu_DrawTexMulti::Load(int file_no)
{
    file = file_no;
    if (file_no != MENU_DRAW_TEX_BLANK)
        buf = file_cache.Req(file_no);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209d00 (MenuDrawTexMultiRelease, vtable slot 3).

void Menu_DrawTexMulti::Release()
{
    if (buf != nullptr)
        file_cache.Release(file);
    Clear();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00209d40 (MenuDrawTexMultiIsReady, vtable slot 4). FF3 returns the
// cache entry's done flag itself; only its truth is used.

int Menu_DrawTexMulti::IsReady()
{
    if (file == MENU_DRAW_TEX_NONE)
        return 0;
    if (file == MENU_DRAW_TEX_BLANK)
        return 1;
    return file_cache.IsLoaded(file) != 0;
}

// ══════════════════════════════════════════════════════════════════════
// MENU_PIC: the picture cross-fader
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Two slots (Menu_DrawTexMulti when multi, else Menu_DrawTex), both at the
// fixed VRAM place; fades 0..0x80 over 8 frames; nothing pending.
// FF3 0x00209d88 (MenuPicCtorBase, no callers) is the base-object copy: the
// same code, its scale constant data 0x371218, also 1.0.
// FF3 0x00209eb8 (MenuPicCtor; the scale constant is data 0x37121c, 1.0).
// callcheck: operator_new, MenuDrawTexMultiCtor, MenuDrawTexCtor (new Menu_DrawTexMulti / Menu_DrawTex)

MENU_PIC::MENU_PIC(int multi)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (multi != 0)
            load[i] = new Menu_DrawTexMulti();
        else
            load[i] = new Menu_DrawTex();
    }
    cur          = 0;
    fade_min     = 0;
    rgb[0]       = 0x80;
    scale        = 1.0f;           /* data 0x37121c */
    alphar       = MENU_DRAW_TEX_ALPHA_BLEND;
    w            = 0xff;
    fade         = -1;
    fade_len     = 8;
    fade_max     = 0x80;
    fix          = 0;
    mode         = 0;
    x            = 0;
    y            = 0;
    z            = 0;
    pri          = 0;
    rgb[2]       = 0x80;
    rgb[1]       = 0x80;
    v            = 0;
    u            = 0;
    h            = 0xff;
    rot          = 0;
    pic          = -1;
    enable       = 0;
    release_wait = 0;
    if (load[0] != nullptr)
        load[0]->direct = 1;
    if (load[1] != nullptr)
        load[1]->direct = 1;
}

// ──────────────────────────────────────────────────────────────────────
// Deletes both slots (their deleting dtors, vtable slot 1).
// FF3 0x0020a040
// callcheck: (virtual deleting dtors)

MENU_PIC::~MENU_PIC()
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (load[i] != nullptr)
            delete load[i];
    }
}

// ──────────────────────────────────────────────────────────────────────
// Draws slot `slot` with alpha (times scale, at most 0xff) and the given
// ALPHA / TEST. In dissolve mode, while a fade runs, four extra additive
// passes come first: alpha alpha / n, alpha test ref fade + 4n - 8 (n 1..4),
// which soften the dissolve edge. fix replaces ALPHA by a FIX blend.
// PC: the dissolve is the GS alpha test on the picture's own alpha, which the
// host renderer applies to 2D draws only inside ReiPan_RendererSet2dAlphaTest;
// without it the developed picture appeared at once in the darkroom.
// FF3 0x0020a098

void MENU_PIC::DrawSlot(int slot, u_char alpha, u_long alphar, u_long test)
{
    Menu_DrawTex *tex;
    int a;
    int ref;
    int n;

    a = (int)((float)alpha * scale);
    if (a < 0)
        return;
    if (a > 0xff)
        a = 0xff;

    tex    = load[slot];
    tex->v = v;
    tex->u = u;
    tex->h = h;
    tex->w = w;

    if (mode == 1)
        ReiPan_RendererSet2dAlphaTest(1);   // PC
    if (fade >= 0 && mode == 1)
    {
        n = 0;
        do
        {
            ref = GetFade() + n * 4 - 8;
            if (ref < 0)
                ref = 0;
            else if (ref > 0xff)
                ref = 0xff;
            n++;
            load[slot]->alphar = (u_long)(a / n) << 32 | 0x68;    // Cs * FIX + Cd
            load[slot]->test   = (u_long)ref << 4 | MENU_DRAW_TEX_TEST_AREF;
            load[slot]->Draw(x, y, z, pri,
                            rgb[0], rgb[1], rgb[2], (u_char)a, rot);
        } while (n < 4);
    }

    if (fix != 0)
        alphar = (u_long)a << 32 | 0x64;                                // (Cs - Cd) * FIX + Cd
    load[slot]->alphar = alphar;
    load[slot]->test   = test;
    load[slot]->Draw(x, y, z, pri,
                    rgb[0], rgb[1], rgb[2], (u_char)a, rot);
    if (mode == 1)
        ReiPan_RendererSet2dAlphaTest(0);   // PC
}

// ──────────────────────────────────────────────────────────────────────
// fade_max at rest; while fading fade_min..fade_max with fade / fade_len
// (fade clamped to fade_len), rounded.
// FF3 0x0020a288

int MENU_PIC::GetFade()
{
    int cnt = fade;
    int len;

    if (cnt >= 0)
    {
        len = fade_len;
        if (len > 0)
        {
            if (len < cnt)
            {
                fade = len;
                cnt = len;
            }
            return (int)((float)(int)(fade_max - fade_min) * ((float)cnt / (float)len)
                         + (float)fade_min + 0.5f) & 0xff;        /* data 0x371220 */
        }
    }
    return fade_max;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a318

int MENU_PIC::IsNextReady()
{
    return load[cur ^ 1]->IsReady();
}

// ──────────────────────────────────────────────────────────────────────
// The shown (outgoing) picture: cross-fade alpha GetFade; dissolve at full
// alpha where the texel alpha passes fade_max - GetFade.
// FF3 0x0020a350

void MENU_PIC::Draw()
{
    if (mode == 0)
    {
        DrawSlot(cur, (u_char)GetFade(), alphar, MENU_DRAW_TEX_TEST_ALWAYS);
    }
    else if (mode == 1)
    {
        int f = GetFade();
        DrawSlot(cur, 0x80, alphar, (u_long)((fade_max - f) & 0xff) << 4 | MENU_DRAW_TEX_TEST_AREF);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The incoming picture: cross-fade alpha fade_max - GetFade; dissolve at
// full alpha where the texel alpha passes GetFade.
// FF3 0x0020a3f8

void MENU_PIC::DrawNext()
{
    if (mode == 0)
    {
        u_char a = (u_char)(fade_max - GetFade());
        DrawSlot(cur ^ 1, a, alphar, MENU_DRAW_TEX_TEST_ALWAYS);
    }
    else if (mode == 1)
    {
        u_int f = (u_int)GetFade();
        DrawSlot(cur ^ 1, 0x80, alphar, (u_long)(f & 0xff) << 4 | MENU_DRAW_TEX_TEST_AREF);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Ends a running fade at once (the incoming picture becomes the shown one);
// the old slot is freed next frame by MenuPicStep.
// FF3 0x0020a4a8

int MENU_PIC::CutFade()
{
    if (fade >= 0)
    {
        fade = -1;
        cur ^= 1;
        if (release_wait == 0)
            release_wait = 1;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Per frame. Frees the old slot when its wait ends; takes the pending
// picture: already shown -> drop whatever comes in (or turn a running fade
// back), already coming in -> nothing, else cut a running fade, and load it
// into the free slot (or wait while that slot is still taken). When enabled,
// starts the fade once the incoming picture is ready and counts it down;
// at the end the shown slot is released and cur flips. 1 while fading.
// FF3 0x0020a4e8
// callcheck: (the slots' Load / Release / IsReady are virtual)

int MENU_PIC::Step()
{
    Menu_DrawTex *next;
    int f;
    int old;

    if (release_wait > 0 && --release_wait == 0)
    {
        next = load[cur ^ 1];
        if (next->IsSet())
            next->Release();
    }

    if (pic != -1)
    {
        if (load[cur]->IsFile(pic))
        {
            if (fade < 0)
            {
                load[cur ^ 1]->Release();
            }
            else
            {
                cur ^= 1;
                fade = fade_len - fade;
            }
            pic = -1;
        }
        else if (load[cur ^ 1]->IsFile(pic))
        {
            pic = -1;
        }
        else
        {
            if (fade < 0)
            {
                next = load[cur ^ 1];
                if (next->IsSet())
                    next->Release();
            }
            else
            {
                CutFade();
            }
            if (!load[cur ^ 1]->IsSet())
            {
                load[cur ^ 1]->Load(pic);
                pic = -1;
            }
        }
    }

    if (enable == 0)
        return 0;

    f = fade;
    if (pic == -1 && f < 0)
    {
        next = load[cur ^ 1];
        if (next->IsSet() && next->IsReady())
        {
            f = fade_len;
            fade = f;
        }
        else
        {
            f = fade;
        }
    }
    if (f < 0)
        return 0;

    fade = f - 1;
    if (f - 1 < 0)
    {
        old = cur;
        cur = (s_char)(old ^ 1);
        load[old]->Release();
    }
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// MENU_MSG_OBJ: the message-text cross-fader (no callers in the game)
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a770 (MenuMsgObjCtorBase, no callers) is the base-object copy
// (its scale constant data 0x371224, also 1.0).
// FF3 0x0020a818 (MenuMsgObjCtor; the scale constant is data 0x371228, 1.0).

MENU_MSG_OBJ::MENU_MSG_OBJ()
{
    int i;

    for (i = 0; i < 2; i++)
        ClearSlot((s_char)i);
    enable   = 0;
    cur      = 0;
    rgb[0]   = 0x80;
    scale    = 1.0f;               /* data 0x371228 */
    fade     = -1;
    fade_len = 8;
    fade_min = 0;
    fade_max = 0x80;
    mode     = 0;
    x        = 0;
    y        = 0;
    _038     = 0;
    pri      = 0;
    rgb[2]   = 0x80;
    rgb[1]   = 0x80;
    y_wide   = 0;
    x_wide   = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a8c0 (MenuMsgObjDtorBase) is the base-object copy, the same
// code. FF3 0x0020a910 (MenuMsgObjDtor).

MENU_MSG_OBJ::~MENU_MSG_OBJ()
{
    int i;

    for (i = 0; i < 2; i++)
        ClearSlot((s_char)i);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a960

void MENU_MSG_OBJ::ClearSlot(int slot)
{
    MENU_MSG_SLOT *s = &this->slot[(s_char)slot];

    s->page = -1;
    s->type = -1;
    s->id   = -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a988

void MENU_MSG_OBJ::SetSlot(int slot, int type, int id, int page)
{
    MENU_MSG_SLOT *s = &this->slot[(s_char)slot];

    s->page = page;
    s->type = type;
    s->id   = id;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020a9b0

int MENU_MSG_OBJ::IsSlot(int slot, int type, int id, int page)
{
    MENU_MSG_SLOT *s = &this->slot[(s_char)slot];

    return s->type == type && s->id == id && s->page == page;
}

// ──────────────────────────────────────────────────────────────────────
// Prints slot `slot` in the object's colour at alpha (mirrored in mode 2,
// times scale, at most 0xff); empty slots print nothing.
// FF3 0x0020a9f8

void MENU_MSG_OBJ::DrawSlot(int slot, int alpha)
{
    MENU_MSG_SLOT *s;
    int a;

    if (mode == 2)
    {
        alpha = (fade_max - fade_min) - alpha;
        if (alpha < 0)
            alpha = -alpha;
    }
    a = (int)((float)alpha * scale);
    if (a < 0)
        return;
    s = &this->slot[(s_char)slot];
    if (a > 0xff)
        a = 0xff;
    if (s->type < 0 || s->id < 0 || s->page < 0)
        return;
    SetCurColorRGB(rgb[0], rgb[1], rgb[2]);
    MsgSetPage((signed char)s->page);
    MsgPrintPageZ(s->type, s->id, x, y, MSG_COL_CURRENT, a, pri,
                  x_wide, y_wide);
}

// ──────────────────────────────────────────────────────────────────────
// As MenuPicGetFade (not masked to a byte); mode 2 maps fade / fade_len
// to 2t - 1, so the alpha goes fade_max .. fade_min - range and
// MenuMsgObjDrawSlot mirrors it back (there and back again).
// FF3 0x0020aaf0

int MENU_MSG_OBJ::GetFade()
{
    int   cnt = fade;
    int   len;
    float t;
    float range;

    if (cnt < 0)
        return fade_max;
    len = fade_len;
    if (len <= 0)
        return fade_max;
    if (len < cnt)
    {
        fade = len;
        cnt = len;
    }
    t     = (float)cnt / (float)len;
    range = (float)(int)(fade_max - fade_min);
    if (mode == 2)
        return (int)(range * ((t + t) - 1.0f) + (float)fade_min + 0.5f);  /* data 0x37122c, 0x371230 */
    return (int)(range * t + (float)fade_min + 0.5f);                      /* data 0x371230 */
}

// ──────────────────────────────────────────────────────────────────────
// Ends a running fade: the incoming text is the shown one, the old slot is
// cleared.
// FF3 0x0020abc0

int MENU_MSG_OBJ::CutFade()
{
    if (fade < 0)
        return 0;
    fade = -1;
    cur ^= 1;
    ClearSlot(cur ^ 1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Fade to message (type, id, page): turns a running fade back when it is
// the shown one, nothing when it is the incoming one, else cuts a running
// fade and starts a new one with it in the free slot.
// FF3 0x0020ac08

void MENU_MSG_OBJ::SetMsg(int type, int id, int page)
{
    if (IsSlot(cur, type, id, page))
    {
        if (fade < 0)
            return;
        cur ^= 1;
        fade = fade_len - fade;
        return;
    }
    if (IsSlot(cur ^ 1, type, id, page))
        return;
    CutFade();
    ClearSlot(cur ^ 1);
    SetSlot(cur ^ 1, type, id, page);
    fade = fade_len;
}

// ──────────────────────────────────────────────────────────────────────
// Per frame when enabled: counts the fade down; at the end the incoming
// text is the shown one and the old slot is cleared. 1 while fading.
// FF3 0x0020ace8

int MENU_MSG_OBJ::Step()
{
    int cnt;

    if (enable == 0 || fade < 0)
        return 0;
    cnt = fade - 1;
    fade = cnt;
    if (cnt < 0)
    {
        cur ^= 1;
        ClearSlot(cur ^ 1);
    }
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// TITLE_MENU_OBJ: the title menu's item cross-fader
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020ad38 (TitleMenuObjCtorBase, no callers) is the base-object copy
// (its scale constant data 0x371234, also 1.0).
// FF3 0x0020adb8 (TitleMenuObjCtor; the scale constant is data 0x371238, 1.0).

TITLE_MENU_OBJ::TITLE_MENU_OBJ()
{
    int i;

    for (i = 0; i < 2; i++)
        ClearSlot((s_char)i);
    enable   = 0;
    fade_max = 0x80;
    len      = 8;
    cur      = 0;
    fade_min = 0;
    scale    = 1.0f;               /* data 0x371238 */
    cnt      = -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020ae38 (TitleMenuObjDtorBase, no callers) is the base-object
// copy. FF3 0x0020ae88 (TitleMenuObjDtor).

TITLE_MENU_OBJ::~TITLE_MENU_OBJ()
{
    int i;

    for (i = 0; i < 2; i++)
        ClearSlot((s_char)i);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020aed8

void TITLE_MENU_OBJ::ClearSlot(int slot)
{
    this->slot[(s_char)slot] = -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020aef8

void TITLE_MENU_OBJ::SetSlot(int slot, int item)
{
    this->slot[(s_char)slot] = item;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020af10

int TITLE_MENU_OBJ::IsSlot(int slot, int item)
{
    return this->slot[(s_char)slot] == item;
}

// ──────────────────────────────────────────────────────────────────────
// fade_max at rest; while fading fade_min..fade_max with cnt / len (cnt
// clamped to len), rounded.
// FF3 0x0020af30

int TITLE_MENU_OBJ::GetFade()
{
    int cnt = this->cnt;
    int len;

    if (cnt >= 0)
    {
        len = this->len;
        if (len > 0)
        {
            if (len < cnt)
            {
                this->cnt = len;
                cnt = len;
            }
            return (int)((float)(int)(fade_max - fade_min) * ((float)cnt / (float)len)
                         + (float)fade_min + 0.5f);               /* data 0x37123c */
        }
    }
    return fade_max;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020afc0

void TITLE_MENU_OBJ::Reset()
{
    cur = 0;
    cnt = -1;
    ClearSlot(0);
    ClearSlot(1);
}

// ──────────────────────────────────────────────────────────────────────
// Ends a running fade: the incoming item is the shown one, the old slot is
// cleared.
// FF3 0x0020b000

int TITLE_MENU_OBJ::CutFade()
{
    int ran = cnt >= 0;
    int old;

    if (ran)
    {
        old = cur;
        cnt = -1;
        cur = (s_char)(old ^ 1);
        ClearSlot(old);
    }
    return ran;
}

// ──────────────────────────────────────────────────────────────────────
// Fade to `item`: turns a running fade back when it is the shown one,
// nothing when it is the incoming one, else cuts a running fade and starts
// a new one with it in the free slot.
// FF3 0x0020b048

void TITLE_MENU_OBJ::SetItem(int item)
{
    int cnt;

    if (!IsSlot(cur, item))
    {
        if (IsSlot(cur ^ 1, item))
            return;
        CutFade();
        ClearSlot(cur ^ 1);
        SetSlot(cur ^ 1, item);
        cnt = len;
    }
    else
    {
        if (this->cnt < 0)
            return;
        cnt = len - this->cnt;
        cur ^= 1;
    }
    this->cnt = cnt;
}

// ──────────────────────────────────────────────────────────────────────
// Per frame when enabled: counts the fade down; at the end the incoming
// item is the shown one and the old slot is cleared. 1 while fading.
// FF3 0x0020b0f8

int TITLE_MENU_OBJ::Step()
{
    int cnt;
    int old;

    if (enable == 0 || this->cnt < 0)
        return 0;
    cnt = this->cnt - 1;
    this->cnt = cnt;
    if (cnt < 0)
    {
        old = cur;
        cur = (s_char)(old ^ 1);
        ClearSlot(old);
    }
    return 1;
}
