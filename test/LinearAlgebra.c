
#include "LinearAlgebra.h"
#include <string.h>


internal void PrintV3(la_vec3f Vec)
{
    v3f V = Vec3f_ToRaw(Vec);
    (void)eprintfln("[%3g, %3g, %3g]", V.x, V.y, V.z);
}
internal void PrintV4(la_vec4f Vec)
{
    v4f V = Vec4f_ToRaw(Vec);
    (void)eprintfln("[%3g, %3g, %3g, %3g]", V.x, V.y, V.z, V.w);
}
internal void PrintHeader(int Indent, const char *Str)
{
    int Length = strlen(Str);
    for (int i = 0; i < Indent + Length + Indent; i++)
        (void)eprintf("=");
    (void)eprintfln("\n%*s", Indent + Length, Str);
    for (int i = 0; i < Indent + Length + Indent; i++)
        (void)eprintf("=");
    (void)eprintf("\n");
}
internal void PrintM4(int Indent, la_mat4f Mat)
{
    for (int i = 0; i < 4; i++)
    {
        (void)eprintf("%*s", Indent, ""); PrintV4(Mat.Rows[i]);
    }
}
internal void PrintM4Raw(int Indent, m4f Mat)
{
    for (int i = 0; i < 4; i++)
    {
        (void)eprintf("%*s[", Indent, "");
        for (int k = 0; k < 4; k++)
        {
            eprintf("%3g", Mat.At[i][k]);
            if (k != 3)
                eprintf(", ");
            else
                eprintfln("]");
        }
    }
}
internal void PrintV4Indented(int Indent, la_vec4f Vec)
{
    (void)eprintf("%*s", Indent, ""); PrintV4(Vec);
}

internal bool8 CheckMatEquality(la_mat4f A, la_mat4f B)
{
    float Delta = 0.001f;
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            float ValA = Mat4f_Get(&A, x, y);
            float ValB = Mat4f_Get(&B, x, y);
            if (!IN_RANGE(ValB - Delta, ValA, ValB + Delta))
                return false;
        }
    }
    return true;
}

