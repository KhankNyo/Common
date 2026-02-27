
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef LINEAR_ALGEBRA_H
#define LINEAR_ALGEBRA_H

#include <math.h>
#include "Common.h"

#ifdef ARCH_X86_HAS_COMPILER_INTRINSICS
#  include <x86intrin.h>
#endif

#ifndef LA_F32_COMPARE_EPSILON
#  include <float.h>
#  define LA_F32_COMPARE_EPSILON FLT_EPSILON
#endif

#if defined(LA_DEPTH_ZERO_TO_ONE)
#  define LA_DEPTH 1.0f
#else
#  define LA_DEPTH 2.0f
#endif

#define LA_F32_IS_ZERO(val) IN_RANGE(-LA_F32_COMPARE_EPSILON, (val), LA_F32_COMPARE_EPSILON)
#define LA_PI 3.1415926535897932385
#define LA_PIF 3.1415926535897932385f
#define LA_TO_RADIAN(degf64) ((degf64) * (LA_PI / 180.0))
#define LA_TO_RADIANF(degf32) ((degf32) * (LA_PIF / 180.0f))
#define LA_TO_DEGF(radf32) ((radf32) * (180.0f / LA_PIF))

/* NOTE: do not access these fields directly */
#if defined(ARCH_X86_HAS_COMPILER_INTRINSICS)
typedef struct {__m128 _Xmm;} la_vec3f;
typedef struct {__m128 _Xmm;} la_vec4f;
#else
typedef struct { v3f _Internal; }la_vec3f;
typedef struct { v4f _Internal; } la_vec4f;
#endif
typedef struct { la_vec4f Rows[4]; } la_mat4f;

#define Vec3f(...) Vec3f_FromRaw((v3f){__VA_ARGS__})
force_inline v3f Vec3f_ToRaw(la_vec3f V);
force_inline la_vec3f Vec3f_FromRaw(v3f V);
force_inline la_vec3f Vec3f_FromSingle(float Val);
force_inline la_vec3f Vec3f_Cross(la_vec3f A, la_vec3f B);
force_inline la_vec4f Vec3f_ExtendToVec4f(la_vec3f V, float W);
force_inline la_vec3f Vec4f_TruncateToVec3f(la_vec4f V);

force_inline bool32 Vec3f_IsZero(la_vec3f V); /* compared with LA_F32_EPSILON */
force_inline float Vec3f_GetX(la_vec3f V);
force_inline float Vec3f_GetY(la_vec3f V);
force_inline float Vec3f_GetZ(la_vec3f V);
force_inline la_vec3f Vec3f_Normalize(la_vec3f V);
force_inline la_vec3f Vec3f_Dot(la_vec3f A, la_vec3f B);
force_inline la_vec3f Vec3f_Add(la_vec3f A, la_vec3f B);
force_inline la_vec3f Vec3f_Sub(la_vec3f A, la_vec3f B);
force_inline la_vec3f Vec3f_Mul(la_vec3f A, la_vec3f B);
force_inline la_vec3f Vec3f_Div(la_vec3f A, la_vec3f B);
force_inline la_vec3f Vec3f_Sqrt(la_vec3f V);         /* sqrt(V) */
force_inline la_vec3f Vec3f_InvSqrt(la_vec3f V);      /* 1/sqrt(V), NOTE: this is an approximation */
force_inline la_vec3f Vec3f_Mag(la_vec3f V);          /* |V| */

#define Vec4f(...) Vec4f_FromRaw((v4f){__VA_ARGS__})
force_inline v4f Vec4f_ToRaw(la_vec4f V);
force_inline la_vec4f Vec4f_FromRaw(v4f V);
force_inline la_vec4f Vec4f_FromSingle(float Val);

force_inline bool32 Vec4f_IsZero(la_vec4f V); /* compared with LA_F32_EPSILON */
force_inline float Vec4f_GetX(la_vec4f V);
force_inline float Vec4f_GetY(la_vec4f V);
force_inline float Vec4f_GetZ(la_vec4f V);
force_inline float Vec4f_GetW(la_vec4f V);
force_inline la_vec4f Vec4f_Normalize(la_vec4f V);
force_inline la_vec4f Vec4f_Dot(la_vec4f A, la_vec4f B);
force_inline la_vec4f Vec4f_Add(la_vec4f A, la_vec4f B);
force_inline la_vec4f Vec4f_Sub(la_vec4f A, la_vec4f B);
force_inline la_vec4f Vec4f_Mul(la_vec4f A, la_vec4f B);
force_inline la_vec4f Vec4f_Div(la_vec4f A, la_vec4f B);
force_inline la_vec4f Vec4f_Sqrt(la_vec4f V);
force_inline la_vec4f Vec4f_InvSqrt(la_vec4f V);
force_inline la_vec4f Vec4f_Mag(la_vec4f V);

#define Mat4f(...) Mat4f_FromRaw(&(m4f) {.At = {__VA_ARGS__}})
#define Mat4f_Rows(...) Mat4f_FromRaw(&(m4f) {.Rows = {__VA_ARGS__}})
#define Mat4f_Diagonal(...) Mat4f_DiagonalVec(Vec4f(__VA_ARGS__))
force_inline la_mat4f Mat4f_FromVec(la_vec4f Row0, la_vec4f Row1, la_vec4f Row2, la_vec4f Row3);
force_inline la_mat4f Mat4f_FromRaw(const m4f *Raw);
force_inline m4f Mat4f_ToRaw(const la_mat4f *Mat);

