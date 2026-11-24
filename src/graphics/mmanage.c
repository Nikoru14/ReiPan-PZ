/* ==========================================================================
 *  graphics/mmanage.c
 *
 *  FF3 0x20f388..0x210818: the model collision registry, ModelCcs (chain
 *  physics), MdlPk4GetPart and RTTI ModelManager. FF3-only C++; the FF2 file
 *  of the same name in the link order has none of it. ModelManager's empty
 *  virtual slots are inline code FF3 emits at 0x2bd7b0..0x2bd838. Written
 *  from the FF3 disassembly (the VU0 macro code: xyz unless noted).
 *
 *  FF3 compiles each constructor and destructor twice (complete and
 *  base-object, same body) plus a deleting destructor; one C++ definition
 *  stands for each set.
 * ======================================================================== */
#include "graphics/mmanage.h"

#include <float.h>                      // FLT_MAX
#include <math.h>                       // sqrtf, isfinite
#include <string.h>                     // memcpy, strcmp

#include "common/heapctrl.h"            // heapCtrlMalloc / heapCtrlFree
#include "common/pk4.h"                 // PK4_HEADER, Pk4GetName / Pk4GetData
#include "graphics/graph3d/sgd_types.h" // SGDCOORDINATE, SgdPtr
#include "graphics/model_builder.h"     // ModelCollision, MDL_CLD_BODY
#include "graphics/motion/motion.h"     // ModelObject
#include "ingame/ingame.h"              // draw_en_37127c
#include "reipan/os/reipan_rand.h"      // ReiPan_Rand, REIPAN_RAND_MAXF
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP

// data 0x32d6c0, built before main by the file's static initializer
// (0x20f648 / _GLOBAL__I_ 0x20f6c0 / _GLOBAL__D_ 0x20f6e0).
ModelCollisionSet model_collision_set;

// The names of the model PK4 members (table data 0x32d6e0 -> sdata 0x3712c0).
static const char *const mdl_pk4_part_name[] = {
    "mpk", "tpk", "mlt", "ccs", "cld", "cls", "mono", "flgs",
};

// The model space -> world matrix flip (data 0x329400): y and z negated.
alignas(16) sceVu0FMATRIX mdl_flip_yz = {
    { 1.0f,  0.0f,  0.0f, 0.0f },
    { 0.0f, -1.0f,  0.0f, 0.0f },
    { 0.0f,  0.0f, -1.0f, 0.0f },
    { 0.0f,  0.0f,  0.0f, 1.0f },
};

// ModelCcs constants (sdata 0x371280..0x3712b8).
#define CCS_LINK_STIFF      0.5f        // share of a link's stretch each end takes back
#define CCS_UP_SWITCH       0.71f       // link x above this: the other up vector
#define CCS_UP_ALT_Y        (-1.0f)     // that vector: (0, -1, 0, 0)
#define CCS_HANG_X          0.04f       // wind kind 4: the last point off the first
#define CCS_HANG_Y          12.0f
#define CCS_WIND_CNT_RANGE  120.0f      // SetWind: the phase, 0..120
#define CCS_WIND_PERIOD     20.0f       // SetWind: the period, 110..130 frames
#define CCS_WIND_PERIOD_MIN 110.0f

// ══════════════════════════════════════════════════════════════════════
// Vector helpers (VU0 macro code in FF3)
// ══════════════════════════════════════════════════════════════════════

