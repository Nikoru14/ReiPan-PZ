// graphics/motion/motion.c -- FF3 0x2125f8..0x217c90 (+ the inline virtuals at
// 0x2bd8c0..0x2be0e0 and motQuaternion2Matrix 0x2bb108): the motion classes
// (RTTI Motion, BoneMotion, BmdMotion, VertexMotion, ShpMotion,
// ShpPhysicsMotion, ClothMotion) and their data (MotionData, BmdData, ShpData,
// ClothData). FF3-only C++: FF2's motion.c (MioPan) is the C ANI_CTRL system;
// only motQuaternion2Matrix is FF2's.
//
// Also here, by address: two ModelObject bone turns (IK), IkLimb 0x2165d8
// and IkChain 0x216c40, which char_model.c calls.

#include "graphics/motion/motion.h"

#include <math.h>                       // sqrtf
#include <string.h>                     // memcpy, memset, strncmp
#include <vector>

#include "common/heapctrl.h"            // heapCtrlMalloc / heapCtrlFree
#include "common/pk4.h"                 // PK4_HEADER, Pk4GetData
#include "common/reipan_stub.h"
#include "common/vu0math.h"             // Vu0Acosf, Vu0Sinf, NormalizeAngle, RotLimitChk
#include "graphics/graph3d/sgd_types.h" // SGDFILEHEADER, SGDCOORDINATE, PHEAD, SgdPtr / SgdAddr
#include "graphics/graph3d/gra3dSGDData.h"  // sgdClearCoordCalcFlgParents, sgdCalcCoordinateMatrix
#include "graphics/model_builder.h"     // MDL_CLD_BODY
#include "ingame/ingame.h"              // draw_en_371348
#include "ingame/movie_room_menu/prg/movie_projecter.h"    // IsMoviePhase
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetFrameTimeScale

// PC: FF3 keeps the per-bone key buffers on the stack (num_bone * 16 bytes
// each, a variable-length frame); the host takes them from a vector.
struct alignas(16) MOT_VEC
{
    float v[4];
};
static_assert(sizeof(MOT_VEC) == 0x10, "MOT_VEC is one sceVu0FVECTOR");

// layout: host -- a host-only owner of the buffers above.
class MotBoneBuf
{
public:
    explicit MotBoneBuf(int n) : buf(n > 0 ? n : 1) {}
    sceVu0FVECTOR *Get() { return (sceVu0FVECTOR *)buf.data(); }

private:
    std::vector<MOT_VEC> buf;
};