force_inline la_mat4f Mat4f_Identity(float ValueOnDiagonal);
force_inline la_mat4f Mat4f_DiagonalVec(la_vec4f V);
force_inline float Mat4f_Get(const la_mat4f *Mat, int X, int Y);
force_inline void Mat4f_Transpose(la_mat4f *Mat);
header_function la_mat4f Mat4f_Mul_Mat4f(const la_mat4f *A, const la_mat4f *B);
header_function la_vec4f Mat4f_Mul_Vec4f(const la_mat4f *A, la_vec4f V);

header_function la_mat4f LA_RotationMatrixX(float Rads);
header_function la_mat4f LA_RotationMatrixY(float Rads);
header_function la_mat4f LA_RotationMatrixZ(float Rads);
header_function void LA_Translate(la_mat4f *Mat, la_vec3f Translation); /* model */
header_function la_mat4f LA_LookAt(la_vec3f CameraOrigin, la_vec3f CameraTarget, la_vec3f Up); /* view */
header_function la_mat4f LA_Perspective(float FovRads, float AspectRatio, float Near, float Far); /* projection */
/* returns <
 *      -X . CameraOrigin, 
 *      -Y . CameraOrigin, 
 *      -Z . CameraOrigin, 
 *      1.0f
 * > */
force_inline la_vec4f LA_LookAtDots(la_vec3f X, la_vec3f Y, la_vec3f Z, la_vec3f CameraOrigin);

/* ======================================================
 * Platform intrinsics:
 * ======================================================
 * // returns a vector with elems selected from ShuffleIndex, use Vec*f_ShufflePositions() to derive shuffle index
 * // excample pseudocode for la_vec4f
 * // for i in 0..3:
 * //   lane_index = (ShufflePositions >> i*2) & 0b11;
 * //   Result[i] = V[lane_index];
 * la_vec4f Vec4f_Shuffle(la_vec4f V, constexpr u8 ShuffleIndex)
 * la_vec3f Vec3f_Shuffle(la_vec3f V, constexpr u8 ShuffleIndex)
 * constexpr u8 Vec4f_ShufflePositions(constexpr u2 Elem0, Elem1, Elem2, Elem3)
 * constexpr u8 Vec3f_ShufflePositions(constexpr u2 Elem0, Elem1, Elem2)
 *
 * // returns a vector with elems selected from A and B according to SelMask (use Vec*f_ShufflePositions() to derive SelMask)
 * // example pseudocode for la_vec4f:
 * // for i in 0..3:
 * //   if SelMask & (1 << i):
 * //       Result[i] = B[i];
 * //   else Result[i] = A[i];
 * la_vec4f Vec4f_Blend(la_vec4f A, B, constexpr u8 SelMask)
 * la_vec3f Vec3f_Blend(la_vec3f A, B, constexpr u8 SelMask)
 * */

#if defined(ARCH_X86_HAS_COMPILER_INTRINSICS)
#    define Vec4f_ShufflePositions(e0_src_index, e1_src_index, e2_src_index, e3_src_index) \
        _MM_SHUFFLE(e3_src_index, e2_src_index, e1_src_index, e0_src_index)
#    define Vec3f_ShufflePositions(e0_src_index, e1_src_index, e2_src_index) \
        _MM_SHUFFLE(3, e2_src_index, e1_src_index, e0_src_index)
#    define Vec4f_Shuffle(v4f, shuffle_positions) (la_vec4f){_mm_shuffle_ps(((la_vec4f)(v4f))._Xmm, ((la_vec4f)(v4f))._Xmm, shuffle_positions)}
#    define Vec3f_Shuffle(v3f, shuffle_positions) (la_vec3f){_mm_shuffle_ps(((la_vec3f)(v3f))._Xmm, ((la_vec3f)(v3f))._Xmm, shuffle_positions)}
#    define Vec4f_Blend(a, b, selmask) (la_vec4f) {_mm_blend_ps(((la_vec4f)(a))._Xmm, ((la_vec4f)(b))._Xmm, selmask)}
#    define Vec3f_Blend(a, b, selmask) (la_vec3f) {_mm_blend_ps(((la_vec3f)(a))._Xmm, ((la_vec3f)(b))._Xmm, selmask)}
#else
#    define Vec4f_ShufflePositions(e0_src_index, e1_src_index, e2_src_index, e3_src_index) \
    (u8)((e0_src_index) \
     | (e1_src_index) << 2\
     | (e2_src_index) << 4\
     | (e3_src_index) << 6\
     )
#    define Vec3f_ShufflePositions(e0_src_index, e1_src_index, e2_src_index) \
    (u8)((e0_src_index) \
     | (e1_src_index) << 2\
     | (e2_src_index) << 4\
     )
force_inline la_vec4f Vec4f_Shuffle(la_vec4f V, u8 Pos)
{
    u8 E0 = Pos & 0x3;
    u8 E1 = (Pos >> 2) & 0x3;
    u8 E2 = (Pos >> 4) & 0x3;
    u8 E3 = (Pos >> 6) & 0x3;
    la_vec4f Result = {
        ._Internal.At[0] = V._Internal.At[E0],
        ._Internal.At[1] = V._Internal.At[E1],
        ._Internal.At[2] = V._Internal.At[E2],
        ._Internal.At[3] = V._Internal.At[E3],
    };
    return Result;
}
force_inline la_vec3f Vec3f_Shuffle(la_vec3f V, u8 Pos)
{
    u8 E0 = Pos & 0x3;
    u8 E1 = (Pos >> 2) & 0x3;
    u8 E2 = (Pos >> 4) & 0x3;
    la_vec3f Result = {
        ._Internal.At[0] = V._Internal.At[E0],
        ._Internal.At[1] = V._Internal.At[E1],
        ._Internal.At[2] = V._Internal.At[E2],
    };
    return Result;
}
force_inline la_vec4f Vec4f_Blend(la_vec4f A, la_vec4f B, u8 Bitmask)
{
    la_vec4f Result = {
        ._Internal.x = Bitmask & 1? B._Internal.x : A._Internal.x,
        ._Internal.y = Bitmask & 2? B._Internal.y : A._Internal.y,
        ._Internal.z = Bitmask & 4? B._Internal.z : A._Internal.z,
        ._Internal.w = Bitmask & 8? B._Internal.w : A._Internal.w,
    };
    return Result;
}
force_inline la_vec3f Vec3f_Blend(la_vec3f A, la_vec3f B, u8 Bitmask)
{
    la_vec3f Result = {
        ._Internal.x = Bitmask & 1? B._Internal.x : A._Internal.x,
        ._Internal.y = Bitmask & 2? B._Internal.y : A._Internal.y,
        ._Internal.z = Bitmask & 4? B._Internal.z : A._Internal.z,
    };
    return Result;
}
#endif



