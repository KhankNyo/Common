#include "Common.h"
#define FREE_LIST_IMPLEMENTATION
#include "FreeList.h"

#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */


internal int g_FreeCount, g_AllocCount;

internal void *Alloc(void *UserData, isize SizeBytes, usize Alignment)
{
    (void)UserData, (void)Alignment;
    g_AllocCount++;
    return malloc(SizeBytes);
}

internal void Free(void *UserData, void *Ptr)
{
    (void)UserData;
    g_FreeCount++;
    free(Ptr);
}

internal arena_user_allocator g_StdAlloc = {
    .Allocate = Alloc,
    .Free = Free,
};
internal int g_Index;
#define CHECK_MEM(p, b, s) ASSERT(CheckMem(p, b, s), "FAILED: %s[%d] = %02x (expected %02x)", #p, g_Index, (p)[g_Index], b)

internal bool32 CheckMem(const u8 *Ptr, u8 Byte, isize SizeBytes)
{
    for (int i = 0; i < SizeBytes; i++)
    {
        if (Ptr[i] != Byte)
        {
            g_Index = i;
            return false;
        }
    }
    return true;
}

internal void BasicTests(void)
{
    freelist_alloc Alloc;
    FreeList_Create(&Alloc, g_StdAlloc, 64, 1);
    {
        u8 *E = FreeList_Alloc(&Alloc, 16);
        memset(E, 'e', 16);
        {
            u8 *D;
            {
                u8 *C; 
                {
                    u8 *B; 
                    {
                        u8 *A = FreeList_Alloc(&Alloc, 128);
                        {
                    /* NOTE: intentional indentation, B's lifetime starts here */
                    B = FreeList_Alloc(&Alloc, 90);
                /* NOTE: intentional indentation, C's lifetime starts here */
                C = FreeList_Alloc(&Alloc, 16);
                            memset(A, 'a', 128);
                            memset(B, 'b', 90);
                            memset(C, 'c', 16);
                            CHECK_MEM(A, 'a', 128);
                            CHECK_MEM(B, 'b', 90);
                            CHECK_MEM(C, 'c', 16);
                            CHECK_MEM(E, 'e', 16);
                        }
                        FreeList_Free(&Alloc, A);
                        CHECK_MEM(B, 'b', 90);
                        CHECK_MEM(C, 'c', 16);
                        CHECK_MEM(E, 'e', 16);

                        B = FreeList_Realloc(&Alloc, B, 128);
                        CHECK_MEM(B, 'b', 90);
                        CHECK_MEM(C, 'c', 16);
                        CHECK_MEM(E, 'e', 16);
                        memset(B + 90, 'b', 128 - 90);
                        CHECK_MEM(B, 'b', 128);
                        CHECK_MEM(C, 'c', 16);
                        CHECK_MEM(E, 'e', 16);

                        C = FreeList_Realloc(&Alloc, C, 24);
                        CHECK_MEM(B, 'b', 128);
                        CHECK_MEM(C, 'c', 16);
                        CHECK_MEM(E, 'e', 16);
                        memset(C, 'c', 24);
                        CHECK_MEM(B, 'b', 128);
                        CHECK_MEM(E, 'e', 16);
                    }
                    FreeList_Free(&Alloc, B);
                    CHECK_MEM(C, 'c', 24);
                    CHECK_MEM(E, 'e', 16);

                    C = FreeList_Realloc(&Alloc, C, 32);
                    CHECK_MEM(C, 'c', 24);
                    CHECK_MEM(E, 'e', 16);
                    memset(C, 'c', 32);
                    CHECK_MEM(C, 'c', 32);
                    CHECK_MEM(E, 'e', 16);

            /* NOTE: intentional indentation, D's lifetime starts here */
            D = FreeList_Alloc(&Alloc, 16);
                    CHECK_MEM(C, 'c', 32);
                    CHECK_MEM(E, 'e', 16);
                    memset(D, 'd', 16);
                    CHECK_MEM(C, 'c', 32);
                    CHECK_MEM(E, 'e', 16);

                    C = FreeList_Realloc(&Alloc, C, 48);
                    CHECK_MEM(C, 'c', 32);
                    CHECK_MEM(D, 'd', 16);
                    CHECK_MEM(E, 'e', 16);
                    memset(C, 'c', 48);
                    CHECK_MEM(D, 'd', 16);
                    CHECK_MEM(E, 'e', 16);

                    D = FreeList_Realloc(&Alloc, D, 32);
                    CHECK_MEM(C, 'c', 48);
                    CHECK_MEM(D, 'd', 16);
                    CHECK_MEM(E, 'e', 16);
                    memset(D, 'd', 32);
                    CHECK_MEM(C, 'c', 48);
                    CHECK_MEM(E, 'e', 16);
                }
                FreeList_Free(&Alloc, C);
                CHECK_MEM(E, 'e', 16);

                D = FreeList_Realloc(&Alloc, D, 48);
                CHECK_MEM(D, 'd', 32);
                CHECK_MEM(E, 'e', 16);
            }
            FreeList_Free(&Alloc, D);
            CHECK_MEM(E, 'e', 16);
        }
        E = FreeList_Realloc(&Alloc, E, 24);
        CHECK_MEM(E, 'e', 16);
    }
    FreeList_Destroy(&Alloc);
}

internal void SizingTests(void)
{
    g_FreeCount = 0;
    g_AllocCount = 0;
    freelist_alloc FreeList;
    FreeList_Create(&FreeList, g_StdAlloc, 4096, 8);
    {
        int ArrayCount = 16;
        u8 **Arrays; 
        FreeList_AllocArray(&FreeList, &Arrays, ArrayCount);
        for (int i = 0; i < ArrayCount; i++)
        {
            FreeList_AllocArray(&FreeList, &Arrays[i], 1);
            Arrays[i][0] = 'a' + i;
        }

        for (int i = 0; i < ArrayCount; i++)
        {
            CHECK_MEM(Arrays[i], 'a' + i, 1);
        }

        for (int i = 0; i < ArrayCount; i++)
        {
            Arrays[i] = FreeList_Realloc(&FreeList, Arrays[i], 16);
            memset(Arrays[i], 'a' + ArrayCount + i, 16);
        }

        for (int i = 0; i < ArrayCount; i++)
        {
            CHECK_MEM(Arrays[i], 'a' + ArrayCount + i, 16);
        }
    }
    FreeList_Destroy(&FreeList);
    ASSERT(g_FreeCount == 1 && g_AllocCount == 1, "Must be able to use only 1 alloc and free, used: %d, %d", g_AllocCount, g_FreeCount);
}


int main(void)
{
    BasicTests();
    (void)printfln("FreeList Basic tests ok: frees: %d, allocs: %d.", g_FreeCount, g_AllocCount);
    g_FreeCount = 0;
    g_AllocCount = 0;
    SizingTests();
    (void)printfln("FreeList Sizing tests ok: frees: %d, allocs: %d.", g_FreeCount, g_AllocCount);
    return 0;
}
