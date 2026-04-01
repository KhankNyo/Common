#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#ifndef MEMORY_H
#define MEMORY_H

#include "Common.h"


typedef_struct(memory_alloc_parameter);
typedef_struct(memory_alloc_interface);
typedef void *(*memory_alloc_routine)(void *UserData, const memory_alloc_parameter *Param);

#define Memory_Alloc(p_interface, isize_size_bytes, u32_alignment) \
    (p_interface)->Routine((p_interface)->UserData, &(memory_alloc_parameter) {\
        .Mode = ALLOCATOR_ALLOCATE,\
        .As.Allocate = {\
            .SizeBytes = (isize_size_bytes), \
            .Alignment = (u32_alignment), \
        }\
    })

#define Memory_Free(p_interface, p_buffer) \
    (void)(p_interface)->Routine((p_interface)->UserData, &(memory_alloc_parameter) {\
        .Mode = ALLOCATOR_FREE,\
        .As.Free = {\
            .Ptr = (p_buffer),\
        }\
    })

typedef enum 
{
    ALLOCATOR_ALLOCATE,
    ALLOCATOR_FREE,
} memory_alloc_mode;

struct memory_alloc_parameter
{
    memory_alloc_mode Mode;
    union {
        struct {
            isize SizeBytes;
            u32 Alignment;
        } Allocate;
        struct {
            void *Ptr;
        } Free;
    } As;
};

struct memory_alloc_interface
{
    void *UserData;
    memory_alloc_routine Routine;
};


force_inline i64 Memory_AlignSize(i64 Size, u64 Alignment)
{
    ASSERT(IS_POWER_OF_2(Alignment), "Alignment must be a power of 2");
    if ((u64)Size & (Alignment - 1))
        return ((u64)Size + Alignment) & ~(Alignment - 1);
    else
        return Size;
}

force_inline i64 Memory_AlignSizeDown(i64 Size, u64 Alignment)
{
    ASSERT(IS_POWER_OF_2(Alignment), "Alignment must be a power of 2");
    if ((u64)Size & (u64)(Alignment - 1))
        return (u64)Size & ~(Alignment - 1);
    return Size;
}

force_inline void *Memory_AlignPointer(void *Pointer, usize Alignment)
{
    return (void *)Memory_AlignSize((isize)Pointer, Alignment);
}


#endif /* MEMORY_H */


#ifdef __cplusplus
}
#endif /* __cplusplus */