#if defined(ARCH_X86_HAS_COMPILER_INTRINSICS)

force_inline la_vec4f Vec4f_Sub(la_vec4f A, la_vec4f B) { return (la_vec4f){_mm_sub_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec4f Vec4f_Add(la_vec4f A, la_vec4f B) { return (la_vec4f){_mm_add_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec4f Vec4f_Mul(la_vec4f A, la_vec4f B) { return (la_vec4f){_mm_mul_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec4f Vec4f_Div(la_vec4f A, la_vec4f B) { return (la_vec4f){_mm_div_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec3f Vec3f_Sub(la_vec3f A, la_vec3f B) { return (la_vec3f){_mm_sub_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec3f Vec3f_Add(la_vec3f A, la_vec3f B) { return (la_vec3f){_mm_add_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec3f Vec3f_Mul(la_vec3f A, la_vec3f B) { return (la_vec3f){_mm_mul_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec3f Vec3f_Div(la_vec3f A, la_vec3f B) { return (la_vec3f){_mm_div_ps(A._Xmm, B._Xmm)}; }
force_inline la_vec3f Vec3f_Sqrt(la_vec3f A) { return (la_vec3f){_mm_sqrt_ps(A._Xmm)}; }
force_inline la_vec3f Vec3f_InvSqrt(la_vec3f A) { return (la_vec3f){_mm_rsqrt_ps(A._Xmm)}; }
force_inline la_vec4f Vec4f_Sqrt(la_vec4f A) { return (la_vec4f){_mm_sqrt_ps(A._Xmm)}; }
force_inline la_vec4f Vec4f_InvSqrt(la_vec4f A) { return (la_vec4f){_mm_rsqrt_ps(A._Xmm)}; }

#define LA_DEFINE_GET_FN(type, name, pos)\
force_inline float name(type V) {\
    fu32 Tmp = {.U32 = _mm_extract_ps(V._Xmm, pos)};\
    return Tmp.F32;\
}\
force_inline float name(type V)

LA_DEFINE_GET_FN(la_vec3f, Vec3f_GetX, 0);
LA_DEFINE_GET_FN(la_vec3f, Vec3f_GetY, 1);
LA_DEFINE_GET_FN(la_vec3f, Vec3f_GetZ, 2);
LA_DEFINE_GET_FN(la_vec4f, Vec4f_GetX, 0);
LA_DEFINE_GET_FN(la_vec4f, Vec4f_GetY, 1);
LA_DEFINE_GET_FN(la_vec4f, Vec4f_GetZ, 2);
LA_DEFINE_GET_FN(la_vec4f, Vec4f_GetW, 3);

#undef LA_DEFINE_GET_FN

force_inline bool32 Vec3f_IsZero(la_vec3f V)
{
    __m128 Upper = _mm_set1_ps(LA_F32_COMPARE_EPSILON);
    __m128 Lower = _mm_set1_ps(-LA_F32_COMPARE_EPSILON);
    __m128 LowerOK = _mm_cmpge_ps(V._Xmm, Lower);
    __m128 UpperOK = _mm_cmple_ps(V._Xmm, Upper);
    int IsZero = _mm_movemask_ps(_mm_and_ps(LowerOK, UpperOK));
    return (IsZero & 0x7) == 0x7; /* only checking x, y, z lanes */
}

force_inline bool32 Vec4f_IsZero(la_vec4f V)
{
    __m128 Upper = _mm_set1_ps(LA_F32_COMPARE_EPSILON);
    __m128 Lower = _mm_set1_ps(-LA_F32_COMPARE_EPSILON);
    __m128 LowerOK = _mm_cmpge_ps(V._Xmm, Lower);
    __m128 UpperOK = _mm_cmple_ps(V._Xmm, Upper);
    int IsZero = _mm_movemask_ps(_mm_and_ps(LowerOK, UpperOK));
    return (IsZero & 0xF) == 0xF; /* x, y, z, w lanes */
}

force_inline la_vec4f Vec3f_ExtendToVec4f(la_vec3f V, float W)
{
    __m128 Value = _mm_set1_ps(W);
    /* NOTE: gcc doesn't actually generate a (v)blendps instruction. 
     * It generates a (v)insertps, which has a worse throughput (0.33 vs 0.25 on zen4, https://www.agner.org/optimize/instruction_tables.pdf)
     * TODO: force gcc to generate (v)blendps
     */
    la_vec4f Result = { _mm_blend_ps(V._Xmm, Value, 0x08) }; /* [Vx, Vy, Vz, W] */
    return Result;
}
force_inline la_vec3f Vec4f_TruncateToVec3f(la_vec4f V)
{
    __m128 Value = _mm_setzero_ps();
    la_vec3f Result = { _mm_blend_ps(V._Xmm, Value, 0x08) }; /* [Vx, Vy, Vz, 0] */
    return Result;
}
force_inline v4f Vec4f_ToRaw(la_vec4f V)
{
    v4f Result = {
        .x = Vec4f_GetX(V),
        .y = Vec4f_GetY(V),
        .z = Vec4f_GetZ(V),
        .w = Vec4f_GetW(V),
    };
    return Result;
}
force_inline v3f Vec3f_ToRaw(la_vec3f V)
{
    v3f Result = {
        .x = Vec3f_GetX(V),
        .y = Vec3f_GetY(V),
        .z = Vec3f_GetZ(V),
    };
    return Result;
}
force_inline la_vec3f Vec3f_FromRaw(v3f V)
{
    la_vec3f Result = {_mm_set_ps(0.0f, V.z, V.y, V.x)};
    return Result;
}
force_inline la_vec4f Vec4f_FromRaw(v4f V)
{
    la_vec4f Result = {_mm_set_ps(V.w, V.z, V.y, V.x)};
    return Result;
}
force_inline la_vec4f Vec4f_FromSingle(float Val)
{
    la_vec4f Result = {_mm_set1_ps(Val)};
    return Result;
}
force_inline la_vec3f Vec3f_FromSingle(float Val)
{
    la_vec3f Result = {_mm_set1_ps(Val)};
    return Result;
}

force_inline la_vec3f Vec3f_Dot(la_vec3f A, la_vec3f B)
{
    __m128 Squared = _mm_mul_ps(A._Xmm, B._Xmm);
    __m128 ZXY = _mm_shuffle_ps(Squared, Squared, Vec3f_ShufflePositions(2, 0, 1)); /* [z, x, y, 0] */
    __m128 Tmp0 = _mm_add_ps(Squared, ZXY);                                         /* [x+z, y+x, z+y, 0] */
    __m128 YZX = _mm_shuffle_ps(Squared, Squared, Vec3f_ShufflePositions(1, 2, 0)); /* [y, z, x, 0] */
    __m128 DotAB = _mm_add_ps(Tmp0, YZX);                                           /* [x+z+y, y+x+z, z+y+x, 0] */
    return (la_vec3f){DotAB};
}
force_inline la_vec4f Vec4f_Dot(la_vec4f A, la_vec4f B)
{
    __m128 Squared = _mm_mul_ps(A._Xmm, B._Xmm);
    __m128 XXZZ = _mm_shuffle_ps(Squared, Squared, Vec4f_ShufflePositions(0, 0, 2, 2));     /* [x, x, z, z] */
    __m128 YYWW = _mm_shuffle_ps(Squared, Squared, Vec4f_ShufflePositions(1, 1, 3, 3));     /* [y, y, w, w] */
    __m128 Tmp0 = _mm_add_ps(XXZZ, YYWW);                                                   /* [x+y, x+y, z+w, z+w] */
    __m128 Tmp1 = _mm_shuffle_ps(Tmp0, Tmp0, Vec4f_ShufflePositions(2, 2, 0, 0));           /* [z+w, z+w, x+y, x+y] */
    __m128 DotAB = _mm_add_ps(Tmp0, Tmp1);                                                  /* [x+y+z+w, x+y+z+w, x+y+z+w, x+y+z+w] */
    return (la_vec4f){DotAB};
}
force_inline la_vec4f Vec4f_Normalize(la_vec4f V)
{
    la_vec4f DotVV = Vec4f_Dot(V, V);
    __m128 InvMagV = _mm_rsqrt_ps(DotVV._Xmm);          /* 1/|V| */
    __m128 Normalized = _mm_mul_ps(V._Xmm, InvMagV);    /* V/|V| */
    return (la_vec4f){Normalized};
}
force_inline la_vec3f Vec3f_Normalize(la_vec3f V)
{
    la_vec3f DotVV = Vec3f_Dot(V, V);
    la_vec3f InvMagV = (la_vec3f){_mm_rsqrt_ps(DotVV._Xmm)};  /* 1/|V| */
    la_vec3f Normalized = Vec3f_Mul(V, InvMagV);           /* V/|V| */
    return Normalized;
}
force_inline la_vec3f Vec3f_Cross(la_vec3f A, la_vec3f B)
{
    /*
        la_vec3f Result = {
            .x = A.y*B.z - A.z*B.y,
            .y = A.z*B.x - A.x*B.z,
            .z = A.x*B.y - A.y*B.x,
        };
    */
    la_vec3f InterleavedA = Vec3f_Shuffle(A, Vec3f_ShufflePositions(2, 0, 1));             /* [Az, Ax, Ay, 0] */
    la_vec3f InterleavedB = Vec3f_Shuffle(B, Vec3f_ShufflePositions(1, 2, 0));             /* [By, Bz, Bx, 0] */
    la_vec3f A_InterleavedB = Vec3f_Mul(A, InterleavedB);                                  /* [AxBy, AyBz, AzBx, 0] */
    la_vec3f Rhs = Vec3f_Mul(InterleavedA, InterleavedB);                                  /* [AzBy, AxBz, AyBx, 0] */
    la_vec3f Lhs = Vec3f_Shuffle(A_InterleavedB, Vec3f_ShufflePositions(1, 2, 0));         /* [AyBz, AzBx, AxBy, 0] */
    la_vec3f Result = Vec3f_Sub(Lhs, Rhs);                                                 /* [AyBz - AzBy, AzBx - AxBz, AxBy - AyBx, 0] */
    return Result;
}


force_inline float Mat4f_Get(const la_mat4f *Mat, int X, int Y)
{
    union {
        __m128 _Xmm;
        float Array[4];
    } Tmp = { ._Xmm = Mat->Rows[Y]._Xmm};
    return Tmp.Array[X];
}

force_inline void Mat4f_Transpose(la_mat4f *M)
{
    _MM_TRANSPOSE4_PS(
        M->Rows[0]._Xmm,
        M->Rows[1]._Xmm,
        M->Rows[2]._Xmm,
        M->Rows[3]._Xmm
    );
}

force_inline la_vec4f LA_LookAtDots(la_vec3f X, la_vec3f Y, la_vec3f Z, la_vec3f CameraOrigin)
{
    __m128 Dots;
    {
        __m128 XC = _mm_mul_ps(X._Xmm, CameraOrigin._Xmm);
        __m128 YC = _mm_mul_ps(Y._Xmm, CameraOrigin._Xmm);
        __m128 ZC = _mm_mul_ps(Z._Xmm, CameraOrigin._Xmm);
        __m128 Tmp0 = _mm_shuffle_ps(XC, YC, Vec4f_ShufflePositions(0, 1, 0, 1));           /* [XCx, XCy, YCx, YCy] */
        __m128 XYZx = _mm_shuffle_ps(Tmp0, ZC, Vec4f_ShufflePositions(0, 2, 0, 3));         /* [XCx, YCx, ZCx,   0] */
        __m128 XYZy = _mm_shuffle_ps(Tmp0, ZC, Vec4f_ShufflePositions(1, 3, 1, 3));         /* [XCy, YCy, ZCy,   0] */
        __m128 XYZxy = _mm_add_ps(XYZx, XYZy);                                              /* [XCx+XCy, YCx+YCy, ZCx + ZCy, 0] */
        __m128 Tmp1 = _mm_shuffle_ps(XC, YC, Vec4f_ShufflePositions(2, 3, 2, 3));           /* [XCz,   0, YCz,   0] */
        __m128 XYZz = _mm_shuffle_ps(Tmp1, ZC, Vec4f_ShufflePositions(0, 2, 2, 3));         /* [XCz, YCz, ZCz,   0] */
        __m128 Rhs = _mm_add_ps(XYZxy, XYZz);                                               /* [DotXC, DotYC, DotZC, 0] */
        Dots = _mm_sub_ps(_mm_setzero_ps(), Rhs);                                           /* [-DotXC, -DotYC, -DotZC, 0] */
        Dots = _mm_blend_ps(Dots, _mm_set1_ps(1.0f), 0x08);                                 /* [-DotXC, -DotYC, -DotZC, 1] */
    }
    la_vec4f Result = { Dots };
    return Result;
}

header_function la_mat4f LA_LookAt(la_vec3f CameraOrigin, la_vec3f CameraTarget, la_vec3f Up)
{
    la_vec3f X, Y, Z;
    Z = Vec3f_Sub(CameraOrigin, CameraTarget);
    Z = Vec3f_Normalize(Z);
    Y = Up;
    X = Vec3f_Cross(Y, Z);
    Y = Vec3f_Cross(Z, X);
    X = Vec3f_Normalize(X);
    Y = Vec3f_Normalize(Y);

    /*
        la_mat4f Result = {
            .Rows = {
                {X.x,  Y.x,  Z.x, -Vec3f_Dot(X, CameraOrigin)},
                {X.y,  Y.y,  Z.y, -Vec3f_Dot(Y, CameraOrigin)},
                {X.z,  Y.z,  Z.z, -Vec3f_Dot(Z, CameraOrigin)},
                {0.0f, 0.0f, 0.0f, 1.0f},
            },
        };
        return Result;
    */
    la_vec4f Dots = LA_LookAtDots(X, Y, Z, CameraOrigin);
    la_mat4f Result = {
        .Rows = {
            [0] = {X._Xmm}, 
            [1] = {Y._Xmm}, 
            [2] = {Z._Xmm}, 
        },
    };
    Mat4f_Transpose(&Result);
    Result.Rows[3] = Dots;
    return Result;
}
header_function la_vec4f Mat4f_Mul_Vec4f(const la_mat4f *A, la_vec4f V)
{
    __m128 Row0 = _mm_mul_ps(A->Rows[0]._Xmm, V._Xmm);
    __m128 Row1 = _mm_mul_ps(A->Rows[1]._Xmm, V._Xmm);
    __m128 Tmp0 = _mm_shuffle_ps(Row0, Row1, Vec4f_ShufflePositions(0, 1, 0, 1));                   /* [R0x, R0y, R1x, R1y] */
    __m128 Tmp1 = _mm_shuffle_ps(Row0, Row1, Vec4f_ShufflePositions(2, 3, 2, 3));                   /* [R0z, R0w, R1z, R1w] */
    __m128 Tmp2 = _mm_add_ps(Tmp0, Tmp1);                                                           /* [R0xz, R0yw, R1xz, R1yw] */
    __m128 Tmp3 = _mm_shuffle_ps(Tmp2, Tmp2, Vec4f_ShufflePositions(1, 0, 3, 2));                   /* [R0yw, R0xz, R1yw, R1xz] */
    __m128 Row01Result = _mm_add_ps(Tmp2, Tmp3);                                                    /* [R0xzyw, same, R1xzyw, same] */

    __m128 Row2 = _mm_mul_ps(A->Rows[2]._Xmm, V._Xmm);
    __m128 Row3 = _mm_mul_ps(A->Rows[3]._Xmm, V._Xmm);
    Tmp0 = _mm_shuffle_ps(Row2, Row3, Vec4f_ShufflePositions(0, 1, 0, 1));                          /* [R2x, R2y, R3x, R3y] */
    Tmp1 = _mm_shuffle_ps(Row2, Row3, Vec4f_ShufflePositions(2, 3, 2, 3));                          /* [R2z, R2w, R3z, R3w] */
    Tmp2 = _mm_add_ps(Tmp0, Tmp1);                                                                  /* [R2xz, R2yw, R3xz, R3yw] */
    Tmp3 = _mm_shuffle_ps(Tmp2, Tmp2, Vec4f_ShufflePositions(1, 0, 3, 2));                          /* [R2yw, R2xz, R3w, R3xz] */
    __m128 Row23Result = _mm_add_ps(Tmp2, Tmp3);                                                    /* [R2xzyw, same, R3xzyw, same] */

    __m128 Result = _mm_shuffle_ps(Row01Result, Row23Result, Vec4f_ShufflePositions(0, 2, 0, 2));   /* [R0xyzw, R1xyzw, R2xyzw, R3xyzw] */
    return (la_vec4f){ Result };
}

#else

force_inline v3f Vec3f_ToRaw(la_vec3f V) { return V._Internal; }
force_inline la_vec3f Vec3f_FromRaw(v3f V) { return (la_vec3f) { V }; }
force_inline la_vec4f Vec4f_FromSingle(float Val) 
{
    la_vec4f Result = {
        ._Internal.x = Val,
        ._Internal.y = Val,
        ._Internal.z = Val,
        ._Internal.w = Val,
    };
    return Result;
}
force_inline la_vec3f Vec3f_FromSingle(float Val)
{
    la_vec3f Result = {
        ._Internal.x = Val,
        ._Internal.y = Val,
        ._Internal.z = Val,
    };
    return Result;
}
force_inline la_vec3f Vec4f_TruncateToVec3f(la_vec4f V)
{
    la_vec3f Result = {
        ._Internal.x = V._Internal.x,
        ._Internal.y = V._Internal.y,
        ._Internal.z = V._Internal.z,
    };
    return Result;
}
force_inline la_vec4f Vec3f_ExtendToVec4f(la_vec3f V, float W)
{
    la_vec4f Result = {
        ._Internal.x = V._Internal.x, 
        ._Internal.y = V._Internal.y, 
        ._Internal.z = V._Internal.z, 
        ._Internal.w = W
    };
    return Result;
}
force_inline la_vec3f Vec3f_Cross(la_vec3f A, la_vec3f B)
{
    la_vec3f Result = {
        ._Internal.x = A._Internal.y*B._Internal.z - A._Internal.z*B._Internal.y,
        ._Internal.y = A._Internal.z*B._Internal.x - A._Internal.x*B._Internal.z,
        ._Internal.z = A._Internal.x*B._Internal.y - A._Internal.y*B._Internal.x,
    };
    return Result;
}
force_inline la_vec3f Vec3f_Dot(la_vec3f A, la_vec3f B)
{
    float Value = A._Internal.x*B._Internal.x + A._Internal.y*B._Internal.y + A._Internal.z*B._Internal.z;
    la_vec3f Result = {
        ._Internal.x = Value, 
        ._Internal.y = Value, 
        ._Internal.z = Value,
    };
    return Result;
}

force_inline bool32 Vec3f_IsZero(la_vec3f V)
{
    bool32 Result = 
        LA_F32_IS_ZERO(V._Internal.x)
        && LA_F32_IS_ZERO(V._Internal.y)
        && LA_F32_IS_ZERO(V._Internal.z);
    return Result;
}

force_inline bool32 Vec4f_IsZero(la_vec4f V)
{
    bool32 Result = 
        LA_F32_IS_ZERO(V._Internal.x)
        && LA_F32_IS_ZERO(V._Internal.y)
        && LA_F32_IS_ZERO(V._Internal.z)
        && LA_F32_IS_ZERO(V._Internal.w);
    return Result;
}

force_inline float Vec3f_GetX(la_vec3f V) { return V._Internal.x; }
force_inline float Vec3f_GetY(la_vec3f V) { return V._Internal.y; }
force_inline float Vec3f_GetZ(la_vec3f V) { return V._Internal.z; }
force_inline la_vec3f Vec3f_Normalize(la_vec3f V) 
{
    float MagInv = 1.0f/sqrtf(Vec3f_Dot(V, V)._Internal.x);
    la_vec3f Result = { 
        ._Internal.x = V._Internal.x * MagInv,
        ._Internal.y = V._Internal.y * MagInv,
        ._Internal.z = V._Internal.z * MagInv,
    };
    return Result;
}
#define LA_DEFINE_BINARY_OP_FN(type, name, op)\
    force_inline type name (type A, type B) {\
        type Result = {\
            ._Internal.x = A._Internal.x op B._Internal.x,\
            ._Internal.y = A._Internal.y op B._Internal.y,\
            ._Internal.z = A._Internal.z op B._Internal.z,\
        };\
        return Result;\
    }\
    force_inline type name (type A, type B)
LA_DEFINE_BINARY_OP_FN(la_vec3f, Vec3f_Add, +);
LA_DEFINE_BINARY_OP_FN(la_vec3f, Vec3f_Sub, -);
LA_DEFINE_BINARY_OP_FN(la_vec3f, Vec3f_Mul, *);
LA_DEFINE_BINARY_OP_FN(la_vec3f, Vec3f_Div, /);
LA_DEFINE_BINARY_OP_FN(la_vec4f, Vec4f_Add, +);
LA_DEFINE_BINARY_OP_FN(la_vec4f, Vec4f_Sub, -);
LA_DEFINE_BINARY_OP_FN(la_vec4f, Vec4f_Mul, *);
LA_DEFINE_BINARY_OP_FN(la_vec4f, Vec4f_Div, /);
#undef LA_DEFINE_BINARY_OP_FN

force_inline la_vec4f Vec4f_Sqrt(la_vec4f V)
{
    la_vec4f Result = {
        ._Internal.x = sqrtf(V._Internal.x),
        ._Internal.y = sqrtf(V._Internal.y),
        ._Internal.z = sqrtf(V._Internal.z),
        ._Internal.w = sqrtf(V._Internal.w),
    };
    return Result;
}
force_inline la_vec4f Vec4f_InvSqrt(la_vec4f V)
{
    la_vec4f Result = {
        ._Internal.x = 1.0f / sqrtf(V._Internal.x),
        ._Internal.y = 1.0f / sqrtf(V._Internal.y),
        ._Internal.z = 1.0f / sqrtf(V._Internal.z),
        ._Internal.w = 1.0f / sqrtf(V._Internal.w),
    };
    return Result;
}
force_inline la_vec3f Vec3f_Sqrt(la_vec3f V)
{
    la_vec3f Result = {
        ._Internal.x = sqrtf(V._Internal.x),
        ._Internal.y = sqrtf(V._Internal.y),
        ._Internal.z = sqrtf(V._Internal.z),
    };
    return Result;
}
force_inline la_vec3f Vec3f_InvSqrt(la_vec3f V)
{
    la_vec3f Result = {
        ._Internal.x = 1.0f / sqrtf(V._Internal.x),
        ._Internal.y = 1.0f / sqrtf(V._Internal.y),
        ._Internal.z = 1.0f / sqrtf(V._Internal.z),
    };
    return Result;
}

force_inline v4f Vec4f_ToRaw(la_vec4f V) { return V._Internal; }
force_inline la_vec4f Vec4f_FromRaw(v4f V) { return (la_vec4f) { V }; }
force_inline float Vec4f_GetX(la_vec4f V) { return V._Internal.x; }
force_inline float Vec4f_GetY(la_vec4f V) { return V._Internal.y; }
force_inline float Vec4f_GetZ(la_vec4f V) { return V._Internal.z; }
force_inline float Vec4f_GetW(la_vec4f V) { return V._Internal.w; }
force_inline la_vec4f Vec4f_Normalize(la_vec4f V) 
{
    float MagInv = 1.0f/sqrtf(Vec4f_Dot(V, V)._Internal.x);
    la_vec4f Result = {
        ._Internal.x = V._Internal.x * MagInv,
        ._Internal.y = V._Internal.y * MagInv,
        ._Internal.z = V._Internal.z * MagInv,
        ._Internal.w = V._Internal.w * MagInv,
    };
    return Result;
}
force_inline la_vec4f Vec4f_Dot(la_vec4f A, la_vec4f B)
{
    float Value = A._Internal.x*B._Internal.x + A._Internal.y*B._Internal.y + A._Internal.z*B._Internal.z + A._Internal.w*B._Internal.w;
    la_vec4f Result = {
        ._Internal.x = Value, 
        ._Internal.y = Value, 
        ._Internal.z = Value,
        ._Internal.w = Value,
    };
    return Result;
}

force_inline void Mat4f_Transpose(la_mat4f *Mat)
{
    SWAP(float, Mat->Rows[0]._Internal.At[1], Mat->Rows[1]._Internal.At[0]);
    SWAP(float, Mat->Rows[0]._Internal.At[2], Mat->Rows[2]._Internal.At[0]);
    SWAP(float, Mat->Rows[0]._Internal.At[3], Mat->Rows[3]._Internal.At[0]);
    SWAP(float, Mat->Rows[1]._Internal.At[2], Mat->Rows[2]._Internal.At[1]);
    SWAP(float, Mat->Rows[1]._Internal.At[3], Mat->Rows[3]._Internal.At[1]);
    SWAP(float, Mat->Rows[2]._Internal.At[3], Mat->Rows[3]._Internal.At[2]);
}

force_inline float Mat4f_Get(const la_mat4f *Mat, int X, int Y)
{
    return Mat->Rows[Y]._Internal.At[X];
}

force_inline la_vec4f LA_LookAtDots(la_vec3f X, la_vec3f Y, la_vec3f Z, la_vec3f CameraOrigin)
{
    la_vec4f Dots = {
        ._Internal.x = -Vec3f_Dot(X, CameraOrigin)._Internal.x,
        ._Internal.y = -Vec3f_Dot(Y, CameraOrigin)._Internal.x,
        ._Internal.z = -Vec3f_Dot(Z, CameraOrigin)._Internal.x,
        ._Internal.w = 1.0f,
    };
    return Dots;
}

header_function la_mat4f LA_LookAt(la_vec3f CameraOrigin, la_vec3f CameraTarget, la_vec3f Up)
{
    la_vec3f X, Y, Z;
    Z = Vec3f_Sub(CameraOrigin, CameraTarget);
    Z = Vec3f_Normalize(Z);
    Y = Up;
    X = Vec3f_Cross(Y, Z);
    Y = Vec3f_Cross(Z, X);
    X = Vec3f_Normalize(X);
    Y = Vec3f_Normalize(Y);

    la_vec4f Dots = LA_LookAtDots(X, Y, Z, CameraOrigin);
    la_mat4f Result = {
        .Rows = {
            [0] = {X._Internal.x, Y._Internal.x, Z._Internal.x, 0}, 
            [1] = {X._Internal.y, Y._Internal.y, Z._Internal.y, 0}, 
            [2] = {X._Internal.z, Y._Internal.z, Z._Internal.z, 0}, 
            [3] = Dots,
        },
    };
    return Result;
}


header_function la_vec4f Mat4f_Mul_Vec4f(const la_mat4f *A, la_vec4f V)
{
    la_vec4f Result;
    for (int i = 0; i < 4; i++)
    {
        float Accum = 0.0f;
        for (int k = 0; k < 4; k++)
        {
            Accum += A->Rows[i]._Internal.At[k] * V._Internal.At[k];
        }
        Result._Internal.At[i] = Accum;
    }
    return Result;
}

#endif



force_inline m4f Mat4f_ToRaw(const la_mat4f *Mat) 
{ 
    m4f Raw = {
        .Rows[0] = Vec4f_ToRaw(Mat->Rows[0]),
        .Rows[1] = Vec4f_ToRaw(Mat->Rows[1]),
        .Rows[2] = Vec4f_ToRaw(Mat->Rows[2]),
        .Rows[3] = Vec4f_ToRaw(Mat->Rows[3]),
    };
    return Raw;
}

force_inline la_vec3f Vec3f_Mag(la_vec3f V)
{
    la_vec3f Dots = Vec3f_Dot(V, V);
    la_vec3f Result = Vec3f_Sqrt(Dots);
    return Result;
}
force_inline la_vec4f Vec4f_Mag(la_vec4f V)
{
    la_vec4f Dots = Vec4f_Dot(V, V);
    la_vec4f Result = Vec4f_Sqrt(Dots);
    return Result;
}
force_inline la_mat4f Mat4f_FromRaw(const m4f *Raw)
{
    return Mat4f_FromVec(
        Vec4f_FromRaw(Raw->Rows[0]), 
        Vec4f_FromRaw(Raw->Rows[1]), 
        Vec4f_FromRaw(Raw->Rows[2]), 
        Vec4f_FromRaw(Raw->Rows[3])
    );
}
force_inline la_mat4f Mat4f_FromVec(la_vec4f R0, la_vec4f R1, la_vec4f R2, la_vec4f R3)
{
    la_mat4f Result = {
        .Rows = {
            R0, R1, R2, R3
        },
    };
    return Result;
}
force_inline la_mat4f Mat4f_Identity(float Scalar)
{
    return Mat4f_DiagonalVec(Vec4f_FromSingle(Scalar));
}
force_inline la_mat4f Mat4f_DiagonalVec(la_vec4f V)
{
    la_vec4f Zeros = Vec4f(0);
    la_mat4f Result = {
        .Rows[0] = Vec4f_Blend(Zeros, V, 0x01),
        .Rows[1] = Vec4f_Blend(Zeros, V, 0x02),
        .Rows[2] = Vec4f_Blend(Zeros, V, 0x04),
        .Rows[3] = Vec4f_Blend(Zeros, V, 0x08),
    };
    return Result;
}

header_function la_mat4f Mat4f_Mul_Mat4f(const la_mat4f *A, const la_mat4f *B)
{
    la_mat4f TransposedB = *B;
    Mat4f_Transpose(&TransposedB);
    la_vec4f Row0 = Mat4f_Mul_Vec4f(A, TransposedB.Rows[0]);
    la_vec4f Row1 = Mat4f_Mul_Vec4f(A, TransposedB.Rows[1]);
    la_vec4f Row2 = Mat4f_Mul_Vec4f(A, TransposedB.Rows[2]);
    la_vec4f Row3 = Mat4f_Mul_Vec4f(A, TransposedB.Rows[3]);
    la_mat4f Result = {
        .Rows = {
            Row0, 
            Row1, 
            Row2, 
            Row3,
        }
    };
    Mat4f_Transpose(&Result);
    return Result;
}

header_function la_mat4f LA_RotationMatrixX(float Rads)
{
    float C = cosf(Rads);
    float S = sinf(Rads);
    la_mat4f Mat;
    Mat.Rows[0] = Vec4f(1, 0,  0, 0);
    Mat.Rows[1] = Vec4f(0, C, -S, 0);
    Mat.Rows[2] = Vec4f(0, S,  C, 0);
    Mat.Rows[3] = Vec4f(0, 0,  0, 1);
    return Mat;
}
header_function la_mat4f LA_RotationMatrixY(float Rads)
{
    float C = cosf(Rads);
    float S = sinf(Rads);
    la_mat4f Mat;
    Mat.Rows[0] = Vec4f(C,  0,  S, 0);
    Mat.Rows[1] = Vec4f(0,  1,  0, 0);
    Mat.Rows[2] = Vec4f(-S, 0,  C, 0);
    Mat.Rows[3] = Vec4f(0,  0,  0, 1);
    return Mat;
}
header_function la_mat4f LA_RotationMatrixZ(float Rads)
{
    float C = cosf(Rads);
    float S = sinf(Rads);
    la_mat4f Mat;
    Mat.Rows[0] = Vec4f(C, -S, 0, 0);
    Mat.Rows[1] = Vec4f(S,  C, 0, 0);
    Mat.Rows[2] = Vec4f(0,  0, 1, 0);
    Mat.Rows[3] = Vec4f(0,  0, 0, 1);
    return Mat;
}

header_function void LA_Translate(la_mat4f *Mat, la_vec3f Translation)
{
    la_vec4f TransX = Vec4f(.z = Vec3f_GetX(Translation));
    la_vec4f TransY = Vec4f(.z = Vec3f_GetY(Translation));
    la_vec4f TransZ = Vec4f(.z = Vec3f_GetZ(Translation));
    Mat->Rows[0] = Vec4f_Add(Mat->Rows[0], TransX);
    Mat->Rows[1] = Vec4f_Add(Mat->Rows[1], TransY);
    Mat->Rows[2] = Vec4f_Add(Mat->Rows[2], TransZ);
}

header_function la_mat4f LA_Perspective(float FovYRads, float AspectRatio, float Near, float Far)
{
    float TanThetaHalf = tanf(FovYRads) * 0.5f;
    float DFarNearInv = 1.0f / (Far - Near);
    la_mat4f Result = Mat4f_Diagonal(
        1.0f / (AspectRatio * TanThetaHalf),
        1.0f / TanThetaHalf, 
        0.0f,
        0.0f
    );
    Result.Rows[2] = Vec4f(
        .z = -(Far + Near) * DFarNearInv,
        .w = -1.0f,
    );
    Result.Rows[3] = Vec4f(
        .z = -(LA_DEPTH * Far * Near) * DFarNearInv,
    );
    return Result;
}

#endif /* LINEAR_ALGEBRA_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */

