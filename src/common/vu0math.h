/* ==========================================================================
 *  common/vu0math.h
 *
 *  FF3's maths helpers (FF3-only object, 0x1ef370..0x1efcf0, between
 *  my_malloc.c and cpp_heap.c). FF2 spread these over utility.c (GetRandValF),
 *  unit_ctl.c (RotLimitChk, GetTrgtRot) and g3dMath.c (g3dAtan2f, on libm);
 *  FF3 gathers them and replaces libm's sin/cos/atan/acos with short VU0
 *  macro-mode polynomials.
 *
 *  Angles are radians. The rotation helpers take the angle as a float (f12
 *  on the EE, so its place in the prototype is not observable) and the
 *  vectors as quadwords.
 * ======================================================================== */
#ifndef _COMMON_VU0MATH_H
#define _COMMON_VU0MATH_H

#include "sdk/libvu0.h"

#include <float.h>                  // FLT_MAX
#include <math.h>                   // sqrtf, signbit

// PC: division as the PS2 does it.  The EE's FPU and the VU have no infinity
// or NaN: x / 0 is the largest float, signed as the operands' signs say, and
// so is rsqrt(0).  IEEE gives infinity, and infinity times the zero vector a
// zero length belongs to is NaN, which a chain, a cloth or a position then
// keeps for good -- the PS2 gets zero.  These give the PS2's answer.
static inline float Vu0Rsqrt(float x)   // vrsqrt: 1 / sqrt(x)
{
    return x > 0.0f ? 1.0f / sqrtf(x) : FLT_MAX;
}

static inline float Ps2Divf(float a, float b)   // div.s / vdiv: a / b
{
    if (b != 0.0f)
        return a / b;
    return (signbit(a) != signbit(b)) ? -FLT_MAX : FLT_MAX;
}

// Rotate src about one axis by rot (normalised first) into dst.
void  RotVectorX(float rot, float *dst, float *src);
void  RotVectorY(float rot, float *dst, float *src);
void  RotVectorZ(float rot, float *dst, float *src);
// Copies in to out, then Z, X and Y by rot[2], rot[0], rot[1] (each of
// them rotates `in`: see vu0math.c).
void  RotVectorZXY(float *out, float *in, float *rot);

float NormalizeAngle(float fAngle);     // into (-2pi, 2pi), sign kept
float Vu0Atanf(float x);
float g3dAtan2f(float fX, float fY);    // atan2(fX, fY), in (-pi, pi]
float Vu0Sinf(float x);
float Vu0Cosf(float x);
float Vu0Acosf(float x);
float LogBasef(float base, float x);    // log_base(x); no callers
int   IntLogBase(int base, int n);      // k with base^k == n

// Cubic curves through quadwords; no callers in the retail ELF.
void  HermiteVector(float t, float *out, float *p0, float *p1, float *m0, float *m1);
void  BSplineVector(float t, float *out, float *p0, float *p1, float *p2, float *p3);

float GetRandValF(float max);           // [0, max]
int   GetRandValIByF(int max);          // (int)GetRandValF(max)

void  RotLimitChk(float *rot);          // wraps one step into [-pi, pi]
// Rotation that turns p0 towards p1: id bit 1 -> pitch into rot[0],
// bit 2 -> yaw into rot[1]; each wrapped by RotLimitChk.
void  GetTrgtRot(float *p0, float *p1, float *rot, int id);

#endif /* _COMMON_VU0MATH_H */
