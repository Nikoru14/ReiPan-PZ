/* ==========================================================================
 *  graphics/model_builder.c
 *
 *  FF3's model builders (RTTI ModelBuilder / NormalModelBuilder /
 *  MotionModelBuilder; FF3-only C++, no FF2 counterpart) and the director
 *  that runs them, plus the model's collision shapes (ModelCollision) that
 *  NormalModelBuilder makes from the cld member. FF3 0x20df28..0x20f388, with
 *  ModelBuilder's inline code (0x2bd118..0x2bd158, 0x2ac730 / 0x2ac738) and
 *  the shape tests (0x2bd160..0x2bd7b0). Written from the FF3 decompilation.
 *
 *  FF3 compiles each constructor and destructor twice (complete and
 *  base-object, same body) plus a deleting destructor; one C++ definition
 *  stands for each set.
 * ======================================================================== */
#include "graphics/model_builder.h"
#include "common/vu0math.h"          // PC: Vu0Rsqrt / Ps2Divf

#include <float.h>                      // FLT_MAX
#include <math.h>                       // sqrtf
#include <string.h>                     // memset
#include <new>                          // placement new on FF3's C++ heap (operator_new)

#include "common/cpp_heap.h"            // operator_new / operator_delete(_array)
#include "common/heapctrl.h"            // heapCtrlMalloc / heapCtrlFree
#include "common/pk4.h"                 // PK4_HEADER, Pk4GetData
#include "graphics/graph3d/sgd_types.h" // SGDCOORDINATE, SGDFILEHEADER, SgdPtr
#include "graphics/mmanage.h"           // MdlPk4GetPart, ModelCcs
#include "graphics/motion_model_manager.h"  // MotionModelManager
#include "graphics/motion/motion.h"     // ModelObject and co., the motion classes
#include "ingame/ingame.h"              // draw_en_37126c
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP

// A plane's normal is stored with y and z negated (data 0x329400).
alignas(16) static const sceVu0FMATRIX cld_flip_yz = {
    { 1.0f,  0.0f,  0.0f, 0.0f },
    { 0.0f, -1.0f,  0.0f, 0.0f },
    { 0.0f,  0.0f, -1.0f, 0.0f },
    { 0.0f,  0.0f,  0.0f, 1.0f },
};

// A shape's push below this does not push back along the move (sdata
// 0x371274 / 0x371278).
#define CLD_PUSH_MIN 1e-5f

// The speed the builder starts a ShpPhysicsMotion with (sdata 0x371268).
#define MODEL_BUILDER_SHP_SPEED 1.0f

// ══════════════════════════════════════════════════════════════════════
// Vector helpers (VU0 macro code in FF3, xyz only)
// ══════════════════════════════════════════════════════════════════════

// vmul.xyz / vadday.x / vmaddz.x: (x + y) + z.
static inline float CldDot(const float *a, const float *b)
{
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}

// out.xyz = a - b, out.w = 0 (FF3 stores 0 into w).
static inline void CldSub(float *out, const float *a, const float *b)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
    out[3] = 0.0f;
}

// v.xyz *= s.
static inline void CldScale(float *v, float s)
{
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
}

// vrsqrt / vmulq.xyz: v.xyz / |v|.  PC: a zero vector stays zero, as on the
// PS2, whose VU gives the largest float for 1 / 0 (and zero times it is zero);
// IEEE's infinity times zero is NaN, which a hair point then never sheds
// (mmanage.c's CcsRecip()).
static inline void CldNormalize(float *v)
{
    const float len2 = CldDot(v, v);
    CldScale(v, len2 > 0.0f ? 1.0f / sqrtf(len2) : FLT_MAX);
}

