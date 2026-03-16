#ifndef FREE_LIST_H
#define FREE_LIST_H


#include "Common.h"
#include "Memory.h"


typedef_struct(freelist_alloc);
typedef_struct(freelist__header);

struct freelist_alloc
{
    arena_alloc *Arena;
    freelist__header *Alloced;
    freelist__header *Freed;
};
struct freelist__header
{
    freelist__header *Next, *Prev;
    isize SizeBytes, CapacityBytes;
};


void FreeList_Create(freelist_alloc *Allocator);
void FreeList_CreateWithArena(freelist_alloc *Allocator, arena_alloc *Arena);
void FreeList_Destroy(freelist_alloc *Allocator);

void *FreeList_Alloc(freelist_alloc *Allocator, isize SizeBytes);
void *FreeList_AllocNonZero(freelist_alloc *Allocator, isize SizeBytes);
void *FreeList_Realloc(freelist_alloc *Allocator, void *Ptr, isize SizeBytes);
void *FreeList_ReallocNonZero(freelist_alloc *Allocator, isize SizeBytes);
void FreeList_Free(freelist_alloc *Allocator, isize SizeBytes);

#define FreeList_AllocArray(p_freelist, p_array_ptr, isize_count) \
    (*(p_array_ptr) = FreeList_Alloc(p_freelist, (isize_count) * sizeof((p_array_ptr)[0][0])))
#define FreeList_AllocArrayNonZero(p_freelist, p_array_ptr, isize_count) \
    (*(p_array_ptr) = FreeList_AllocNonZero(p_freelist, (isize_count) * sizeof((p_array_ptr)[0][0])))
#define FreeList_ReallocArray(p_freelist, p_array_ptr, isize_count) \
    (*(p_array_ptr) = FreeList_Realloc(p_freelist, *(p_array_ptr), (isize_count) * sizeof((p_array_ptr)[0][0])))
#define FreeList_ReallocArrayNonZero(p_freelist, p_array_ptr, isize_count) \
    (*(p_array_ptr) = FreeList_ReallocNonZero(p_freelist, *(p_array_ptr), (isize_count) * sizeof((p_array_ptr)[0][0])))



#endif /* FREE_LIST_H */

#if defined(FREE_LIST_IMPLEMENTATION) && !defined(FREE_LIST_ALREADY_IMPLEMENTED)
#define FREE_LIST_ALREADY_IMPLEMENTED

#endif /* FREE_LIST_IMPLEMENTATION */