// vmul.xyz / vadday.x / vmaddz.x vd, vf1, vd (vf1.x = 1): (x*x + y*y) + z*z.
static inline float CcsLength(const float *v)
{
    return sqrtf((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
}

// PC: 1 / x as the PS2 does it.  Its FPU and VU have no infinity or NaN: a
// division by zero gives the largest float, which times the zero vector that
// had the zero length gives zero again.  IEEE gives infinity, and infinity
// times zero is NaN -- a NaN that then never leaves the chain, so a hair point
// that once lands exactly on another (or on a collision capsule's axis) took
// that piece of Rei's hair off for good.
static inline float CcsRecip(float x)
{
    return x > 0.0f ? 1.0f / x : FLT_MAX;
}

// vopmula.xyz / vopmsub.xyz: out.xyz = a x b, out.w = a.w.
static inline void CcsCross(float *out, const float *a, const float *b)
{
    float x = a[1] * b[2] - b[1] * a[2];
    float y = a[2] * b[0] - b[2] * a[0];
    float z = a[0] * b[1] - b[0] * a[1];

    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = a[3];
}

// rand() / RAND_MAX as FF3 spells it (lit 0x3712a4 / 0x3712ac).
static inline float CcsRand01(void)
{
    return (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
}

// ══════════════════════════════════════════════════════════════════════
// ModelCollisionSet
// ══════════════════════════════════════════════════════════════════════

// FF3 0x0020f3a0 (complete) and FF3 0x0020f388 (base object).
ModelCollisionSet::ModelCollisionSet()
{
    Init();
}

// FF3 0x0020f3d0 (complete) and FF3 0x0020f3b8 (base object).
ModelCollisionSet::~ModelCollisionSet()
{
    Term();
}

// FF3 0x0020f3e8
void ModelCollisionSet::Init()
{
    int i;

    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
        slot[i] = nullptr;
}

// FF3 0x0020f410
void ModelCollisionSet::Term()
{
    int i;

    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
        slot[i] = nullptr;
}

// FF3 0x0020f438
int ModelCollisionSet::FindFree()
{
    int i;

    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
    {
        if (slot[i] == nullptr)
            return i;
    }
    return -1;
}

// FF3 0x0020f468
int ModelCollisionSet::Find(ModelCollision *cld)
{
    int i;

    if (cld == nullptr)
        return -1;
    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
    {
        if (slot[i] == cld)
            return i;
    }
    return -1;
}

// FF3 0x0020f4a0
int ModelCollisionSet::Add(ModelCollision *cld)
{
    int i;

    if (cld == nullptr || Find(cld) >= 0)
        return 0;
    i = FindFree();
    if (i < 0)
        return 0;
    slot[i] = cld;
    return 1;
}

// FF3 0x0020f510
int ModelCollisionSet::Remove(ModelCollision *cld)
{
    int i;

    if (cld == nullptr)
        return 0;
    i = Find(cld);
    if (i < 0)
        return 0;
    slot[i] = nullptr;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Every set gets its test (no early out).
// FF3 0x0020f560

int ModelCollisionSet::HitCheck(MDL_CLD_BODY *body, int clear)
{
    int i;
    int hit = 0;

    if (body == nullptr)
        return 0;
    if (clear != 0)
        ClearHit();
    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
    {
        if (slot[i] != nullptr && slot[i]->HitCheck(body) != 0)
            hit = 1;
    }
    return hit;
}

// FF3 0x0020f5f8
void ModelCollisionSet::ClearHit()
{
    int i;

    for (i = 0; i < MODEL_COLLISION_SET_NUM; i++)
    {
        if (slot[i] != nullptr)
            slot[i]->ClearHit();
    }
}

// ══════════════════════════════════════════════════════════════════════
// ModelCcs
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// A point per ccs point and a link between each two in a row, then the
// rest state (the first Update resets again).
// FF3 0x0020f7b8 (complete) and FF3 0x0020f700 (base object).

ModelCcs::ModelCcs(u_int coord, void *ccs)
{
    short n;

    data = (MDL_CCS_HEAD *)ccs;
    n    = (short)data->num;
    this->coord = coord;
    point = (MDL_CLD_BODY *)heapCtrlMalloc(GetSystemHeapWrkP(), n * sizeof(MDL_CLD_BODY), 0, 0, 0);
    link  = (MDL_CCS_LINK *)heapCtrlMalloc(GetSystemHeapWrkP(), (short)(n - 1) * sizeof(MDL_CCS_LINK), 0, 0, 0);
    point_num = n;
    link_num  = (short)(n - 1);
    reset = 1;
    stop  = 0;
    wind  = 0;
    Reset();
}

// FF3 0x0020f8c8 (complete) and FF3 0x0020f870 (base object).
ModelCcs::~ModelCcs()
{
    heapCtrlFree(GetSystemHeapWrkP(), point, 0, 0, 0);
    heapCtrlFree(GetSystemHeapWrkP(), link, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Verlet-style step: the velocity gets the force, the position the velocity;
// each link takes back half its stretch at either end; free points are
// pushed out of the registered collisions (in coordinate 0's translation);
// pinned points go back to rest; the velocity becomes the damped motion of
// the step. Then the SGD coordinates.
// FF3 0x0020f920

void ModelCcs::Update()
{
    int            i;
    float          d[4];
    float          len;
    float          k;
    MDL_CLD_BODY  *p;
    MDL_CLD_BODY  *a;
    MDL_CLD_BODY  *b;
    SGDCOORDINATE *c;
    static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // PC: no coordinates

    if (reset != 0)
    {
        Reset();
        reset = 0;
    }
    if (stop != 0)
        return;

    CalcWind();

    for (i = 0; i < point_num; i++)
    {
        p = &point[i];
        p->move[0] += p->force[0];
        p->move[1] += p->force[1];
        p->move[2] += p->force[2];
        p->pos[0] += p->move[0];
        p->pos[1] += p->move[1];
        p->pos[2] += p->move[2];
    }

    for (i = 0; i < link_num; i++)
    {
        a = &point[link[i].a];
        b = &point[link[i].b];
        d[0] = a->pos[0] - b->pos[0];
        d[1] = a->pos[1] - b->pos[1];
        d[2] = a->pos[2] - b->pos[2];
        len = CcsLength(d);
        k = ((link[i].len - len) * CCS_LINK_STIFF) * CcsRecip(len);    // PC: PS2 division
        d[0] *= k;
        d[1] *= k;
        d[2] *= k;
        a->pos[0] += d[0];
        a->pos[1] += d[1];
        a->pos[2] += d[2];
        b->pos[0] -= d[0];
        b->pos[1] -= d[1];
        b->pos[2] -= d[2];
    }

    if (draw_en_37127c != 0)
    {
        for (i = 0; i < point_num; i++)
        {
            const float *t;

            if (data->point[i].pinned != 0)
                continue;
            p = &point[i];
            c = SgdPtr<SGDCOORDINATE>(coord);
            t = (c != nullptr) ? c->matCoord[3] : zero;
            p->pos[0] += t[0];
            p->pos[1] += t[1];
            p->pos[2] += t[2];
            model_collision_set.HitCheck(p, 0);
            c = SgdPtr<SGDCOORDINATE>(coord);
            t = (c != nullptr) ? c->matCoord[3] : zero;
            p->pos[0] -= t[0];
            p->pos[1] -= t[1];
            p->pos[2] -= t[2];
        }
    }

    for (i = 0; i < point_num; i++)
    {
        if (data->point[i].pinned != 0)
        {
            point[i].pos[0] = data->point[i].pos[0];
            point[i].pos[1] = data->point[i].pos[1];
            point[i].pos[2] = data->point[i].pos[2];
        }
    }

    // vsub.xyzw / vmulx.xyzw: all four components.
    for (i = 0; i < point_num; i++)
    {
        p = &point[i];
        p->move[0] = (p->pos[0] - p->prev[0]) * data->damp;
        p->move[1] = (p->pos[1] - p->prev[1]) * data->damp;
        p->move[2] = (p->pos[2] - p->prev[2]) * data->damp;
        p->move[3] = (p->pos[3] - p->prev[3]) * data->damp;
    }

    for (i = 0; i < point_num; i++)
        memcpy(point[i].prev, point[i].pos, sizeof(point[i].prev));

    // PC: a chain that still went non-finite (through a coordinate or a
    // collision shape rather than its own divisions) starts over at rest
    // rather than staying broken; the PS2 cannot get here.
    for (i = 0; i < point_num; i++)
    {
        if (!isfinite(point[i].pos[0]) || !isfinite(point[i].pos[1]) ||
            !isfinite(point[i].pos[2]))
        {
            Reset();
            break;
        }
    }

    SetCoordinate();
}

// ──────────────────────────────────────────────────────────────────────
// Link i sets coordinate i + 1's matLocalWorld: row 0 the link's direction,
// rows 1 / 2 from cross products with an up vector ((1, 0, 0) unless the
// link points that way), row 3 coordinate 0's translation plus point i.
// FF3 leaves w = 1 in rows 0 and 2 (it normalizes the point difference with
// w = 1 and crosses from row 0); the PC clears it (see below). Then every coordinate is marked for
// recalculation.
// FF3 0x0020fd08

void ModelCcs::SetCoordinate()
{
    int            i;
    sceVu0FMATRIX  m;
    float          up[4];
    float          s;
    SGDCOORDINATE *c = SgdPtr<SGDCOORDINATE>(coord);

    if (c == nullptr)                   // PC: FF3 writes through a null coordinate pointer
        return;

    for (i = 0; i < link_num; i++)
    {
        sceVu0UnitMatrix(m);

        // vrsqrt / vmul.w / vmulq.xyz: (d.xyz / |d|, 1)
        m[0][0] = point[i + 1].pos[0] - point[i].pos[0];
        m[0][1] = point[i + 1].pos[1] - point[i].pos[1];
        m[0][2] = point[i + 1].pos[2] - point[i].pos[2];
        s = CcsRecip(CcsLength(m[0]));                    // PC: PS2 division
        m[0][0] *= s;
        m[0][1] *= s;
        m[0][2] *= s;
        m[0][3] = 1.0f;

        if (m[0][0] < CCS_UP_SWITCH)
        {
            up[0] = 1.0f;               // data 0x329440
            up[1] = 0.0f;
        }
        else
        {
            up[0] = 0.0f;
            up[1] = CCS_UP_ALT_Y;
        }
        up[2] = 0.0f;
        up[3] = 0.0f;
        CcsCross(m[1], up, m[0]);
        CcsCross(m[2], m[0], m[1]);

        // PC: the ROM leaves w = 1 in rows 0 and 2 (the normalised point
        // difference's w, carried into row 2 by the cross product).  The
        // VU1 never reads that column, but the host's renderer takes
        // matLocalWorld as a full 4x4 into the model-view-projection, where
        // those 1s land in the clip w: the chain's meshes -- the paper
        // streamers (shide) on the rope in the Stained Corridor -- exploded
        // into spikes across the screen.  An affine matrix's w column is
        // (0, 0, 0, 1).
        m[0][3] = 0.0f;
        m[1][3] = 0.0f;
        m[2][3] = 0.0f;

        m[3][0] = (0.0f + c[0].matLocalWorld[3][0]) + point[i].pos[0];
        m[3][1] = (0.0f + c[0].matLocalWorld[3][1]) + point[i].pos[1];
        m[3][2] = (0.0f + c[0].matLocalWorld[3][2]) + point[i].pos[2];
        m[3][3] = 1.0f;

        memcpy(c[i + 1].matLocalWorld, m, sizeof(m));
    }

    for (i = 0; i < link_num + 1; i++)
        c[i].bCalc = 1;
}

// ──────────────────────────────────────────────────────────────────────
// Points at rest (still, at their data positions) and links i -> i + 1 at
// their rest lengths.
// FF3 0x0020ffe0

void ModelCcs::Reset()
{
    int            i;
    float          d[4];
    MDL_CLD_BODY  *p;
    MDL_CCS_POINT *e;

    for (i = 0; i < point_num; i++)
    {
        p = &point[i];
        e = &data->point[i];
        memcpy(p->pos, e->pos, sizeof(p->pos));
        memcpy(p->prev, e->pos, sizeof(p->prev));
        p->move[0] = 0.0f;              // data 0x329500 (0, 0, 0, 1)
        p->move[1] = 0.0f;
        p->move[2] = 0.0f;
        p->move[3] = 1.0f;
        memcpy(p->force, e->force, sizeof(p->force));
        p->off  = e->pinned;
        p->mask = e->mask;
        p->r    = e->r;
    }

    for (i = 0; i < link_num; i++)
    {
        link[i].a = (short)i;
        link[i].b = (short)(i + 1);
        d[0] = point[i].pos[0] - point[i + 1].pos[0];
        d[1] = point[i].pos[1] - point[i + 1].pos[1];
        d[2] = point[i].pos[2] - point[i + 1].pos[2];
        link[i].len = CcsLength(d);
    }
}

// ──────────────────────────────────────────────────────────────────────
// While the wind is on. Kind 4 hangs the last point off the first. The
// others push every point along the wind direction during the first third
// of each period; kind 1 draws a new wind (phase, period, direction) every
// frame.
// FF3 0x00210158

void ModelCcs::CalcWind()
{
    int   i;
    float push[4];

    if (wind == 0)
        return;

    if (wind_kind == MDL_CCS_WIND_HANG)
    {
        MDL_CLD_BODY *last = &point[point_num - 1];

        memcpy(last->pos, point[0].pos, sizeof(last->pos));
        last->pos[0] = last->pos[0] + CCS_HANG_X;
        last->pos[1] = last->pos[1] - CCS_HANG_Y;
        return;
    }

    if (wind_kind == 1)
        SetWind(1);

    wind_cnt = wind_cnt + 1;
    if (wind_cnt % wind_period < wind_period / 3)
    {
        push[0] = wind_dir[0] * wind_power;
        push[1] = wind_dir[1] * wind_power;
        push[2] = wind_dir[2] * wind_power;
        push[3] = wind_dir[3] * wind_power;
        for (i = 0; i < point_num; i++)
        {
            point[i].move[0] += push[0];
            point[i].move[1] += push[1];
            point[i].move[2] += push[2];
        }
    }
    if (wind_period < wind_cnt)
        wind_cnt = 0;
}

// FF3 0x002102a0 (sdata 0x371294..0x3712a0)
float ModelCcs::GetWindPower()
{
    switch (wind_kind)
    {
    case 0:  return 0.01f;
    case 1:  return 0.021f;
    case 2:  return 0.02f;
    case 3:  return 0.03f;
    default: return 0.0f;
    }
}

// ──────────────────────────────────────────────────────────────────────
// x and z each 2 * rand / RAND_MAX - 1, y 0, normalized.
// FF3 0x00210318
// callcheck: rand (CcsRand01: ReiPan_Rand)

void ModelCcs::SetWindDir()
{
    float r;
    float s;

    r = CcsRand01();
    wind_dir[0] = r + r + -1.0f;
    wind_dir[1] = 0.0f;
    r = CcsRand01();
    wind_dir[3] = 0.0f;
    wind_dir[2] = r + r + -1.0f;

    // vrsqrt / vmul.w / vmulq.xyz: w = 0 * 1
    s = CcsRecip(CcsLength(wind_dir));                // PC: PS2 division
    wind_dir[0] *= s;
    wind_dir[1] *= s;
    wind_dir[2] *= s;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002103e0 (MapObjSetWindCloth: the furniture row's strength)
// callcheck: rand (CcsRand01: ReiPan_Rand)

void ModelCcs::SetWind(int kind)
{
    wind_kind = kind;
    wind      = 1;
    wind_cnt    = (u_int)(int)(CcsRand01() * CCS_WIND_CNT_RANGE + 0.0f);
    wind_period = (u_int)(int)(CcsRand01() * CCS_WIND_PERIOD + CCS_WIND_PERIOD_MIN);
    wind_power  = GetWindPower();
    SetWindDir();
}

// FF3 0x002104b8 (MapObjSetWindCloth: strength 5)
void ModelCcs::StopWind()
{
    wind = 0;
}

// ══════════════════════════════════════════════════════════════════════
// Model PK4 members
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002104c0

void *MdlPk4GetPart(void *const *pk4, int part)
{
    int   i;
    char *name;

    if (*pk4 == nullptr)
        return nullptr;
    for (i = 0; i < ((*pk4 != nullptr) ? ((PK4_HEADER *)*pk4)->num : 0); i++)
    {
        name = Pk4GetName(*pk4, i);
        if (name != nullptr && strcmp(name, mdl_pk4_part_name[part]) == 0)
            return Pk4GetData(*pk4, i);
    }
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════
// ModelManager
// ══════════════════════════════════════════════════════════════════════

// FF3 0x002105c0 (complete) and FF3 0x00210578 (base object).
ModelManager::ModelManager(ModelObject *obj, ModelCollision *collision)
{
    ani_ctrl = obj;
    this->collision = collision;
    if (collision != nullptr)
        model_collision_set.Add(collision);
}

// FF3 0x00210640 (deleting 0x00210678; base object 0x00210608).
ModelManager::~ModelManager()
{
    if (collision != nullptr)
        model_collision_set.Remove(collision);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00210768 (vtable +0x08)

void ModelManager::SetMatrix(float *matrix)
{
    sceVu0FMATRIX m;

    memcpy(m, matrix, sizeof(m));       // PC: the caller's matrix may not be 16-byte aligned
    sceVu0MulMatrix(m, m, mdl_flip_yz);
    ani_ctrl->SetMatrix(&m[0][0]);
    ani_ctrl->CalcBoneCoordinate();
}

// The empty slots (FF3 inline code 0x2bd7b0..0x2bd838; MotionModelManager
// overrides them).

// FF3 0x002bd7b0
void ModelManager::PlayMotion(int, int, float, float) {}
// FF3 0x002bd7b8
void ModelManager::PlayShape(int, int, int, int, float, float) {}
// FF3 0x002bd7c0
void ModelManager::PlayCloth(int, int, float, float) {}
// FF3 0x002bd7c8
void ModelManager::ResetCloth(int, int) {}
// FF3 0x002bd7d0
void ModelManager::SetClothWind(int, const float *, int, float) {}
// FF3 0x002bd7d8
void ModelManager::StopClothWind(int) {}
// FF3 0x002bd7e0
void ModelManager::SetSpeed(float) {}
// FF3 0x002bd7e8
void ModelManager::SetFrame(float) {}
// FF3 0x002bd7f0
void ModelManager::StopShape(int) {}
// FF3 0x002bd7f8
void ModelManager::StopShapeAll(void) {}
// FF3 0x002bd800
void ModelManager::WriteShapeAll(void) {}
// FF3 0x002bd808
int ModelManager::IsMotionEnd(void) { return 0; }
// FF3 0x002bd810
int ModelManager::IsShapeEnd(int) { return 0; }
// FF3 0x002bd818
MOT_CTRL *ModelManager::GetBoneMotion(void) { return nullptr; }
// FF3 0x002bd820
ModelCcs *ModelManager::GetCcs(void) { return nullptr; }
// FF3 0x002bd828
int ModelManager::GetShapeNum(void) { return 0; }
// FF3 0x002bd830
void ModelManager::SetShapeLoop(int, int) {}
// FF3 0x002bd838
ModelCollision *ModelManager::GetCollision(void) { return collision; }

// ──────────────────────────────────────────────────────────────────────
// The forwarders: FF3 loads this->obj and jumps to its member.

// FF3 0x002106c0
void ModelManager::Draw() { ani_ctrl->Draw(); }
// FF3 0x002106d8
void ModelManager::DrawAlpha() { ani_ctrl->DrawAlpha(); }
// FF3 0x002106f0
void ModelManager::ExecPrelight(float *trans, float *rot) { ani_ctrl->ExecPrelight(trans, rot); }
// FF3 0x00210708
void ModelManager::ExecPrelightMatrix(float *matrix) { ani_ctrl->ExecPrelightMatrix(matrix); }
// FF3 0x00210720
void ModelManager::ClearPrelight() { ani_ctrl->ClearPrelight(); }
// FF3 0x00210738
void ModelManager::DrawShadow(G3DLIGHT *light) { ani_ctrl->DrawShadow(light); }
// FF3 0x00210750
void ModelManager::ReqDraw() { ani_ctrl->ReqDraw(); }