// The end of every hit: with a push, d also gets the body's move times
// -push; then body.pos.xyz += d.xyz.
static int CldPushBody(MDL_CLD_BODY *body, float *d, float push)
{
    if (CLD_PUSH_MIN < push)
    {
        d[0] += body->move[0] * -push;
        d[1] += body->move[1] * -push;
        d[2] += body->move[2] * -push;
    }
    body->pos[0] += d[0];
    body->pos[1] += d[1];
    body->pos[2] += d[2];
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// ModelCollision
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// One shape into the heap copy: both points get w = 1 (sdata 0x371270), a
// plane's normal goes through cld_flip_yz.
// FF3 0x0020ee70

static void ModelCollisionCopyShape(MDL_CLD_SHAPE *dst, MDL_CLD_SHAPE *src)
{
    *dst = *src;
    dst->p[0][3] = 1.0f;
    dst->p[1][3] = 1.0f;

    switch (src->type)
    {
    case 2:
        sceVu0ApplyMatrix(dst->p[0], cld_flip_yz, dst->p[0]);
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Heap copies of the cld shapes and room for their world points.
// FF3 0x0020efd8 (the builder's) and FF3 0x0020eef0 (identical, no callers).

ModelCollision::ModelCollision(u_int coord, MDL_CLD_HEAD *cld)
{
    int i;

    num  = cld->num;
    mask = cld->mask;
    shape = (MDL_CLD_SHAPE *)heapCtrlMalloc(GetSystemHeapWrkP(), num * sizeof(MDL_CLD_SHAPE), 0, 0, 0);
    this->coord = coord;
    world = (MDL_CLD_WORLD *)heapCtrlMalloc(GetSystemHeapWrkP(), num * sizeof(MDL_CLD_WORLD), 0, 0, 0);

    for (i = 0; i < num; i++)
        ModelCollisionCopyShape(&shape[i], &cld->shape[i]);

    ClearHit();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020f140 (the builder's) and FF3 0x0020f0c0 (identical, no callers).

ModelCollision::~ModelCollision()
{
    if (shape != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), shape, 0, 0, 0);
    if (world != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), world, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Pushes body out of the shapes while collision is on (sdata 0x37126c) and
// the masks share a bit; 1 at the first shape that pushed it.
// FF3 0x0020f1c0

int ModelCollision::HitCheck(MDL_CLD_BODY *body)
{
    int i;

    if (draw_en_37126c != 0 && body != nullptr && shape != nullptr && (mask & body->mask) != 0)
    {
        for (i = 0; i < num; i++)
        {
            if (HitShape(body, i) != 0)
                return 1;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0020f270

void ModelCollision::ClearHit()
{
    int i;

    for (i = 0; i < num; i++)
        world[i].hit = 0;
}

// ──────────────────────────────────────────────────────────────────────
// The shape's two points through their SGD coordinates' local-to-world
// matrices; without coordinates FF3 adds the local points to what world
// already holds.
// FF3 0x0020f2b0

void ModelCollision::CalcWorld(MDL_CLD_SHAPE *shape, MDL_CLD_WORLD *world)
{
    SGDCOORDINATE *c;

    if (shape == nullptr || world == nullptr)
        return;

    if (coord != 0)
    {
        c = SgdPtr<SGDCOORDINATE>(coord);   // PC: an EE address
        sceVu0ApplyMatrix(world->p[0], c[shape->coord[0]].matLocalWorld, shape->p[0]);
        sceVu0ApplyMatrix(world->p[1], c[shape->coord[1]].matLocalWorld, shape->p[1]);
        return;
    }
    sceVu0AddVector(world->p[0], world->p[0], shape->p[0]);
    sceVu0AddVector(world->p[1], world->p[1], shape->p[1]);
}

// ──────────────────────────────────────────────────────────────────────
// Capsule a..b of radius size: past either end the body is pushed off that
// end point, in between off the axis. FF3 adds the body's radius a second
// time for the two ends (not in between); kept.
// FF3 0x002bd3a8 (f12 size, f13 push)

static int ModelCollisionHitCapsule(MDL_CLD_BODY *body, float *a, float *b, float size, float push)
{
    alignas(16) sceVu0FVECTOR v0;
    alignas(16) sceVu0FVECTOR v1;
    alignas(16) sceVu0FVECTOR d;
    alignas(16) sceVu0FVECTOR axis;
    float r;
    float len;
    float t;

    r = size + body->r;

    // past a
    CldSub(v0, body->pos, a);
    CldSub(v1, a, b);
    if (0.0f < CldDot(v0, v1))
    {
        r += body->r;
        CldSub(d, body->pos, a);
        len = sqrtf(CldDot(d, d));
        if (!(len < r))
            return 0;
        CldNormalize(d);
        CldScale(d, r - len);
        return CldPushBody(body, d, push);
    }

    // past b
    CldSub(v0, b, a);
    CldSub(v1, body->pos, b);
    if (0.0f < CldDot(v0, v1))
    {
        r += body->r;
        CldSub(d, body->pos, b);
        len = sqrtf(CldDot(d, d));
        if (!(len < r))
            return 0;
        CldNormalize(d);
        CldScale(d, r - len);
        return CldPushBody(body, d, push);
    }

    // in between: d = the foot on the axis - pos
    CldSub(v0, body->pos, a);
    CldSub(v1, b, a);
    axis[0] = v1[0];
    axis[1] = v1[1];
    axis[2] = v1[2];
    axis[3] = 0.0f;
    CldNormalize(axis);
    len = CldDot(v1, v1);
    t   = Ps2Divf(CldDot(axis, v0), sqrtf(len));    // PC: PS2 division (a point capsule)
    d[0] = v1[0] * t + v0[0] * -1.0f;
    d[1] = v1[1] * t + v0[1] * -1.0f;
    d[2] = v1[2] * t + v0[2] * -1.0f;
    d[3] = 0.0f;
    len = sqrtf(CldDot(d, d));
    if (!(len < r))
        return 0;
    CldNormalize(d);
    CldScale(d, -(r - len));
    return CldPushBody(body, d, push);
}

// ──────────────────────────────────────────────────────────────────────
// Shape no against body (skipped while body->off is set): its world points
// first, then the sphere / capsule / plane test; 1 when body was pushed.
// FF3 0x002bd160

int ModelCollision::HitShape(MDL_CLD_BODY *body, int no)
{
    MDL_CLD_SHAPE *s;
    MDL_CLD_WORLD *w;
    alignas(16) sceVu0FVECTOR d;
    float r;
    float len;
    float dist;

    s = &shape[no];
    w = &world[no];
    if (body->off != 0)
        return 0;

    CalcWorld(s, w);
    switch (s->type)
    {
    case 0:                             // sphere at p[0]
        r = s->size + body->r;
        CldSub(d, body->pos, w->p[0]);
        len = sqrtf(CldDot(d, d));
        if (!(len < r))
            return 0;
        CldNormalize(d);
        CldScale(d, r - len);
        return CldPushBody(body, d, s->push);

    case 1:                             // capsule p[0]..p[1]
        return ModelCollisionHitCapsule(body, w->p[0], w->p[1], s->size, s->push);

    case 2:                             // plane: normal s->p[0] through w->p[1]
        dist = CldDot(body->pos, s->p[0]) + (body->r - CldDot(w->p[1], s->p[0]));
        if (!(dist < 0.0f))
            return 0;
        // FF3 moves body before the push-back here (CldPushBody adds both at once)
        body->pos[0] += s->p[0][0] * -dist;
        body->pos[1] += s->p[0][1] * -dist;
        body->pos[2] += s->p[0][2] * -dist;
        if (CLD_PUSH_MIN < s->push)
        {
            body->pos[0] += body->move[0] * -s->push;
            body->pos[1] += body->move[1] * -s->push;
            body->pos[2] += body->move[2] * -s->push;
        }
        return 1;
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// ModelBuilder (inline code FF3 emitted at 0x2bd118.. / 0x2ac730)
// ══════════════════════════════════════════════════════════════════════

// FF3 0x002bd118 (deleting 0x002bd128): only the vptr.
ModelBuilder::~ModelBuilder()
{
}

// FF3 0x002bd148
void ModelBuilder::BuildObject()
{
}

// FF3 0x002ac730
void ModelBuilder::BuildTexture()
{
}

// FF3 0x002bd150
void ModelBuilder::BuildMotion()
{
}

// FF3 0x002bd158
MDL_MOT *ModelBuilder::GetResult()
{
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The builder's three steps in order.
// FF3 0x0020ee10
// callcheck: BuildObject, BuildTexture, BuildMotion (builder vtable +0x08 / +0x0c / +0x10)

void ModelDirector::Construct()
{
    builder->BuildObject();
    builder->BuildTexture();
    builder->BuildMotion();
}

// ══════════════════════════════════════════════════════════════════════
// NormalModelBuilder
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Takes the model PK4's members; nothing built yet.
// FF3 0x0020dfd0 (complete) and FF3 0x0020df28 (the base-object twin the
// derived constructors call).

NormalModelBuilder::NormalModelBuilder(void *const *pk4)
{
    result = nullptr;
    obj    = nullptr;
    mpk    = MdlPk4GetPart(pk4, MDL_PK4_MPK);
    flgs   = MdlPk4GetPart(pk4, MDL_PK4_FLGS);
    tpk    = MdlPk4GetPart(pk4, MDL_PK4_TPK);
    mlt    = MdlPk4GetPart(pk4, MDL_PK4_MLT);
    mono   = MdlPk4GetPart(pk4, MDL_PK4_MONO);
    cld    = (MDL_CLD_HEAD *)MdlPk4GetPart(pk4, MDL_PK4_CLD);
    collision = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// Deletes what it built: the result, the object, the collision shapes.
// FF3 0x0020e110 (vtable +0x00), FF3 0x0020e078 (base object), FF3 0x0020e1a8
// (deleting: then operator_delete).
// callcheck: ModelCollisionDtorBase (collision's destructor, explicit call)

NormalModelBuilder::~NormalModelBuilder()
{
    if (result != nullptr)
    {
        // FF3: result's vtable +4, its deleting destructor
        result->~MDL_MOT();
        operator_delete(result);
    }
    if (obj != nullptr)
    {
        // FF3: obj's vtable +0x10, its deleting destructor
        obj->~ModelObject();
        operator_delete(obj);
    }
    if (collision != nullptr)
    {
        collision->~ModelCollision();
        operator_delete(collision);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The object over the SGDs -- textured when the PK4 has a tpk member, with
// several texture sets when it also has an mlt member -- and the collision
// shapes on its first SGD's coordinates when it has a cld member.
// FF3 0x0020e240 (vtable +0x08).
// callcheck: ModelObjectCtor, ModelObjectCtorBase, MultiTextureModelObjectCtor, ModelCollisionCtorBase (new-expressions; the base-object one inside the inlined TextureModelObject constructor)

void NormalModelBuilder::BuildObject()
{
    if (tpk == nullptr)
        obj = new (operator_new(sizeof(ModelObject))) ModelObject(mpk, flgs);
    else if (mlt == nullptr)
        obj = new (operator_new(sizeof(TextureModelObject))) TextureModelObject(mpk, flgs, tpk, mono);
    else
        obj = new (operator_new(sizeof(MultiTextureModelObject))) MultiTextureModelObject(mpk, flgs, tpk, mono, mlt);

    if (cld != nullptr)
        collision = new (operator_new(sizeof(ModelCollision))) ModelCollision(SgdAddr(obj->GetCoord(0)), cld);
}

// ──────────────────────────────────────────────────────────────────────
// The result: a ModelManager over the object and the collision shapes.
// FF3 0x0020e368 (vtable +0x10).
// callcheck: operator_new, ModelManagerCtor (new-expression)

void NormalModelBuilder::BuildMotion()
{
    result = new (operator_new(sizeof(ModelManager))) ModelManager(obj, collision);
}

// FF3 0x002ac738 (vtable +0x14)
MDL_MOT *NormalModelBuilder::GetResult()
{
    return result;
}

// ══════════════════════════════════════════════════════════════════════
// MotionModelBuilder
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// NormalModelBuilder's members plus ccs and cls, the motion PK4 and the
// shape data; nothing built yet.
// FF3 0x0020e448 (complete) and FF3 0x0020e3b0 (base object).
// callcheck: NormalModelBuilderCtorBase (the base-class initializer)

MotionModelBuilder::MotionModelBuilder(void *const *pk4, void *mot, void *shp)
    : NormalModelBuilder(pk4)
{
    ccs  = MdlPk4GetPart(pk4, MDL_PK4_CCS);
    cls  = MdlPk4GetPart(pk4, MDL_PK4_CLS);
    this->mot = mot;
    this->shp = shp;
    cloth_data = nullptr;
    ccs_obj    = nullptr;
    bmd_mot    = nullptr;
    shp_mot    = nullptr;
    cloth_mot  = nullptr;
    bmd_data   = nullptr;
    shp_data   = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// Deletes the ccs object, the motions and their data, then
// NormalModelBuilder's part.
// FF3 0x0020e680 (vtable +0x00), FF3 0x0020e4e0 (base object), FF3 0x0020e820
// (deleting: then operator_delete).
// callcheck: ModelCcsDtor, NormalModelBuilderDtorBase (explicit destructor call; the base class destructor); vtable +4 deletes of the motions and data

MotionModelBuilder::~MotionModelBuilder()
{
    int i;

    if (ccs_obj != nullptr)
    {
        ccs_obj->~ModelCcs();
        operator_delete(ccs_obj);
    }
    if (bmd_mot != nullptr)
    {
        bmd_mot->~BmdMotion();          // FF3: vtable +4
        operator_delete(bmd_mot);
    }
    if (shp_mot != nullptr)
    {
        for (i = 0; i < shp_data->GetNum(); i++)
        {
            if (shp_mot[i] != nullptr)
            {
                shp_mot[i]->~ShpMotion();   // FF3: vtable +4
                operator_delete(shp_mot[i]);
            }
        }
        operator_delete_array(shp_mot);
    }
    if (cloth_mot != nullptr)
    {
        for (i = 0; i < cloth_data->GetNum(); i++)
        {
            if (cloth_mot[i] != nullptr)
            {
                cloth_mot[i]->~ClothMotion();   // FF3: vtable +4
                operator_delete(cloth_mot[i]);
            }
        }
        operator_delete_array(cloth_mot);
    }
    if (bmd_data != nullptr)
    {
        bmd_data->~BmdData();           // FF3: vtable +4
        operator_delete(bmd_data);
    }
    if (shp_data != nullptr)
    {
        shp_data->~ShpData();
        operator_delete(shp_data);
    }
    if (cloth_data != nullptr)
    {
        cloth_data->~ClothData();
        operator_delete(cloth_data);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The motions:
//  - ccs: the ccs object on the first SGD's coordinates;
//  - mot: the motion PK4's BMD whose bone count is the SGD's block count - 2
//    (else its first) as BmdData, and a BmdMotion over it;
//  - shp: a ShpMotion (type 0) or ShpPhysicsMotion (type 1, started at
//    once) per shape motion; motions of the same mesh share one vertex
//    buffer (the first makes it);
//  - cls: a ClothMotion per cloth.
// FF3 0x0020e9c8 (vtable +0x0c).
// callcheck: ModelCcsCtor, BmdDataCtor, BmdMotionCtor, ShpDataCtor, ShpMotionCtor, ShpPhysicsMotionCtor, ClothDataCtor, ClothMotionCtor (new-expressions); BmdData vtable +4 (the probe's delete)

void MotionModelBuilder::BuildTexture()
{
    int            i;
    int            n;
    int            found;
    int            num_block;
    int            mesh;
    SGDFILEHEADER *sgd;
    BmdData       *probe;
    sceVu0FVECTOR *vbuf[0x100];         // FF3: 0x400 bytes on the stack, one vertex buffer per mesh

    if (ccs != nullptr)
        ccs_obj = new (operator_new(sizeof(ModelCcs))) ModelCcs(SgdAddr(obj->GetCoord(0)), ccs);

    if (mot != nullptr)
    {
        found = -1;
        i     = 0;
        sgd   = obj->GetSgd(0);
        num_block = (int)sgd->uiNumBlock;
        probe = new (operator_new(sizeof(BmdData))) BmdData(nullptr);
        while (i < ((mot != nullptr) ? ((PK4_HEADER *)mot)->num : 0))
        {
            probe->SetData(Pk4GetData(mot, i));
            if (found < 0 && probe->head->num_bone == num_block - 2)
                found = i;
            i++;
        }
        if (probe != nullptr)
        {
            probe->~BmdData();          // FF3: vtable +4
            operator_delete(probe);
        }
        if (found < 0)
            found = 0;

        bmd_data = new (operator_new(sizeof(BmdData))) BmdData(Pk4GetData(mot, found));
        bmd_mot  = new (operator_new(sizeof(BmdMotion))) BmdMotion(obj, bmd_data, mot);
    }

    if (shp != nullptr)
    {
        shp_data = new (operator_new(sizeof(ShpData))) ShpData(shp);
        n = shp_data->GetNum();
        if (0 < n)
        {
            shp_mot = (ShpMotion **)operator_new_array(n * sizeof(ShpMotion *));   // PC: FF3 n * 4
            memset(vbuf, 0, sizeof(vbuf));
            for (i = 0; i < n; i++)
            {
                // FF3 leaves the slot unset for any other type
                switch (shp_data->GetType(i))
                {
                case 0:
                    shp_mot[i] = new (operator_new(sizeof(ShpMotion))) ShpMotion();
                    break;
                case 1:
                    shp_mot[i] = new (operator_new(sizeof(ShpPhysicsMotion))) ShpPhysicsMotion();
                    break;
                }
                shp_mot[i]->Init(obj, shp_data, i);
                mesh = shp_mot[i]->GetMeshNo();
                vbuf[mesh] = shp_mot[i]->SetVertexBuf(vbuf[mesh]);
                if (shp_data->GetType(i) == 1)
                    shp_mot[i]->Play(0, 0, 0, 0.0f, MODEL_BUILDER_SHP_SPEED);
            }
        }
    }

    if (cls != nullptr)
    {
        cloth_data = new (operator_new(sizeof(ClothData))) ClothData(cls);
        if (0 < cloth_data->GetNum())
        {
            cloth_mot = (ClothMotion **)operator_new_array(cloth_data->GetNum() * sizeof(ClothMotion *));    // PC: FF3 n * 4
            for (i = 0; i < cloth_data->GetNum(); i++)
            {
                cloth_mot[i] = new (operator_new(sizeof(ClothMotion))) ClothMotion();
                cloth_mot[i]->Init(obj, cloth_data, 0, i);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The result: a MotionModelManager over the object, the ccs object, the
// collision shapes and the motions.
// FF3 0x0020ed58 (vtable +0x10).
// callcheck: operator_new, MotionModelManagerCtor (new-expression)

void MotionModelBuilder::BuildMotion()
{
    short shp_num;
    short cloth_num;

    shp_num = 0;
    if (shp_data != nullptr)
        shp_num = (short)shp_data->GetNum();
    cloth_num = 0;
    if (cloth_data != nullptr)
        cloth_num = (short)cloth_data->GetNum();

    result = new (operator_new(sizeof(MotionModelManager))) MotionModelManager(obj, ccs_obj, collision,
                                                                             bmd_mot, shp_mot, shp_num,
                                                                             cloth_mot, cloth_num);
}