static inline void MotCopyVec(float *dst, const float *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

// ══════════════════════════════════════════════════════════════════════
// Motion
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212618 (complete-object constructor; base-object twin FF3 0x002125f8
// has the same body).

Motion::Motion(ModelObject *obj)
{
    Init(obj);
}

// FF3 0x002bd8c0 (deleting FF3 0x002bd8d0): the vptr only.
Motion::~Motion() {}

// FF3 0x002bd8f0 (vtable +0x08).
int Motion::Step() { return 1; }

// FF3 0x002bd8f8 (vtable +0x0c): never overridden; no caller seen.
int Motion::Vtbl0C() { return 0; }

// FF3 0x002bd900 (vtable +0x10).
int Motion::IsEnd() { return 1; }

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212638

void Motion::Init(ModelObject *obj)
{
    frame     = 0.0f;
    this->obj = obj;
    speed     = 1.0f;                   // sdata 0x37130c
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212658

void Motion::SetFrame(float frame)
{
    if (frame < 0.0f)
        frame = 0.0f;
    this->frame = frame;
}

// ──────────────────────────────────────────────────────────────────────
// The frames one update advances (PAL runs 1.2 a frame).
// FF3 0x00212670

float Motion::GetStep()
{
    return speed * GetFrameTimeScale();
}

// ══════════════════════════════════════════════════════════════════════
// BoneMotion
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002126f8 (complete-object constructor; base-object twin FF3 0x002126a0
// has the same body).
// callcheck: MotionCtorBase (the implicit base constructor)

BoneMotion::BoneMotion(ModelObject *obj, void *mot)
    : Motion(obj)
{
    this->mot   = mot;
    blend_num   = 0;
    mot_ext     = nullptr;
    no          = 0;
    blend_frame = 0.0f;
    frame_over  = 0.0f;
    blend       = 0;
}

// FF3 0x002bd908 (deleting FF3 0x002bd918): the vptr only.
BoneMotion::~BoneMotion() {}

// FF3 0x002bd938 (vtable +0x14)
void BoneMotion::Pose(float *matrix) { (void)matrix; }

// FF3 0x002bd940 (vtable +0x18)
void BoneMotion::SetMotion(int no) { (void)no; }

// FF3 0x002bd948 (vtable +0x1c)
void BoneMotion::EndBlend() {}

// FF3 0x002bd950 (vtable +0x20)
void BoneMotion::BlendPose(float *matrix) { (void)matrix; }

// FF3 0x002bd958 (vtable +0x24)
int BoneMotion::GetFrameNum() { return 0; }

// ──────────────────────────────────────────────────────────────────────
// Advance the motion (or, while blending, the blend) by one step. At the
// end the overshoot goes to frame_over and the motion holds its last frame
// (the frame count); 1 then.
// FF3 0x00212750 (vtable +0x08).
// callcheck: GetFrameNum (vtable +0x24), IsEnd (vtable +0x10)

int BoneMotion::Step()
{
    int   num  = GetFrameNum();
    float step = GetStep();

    if (blend == 0)
        SetFrame(frame + step);
    else
        blend_frame += step;

    if (blend != 0)
    {
        if ((float)num <= blend_frame)
        {
            frame_over  = blend_frame - (float)num;
            blend_frame = (float)num;
            return 1;
        }
        return 0;
    }

    if (IsEnd() == 0)
        return 0;

    frame_over = frame - (float)num + 1.0f;     // sdata 0x371310
    SetFrame((float)num);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Pose (or the blend pose) with the world matrix, then a step. A blend that
// ends goes on in the motion from its overshoot (EndBlend); 1 only when the
// motion itself ended.
// FF3 0x00212840
// callcheck: Pose / BlendPose / Step / EndBlend (vtable +0x14 / +0x20 / +0x08 / +0x1c)

int BoneMotion::Update(float *matrix)
{
    if (blend == 0)
        Pose(matrix);
    else
        BlendPose(matrix);

    if (Step() == 0)
        return 0;
    if (blend == 0)
        return 1;

    SetFrame(frame_over + frame);
    blend_frame = 0.0f;
    blend       = 0;
    EndBlend();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002128d8 (vtable +0x10)
// callcheck: GetFrameNum (vtable +0x24)

int BoneMotion::IsEnd()
{
    float f   = frame;
    int   num = GetFrameNum();

    if (f < (float)(num - 1) || blend != 0)
        return 0;
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// VertexMotion
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212998 (complete-object constructor; base-object twin FF3 0x00212940
// has the same body): Motion with no object, then Init.
// callcheck: MotionCtorBase (the base constructor), VertexMotionInit (member)

VertexMotion::VertexMotion(ModelObject *obj, int no)
    : Motion(nullptr)
{
    Init(obj, no);
}

// FF3 0x002bd960 (deleting FF3 0x002bd970): the vptr only.
VertexMotion::~VertexMotion() {}

// FF3 0x002bd990 (vtable +0x18)
void VertexMotion::Pose() {}

// FF3 0x002bd998 (vtable +0x1c)
int VertexMotion::GetFrameNum(int no) { (void)no; return 0; }

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002129f0
// callcheck: MotionInit (member)

void VertexMotion::Init(ModelObject *obj, int no)
{
    Motion::Init(obj);
    this->no = no;
    stop_end = 1;
    playing  = 0;
    hold     = 0;
    loop     = 0;
    reverse  = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Start playing at frame (from the end when reverse), clamped to
// [0, count - 1].
// FF3 0x00212a40 (Ghidra VertexMotionPlay; MotionModelManager::PlayShape /
// PlayCloth and the builder's physics shapes call it).
// callcheck: GetFrameNum (vtable +0x1c)

void VertexMotion::Play(int loop, int stop_end, int reverse, float frame, float speed)
{
    int   num;
    float f;

    this->loop     = loop;
    playing        = 1;
    this->stop_end = stop_end;
    this->reverse  = reverse;
    this->speed    = speed;
    hold           = 0;
    num = GetFrameNum(no);

    if (this->reverse != 0)
        frame = (float)num - frame;

    f = 0.0f;
    if (!(frame < 0.0f))
        f = ((float)(num - 1) < frame) ? (float)(num - 1) : frame;
    SetFrame(f);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212ae0 (vtable +0x14)

void VertexMotion::Stop()
{
    playing = 0;
    SetFrame(0.0f);
}

// ──────────────────────────────────────────────────────────────────────
// Advance (back when reverse) unless held. At the end: count - 1 when
// reverse == loop (a forward motion holding, a reverse one looping), else 0;
// 1 then.
// FF3 0x00212b00 (vtable +0x08; Ghidra had no function here).
// callcheck: IsEnd / GetFrameNum (vtable +0x10 / +0x1c)

int VertexMotion::Step()
{
    float f;

    if (hold == 0)
    {
        if (reverse == 0)
            SetFrame(frame + GetStep());
        else
            SetFrame(frame - GetStep());
    }

    if (IsEnd() == 0)
        return 0;

    f = 0.0f;
    if (reverse == loop)
        f = (float)(GetFrameNum(no) - 1);
    SetFrame(f);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212bc8 (vtable +0x10)
// callcheck: GetFrameNum (vtable +0x1c)

int VertexMotion::IsEnd()
{
    if (hold != 0)
        return 0;
    if (reverse != 0)
        return frame <= 0.0f;
    return (float)(GetFrameNum(no) - 1) <= frame;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00212c40
// callcheck: Pose / Step / Stop (vtable +0x18 / +0x08 / +0x14)

int VertexMotion::Update()
{
    if (playing == 0)
        return 0;

    Pose();
    if (Step() == 0)
        return 0;
    if (stop_end != 0)
        Stop();
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// MotionData, BmdData
// ══════════════════════════════════════════════════════════════════════

// FF3 0x002bdb68 (deleting FF3 0x002bdb78): the vptr only.
MotionData::~MotionData() {}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00214238 (complete-object constructor; base-object twin FF3 0x00214200
// has the same body).
// callcheck: BmdDataSetData (member)

BmdData::BmdData(void *data)
{
    this->data = data;
    if (data != nullptr)
        SetData(data);
}

// FF3 0x002bdb38 (deleting FF3 0x002bdb48): the vptr only.
BmdData::~BmdData() {}

// ──────────────────────────────────────────────────────────────────────
// The entry and its tables: parents at +0x10, the key table 16-aligned after
// them, the key data 16-aligned after that (Relocate's base).
// FF3 0x00214300
// callcheck: BmdDataRelocate (member)

void BmdData::SetData(void *data)
{
    u_int ee = SgdAddr(data);
    u_int tbl;

    this->data = data;
    parent     = SgdPtr<short>(ee + 0x10);
    head       = (BMD_HEAD *)data;
    tbl        = (ee + 0x10 + head->num_bone * 2 + 0xf) & ~0xfu;
    key        = SgdPtr<u_int>(tbl);
    Relocate(data, (tbl + head->num_frame * 4 + 0xf) & ~0xfu);
}

// ──────────────────────────────────────────────────────────────────────
// The key offsets + base (an EE address), once per entry.
// FF3 0x00214270

int BmdData::Relocate(void *data, u_int base)
{
    BMD_HEAD *bmd = (BMD_HEAD *)data;
    int       i;

    if (bmd->relocated != 0)
        return 0;

    for (i = 0; i < head->num_frame; i++)
        key[i] += base;
    bmd->relocated = 1;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002142d0

short BmdData::GetParent(short bone)
{
    if (bone == -1)
        return -1;
    return parent[bone];
}

// ──────────────────────────────────────────────────────────────────────
// Key no's data of type for every bone (bone < 0) or for bone alone (written
// at buf[bone]). Rotations come straight from the file, or unpacked into buf
// when packed; translations of mode 2 hold only the root bones after key 0
// (the others are taken from key 0, into buf). Returns where the data is.
// FF3 0x00214358
// callcheck: BmdDataGetKeyAddr, BmdDataGetParent, BmdDataUnpackQuat (members)

sceVu0FVECTOR *BmdData::GetKeyData(sceVu0FVECTOR *buf, short no, short type, short bone)
{
    int            mode;
    int            i;
    sceVu0FVECTOR *cur;
    sceVu0FVECTOR *first;
    u_short       *packed_q;
    sceVu0FVECTOR  q;

    switch (type)
    {
    case BMD_KEY_TYPE0:
        mode = head->mode_type0;
        break;
    case BMD_KEY_ROT:
        mode = 3;
        break;
    case BMD_KEY_TRANS:
        mode = head->mode_trans;
        break;
    default:
        return buf;
    }

    cur = (sceVu0FVECTOR *)GetKeyAddr(no, type);

    if (mode == 2 && 0 < no)
    {
        first = (sceVu0FVECTOR *)GetKeyAddr(0, type);
        for (i = 0; i < head->num_bone; i++)
        {
            if (GetParent((short)i) == -1)
            {
                if (bone < 0 || i == bone)
                    MotCopyVec(buf[i], *cur);
                if (i == bone)
                    return buf;
                cur++;
            }
            else
            {
                if (bone < 0 || i == bone)
                    MotCopyVec(buf[i], first[i]);
                if (i == bone)
                    return buf;
            }
        }
        return buf;
    }

    if (head->packed != 0 && type == BMD_KEY_ROT)
    {
        packed_q = (u_short *)cur;
        for (i = 0; i < head->num_bone; i++, packed_q += 4)
        {
            if (bone < 0 || i == bone)
            {
                UnpackQuat(q, packed_q);
                MotCopyVec(buf[i], q);
                if (i == bone)
                    return buf;
            }
        }
        return buf;
    }

    return cur;
}

// ──────────────────────────────────────────────────────────────────────
// Four packed components: bit 14 set = 1.0, else bits 0..13 / 16384; bit 15
// negates.
// FF3 0x00214628

void BmdData::UnpackQuat(float *out, const u_short *in)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        u_short v = in[i];

        if (((v >> 14) & 1) == 0)
            out[i] = (float)(v & 0x3fff) * (1.0f / 16384.0f);  // sdata 0x371344
        else
            out[i] = 1.0f;                                      // sdata 0x371340
        if ((short)v < 0)
            out[i] = -out[i];
    }
}

// ──────────────────────────────────────────────────────────────────────
// Key no's data (clamped to the last key); translations follow the
// rotations (8 bytes a bone, the block 16-aligned, when packed).
// FF3 0x002146c0

void *BmdData::GetKeyAddr(short no, short type)
{
    int   k = no;
    u_int addr;

    if (head->num_frame <= k)
        k = (short)(head->num_frame - 1);

    addr = key[k];
    if (type == BMD_KEY_TRANS)
    {
        if (head->packed != 0)
            addr = (addr + head->num_bone * 8 + 0xf) & ~0xfu;
        else
            addr = addr + head->num_bone * 0x10;
    }
    return SgdPtr<void>(addr);
}

// ══════════════════════════════════════════════════════════════════════
// BmdMotion
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Four bone buffers from the system heap (cleared), the data, then the
// coordinate tree linked by the BMD's parents.
// FF3 0x00212de8 (complete-object constructor; base-object twin FF3 0x00212cb8
// has the same body).
// callcheck: BoneMotionCtorBase (the base constructor), BmdMotionLinkCoords (member)

BmdMotion::BmdMotion(ModelObject *obj, BmdData *data, void *mot)
    : BoneMotion(obj, mot)
{
    int size = data->head->num_bone << 4;

    pose_rot   = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    pose_trans = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    cur_rot    = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    cur_trans  = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    memset(pose_rot, 0, size);
    memset(pose_trans, 0, size);
    memset(cur_rot, 0, size);
    memset(cur_trans, 0, size);
    this->data = data;
    blend_yaw  = 0.0f;
    motion_yaw = 0.0f;
    yaw_func   = nullptr;
    yaw_arg    = nullptr;
    LinkCoords();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213000 (base-object twin FF3 0x00212f18, deleting FF3 0x002130e8)

BmdMotion::~BmdMotion()
{
    if (pose_rot != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), pose_rot, 0, 0, 0);
    if (pose_trans != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), pose_trans, 0, 0, 0);
    if (cur_rot != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), cur_rot, 0, 0, 0);
    if (cur_trans != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), cur_trans, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002131d0

int BmdMotion::IsHalfRate()
{
    if (blend != 0)
        return 0;
    return data->head->half_rate != 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002131f8 (vtable +0x24; Ghidra had no function here).

int BmdMotion::GetFrameNum()
{
    if (blend != 0)
        return (short)blend_num;
    return data->head->num_frame;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213218

int BmdMotion::GetNum()
{
    if (mot == nullptr)
        return 0;
    return ((PK4_HEADER *)mot)->num;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213230

short BmdMotion::GetKeyFloor(float frame)
{
    short step = (short)(IsHalfRate() + 1);

    return (short)(((short)(int)frame / step) * step);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213290

short BmdMotion::GetKeyNext(float frame)
{
    short key = GetKeyFloor(frame);

    return (short)(key + IsHalfRate() + 1);
}

// ──────────────────────────────────────────────────────────────────────
// The weight of the next key: (frame - key) / step. ROM quirk kept: an
// even-length motion near its end (this->frame >= count - 2) whose PK4 holds an
// even number of motions uses frame - key unscaled.
// FF3 0x002132e0
// callcheck: GetFrameNum (vtable +0x24)

float BmdMotion::GetKeyRatio(float frame, short key)
{
    int   num  = GetFrameNum();
    short step = (short)(IsHalfRate() + 1);
    float d    = frame - (float)key;
    float t    = d / (float)step;

    if ((num & 1) == 0 && (float)(num - 2) <= this->frame && (GetNum() & 1) == 0)
        t = d;
    return t;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002133b8

short BmdMotion::GetBoneNum()
{
    if (data == nullptr)
        return 0;
    return data->head->num_bone;
}

// ──────────────────────────────────────────────────────────────────────
// The pose at frame: each bone's rotation slerped and its translation lerped
// between the two keys around frame, into coordinate bone + 1 (rotation
// matrix, translation added to row 3) and kept as the current pose. matrix
// (when given) becomes the root coordinate. Only when the BMD's bone count
// matches the model (SGD 0's block count - 2).
// FF3 0x002133d8 (vtable +0x14). FF3 inlines the slerp of motQuatSlerp.
// callcheck: Vu0Acosf, Vu0Sinf (in motQuatSlerp, which FF3 inlines here); ModelObjectGetSgd / ModelObjectGetCoord, BmdMotionGetKey*, BmdDataGetKeyData (members), motQuaternion2Matrix

void BmdMotion::Pose(float *matrix)
{
    int             num_bone = data->head->num_bone;
    SGDCOORDINATE  *coord;
    short           key0;
    short           key1;
    float           t;
    int             i;
    sceVu0FVECTOR  *rot0;
    sceVu0FVECTOR  *rot1;
    sceVu0FVECTOR  *trans0;
    sceVu0FVECTOR  *trans1;
    sceVu0FVECTOR   q;
    sceVu0FVECTOR   tr;

    if (num_bone != (int)obj->GetSgd(0)->uiNumBlock - 2)
        return;

    coord = obj->GetCoord(0);
    key0  = GetKeyFloor(frame);
    key1  = GetKeyNext(frame);
    t     = GetKeyRatio(frame, key0);
    if (matrix != nullptr)
        memcpy(coord->matCoord, matrix, sizeof(sceVu0FMATRIX));

    MotBoneBuf buf_rot0(num_bone), buf_rot1(num_bone), buf_trans0(num_bone), buf_trans1(num_bone);
    rot0   = data->GetKeyData(buf_rot0.Get(), key0, BMD_KEY_ROT, -1);
    rot1   = data->GetKeyData(buf_rot1.Get(), key1, BMD_KEY_ROT, -1);
    trans0 = data->GetKeyData(buf_trans0.Get(), key0, BMD_KEY_TRANS, -1);
    trans1 = data->GetKeyData(buf_trans1.Get(), key1, BMD_KEY_TRANS, -1);

    for (i = 0; i < num_bone; i++)
    {
        motQuatSlerp(t, q, rot0[i], rot1[i]);

        // the translation: xyz lerped, w kept from key 0
        tr[0] = trans0[i][0] * (1.0f - t) + trans1[i][0] * t;
        tr[1] = trans0[i][1] * (1.0f - t) + trans1[i][1] * t;
        tr[2] = trans0[i][2] * (1.0f - t) + trans1[i][2] * t;
        tr[3] = trans0[i][3];

        if (coord != nullptr)
        {
            SGDCOORDINATE *c = &coord[i + 1];

            motQuaternion2Matrix(c->matCoord, q);
            c->matCoord[3][0] += tr[0];
            c->matCoord[3][1] += tr[1];
            c->matCoord[3][2] += tr[2];
        }
        MotCopyVec(cur_rot[i], q);
        MotCopyVec(cur_trans[i], tr);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002137d0

void BmdMotion::ResetPose()
{
    int size;

    if (data == nullptr)
        return;
    size = data->head->num_bone << 4;
    memcpy(pose_rot, cur_rot, size);
    memcpy(pose_trans, cur_trans, size);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213840 (vtable +0x18).
// callcheck: BmdMotionFindBmd, BmdDataSetData (members)

void BmdMotion::SetMotion(int no)
{
    BMD_HEAD *bmd = FindBmd(no);

    if (bmd != nullptr)
        data->SetData(bmd);
}

// ──────────────────────────────────────────────────────────────────────
// Member no of the motion PK4 when it is a BMD, else the same member of the
// second pack when that one is.
// FF3 0x00213880

BMD_HEAD *BmdMotion::FindBmd(int no)
{
    BMD_HEAD *bmd = (BMD_HEAD *)Pk4GetData(mot, no);

    // PC: a missing member is null (the EE reads address 0 and fails the test)
    if (bmd != nullptr && strncmp(bmd->magic, "BMD", 4) == 0)  // sdata 0x371320
        return bmd;
    if (mot_ext == nullptr)
        return nullptr;

    bmd = (BMD_HEAD *)Pk4GetData(mot_ext, no);
    if (bmd == nullptr || strncmp(bmd->magic, "BMD", 4) != 0)
        return nullptr;
    return bmd;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213920 (Ghidra BmdMotionGetMotFrameNum; was MotCtrlGetEntryId).

short BmdMotion::GetMotFrameNum(int no)
{
    BMD_HEAD *bmd = FindBmd(no);

    if (bmd == nullptr)
        return 0;
    return bmd->num_frame;
}

// ──────────────────────────────────────────────────────────────────────
// The coordinate tree after the BMD: coordinate 0 is the root (no parent),
// bone i is coordinate i + 1 under coordinate parent + 1 (the root for -1).
// FF3 0x00213950
// callcheck: ModelObjectGetCoord / ModelObjectGetSgd (members), BmdDataGetParent (member)

void BmdMotion::LinkCoords()
{
    SGDCOORDINATE *coord    = obj->GetCoord(0);
    int            num_bone = data->head->num_bone;
    int            i;
    short          parent;

    if (num_bone != (int)obj->GetSgd(0)->uiNumBlock - 2)
        return;

    coord[0].pParent = 0;
    for (i = 0; i < data->head->num_bone; i++)
    {
        parent = data->GetParent((short)i);
        if (parent == -1)
            coord[i + 1].pParent = SgdAddr(&coord[0]);
        else
            coord[i + 1].pParent = SgdAddr(&coord[parent + 1]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The blend: from the saved pose to the new motion's key at frame, weight
// 1 - (blend_num - blend_frame) / blend_num clamped to [0, 1]; the root
// bones' yaw from the saved pose goes to blend_yaw and turns the root
// coordinate (matrix becomes it first).
// FF3 0x00213a40 (vtable +0x20; Ghidra had no function here).
// callcheck: GetFrameNum (vtable +0x24), ModelObjectGetSgd / ModelObjectGetCoord, BmdDataGetKeyData, BmdDataGetParent, BmdMotionCalcYaw (members), motQuatSlerp, motQuaternion2Matrix, NormalizeAngle, sceVu0RotMatrixY, sceVu0MulMatrix (inline VU0)

void BmdMotion::BlendPose(float *matrix)
{
    int             num_bone;
    float           t;
    SGDCOORDINATE  *coord;
    short           key;
    int             i;
    sceVu0FVECTOR  *rot;
    sceVu0FVECTOR  *trans;
    sceVu0FVECTOR   q;
    sceVu0FVECTOR   tr;
    sceVu0FMATRIX   unit;
    sceVu0FMATRIX   rot_y;

    if (GetFrameNum() == 0)
        return;
    num_bone = data->head->num_bone;
    if (num_bone != (int)obj->GetSgd(0)->uiNumBlock - 2)
        return;

    t = 1.0f - ((float)GetFrameNum() - blend_frame) / (float)GetFrameNum();   // sdata 0x371328
    if (t < 0.0f)
        t = 0.0f;
    else if (1.0f < t)
        t = 1.0f;

    coord = obj->GetCoord(0);
    memcpy(coord->matCoord, matrix, sizeof(sceVu0FMATRIX));

    key = (short)(int)frame;
    MotBoneBuf buf_rot(num_bone), buf_trans(num_bone);
    rot   = data->GetKeyData(buf_rot.Get(), key, BMD_KEY_ROT, -1);
    trans = data->GetKeyData(buf_trans.Get(), key, BMD_KEY_TRANS, -1);

    for (i = 0; i < num_bone; i++)
    {
        SGDCOORDINATE *c = &coord[i + 1];

        motQuatSlerp(t, q, pose_rot[i], rot[i]);

        tr[0] = pose_trans[i][0] * (1.0f - t) + trans[i][0] * t;
        tr[1] = pose_trans[i][1] * (1.0f - t) + trans[i][1] * t;
        tr[2] = pose_trans[i][2] * (1.0f - t) + trans[i][2] * t;
        tr[3] = pose_trans[i][3];

        if (data->GetParent((short)i) == -1)
            blend_yaw = CalcYaw(pose_rot[i], q);

        motQuaternion2Matrix(c->matCoord, q);
        c->matCoord[3][0] += tr[0];
        c->matCoord[3][1] += tr[1];
        c->matCoord[3][2] += tr[2];
        MotCopyVec(cur_rot[i], q);
        MotCopyVec(cur_trans[i], tr);
    }

    sceVu0UnitMatrix(unit);             // data 0x3293c0
    sceVu0RotMatrixY(rot_y, unit, NormalizeAngle(blend_yaw));
    sceVu0MulMatrix(coord->matCoord, coord->matCoord, rot_y);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00213da0 (vtable +0x1c).
// callcheck: BmdMotionCalcBlendYaw, BmdMotionApplyBlendYaw (members)

void BmdMotion::EndBlend()
{
    CalcBlendYaw();
    ApplyBlendYaw();
}

// ──────────────────────────────────────────────────────────────────────
// The signed turn about Y from q0's Z axis to q1's (both flattened onto XZ
// and normalised): acos of their dot, negative when the cross product's y
// is >= 0, wrapped into [-pi, pi].
// FF3 0x00213dc8
// callcheck: motQuaternion2Matrix, Vu0Acosf, RotLimitChk

float BmdMotion::CalcYaw(float *q0, float *q1)
{
    sceVu0FMATRIX m0;
    sceVu0FMATRIX m1;
    sceVu0FVECTOR z0;
    sceVu0FVECTOR z1;
    float         cross_y;
    float         sign;
    float         len;
    float         yaw;

    motQuaternion2Matrix(m0, q0);
    motQuaternion2Matrix(m1, q1);
    MotCopyVec(z0, m0[2]);
    MotCopyVec(z1, m1[2]);
    z0[1] = 0.0f;
    z1[1] = 0.0f;
    z1[3] = 1.0f;                       // sdata 0x37132c
    z0[3] = 1.0f;

    // VU0: xyz scaled by the reciprocal square root, w kept
    len = Vu0Rsqrt(z0[0] * z0[0] + z0[1] * z0[1] + z0[2] * z0[2]);    // PC: PS2 rsqrt
    z0[0] *= len;
    z0[1] *= len;
    z0[2] *= len;
    len = Vu0Rsqrt(z1[0] * z1[0] + z1[1] * z1[1] + z1[2] * z1[2]);    // PC: PS2 rsqrt
    z1[0] *= len;
    z1[1] *= len;
    z1[2] *= len;

    cross_y = z0[2] * z1[0] - z0[0] * z1[2];    // vopmula / vopmsub: (z0 x z1).y, the only part read
    sign = 1.0f;                        // sdata 0x37132c
    if (0.0f <= cross_y)
        sign = -1.0f;                   // sdata 0x371330

    yaw = Vu0Acosf(z0[0] * z1[0] + z0[1] * z1[1] + z0[2] * z1[2]) * sign;
    RotLimitChk(&yaw);
    return yaw;
}

// ──────────────────────────────────────────────────────────────────────
// The root bone's turn from the motion's interpolated key at frame to the
// saved pose.
// FF3 0x00213f10 (Ghidra BmdMotionCalcBlendYaw; was MotCtrlMakeBlendWork)
// callcheck: BmdMotionGetKey*, BmdMotionGetRootBone, BmdDataGetKeyData, BmdMotionCalcYaw (members), motQuatSlerp

void BmdMotion::CalcBlendYaw()
{
    float          f    = frame;
    short          key0 = GetKeyFloor(f);
    short          key1 = GetKeyNext(f);
    float          t    = GetKeyRatio(f, key0);
    int            root = GetRootBone();
    int            num_bone = data->head->num_bone;
    sceVu0FVECTOR *rot0;
    sceVu0FVECTOR *rot1;
    sceVu0FVECTOR  q;

    MotBoneBuf buf0(num_bone), buf1(num_bone);
    rot0 = data->GetKeyData(buf0.Get(), key0, BMD_KEY_ROT, -1);
    rot1 = data->GetKeyData(buf1.Get(), key1, BMD_KEY_ROT, -1);
    motQuatSlerp(t, q, rot0[root], rot1[root]);
    blend_yaw = CalcYaw(q, pose_rot[root]);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00214028

short BmdMotion::GetRootBone()
{
    int i;

    for (i = 0; i < data->head->num_bone; i++)
    {
        if (data->GetParent((short)i) == -1)
            return (short)i;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002140b8 (no caller found).

void BmdMotion::ApplyMotionYaw()
{
    if (yaw_func != nullptr)
        yaw_func(motion_yaw, yaw_arg);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002140e8 (Ghidra BmdMotionApplyBlendYaw; was MotCtrlFreeBlendWork)

void BmdMotion::ApplyBlendYaw()
{
    if (yaw_func != nullptr)
        yaw_func(blend_yaw, yaw_arg);
    blend_yaw = 0.0f;
}

// ──────────────────────────────────────────────────────────────────────
// The root bone's turn from the key at time to the last key.
// FF3 0x00214120 (Ghidra BmdMotionCalcMotionYaw; was MotCtrlMakeWork44)
// callcheck: BmdMotionGetKeyFloor, BmdMotionGetRootBone, BmdDataGetKeyData, BmdMotionCalcYaw (members)

void BmdMotion::CalcMotionYaw(float time)
{
    short          key0     = GetKeyFloor(time);
    short          last     = data->head->num_frame;
    int            num_bone = data->head->num_bone;
    int            root     = GetRootBone();
    sceVu0FVECTOR *rot0;
    sceVu0FVECTOR *rot1;

    MotBoneBuf buf0(num_bone), buf1(num_bone);
    rot0 = data->GetKeyData(buf0.Get(), key0, BMD_KEY_ROT, -1);
    rot1 = data->GetKeyData(buf1.Get(), last, BMD_KEY_ROT, -1);
    motion_yaw = CalcYaw(rot0[root], rot1[root]);
}

// ══════════════════════════════════════════════════════════════════════
// ShpData
// ══════════════════════════════════════════════════════════════════════

// FF3 0x002179a0
ShpData::ShpData(void *data)
{
    this->data = data;
}

// FF3 0x002be0d0 (deleting FF3 0x002be0e0): the vptr only.
ShpData::~ShpData() {}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002179b8

int ShpData::GetNum()
{
    PK4_HEADER *pack = (PK4_HEADER *)GetMotPack();

    if (pack == nullptr)
        return 0;
    return pack->num;
}

// FF3 0x002179e8
void *ShpData::GetMotPack() { return Pk4GetData(data, 0); }

// FF3 0x00217a08
void *ShpData::GetDeltaPack() { return Pk4GetData(data, 1); }

// FF3 0x00217a28
SHP_MOT *ShpData::GetMot(int no) { return (SHP_MOT *)Pk4GetData(GetMotPack(), no); }

// ──────────────────────────────────────────────────────────────────────
// Track track of motion no: from +0x10, each 0x10 + frames * 4 bytes, 16-aligned.
// FF3 0x00217a58

SHP_TRACK *ShpData::GetTrack(int no, int track)
{
    SHP_MOT *mot  = GetMot(no);
    u_int    addr = SgdAddr(mot) + 0x10;

    for (; 0 < track; track--)
        addr = (addr + mot->num_frame * 4 + 0x1f) & ~0xfu;
    return SgdPtr<SHP_TRACK>(addr);
}

// FF3 0x00217ab0
SHP_PHYSICS_TRACK *ShpData::GetFirstTrack(int no) { return (SHP_PHYSICS_TRACK *)((u_char *)GetMot(no) + 0x10); }

// FF3 0x00217ad0
float *ShpData::GetTrackWeights(int no, int track) { return GetTrack(no, track)->weight; }

// FF3 0x00217af0
SHP_PHYSICS_PARAM *ShpData::GetPhysicsParam(int no) { return (SHP_PHYSICS_PARAM *)((u_char *)GetFirstTrack(no) + 0x10); }

// FF3 0x00217b10
SHP_DELTA *ShpData::GetDelta(int set) { return (SHP_DELTA *)Pk4GetData(GetDeltaPack(), set); }

// FF3 0x00217b40
SHP_DELTA_VEC *ShpData::GetDeltaVecs(int set) { return GetDelta(set)->vec; }

// FF3 0x00217b60
int ShpData::GetTrackNum(int no) { return GetMot(no)->num_track; }

// FF3 0x00217b80
int ShpData::GetType(int no) { return GetMot(no)->type; }

// FF3 0x00217ba0
int ShpData::GetMotFrameNum(int no) { return GetMot(no)->num_frame; }

// FF3 0x00217bc0
int ShpData::GetMeshNo(int no) { return GetMot(no)->mesh; }

// FF3 0x00217be0
int ShpData::GetTrackDelta(int no, int track) { return GetTrack(no, track)->delta; }

// FF3 0x00217c00 (no caller found).
float ShpData::GetTrackWeight(int no, int track, int frame) { return GetTrackWeights(no, track)[frame]; }

// FF3 0x00217c30
float *ShpData::GetTrackWeightList(int no, int track) { return GetTrackWeights(no, track); }

// FF3 0x00217c48 (Ghidra ShpDataGetDeltaNum; was the FF2 match motSetInvMatrix)
int ShpData::GetDeltaNum(int set) { return GetDelta(set)->num; }

// FF3 0x00217c68
short ShpData::GetFrameNum(int no) { return (short)GetMotFrameNum(no); }

// ══════════════════════════════════════════════════════════════════════
// ShpMotion, ShpPhysicsMotion
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00216f70 (complete-object constructor; base-object twin FF3 0x00216f28
// has the same body).
// callcheck: VertexMotionCtorBase (the base constructor)

ShpMotion::ShpMotion()
    : VertexMotion(nullptr, 0)
{
    dirty = 0;
    data  = nullptr;
    vbuf  = nullptr;
    own   = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The base vertices back into the mesh, then its own copy freed.
// FF3 0x00217020 (base-object FF3 0x00216fb8, deleting FF3 0x00217088)
// callcheck: ShpMotionStop (member)

ShpMotion::~ShpMotion()
{
    ShpMotion::Stop();
    if (own != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), own, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002170f8
// callcheck: VertexMotionInit (member)

void ShpMotion::Init(ModelObject *obj, ShpData *data, int no)
{
    VertexMotion::Init(obj, no);
    this->data = data;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217130
// callcheck: ShpDataGetMeshNo, ModelObjectGetVectorInfo (members)

sceVu0FVECTOR *ShpMotion::SetVertexBuf(sceVu0FVECTOR *buf)
{
    PHEAD         *info;
    sceVu0FVECTOR *src;
    u_int          n;
    u_int          i;

    if (buf != nullptr)
    {
        vbuf = buf;
        own  = nullptr;
        return vbuf;
    }

    info = obj->GetVectorInfo(data->GetMeshNo(no));
    n    = (info->pUniqNormal - info->pUniqVertex) >> 4;
    own  = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), n << 4, 0, 0, 0);
    vbuf = own;
    src  = SgdPtr<sceVu0FVECTOR>(info->pUniqVertex);
    for (i = 0; i < n; i++)
        MotCopyVec(vbuf[i], src[i]);
    return vbuf;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217200 (vtable +0x14)
// callcheck: ShpMotionWriteVertex, VertexMotionStop (members)

void ShpMotion::Stop()
{
    WriteVertex(1);
    VertexMotion::Stop();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217230
// callcheck: ShpMotionGetMeshNo, ModelObjectGetVectorInfo (members)

int ShpMotion::WriteVertex(int force)
{
    PHEAD         *info;
    sceVu0FVECTOR *dst;
    u_int          n;
    u_int          i;

    if (force == 0 && dirty == 0)
        return 0;

    info = obj->GetVectorInfo(GetMeshNo());
    n    = (info->pUniqNormal - info->pUniqVertex) >> 4;
    dst  = SgdPtr<sceVu0FVECTOR>(info->pUniqVertex);
    for (i = 0; i < n; i++)
        MotCopyVec(dst[i], vbuf[i]);
    dirty = 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002172c8
// callcheck: ShpDataGetMeshNo (member)

int ShpMotion::GetMeshNo()
{
    return data->GetMeshNo(no);
}

// ──────────────────────────────────────────────────────────────────────
// Each track's weight at frame (linear between its two frames) times its
// delta set, added onto the mesh's vertices (WriteVertex puts the base back).
// FF3 0x002172e8 (vtable +0x18).
// callcheck: ShpData* (members), ModelObjectGetVectorInfo (member), ShpMotionAddDelta (member)

void ShpMotion::Pose()
{
    int            no    = this->no;
    sceVu0FVECTOR *vtx   = SgdPtr<sceVu0FVECTOR>(obj->GetVectorInfo(data->GetMeshNo(no))->pUniqVertex);
    int            num   = data->GetTrackNum(no);
    int            k     = (int)frame;
    float          frac  = frame - (float)k;
    int            i;
    int            set;
    float         *w;

    for (i = 0; i < num; i++)
    {
        set = data->GetTrackDelta(no, i);
        w   = data->GetTrackWeightList(no, i);
        AddDelta(w[k] + frac * (w[k + 1] - w[k]), vtx, data->GetDeltaVecs(set), data->GetDeltaNum(set));
    }
    dirty = 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217430 (vtable +0x1c).
// callcheck: ShpDataGetFrameNum (member)

int ShpMotion::GetFrameNum(int no)
{
    if (data == nullptr)
        return 0;
    return data->GetFrameNum(no);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217460 (VU0: xyz only)

void ShpMotion::AddDelta(float weight, sceVu0FVECTOR *vtx, SHP_DELTA_VEC *vec, int n)
{
    for (; 0 < n; n--, vec++)
    {
        float *v = vtx[vec->vertex];

        v[0] += vec->d[0] * weight;
        v[1] += vec->d[1] * weight;
        v[2] += vec->d[2] * weight;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00217508 (complete-object constructor; base-object twin FF3 0x002174a8
// has the same body).
// callcheck: ShpMotionCtorBase (the implicit base constructor)

ShpPhysicsMotion::ShpPhysicsMotion()
{
    static const sceVu0FVECTOR zero = { 0.0f, 0.0f, 0.0f, 0.0f };     // data 0x329530

    MotCopyVec(offset, zero);
    MotCopyVec(prev, zero);
    MotCopyVec(vel, zero);
}

// FF3 0x002be060 (deleting FF3 0x002be088): ShpMotion's destructor follows.
// callcheck: ShpMotionDtorBase (the base class destructor)
ShpPhysicsMotion::~ShpPhysicsMotion() {}

// FF3 0x002be0c8 (vtable +0x1c)
int ShpPhysicsMotion::GetFrameNum(int no) { (void)no; return 0; }

// FF3 0x002be0c0 (vtable +0x20)
void ShpPhysicsMotion::StopVirtual() {}

// ──────────────────────────────────────────────────────────────────────
// The spring: the bone point's world movement since the last step plus the
// constant force, both scaled, turned into the bone's axes (its rotation
// rows normalised and inverted) and added to a damped spring offset, which
// stays in [-1, 1].
// FF3 0x00217568
// callcheck: ShpData* (members), ModelObjectGetCoord (member), sceVu0InversMatrix (VU0 inline otherwise)

void ShpPhysicsMotion::Calc()
{
    static const sceVu0FVECTOR origin = { 0.0f, 0.0f, 0.0f, 1.0f };   // data 0x329500
    int                no    = this->no;
    int                mesh  = data->GetMeshNo(no);
    short              bone  = data->GetFirstTrack(no)->bone;
    SHP_PHYSICS_PARAM *prm   = data->GetPhysicsParam(no);
    SGDCOORDINATE     *coord = &obj->GetCoord(mesh)[bone];
    sceVu0FMATRIX      lw;
    sceVu0FMATRIX      axis;
    sceVu0FVECTOR      pos;
    sceVu0FVECTOR      d;
    sceVu0FVECTOR      force;
    sceVu0FVECTOR      local;
    int                i;

    sceVu0CopyMatrix(lw, coord->matLocalWorld);
    sceVu0ApplyMatrix(pos, lw, prm->point);
    for (i = 0; i < 4; i++)
        d[i] = pos[i] - prev[i];
    MotCopyVec(prev, pos);

    for (i = 0; i < 3; i++)
        force[i] = d[i] * prm->scale + prm->force[i] * prm->scale;
    force[3] = 0.0f;

    // the rotation rows: xyz normalised (VU0 rsqrt), w kept
    for (i = 0; i < 3; i++)
    {
        float len = Vu0Rsqrt(lw[i][0] * lw[i][0] + lw[i][1] * lw[i][1] + lw[i][2] * lw[i][2]);   // PC: PS2 rsqrt

        axis[i][0] = lw[i][0] * len;
        axis[i][1] = lw[i][1] * len;
        axis[i][2] = lw[i][2] * len;
        axis[i][3] = lw[i][3];
    }
    MotCopyVec(axis[3], origin);
    sceVu0InversMatrix(axis, axis);
    sceVu0ApplyMatrix(local, axis, force);

    for (i = 0; i < 4; i++)
    {
        vel[i]    = (vel[i] + offset[i] * -prm->spring) * prm->damp;
        offset[i] = offset[i] + vel[i] + local[i];
    }
    sceVu0ClampVector(offset, offset, -1.0f, 1.0f);   // sdata 0x371388 / 0x37138c
}

// ──────────────────────────────────────────────────────────────────────
// The spring's offset shown through the six delta sets of the first track
// (+x, -x, +y, -y, +z, -z), each weighted by |offset| of its axis when the
// offset points its way. ROM quirk kept: the z pair is the other way round
// (+z's set shows a negative offset, -z's a positive one).
// FF3 0x00217810 (vtable +0x18; Ghidra ShpPhysicsMotionPose, was the FF2 match motGetFrameDataRT).
// callcheck: ShpData* (members), ModelObjectGetVectorInfo, ShpPhysicsMotionCalc, ShpMotionAddDelta (members)

void ShpPhysicsMotion::Pose()
{
    int                no   = this->no;
    sceVu0FVECTOR     *vtx  = SgdPtr<sceVu0FVECTOR>(obj->GetVectorInfo(data->GetMeshNo(no))->pUniqVertex);
    SHP_PHYSICS_TRACK *trk;
    int                i;
    int                skip;
    short              set;
    float              v;

    Calc();
    trk = data->GetFirstTrack(no);
    for (i = 0; i < 6; i++)
    {
        set = trk->delta[i];
        if (set < 0)
            continue;
        v = offset[i / 2];
        if (v == 0.0f)
            continue;

        if (i == 1 || i == 3 || i == 4)
            skip = (0.0f <= v);
        else
            skip = (v <= 0.0f);
        if (skip)
            continue;

        if (v < 0.0f)
            v = -v;
        AddDelta(v, vtx, data->GetDeltaVecs(set), data->GetDeltaNum(set));
    }
    dirty = 1;
}

// ══════════════════════════════════════════════════════════════════════
// ClothData, ClothMotion
// ══════════════════════════════════════════════════════════════════════

// FF3 0x00216570
ClothData::ClothData(void *data)
{
    this->data = data;
}

// FF3 0x002be008 (deleting FF3 0x002be018): the vptr only.
ClothData::~ClothData() {}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00216588

int ClothData::GetNum()
{
    if (data == nullptr)
        return 0;
    return ((PK4_HEADER *)data)->num;
}

// FF3 0x002165a0 (Ghidra ClothDataGetCloth; was the FF2 match motInitMotCtrl)
void *ClothData::GetCloth(int no) { return Pk4GetData(data, no); }

// FF3 0x002165b8
void *ClothData::GetParam(int no) { return (u_char *)GetCloth(no) + 0x10; }

// ──────────────────────────────────────────────────────────────────────
// Everything cleared, the nine matrices unit, the three vectors zero, no
// wind.
// FF3 0x002148f0 (complete-object constructor; base-object twin FF3 0x00214758
// has the same body).
// callcheck: VertexMotionCtorBase (the base constructor)

ClothMotion::ClothMotion()
    : VertexMotion(nullptr, 0)
{
    static const sceVu0FVECTOR zero = { 0.0f, 0.0f, 0.0f, 0.0f };     // data 0x329530
    int i;

    cloth_obj = nullptr;
    data      = nullptr;
    vbuf      = nullptr;
    own       = nullptr;
    param     = nullptr;
    point     = nullptr;
    link      = nullptr;
    num_point = 0;
    num_link  = 0;
    stepped   = 0;
    reset_req = 0;
    calc_off  = 0;
    settled   = 0;
    written   = 0;
    wind      = 0;
    memset(wind_dir, 0, 0x20);          // wind_dir .. _2fe
    wind_power  = 0.0f;
    wind_period = 0;
    wind_cycle  = 0;
    wind_frame  = 0;
    wind_rise   = 0;
    wind_fall   = 0;
    _2fe        = 0;
    for (i = 0; i < 9; i++)
        sceVu0UnitMatrix(mat[i]);       // data 0x3293c0
    for (i = 0; i < 3; i++)
        MotCopyVec(vec[i], zero);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00214b50 (base-object FF3 0x00214a88, deleting FF3 0x00214c18)
// callcheck: ClothMotionReset (member)

ClothMotion::~ClothMotion()
{
    Reset();
    if (own != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), own, 0, 0, 0);
    heapCtrlFree(GetSystemHeapWrkP(), param, 0, 0, 0);
    heapCtrlFree(GetSystemHeapWrkP(), point, 0, 0, 0);
    heapCtrlFree(GetSystemHeapWrkP(), link, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002156a0 (vtable +0x14; Ghidra had no function here).
// callcheck: VertexMotionStop, ClothMotionReset (members)

void ClothMotion::Stop()
{
    VertexMotion::Stop();
    Reset();
}

// FF3 0x002bdb98 (vtable +0x1c)
int ClothMotion::GetFrameNum(int no) { (void)no; return 0; }

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00216520

void ClothMotion::SetWind(const float *dir, int period, float power)
{
    MotCopyVec(wind_dir, dir);
    wind_power  = power;
    wind        = 1;
    wind_cycle  = (short)period;
    wind_period = (short)period;
    wind_frame  = 0;
    wind_rise   = 0;
    wind_fall   = 0;
}

// ──────────────────────────────────────────────────────────────────────
// The cloth simulation
// ──────────────────────────────────────────────────────────────────────

// data 0x35caa0: the presets a cloth's param.preset (< 3) picks.
static const CLOTH_PRESET cloth_preset[3] = {
    { { 0.38f, 0.05f }, 0.80f, 0.0f, { 0.0f, 0.11f, 0.0f, 0.0f } },
    { { 0.45f, 0.05f }, 0.90f, 0.0f, { 0.0f, 0.04f, 0.0f, 0.0f } },
    { { 0.43f, 0.05f }, 0.78f, 0.0f, { 0.0f, 0.18f, 0.0f, 0.0f } },
};

// vf0 (0, 0, 0, 1): row 3 of a rotation matrix.
static const sceVu0FVECTOR cloth_w1   = { 0.0f, 0.0f, 0.0f, 1.0f };
static const sceVu0FVECTOR cloth_zero = { 0.0f, 0.0f, 0.0f, 0.0f };   // data 0x329530

// |a - b| over xyz (the inline vsub / vmul.xyz / x+y+z / vsqrt).
static inline float ClothDist(const float *a, const float *b)
{
    float d[3];

    d[0] = a[0] - b[0];
    d[1] = a[1] - b[1];
    d[2] = a[2] - b[2];
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// v.xyz += a.xyz
static inline void ClothAddXYZ(float *v, const float *a)
{
    v[0] += a[0];
    v[1] += a[1];
    v[2] += a[2];
}

// v.xyz *= s
static inline void ClothScaleXYZ(float *v, float s)
{
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
}

// v.xyz normalised, w kept (the inline vrsqrt + vmul.w vd, vs, vf0).
// PC: a zero vector stays zero (the VU's 0 * rsqrt(0) = 0 * MAX).
static inline void MotNormalize3(float *v)
{
    float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

    if (len2 > 0.0f)
    {
        float q = 1.0f / sqrtf(len2);

        v[0] *= q;
        v[1] *= q;
        v[2] *= q;
    }
    else
    {
        v[0] = v[1] = v[2] = 0.0f;
    }
}

// x+y+z of a * b (vmul.xyz + vadday.x / vmaddz.x against vf1)
static inline float MotDot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// out.xyz = a x b, out.w = a.w (vopmula.xyz / vopmsub.xyz; out may be a)
static inline void MotCross3(float *out, const float *a, const float *b)
{
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];

    out[3] = a[3];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// out = a - b (vsub.xyzw)
static inline void MotSub4(float *out, const float *a, const float *b)
{
    for (int i = 0; i < 4; i++)
        out[i] = a[i] - b[i];
}

// v = m * v (vmulax / vmadday / vmaddaz / vmaddw.xyzw)
static inline void ClothApply(float *v, sceVu0FMATRIX m)
{
    sceVu0ApplyMatrix(v, m, v);
}

// One link from point a to point b, its rest length their distance now.
static void ClothSetLink(CLOTH_LINK *l, MDL_CLD_BODY *pt, int a, int b)
{
    l->a    = (u_short)a;
    l->b    = (u_short)b;
    l->len  = ClothDist(pt[a].pos, pt[b].pos);
    l->kind = 0;
}

// ──────────────────────────────────────────────────────────────────────
// The mesh's rest vertices copied, the parameters, the points and their
// links.
// FF3 0x00214ce8 (MotionModelBuilder::BuildTexture; arg is not read).
// callcheck: VertexMotionInit, ClothDataGetCloth, ModelObjectGetVectorInfo, ClothMotionCopyParam, ClothMotionCountLink, ClothMotionInitPoint, ClothMotionInitLink (members)

void ClothMotion::Init(ModelObject *obj, ClothData *data, int arg, int no)
{
    PHEAD         *info;
    sceVu0FVECTOR *src;
    int            n;
    int            i;

    (void)arg;
    cloth_obj = obj;
    VertexMotion::Init(obj, no);
    this->data = data;
    param = CopyParam();

    info = cloth_obj->GetVectorInfo(((CLOTH_HEAD *)this->data->GetCloth(this->no))->mesh);
    n    = param->num_vtx;
    own  = (sceVu0FVECTOR *)heapCtrlMalloc(GetSystemHeapWrkP(), n << 4, 0, 0, 0);
    vbuf = own;
    src  = SgdPtr<sceVu0FVECTOR>(info->pUniqVertex);
    for (i = 0; i < n; i++)
        MotCopyVec(vbuf[i], src[i]);

    num_point = param->num_vtx;
    num_link  = CountLink();
    point = (MDL_CLD_BODY *)heapCtrlMalloc(GetSystemHeapWrkP(), num_point * 0x50, 0, 0, 0);
    link  = (CLOTH_LINK *)heapCtrlMalloc(GetSystemHeapWrkP(), num_link * 0xc, 0, 0, 0);
    InitPoint();
    InitLink();
}

// ──────────────────────────────────────────────────────────────────────
// A heap CLOTH_PARAM from the cloth's file parameters. A preset (< 3)
// replaces the stiffness, damping and gravity -- ROM quirk: the preset is a
// function-local static, so the first cloth that has one fixes it for every
// later cloth (the gp byte 0x198 guards the pointer at gp 0x1a0).
// FF3 0x00214e28
// callcheck: ClothDataGetCloth, ClothDataGetParam, ModelObjectGetVectorInfo (members)

CLOTH_PARAM *ClothMotion::CopyParam()
{
    CLOTH_PARAM      *p;
    CLOTH_FILE_PARAM *src;
    PHEAD            *info;
    u_int             n;
    int               i;

    p    = (CLOTH_PARAM *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(CLOTH_PARAM), 0, 0, 0);
    info = cloth_obj->GetVectorInfo(((CLOTH_HEAD *)data->GetCloth(no))->mesh);
    src  = (CLOTH_FILE_PARAM *)data->GetParam(no);

    p->type    = src->type;
    n          = (info->pUniqNormal - info->pUniqVertex) >> 4;
    p->num_vtx = (int)n;
    p->w       = src->w;
    p->h       = src->h;
    p->layer   = (int)n / (src->w * src->h);
    for (i = 0; i < 2; i++)
        p->bone[i] = src->bone[i];
    p->stiff[0]   = src->stiff[0];
    p->stiff[1]   = src->stiff[1];
    p->damp       = src->damp;
    p->angle      = src->angle;
    p->mask       = src->mask;
    p->gravity[0] = src->gravity[0];
    p->gravity[1] = src->gravity[1];
    p->gravity[2] = src->gravity[2];
    p->gravity[3] = 0.0f;

    if (src->preset < 3)
    {
        static const CLOTH_PRESET *preset = &cloth_preset[src->preset];

        p->stiff[0] = preset->stiff[0];
        p->stiff[1] = preset->stiff[1];
        p->damp     = preset->damp;
        MotCopyVec(p->gravity, preset->gravity);
    }
    return p;
}

// ──────────────────────────────────────────────────────────────────────
// The links InitLink makes: the grid's vertical and horizontal neighbours
// of every layer, and the wrap of the tube / two-bone types.
// FF3 0x00215208

int ClothMotion::CountLink()
{
    CLOTH_PARAM *p = param;
    int          n;

    n = ((p->w - 1) * p->h + p->w * (p->h - 1)) * p->layer;
    switch (p->type)
    {
    case CLOTH_TYPE_TUBE:
    case CLOTH_TYPE_TUBE_F:
        return n + p->w;
    case CLOTH_TYPE_TWO_BONE:
    case CLOTH_TYPE_TWO_BONE_F:
        return n + p->h;
    default:
        return n;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Every point at its rest vertex, still; the param's pinned columns (counted
// from the right) pinned in every row and layer, and for the two-bone types
// the pinned points below the middle row follow bone 1.
// FF3 0x00214fd0
// callcheck: ClothDataGetParam (member)

void ClothMotion::InitPoint()
{
    CLOTH_FILE_PARAM *src;
    CLOTH_PARAM      *p;
    MDL_CLD_BODY     *pt;
    int               h;
    int               w;
    int               i;
    int               layer;
    int               base;
    int               row;
    int               col;
    int               k;

    src = (CLOTH_FILE_PARAM *)data->GetParam(no);
    p   = param;
    h   = p->h;
    w   = p->w;

    for (i = 0; i < num_point; i++)
    {
        pt = &point[i];
        MotCopyVec(pt->pos, vbuf[i]);
        MotCopyVec(pt->prev, pt->pos);
        MotCopyVec(pt->move, cloth_zero);
        MotCopyVec(pt->force, cloth_zero);
        pt->off  = 0;
        pt->bone = 0;
        pt->mask = param->mask;
        pt->r    = 0.0f;
    }

    for (layer = 0, base = 0; layer < p->layer; layer++, base += h * w)
    {
        for (row = 0; row < p->h; row++)
        {
            for (col = 0; col < p->w; col++)
            {
                for (k = 0; k < src->num_pin; k++)
                {
                    if (p->w - col - 1 != src->pin[k])
                        continue;
                    i = p->w * row + base + col;
                    point[i].off = 1;
                    if ((p->type == CLOTH_TYPE_TWO_BONE || p->type == CLOTH_TYPE_TWO_BONE_F)
                        && p->h / 2 < row)
                        point[i].bone = 1;
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The links, in CountLink's order: per layer the vertical neighbours, then
// the horizontal ones; then the tube's first-to-last-row links or the
// two-bone types' layer-to-layer links of the first column (made once, from
// layer 0).
// FF3 0x00215278

void ClothMotion::InitLink()
{
    CLOTH_PARAM *p = param;
    int          w    = p->w;
    int          h1   = p->h - 1;
    int          wh   = p->h * w;
    int          last = w * h1;
    int          n    = 0;
    int          layer;
    int          base;
    int          row;
    int          col;
    int          i;

    for (layer = 0, base = 0; layer < p->layer; layer++, base += wh)
    {
        for (row = 0; row < p->h; row++)
        {
            for (col = 0; col < p->w; col++)
            {
                i = p->w * row + base + col;
                if (row < h1)
                    ClothSetLink(&link[n++], point, i, i + p->w);
            }
        }
        for (row = 0; row < p->h; row++)
        {
            for (col = 0; col < p->w; col++)
            {
                i = p->w * row + base + col;
                if (col < w - 1)
                    ClothSetLink(&link[n++], point, i, i + 1);
            }
        }
    }

    switch (p->type)
    {
    case CLOTH_TYPE_TUBE:
    case CLOTH_TYPE_TUBE_F:
        for (col = 0; col < param->w; col++)
            ClothSetLink(&link[n++], point, col, col + last);
        break;
    case CLOTH_TYPE_TWO_BONE:
    case CLOTH_TYPE_TWO_BONE_F:
        for (row = 0; row < param->h; row++)
            ClothSetLink(&link[n++], point, param->w * row, param->w * row + wh);
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// One step of the solver.
//  1. The bones' world matrices (bone 1's turned by param.angle about z,
//     bone 0's local translation added), their rotations, inverses and
//     inverse rotations; on the first step since a reset they are also the
//     "last step" ones. mat[CLOTH_MAT_BONE1] = bone 1 in bone 0's space.
//  2. Once settled and colliding: a root jump past 3.2 (types 3..5) / 10
//     units, or a turn of the root's xz direction past 0.3 rad, requests a
//     reset and the step stops.
//  3. The points to the world with the last step's matrices (pinned ones
//     with the new ones), half the root's movement added for types 3..5,
//     wind, gravity, velocity; two link passes (CalcLink), each followed by
//     the collision registry when colliding; back to bone space with the
//     new matrices, pinned points at their rest vertices, the velocity from
//     the move, damped (y by 0.88 more).
// FF3 0x002156c8. VU0: the dots are vmul.xyz + vadday.x / vmaddz.x against
// vf1 (a plain x+y+z); the matrix products are sceVu0MulMatrix /
// sceVu0ApplyMatrix spelled inline; masks as in the ROM (xyz adds and
// scales, xyzw subtractions where noted).
// callcheck: ClothDataGetCloth, ModelObjectGetCoord, MotionGetStep, ClothMotionCalcWind, ClothMotionCalcLink (members); ModelCollisionSetHitCheck (model_collision_set member)

void ClothMotion::Calc(int collide)
{
    // PC: zeroed -- with one bone FF3 leaves the bone-1 halves as stack
    // garbage (stored into mat, never read).
    sceVu0FMATRIX  bmat[2] = {};        // sp+0x000: world
    sceVu0FMATRIX  rmat[2] = {};        // sp+0x080: rotation (row 3 = 0, 0, 0, 1)
    sceVu0FMATRIX  inv[2]  = {};        // sp+0x100: world inverse
    sceVu0FMATRIX  rinv[2] = {};        // sp+0x180: rotation inverse (transpose)
    sceVu0FMATRIX  rot;                 // sp+0x200
    sceVu0FMATRIX  unit;                // data 0x3293c0
    sceVu0FVECTOR  dir;
    sceVu0FVECTOR  shift;
    SGDCOORDINATE *coord;
    CLOTH_PARAM   *p;
    MDL_CLD_BODY  *pt;
    int            nbone;
    int            follow;
    int            pass;
    int            i;
    float          limit;

    if (num_point <= 0 || calc_off != 0)
        return;
    sceVu0UnitMatrix(unit);

    nbone = 1;
    coord = cloth_obj->GetCoord(((CLOTH_HEAD *)data->GetCloth(no))->mesh);
    p     = param;
    follow = (u_int)(p->type - CLOTH_TYPE_SHEET_F) < 3;
    if (p->type == CLOTH_TYPE_TWO_BONE || p->type == CLOTH_TYPE_TWO_BONE_F)
        nbone = 2;

    for (i = 0; i < nbone; i++)
    {
        sceVu0CopyMatrix(bmat[i], coord[param->bone[i]].matLocalWorld);
        if (i > 0)
        {
            sceVu0RotMatrixZ(rot, unit,
                             NormalizeAngle(param->angle * 3.1415927f / 180.0f));  // sdata 0x37134c / 0x371350
            ClothAddXYZ(rot[3], coord[param->bone[0]].matCoord[3]);
            sceVu0MulMatrix(bmat[i], bmat[i], rot);
        }
        sceVu0CopyMatrix(rmat[i], bmat[i]);
        MotCopyVec(rmat[i][3], cloth_w1);    // sqc2 vf0
        sceVu0InversMatrix(inv[i], bmat[i]);
        sceVu0TransposeMatrix(rinv[i], rmat[i]);
        if (stepped == 0)
        {
            sceVu0CopyMatrix(mat[CLOTH_MAT_WORLD + i], bmat[i]);
            sceVu0CopyMatrix(mat[CLOTH_MAT_ROT + i], rmat[i]);
            sceVu0CopyMatrix(mat[CLOTH_MAT_INV + i], inv[i]);
            sceVu0CopyMatrix(mat[CLOTH_MAT_RINV + i], rinv[i]);
        }
    }
    sceVu0MulMatrix(mat[CLOTH_MAT_BONE1], inv[0], bmat[1]);

    if (settled != 0 && collide != 0)
    {
        limit = follow ? 3.2f : 10.0f;              // sdata 0x371354 / 0x371358
        if (stepped != 0 && ClothDist(bmat[0][3], vec[0]) > limit)
            reset_req = 1;
        MotCopyVec(vec[0], bmat[0][3]);

        // The root's direction on the xz plane, w kept (the inline normalise:
        // vrsqrt + vmul.w vd, vs, vf0).
        MotCopyVec(dir, bmat[0][3]);
        dir[1] = 0.0f;
        MotNormalize3(dir);
        if (stepped != 0 && Vu0Acosf(MotDot3(dir, vec[1])) > 0.3f)     // sdata 0x37135c
            reset_req = 1;
        MotCopyVec(vec[1], dir);
    }

    if (reset_req != 0)
        return;

    // To the world with the last step's matrices (pinned points: the new).
    for (i = 0; i < num_point; i++)
    {
        pt = &point[i];
        if (pt->off != 0)
            ClothApply(pt->pos, bmat[pt->bone]);
        else
            ClothApply(pt->pos, mat[CLOTH_MAT_WORLD + pt->bone]);
        ClothApply(pt->prev, mat[CLOTH_MAT_WORLD + pt->bone]);
        ClothApply(pt->move, mat[CLOTH_MAT_ROT + pt->bone]);
        MotCopyVec(pt->force, cloth_zero);
    }

    if (settled != 0 && follow)
    {
        for (i = 0; i < 4; i++)
            shift[i] = (bmat[0][3][i] - vec[2][i]) * 0.5f;          // vsub.xyzw, vmulx.xyzw
        for (i = 0; i < num_point; i++)
        {
            ClothAddXYZ(point[i].pos, shift);
            ClothAddXYZ(point[i].prev, shift);
        }
    }
    MotCopyVec(vec[2], bmat[0][3]);

    CalcWind();
    for (i = 0; i < num_point; i++)
    {
        pt = &point[i];
        if (pt->off != 0)
            continue;
        ClothAddXYZ(pt->force, param->gravity);
        if (GetStep() < 2.0f)                       // sdata 0x371360
            ClothScaleXYZ(pt->force, GetStep());
        ClothAddXYZ(pt->move, pt->force);
        ClothAddXYZ(pt->pos, pt->move);
    }

    for (pass = 1; pass >= 0; pass--)
    {
        CalcLink();
        for (i = 0; i < num_point; i++)
        {
            if (point[i].off != 0)
                continue;
            if (GetStep() < 2.0f)                   // sdata 0x371360
                ClothScaleXYZ(point[i].force, GetStep());
            ClothAddXYZ(point[i].pos, point[i].force);
        }
        if (collide != 0)
        {
            for (i = 0; i < num_point; i++)
            {
                if (point[i].off == 0)
                    model_collision_set.HitCheck(&point[i], 0);
            }
        }
    }

    // Back to bone space with the new matrices.
    for (i = 0; i < num_point; i++)
    {
        pt = &point[i];
        ClothApply(pt->pos, inv[pt->bone]);
        ClothApply(pt->prev, inv[pt->bone]);
        ClothApply(pt->move, rinv[pt->bone]);
        if (pt->off != 0)
        {
            pt->pos[0] = vbuf[i][0];
            pt->pos[1] = vbuf[i][1];
            pt->pos[2] = vbuf[i][2];
        }
        for (int c = 0; c < 4; c++)
            pt->move[c] = pt->pos[c] - pt->prev[c];                 // vsub.xyzw
        ClothScaleXYZ(pt->move, param->damp);
        pt->move[1] *= 0.88f;                       // sdata 0x371364
        MotCopyVec(pt->prev, pt->pos);
    }

    // The new matrices become the last step's. ROM: the rotations, not the
    // inverse rotations, go into the CLOTH_MAT_RINV pair here.
    for (i = 0; i < 2; i++)
    {
        sceVu0CopyMatrix(mat[CLOTH_MAT_WORLD + i], bmat[i]);
        sceVu0CopyMatrix(mat[CLOTH_MAT_ROT + i], rmat[i]);
        sceVu0CopyMatrix(mat[CLOTH_MAT_INV + i], inv[i]);
        sceVu0CopyMatrix(mat[CLOTH_MAT_RINV + i], rmat[i]);
    }
    stepped = 1;
}

// ──────────────────────────────────────────────────────────────────────
// The gust cycle: wind_cycle frames (wind_period stretched by 0.85..1.15 at
// each wrap), a sine rise over wind_rise frames, calm until wind_fall, then
// a sine fall at 0.3 of the power. The force along wind_dir goes onto each
// free point with a 1-in-2 chance.
// FF3 0x002bdd90 (out-of-line inline after the motion vtables)

void ClothMotion::CalcWind()
{
    sceVu0FVECTOR f;
    float         s;
    short         third;
    short         half;
    int           i;

    if (wind == 0 || wind_power <= 0.0f || wind_period <= 0)
        return;

    wind_frame = (short)((wind_frame + 1) % wind_cycle);     // ROM: break 7 when wind_cycle is 0
    if (wind_frame == 0)
    {
        wind_cycle = (short)(int)((float)wind_period * (GetRandValF(0.3f) + 0.85f));  // sdata 0x37136c / 0x371370
        third      = (short)(wind_cycle / 3);
        half       = (short)(third / 2);
        wind_rise  = (short)(third + (GetRandValIByF(third) - half));
        wind_fall  = (short)(wind_cycle - (third + (GetRandValIByF(third) - half)));
    }

    s = 0.0f;
    if (wind_frame < wind_rise)
    {
        if (wind_rise > 0)
            s = wind_power * Vu0Sinf((float)wind_frame * 3.1415927f / (float)wind_rise);   // sdata 0x371374
    }
    else
    {
        if (wind_frame < wind_fall)
            return;
        if ((float)(wind_cycle - wind_fall) != 0.0f)
            s = wind_power * 0.3f
              * Vu0Sinf((float)(wind_cycle - wind_frame) * 3.1415927f / (float)(wind_cycle - wind_fall));
    }

    f[0] = wind_dir[0] * s;                         // vmulx.xyz
    f[1] = wind_dir[1] * s;
    f[2] = wind_dir[2] * s;
    f[3] = 0.0f;                                    // PC: stack garbage in FF3, not read
    for (i = 0; i < num_point; i++)
    {
        if (point[i].off != 0)
            continue;
        if (GetRandValIByF(2) != 0)
            ClothAddXYZ(point[i].force, f);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Every force cleared, then each link pushes its ends toward its rest
// length (stiffness by kind); a link stretched past 1.3 times halves both
// ends' forces after its push.
// FF3 0x002bdba0 (out-of-line inline after the motion vtables)

void ClothMotion::CalcLink()
{
    CLOTH_LINK   *l;
    MDL_CLD_BODY *a;
    MDL_CLD_BODY *b;
    float         d[4];
    float         len;
    float         s;
    int           i;

    for (i = 0; i < num_point; i++)
        MotCopyVec(point[i].force, cloth_zero);

    for (i = 0; i < num_link; i++)
    {
        l   = &link[i];
        a   = &point[l->a];
        b   = &point[l->b];
        d[0] = a->pos[0] - b->pos[0];
        d[1] = a->pos[1] - b->pos[1];
        d[2] = a->pos[2] - b->pos[2];
        len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        s   = Ps2Divf(param->stiff[l->kind] * (l->len - len), len);    // PC: PS2 division
        ClothScaleXYZ(d, s);                        // vmulx.xyz
        ClothAddXYZ(a->force, d);
        b->force[0] -= d[0];                        // vsub.xyz
        b->force[1] -= d[1];
        b->force[2] -= d[2];
        if (l->len * 1.3f < len)                    // sdata 0x371368
        {
            for (int c = 0; c < 4; c++)             // vmulx.xyzw
            {
                a->force[c] *= 0.5f;
                b->force[c] *= 0.5f;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The points into the mesh's unique vertices: bone 1's through
// mat[CLOTH_MAT_BONE1], free bone 0 ones as they are; pinned bone 0 points
// keep the vertex.
// FF3 0x002161d8
// callcheck: ClothDataGetCloth, ModelObjectGetVectorInfo (members)

void ClothMotion::WriteVertex()
{
    PHEAD         *info;
    sceVu0FVECTOR *dst;
    int            i;

    info = cloth_obj->GetVectorInfo(((CLOTH_HEAD *)data->GetCloth(no))->mesh);
    dst  = SgdPtr<sceVu0FVECTOR>(info->pUniqVertex);
    for (i = 0; i < num_point; i++)
    {
        if (point[i].bone != 0)
            sceVu0ApplyMatrix(dst[i], mat[CLOTH_MAT_BONE1], point[i].pos);
        else if (point[i].off == 0)
            MotCopyVec(dst[i], point[i].pos);
    }
    written = 1;
}

// ──────────────────────────────────────────────────────────────────────
// The first call settles the cloth (25 steps on PAL, 30 on NTSC: a second,
// no collision) and draws nothing. Then one colliding step a frame --
// skipped once drawn while the in-game draw is off (draw_en_371348 0) for
// a cloth with mask bit 2 --, a requested reset done (and resettled unless
// a movie phase shows a type 2 cloth), and the mesh written.
// FF3 0x002162b8 (vtable +0x18)
// callcheck: ClothMotionCalc, ClothMotionReset (members); ClothMotionWriteVertex (a tail jump in FF3)

void ClothMotion::Pose()
{
    int i;

    if (settled == 0)
    {
        for (i = 0;; i++)
        {
            if (GetPALMode() != 0)
            {
                if (i > 24)
                    break;
            }
            else if (i > 29)
            {
                break;
            }
            Calc(0);
        }
        settled = 1;
        return;
    }

    if (written != 0 && draw_en_371348 == 0 && (param->mask & 2) != 0)
        return;

    Calc(1);
    // PC: a cloth whose points went non-finite starts over (the PS2 cannot
    // make a NaN; see Ps2Divf()).
    for (i = 0; i < num_point && reset_req == 0; i++)
    {
        if (!isfinite(point[i].pos[0]) || !isfinite(point[i].pos[1]) ||
            !isfinite(point[i].pos[2]))
            reset_req = 1;
    }
    if (reset_req != 0)
    {
        Reset();
        reset_req = 0;
        if (IsMoviePhase() == 0 || param->type != CLOTH_TYPE_TWO_BONE)
        {
            for (i = 0;; i++)
            {
                if (GetPALMode() != 0)
                {
                    if (i > 24)
                        break;
                }
                else if (i > 29)
                {
                    break;
                }
                Calc(0);
            }
        }
    }
    WriteVertex();
}

// ──────────────────────────────────────────────────────────────────────
// The mesh's vertices and the points back to the rest shape, still; the
// next step starts from fresh matrices.
// FF3 0x00216410 (the destructors, Stop, Pose, MotionModelManager::ResetCloth)
// callcheck: ClothDataGetCloth, ModelObjectGetVectorInfo (members)

void ClothMotion::Reset()
{
    PHEAD         *info;
    sceVu0FVECTOR *dst;
    MDL_CLD_BODY  *pt;
    u_int          n;
    int            i;

    info = cloth_obj->GetVectorInfo(((CLOTH_HEAD *)data->GetCloth(no))->mesh);
    dst  = SgdPtr<sceVu0FVECTOR>(info->pUniqVertex);
    n    = (info->pUniqNormal - info->pUniqVertex) >> 4;
    for (i = 0; i < (int)n; i++)
        MotCopyVec(dst[i], vbuf[i]);
    for (i = 0; i < (int)n; i++)
    {
        pt = &point[i];
        MotCopyVec(pt->pos, vbuf[i]);
        MotCopyVec(pt->prev, pt->pos);
        MotCopyVec(pt->move, cloth_zero);
        MotCopyVec(pt->force, cloth_zero);
    }
    stepped = 0;
}

// ══════════════════════════════════════════════════════════════════════
// ModelObject bone turns (IK), beside the motions in FF3's link order
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Two-bone IK: root and mid turned about their z axes so end reaches the
// world point target. The limb is solved in the plane the root's yaw (row 1
// on the xz plane against +x) is taken out of: the xy lengths of the two
// bones and of the target, the law of cosines, the sign from which side of
// +y the target and the upper bone lie (against the root's row 2). Then
// end's coordinates are recalculated. Returns 1.
// FF3 0x002165d8 (in motion.c's range; caller 0x1466e8, char_model.c)
// callcheck: ModelObjectGetCoord (member); sgdClearCoordCalcFlgParents, sgdCalcCoordinateMatrix

int ModelObject::IkLimb(float *target, int root, int mid, int end)
{
    static const sceVu0FVECTOR w1 = { 0.0f, 0.0f, 0.0f, 1.0f };     // data 0x329500
    static const sceVu0FVECTOR ax = { 1.0f, 0.0f, 0.0f, 0.0f };     // data 0x329440
    static const sceVu0FVECTOR ay = { 0.0f, 1.0f, 0.0f, 0.0f };     // data 0x329450
    SGDCOORDINATE *c0;
    SGDCOORDINATE *c1;
    SGDCOORDINATE *c2;
    sceVu0FVECTOR  o;
    sceVu0FVECTOR  v1;
    sceVu0FVECTOR  v2;
    sceVu0FVECTOR  a;
    sceVu0FVECTOR  b;
    sceVu0FVECTOR  t;
    sceVu0FVECTOR  f;
    sceVu0FVECTOR  x;
    sceVu0FVECTOR  n;
    sceVu0FMATRIX  r;
    sceVu0FMATRIX  unit;                // data 0x3293c0
    float          yaw;
    float          la;
    float          lb;
    float          tx2;
    float          ty2;
    float          tang;
    float          sign;
    float          alpha;
    float          beta;
    float          up;
    float          bend;
    float          rot0;
    float          rot1;

    c0 = &GetCoord(0)[root];
    c1 = &GetCoord(0)[mid];
    c2 = &GetCoord(0)[end];
    sceVu0UnitMatrix(unit);

    MotCopyVec(o, c0->matLocalWorld[3]);
    MotSub4(v1, c1->matLocalWorld[3], o);
    MotSub4(v2, c2->matLocalWorld[3], o);
    MotSub4(a, v1, w1);                 // the upper bone
    MotSub4(b, v2, v1);                 // the lower bone
    MotSub4(t, target, o);

    MotCopyVec(f, c0->matLocalWorld[1]);
    f[1] = 0.0f;
    MotNormalize3(f);
    yaw = Vu0Acosf(MotDot3(ax, f));
    if (f[2] < 0.0f)
        yaw = -yaw;
    sceVu0RotMatrixY(r, unit, NormalizeAngle(yaw));
    sceVu0ApplyMatrix(t, r, t);
    sceVu0ApplyMatrix(a, r, a);
    sceVu0ApplyMatrix(b, r, b);
    sceVu0ApplyMatrix(x, r, c0->matLocalWorld[2]);

    la   = sqrtf(a[0] * a[0] + a[1] * a[1]);
    a[2] = 0.0f;
    b[2] = 0.0f;
    lb   = sqrtf(b[0] * b[0] + b[1] * b[1]);
    tx2  = t[0] * t[0];
    ty2  = t[1] * t[1];
    tang = Vu0Acosf(Ps2Divf(t[1], sqrtf(tx2 + ty2)));     // PC: PS2 division

    MotCross3(n, ay, t);
    sign = 1.0f;                        // sdata 0x371378
    if (0.0f <= MotDot3(n, x))
        sign = -1.0f;                   // sdata 0x37137c
    alpha = Vu0Acosf((la * la + tx2 + ty2 - lb * lb) / ((la + la) * sqrtf(tx2 + ty2)));
    rot0  = sign * tang - alpha;
    beta  = Vu0Acosf((la * la + lb * lb - tx2 - ty2) / ((la + la) * lb));
    rot1  = 3.1415927f - beta;          // sdata 0x371380

    // The bones' present angles: the upper bone against +y, the knee.
    MotNormalize3(a);
    MotNormalize3(b);
    up   = Vu0Acosf(MotDot3(ay, a));
    bend = Vu0Acosf(MotDot3(a, b));
    MotCross3(n, ay, a);
    if (!(MotDot3(n, x) < 0.0f))
        up = -up;

    sceVu0RotMatrixZ(r, unit, NormalizeAngle(-(rot0 - up)));
    sceVu0MulMatrix(c0->matCoord, c0->matCoord, r);
    sceVu0RotMatrixZ(r, unit, NormalizeAngle(-(rot1 - bend)));
    sceVu0MulMatrix(c1->matCoord, c1->matCoord, r);
    sgdClearCoordCalcFlgParents(c2);
    sgdCalcCoordinateMatrix(c2);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CCD IK: from tip's parent up the chain to stop (101 joints at most), each
// joint turned about its own world axis (0 x, 1 y, 2 z) so the tip points
// at target, when the two directions are more than about 0.26 degrees
// apart (cosine under 0.99999); the bone coordinates are recalculated after
// each turn. Returns 1.
// FF3 0x00216c40 (in motion.c's range; caller 0x146ac8, char_model.c)
// callcheck: ModelObjectGetCoord, ModelObjectCalcBoneCoordinate (members)

int ModelObject::IkChain(float *target, int tip, int stop, int axis)
{
    SGDCOORDINATE *c;
    SGDCOORDINATE *last;
    float         *tip_pos;
    sceVu0FVECTOR  tp;
    sceVu0FVECTOR  jp;
    sceVu0FVECTOR  a;
    sceVu0FVECTOR  b;
    sceVu0FVECTOR  n;
    sceVu0FMATRIX  r;
    sceVu0FMATRIX  unit;                // data 0x3293c0
    float          d;
    float          ang;
    int            i;

    c       = &GetCoord(0)[tip];
    last    = &GetCoord(0)[stop];
    tip_pos = c->matLocalWorld[3];
    sceVu0UnitMatrix(unit);

    for (i = 0;; i++)
    {
        MotCopyVec(tp, tip_pos);
        c = SgdPtr<SGDCOORDINATE>(c->pParent);
        if (c == nullptr)
            return 1;
        MotCopyVec(jp, c->matLocalWorld[3]);
        MotSub4(a, tp, jp);
        MotNormalize3(a);
        MotSub4(b, target, jp);
        MotNormalize3(b);
        d = MotDot3(b, a);
        if (d < 0.99999f)               // sdata 0x371384
        {
            MotCross3(n, a, b);
            ang = Vu0Acosf(d);
            if (MotDot3(n, c->matLocalWorld[axis]) < 0.0f)
                ang = -ang;
            switch (axis)
            {
            case 0:
                sceVu0RotMatrixX(r, unit, NormalizeAngle(ang));
                break;
            case 1:
                sceVu0RotMatrixY(r, unit, NormalizeAngle(ang));
                break;
            case 2:
                sceVu0RotMatrixZ(r, unit, NormalizeAngle(ang));
                break;
            default:
                sceVu0CopyMatrix(r, unit);  // PC: FF3 multiplies by stack garbage (callers pass 0..2)
                break;
            }
            sceVu0MulMatrix(c->matCoord, c->matCoord, r);
            CalcBoneCoordinate();
        }
        if (c == last || i >= 100)
            break;
    }
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// Quaternions
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Slerp from q0 to q1 by t, the short way round (q1 negated when the 4D dot
// is negative); q0 when the two are parallel (1 - d^2 == 0).
// FF3 0x002bd9a0. VU0: the dot is vmul + vadday / vmaddaz / vmaddw against
// vf1 = (1, 1, 1, -1) (hazards.md "VU0 macros that read vf1"): a plain x+y+z+w.

float *motQuatSlerp(float t, float *out, float *q0, float *q1)
{
    sceVu0FVECTOR b;
    float         d;
    float         s;
    float         th;
    float         w0;
    float         w1;
    int           i;

    d = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
    if (d < 0.0f)
    {
        for (i = 0; i < 4; i++)
            b[i] = q1[i] * -1.0f;       // sdata 0x371338
        d = q0[0] * b[0] + q0[1] * b[1] + q0[2] * b[2] + q0[3] * b[3];
    }
    else
    {
        MotCopyVec(b, q1);
    }

    if (1.0f - d * d == 0.0f)           // sdata 0x37133c
    {
        MotCopyVec(out, q0);
        return out;
    }

    s  = sqrtf(1.0f - d * d);
    th = Vu0Acosf(d);
    w1 = Vu0Sinf(th * t) / s;
    w0 = Vu0Sinf(th - th * t) / s;
    for (i = 0; i < 4; i++)
        out[i] = q0[i] * w0 + b[i] * w1;
    return out;
}

// ──────────────────────────────────────────────────────────────────────
// Unit quaternion (x, y, z, w) -> rotation matrix, row 3 = (0, 0, 0, 1).
// FF3 0x002bb108; from MioPan graphics/motion/motion.c motQuaternion2Matrix
// (FF2's formula, the same stores).

void motQuaternion2Matrix(float (*m)[4], float *q)
{
    float x, y, z, w;
    float x2, y2, z2;
    float xx2, wx2;

    x = q[0];
    y = q[1];
    z = q[2];
    w = q[3];

    x2 = x + x;
    y2 = y + y;
    z2 = z + z;

    xx2 = x * x2;
    wx2 = w * x2;

    m[0][0] = 1.0f - (y * y2 + z * z2);
    m[0][1] = x * y2 - w * z2;
    m[0][2] = x * z2 + w * y2;
    m[0][3] = 0.0f;

    m[1][0] = x * y2 + w * z2;
    m[1][1] = 1.0f - (xx2 + z * z2);
    m[1][2] = y * z2 - wx2;
    m[1][3] = 0.0f;

    m[2][0] = x * z2 - w * y2;
    m[2][1] = y * z2 + wx2;
    m[2][2] = 1.0f - (xx2 + y * y2);
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;                     // sdata 0x370a64
}
