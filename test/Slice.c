
#include "Common.h"
#include "Slice.h"
#define MEMORY_IMPLEMENTATION
#include "Memory.h"




internal void *Allocate(void *UserData, isize SizeBytes, usize Alignment)
{
    (void)UserData, (void)Alignment;
    void *Ptr = malloc(SizeBytes);
    ASSERT(Ptr, "malloc()");
    return Ptr;
}

internal void Free(void *UserData, void *Ptr)
{
    (void)UserData;
    free(Ptr);
}

internal arena_user_allocator g_StdAllocator = {
    .Allocate = Allocate,
    .Free = Free,
};

typedef struct 
{
    int Value;
} test_data;

int main(void)
{
    arena_alloc Arena, TmpArena;
    Arena_Create(&Arena, g_StdAllocator, 16, 16);
    Arena_Create(&TmpArena, g_StdAllocator, 16, 16);
    {
        slice(test_data) A; 
        slice(i8) B; 
        slice(i32) C;
        (void)printfln("Testing slice builder: ");
        Arena_Scope(&TmpArena)
        {
            slice_builder(&A, 8) BuilderA = { .Arena = &TmpArena, };
            slice_builder(&B, 3) BuilderB = { .Arena = &TmpArena, };
            slice_builder(&C, 1) BuilderC = { .Arena = &TmpArena, };
            for (int i = 0; i < 20; i++)
            {
                SliceBuilder_Push(&BuilderA, (test_data) { i });
                SliceBuilder_Push(&BuilderB, i + 1);
                SliceBuilder_Push(&BuilderC, i * 2);
            }

            SliceBuilder_CopyToSlice(&BuilderA, &A, &Arena);
            SliceBuilder_CopyToSlice(&BuilderB, &B, &Arena);

            SliceBuilder_CopyToSlice(&BuilderC, &C, &TmpArena);
            (void)printfln("====================== C Initial: ");
            Slice_Foreach(&C, i)
            {
                (void)printfln("%d: C=%d", (int)Slice_GetIteratorIndex(&C, i), *i);
            }

            SliceBuilder_Reset(&BuilderC);
            for (int i = 0; i < 9; i++)
            {
                SliceBuilder_Push(&BuilderC, i + 1);
            }

            (void)printfln("====================== Accessing slice builder: ");
            (void)printfln("====================== BuilderC: ");
            for (int i = 0; i < 9; i++)
            {
                (void)printfln("  %d: %d", i, SliceBuilder_Get(&BuilderC, i));
            }
            SliceBuilder_CopyToSlice(&BuilderC, &C, &Arena);
        }
        /* zeroing out memory */
        Arena_Alloc(&Arena, 1024);

        (void)printfln("====================== C Reset: ");
        Slice_Foreach(&C, i)
        {
            (void)printfln("%d: C=%d", (int)Slice_GetIteratorIndex(&C, i), *i);
        }

        (void)printfln("====================== A, B: ");
        for (int i = 0; i < A.Count; i++)
        {
            (void)printfln("%d: A=%d, B=%d", i, A.Data[i].Value, B.Data[i]);
        }
    }
    Arena_Destroy(&Arena);
    Arena_Destroy(&TmpArena);
    (void)printfln("Done");
    return 0;
}