int main(void)
{
    bool8 ShowShuffle = false;
    int Indent = 6;
    PrintHeader(Indent, "Testing vec3 ops:");
    {
        la_vec3f A = Vec3f(1, 2, 3);
        la_vec3f B = Vec3f(4, 5, 6);
        la_vec3f C = Vec3f(7, 8, 9);
        la_vec3f Cam = Vec3f(1, -1, 1);
        (void)eprintf("A         = "); PrintV3(A);
        (void)eprintf("B         = "); PrintV3(B);
        (void)eprintf("C         = "); PrintV3(C);
        (void)eprintf("Cam       = "); PrintV3(Cam);
        (void)eprintf("A . B     = "); PrintV3(Vec3f_Dot(A, B));
        (void)eprintf("A x B     = "); PrintV3(Vec3f_Cross(A, B));
        (void)eprintf("A + B     = "); PrintV3(Vec3f_Add(A, B));
        (void)eprintf("A - B     = "); PrintV3(Vec3f_Sub(A, B));
        (void)eprintf("A * B     = "); PrintV3(Vec3f_Mul(A, B));
        (void)eprintf("A / B     = "); PrintV3(Vec3f_Div(A, B));
        (void)eprintf("sqrt(A)   = "); PrintV3(Vec3f_Sqrt(A));
        (void)eprintf("1/sqrt(A) = "); PrintV3(Vec3f_InvSqrt(A));
        (void)eprintf("|A|       = "); PrintV3(Vec3f_Mag(A));
        (void)eprintf("norm(A)   = "); PrintV3(Vec3f_Normalize(A));
        (void)eprintf("|norm(A)| = "); PrintV3(Vec3f_Mag(Vec3f_Normalize(A)));
        (void)eprintf("<A, 69>   = "); PrintV4(Vec3f_ExtendToVec4f(A, 69.0f));

        (void)eprintf("Dots(A, B, C, Cam)          = "); PrintV4(LA_LookAtDots(A, B, C, Cam));
        (void)eprintf("<-A.Cam, -B.Cam, -C.Cam, 1> = "); PrintV4(Vec4f(
            .x = -Vec3f_GetX(Vec3f_Dot(A, Cam)),
            .y = -Vec3f_GetX(Vec3f_Dot(B, Cam)),
            .z = -Vec3f_GetX(Vec3f_Dot(C, Cam)),
            .w = 1.0f,
        ));
        if (ShowShuffle)
        {
            (void)eprintfln("Shuffles:");
#define TEST_SHUFFLE(v, e0, e1, e2) \
            (void)eprintf("    "#v "[%d, %d, %d] = ", e0, e1, e2); PrintV3(Vec3f_Shuffle(v, Vec3f_ShufflePositions(e0, e1, e2)))
#define TEST_SHUFFLE_MANY(e)\
            TEST_SHUFFLE(A, e, 0, 0);\
            TEST_SHUFFLE(A, e, 0, 1);\
            TEST_SHUFFLE(A, e, 0, 2);\
            TEST_SHUFFLE(A, e, 1, 0);\
            TEST_SHUFFLE(A, e, 1, 1);\
            TEST_SHUFFLE(A, e, 1, 2);\
            TEST_SHUFFLE(A, e, 2, 0);\
            TEST_SHUFFLE(A, e, 2, 1);\
            TEST_SHUFFLE(A, e, 2, 2)
            TEST_SHUFFLE_MANY(0);
            TEST_SHUFFLE_MANY(1);
            TEST_SHUFFLE_MANY(2);
#undef TEST_SHUFFLE_MANY
#undef TEST_SHUFFLE
        }
    }
    PrintHeader(Indent, "Testing vec4 ops:");
    {
        la_vec4f A = Vec4f(0, 1, 2, 3);
        la_vec4f B = Vec4f(4, 5, 6, 7);
        la_vec4f C = Vec4f(8, 9, 10, 11);
        la_vec4f Cam = Vec4f(1, -3, 2, -2);
        (void)eprintf("A         = "); PrintV4(A);
        (void)eprintf("B         = "); PrintV4(B);
        (void)eprintf("C         = "); PrintV4(C);
        (void)eprintf("Cam       = "); PrintV4(Cam);
        (void)eprintf("A . B     = "); PrintV4(Vec4f_Dot(A, B));
        (void)eprintf("A + B     = "); PrintV4(Vec4f_Add(A, B));
        (void)eprintf("A - B     = "); PrintV4(Vec4f_Sub(A, B));
        (void)eprintf("A * B     = "); PrintV4(Vec4f_Mul(A, B));
        (void)eprintf("A / B     = "); PrintV4(Vec4f_Div(A, B));
        (void)eprintf("sqrt(A)   = "); PrintV4(Vec4f_Sqrt(A));
        (void)eprintf("1/sqrt(A) = "); PrintV4(Vec4f_InvSqrt(A));
        (void)eprintf("|A|       = "); PrintV4(Vec4f_Mag(A));
        (void)eprintf("norm(A)   = "); PrintV4(Vec4f_Normalize(A));
        (void)eprintf("|norm(A)| = "); PrintV4(Vec4f_Mag(Vec4f_Normalize(A)));

        if (ShowShuffle)
        {
            (void)eprintfln("Shuffles: ");
#define TEST_SHUFFLE(v, e0, e1, e2, e3) \
            (void)eprintf("    "#v "[%d, %d, %d, %d] = ", e0, e1, e2, e3); PrintV4(Vec4f_Shuffle(v, Vec4f_ShufflePositions(e0, e1, e2, e3)))
#define TEST_SHUF0(e0, e1, e2)\
            TEST_SHUFFLE(A, e0, e1, e2, 0);\
            TEST_SHUFFLE(A, e0, e1, e2, 1);\
            TEST_SHUFFLE(A, e0, e1, e2, 2);\
            TEST_SHUFFLE(A, e0, e1, e2, 3)
#define TEST_SHUF1(e0, e1)\
            TEST_SHUF0(e0, e1, 0);\
            TEST_SHUF0(e0, e1, 1);\
            TEST_SHUF0(e0, e1, 2);\
            TEST_SHUF0(e0, e1, 3)
#define TEST_SHUF2(e0)\
            TEST_SHUF1(e0, 0);\
            TEST_SHUF1(e0, 1);\
            TEST_SHUF1(e0, 2);\
            TEST_SHUF1(e0, 3)

            TEST_SHUF2(0);
            TEST_SHUF2(1);
            TEST_SHUF2(2);
            TEST_SHUF2(3);
#undef TEST_SHUF2
#undef TEST_SHUF1
#undef TEST_SHUF0
#undef TEST_SHUFFLE
        }
    }
    PrintHeader(Indent, "Testing mat4 ops:");
    {
        la_vec4f A = Vec4f(1, 2, 3, 4);
        la_mat4f Identity = Mat4f_Identity(1);
        la_mat4f Test = Mat4f(
            1, 2, 3, 4,
            5, 6, 7, 8, 
            9, 10, 11, 12, 
            13, 14, 15, 16
        );
        la_mat4f Test2 = Mat4f(
            16,15,14,13,
            12,11,10, 9,
             8, 7, 6, 5,
             4, 3, 2, 1
        );
        m4f TestRaw = Mat4f_ToRaw(&Test);
        la_mat4f TestTimesTest2Result = Mat4f(
            80, 70, 60, 50, 
            240, 214, 188, 162, 
            400, 358, 316, 274, 
            560, 502, 444, 386
        );

        (void)eprintfln("Test(raw)        = "); PrintM4Raw(Indent, TestRaw);
        (void)eprintfln("A                = "); PrintV4Indented(Indent, A);
        (void)eprintfln("Identity         = "); PrintM4(Indent, Identity);
        (void)eprintfln("Test             = "); PrintM4(Indent, Test);
        (void)eprintfln("Test2             = "); PrintM4(Indent, Test2);
        (void)eprintfln("Identity * A     = "); PrintV4Indented(Indent, Mat4f_Mul_Vec4f(&Identity, A));
        (void)eprintfln("Identity^2       = "); PrintM4(Indent, Mat4f_Mul_Mat4f(&Identity, &Identity));
        UNREACHABLE_IF(!CheckMatEquality(Identity, Mat4f_Mul_Mat4f(&Identity, &Identity)),
            "TEST MAT4F MULTIPLY FAILED: Identity^2"
        );
        (void)eprintfln("Test * Identity  = "); PrintM4(Indent, Mat4f_Mul_Mat4f(&Test, &Identity));
        UNREACHABLE_IF(!CheckMatEquality(Test, Mat4f_Mul_Mat4f(&Test, &Identity)),
            "TEST MAT4F MULTIPLY FAILED: Test * Identity"
        );
        (void)eprintfln("Test * Test2  = "); PrintM4(Indent, Mat4f_Mul_Mat4f(&Test, &Test2));
        UNREACHABLE_IF(!CheckMatEquality(TestTimesTest2Result, Mat4f_Mul_Mat4f(&Test, &Test2)),
            "TEST MAT4F MULTIPLY FAILED: Test * Test2"
        );

        la_vec3f CameraOrigin = Vec3f(2, 2, 2);
        la_vec3f CameraTarget = Vec3f(0);
        la_vec3f CameraUp = Vec3f(0, 0, 1);
        float FOV = 1080.0 / 720;

        la_vec3f Z = Vec3f_Sub(CameraOrigin, CameraTarget);
        Z = Vec3f_Normalize(Z);
        PrintV3(Z);

        la_mat4f Model = LA_RotationMatrixZ(LA_TO_RADIANF(90.0f));
        la_mat4f View = LA_LookAt(CameraOrigin, CameraTarget, CameraUp);
        la_mat4f Proj = LA_Perspective(LA_TO_RADIANF(45.0f), FOV, 0.1f, 10.0f);
        Proj.Rows[1] = Vec4f_Mul(Proj.Rows[1], Vec4f(1, -1, 1, 1)); /* flip y-axis to compensate for vulkan's inverted y */
        (void)eprintfln("Model            = "); PrintM4(Indent, Model);
        (void)eprintfln("View(raw)        = "); PrintM4(Indent, View);
        (void)eprintfln("Proj(raw)        = "); PrintM4(Indent, (Proj));

        /*
        Model = Mat4f_ToRaw(&Model),
        View = Mat4f_ToRaw(&View),
        Projection = Mat4f_ToRaw(&Proj),
         * */
    }
}

