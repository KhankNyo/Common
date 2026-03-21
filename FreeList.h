#ifndef FREE_LIST_H
#define FREE_LIST_H


#include "Common.h"
#include "Memory.h"


typedef_struct(freelist_alloc);
typedef_struct(freelist__header);
typedef_struct(freelist__pool_header);

struct freelist_alloc
{
    memory_alloc_interface UserAlloc;
    i64 DefaultPoolCapacityBytes;   /* should only be set once (during FreeList_Create()) */
    i32 Back;
    u32 Alignment;                  /* should only be set once (during FreeList_Create()) */
    freelist__pool_header *Pool;
    freelist__header *Alloced;
    freelist__header *Freed;
};



void FreeList_Create(freelist_alloc *Allocator, memory_alloc_interface UserAlloc, i64 PoolCapacityBytes, u32 Alignment);
void FreeList_Destroy(freelist_alloc *Allocator);
void FreeList_Reset(freelist_alloc *Allocator);

void *FreeList_Alloc(freelist_alloc *Allocator, i32 SizeBytes);
void *FreeList_AllocNonZero(freelist_alloc *Allocator, i32 SizeBytes);
void *FreeList_Realloc(freelist_alloc *Allocator, void *Ptr, i32 SizeBytes);
void *FreeList_ReallocNonZero(freelist_alloc *Allocator, void *Ptr, i32 SizeBytes);
void FreeList_Free(freelist_alloc *Allocator, void *Ptr);

/* freelist_alloc *p_freelist, array_member_type **pp_array, isize isize_count */
#define FreeList_AllocArray(p_freelist, pp_array, isize_count) \
    (*(pp_array) = FreeList_Alloc(p_freelist, (isize_count) * sizeof((pp_array)[0][0])))

/* freelist_alloc *p_freelist, array_member_type **pp_array, isize isize_count */
#define FreeList_AllocArrayNonZero(p_freelist, pp_array, isize_count) \
    (*(pp_array) = FreeList_AllocNonZero(p_freelist, (isize_count) * sizeof((pp_array)[0][0])))

/* freelist_alloc *p_freelist, array_member_type **pp_array, isize isize_count */
#define FreeList_ReallocArray(p_freelist, pp_array, isize_count) \
    (*(pp_array) = FreeList_Realloc(p_freelist, *(pp_array), (isize_count) * sizeof((pp_array)[0][0])))

/* freelist_alloc *p_freelist, array_member_type **pp_array, isize isize_count */
#define FreeList_ReallocArrayNonZero(p_freelist, pp_array, isize_count) \
    (*(pp_array) = FreeList_ReallocNonZero(p_freelist, *(pp_array), (isize_count) * sizeof((pp_array)[0][0])))




#endif /* FREE_LIST_H */

#if defined(FREE_LIST_IMPLEMENTATION) && !defined(FREE_LIST_ALREADY_IMPLEMENTED)
#define FREE_LIST_ALREADY_IMPLEMENTED

#include <string.h> /* memset */

#define FL__POOL_FREE_SPACE_CAP(p_alloc) (i64)((p_alloc)->DefaultPoolCapacity - sizeof(freelist__pool_header) - sizeof(freelist__header))
#define FL__HEADER_FROM_PTR(p_alloc, ptr) ((freelist__header *)((u8 *)(ptr) - (p_alloc)->Back))

struct freelist__header
{
    freelist__header *Next, *Prev;
    i32 SizeBytes, CapacityBytes;
    /* data after, depends on alignment */
};
struct freelist__pool_header
{
    freelist__pool_header *Next;
    i64 CapacityBytes;
    /* data after */
};



internal freelist__header *FreeList__FirstNodeFromPool(void *Pool, i64 PoolCapcityBytes)
{
    u8 *PoolPtr = Pool;
    i64 FreeSpaceCapacity = PoolCapcityBytes - sizeof(freelist__pool_header) - sizeof(freelist__header);
    u8 *FreeSpace = (PoolPtr + sizeof(freelist__pool_header));
    freelist__header *FreeSpaceHeader = Memory_AlignPointer(FreeSpace, alignment_of(freelist__header));
    *FreeSpaceHeader = (freelist__header) {
        .CapacityBytes = FreeSpaceCapacity,
    };
    return FreeSpaceHeader;
}


internal void FreeList__PushNewPool(freelist_alloc *Allocator, i64 PoolCapcityBytes, freelist__header **OutFirstFreeHeader)
{
    u32 Alignment = MAXIMUM(Allocator->Alignment, alignment_of(freelist__pool_header));
    PoolCapcityBytes = Memory_AlignSize(PoolCapcityBytes, Alignment);

    freelist__pool_header *Pool = Memory_Alloc(&Allocator->UserAlloc, PoolCapcityBytes, Alignment);
    Pool->Next = Allocator->Pool;
    Pool->CapacityBytes = PoolCapcityBytes;
    Allocator->Pool = Pool;
    *OutFirstFreeHeader = FreeList__FirstNodeFromPool(Pool, PoolCapcityBytes);
}

internal void FreeList__InsertFreeNode(freelist_alloc *Allocator, freelist__header *FreeNode)
{
    /* insert by increasing addr */
    freelist__header *Curr = Allocator->Freed;
    freelist__header *Prev = NULL;
    while (Curr && (uintptr_t)FreeNode > (uintptr_t)Curr)
    {
        Prev = Curr;
        Curr = Curr->Next;
    }
    if (Prev)
    {
        Prev->Next = FreeNode;
        FreeNode->Prev = Prev;
    }
    else
    {
        /* first node */
        Allocator->Freed = FreeNode;
        FreeNode->Prev = NULL;
    }
    FreeNode->Next = Curr;
    if (Curr)
        Curr->Prev = FreeNode;
}

void FreeList_Create(freelist_alloc *Allocator, memory_alloc_interface UserAlloc, i64 PoolCapacityBytes, u32 Alignment)
{
    Alignment = MAXIMUM(Alignment, MAXIMUM(alignment_of(freelist__pool_header), alignment_of(freelist__header)));
    PoolCapacityBytes = Memory_AlignSize(PoolCapacityBytes + sizeof(freelist__pool_header) + sizeof(freelist__header), Alignment);

    isize AlignedHeaderSize = Memory_AlignSize(sizeof(freelist__header), Alignment);
    *Allocator = (freelist_alloc) {
        .UserAlloc = UserAlloc,
        .DefaultPoolCapacityBytes = PoolCapacityBytes,
        .Alignment = Alignment,
        .Back = MAXIMUM(Alignment, AlignedHeaderSize),
    };
    FreeList__PushNewPool(Allocator, PoolCapacityBytes, &Allocator->Freed);
}

void FreeList_Destroy(freelist_alloc *Allocator)
{
    memory_alloc_interface UserAlloc = Allocator->UserAlloc;
    freelist__pool_header *Pool = Allocator->Pool;
    while (Pool)
    {
        freelist__pool_header *Next = Pool->Next;
        Memory_Free(&UserAlloc, Pool);
        Pool = Next;
    }
}

void FreeList_Reset(freelist_alloc *Allocator)
{
    freelist__pool_header *Pool = Allocator->Pool;
    Allocator->Freed = NULL;
    while (Pool)
    {
        freelist__header *FirstNodeFromPool = FreeList__FirstNodeFromPool(Pool, Pool->CapacityBytes);
        FreeList__InsertFreeNode(Allocator, FirstNodeFromPool);
        Pool = Pool->Next;
    }
}

void *FreeList_AllocNonZero(freelist_alloc *Allocator, i32 SizeBytes)
{
    u32 Alignment = Allocator->Alignment;
    i64 AlignedSizeBytes = Memory_AlignSize(SizeBytes, Alignment);

    /* search free list */
    freelist__header *FreeNode = NULL;
    for (freelist__header *i = Allocator->Freed; i; i = i->Next)
    {
        bool32 Fits = AlignedSizeBytes <= i->CapacityBytes;
        if (Fits)
        {
            FreeNode = i;
            break;
        }
    }
    if (!FreeNode)
    {
        /* allocate a new pool and get a free list from there */
        i64 PoolCapacityBytes = AlignedSizeBytes + sizeof(freelist__header) + sizeof(freelist__pool_header);
        if (PoolCapacityBytes < Allocator->DefaultPoolCapacityBytes)
            PoolCapacityBytes = Allocator->DefaultPoolCapacityBytes;
        FreeList__PushNewPool(Allocator, PoolCapacityBytes, &FreeNode);
    }
    else
    {
        /* unlink FreeNode */
        freelist__header *Prev = FreeNode->Prev;
        freelist__header *Next = FreeNode->Next;
        if (Prev)
            Prev->Next = Next;
        else
            Allocator->Freed = Next;
        if (Next)
            Next->Prev = Prev;
    }

    /* divide free header */
    i64 NewFreeNodeSize = FreeNode->CapacityBytes - AlignedSizeBytes;
    bool32 Divisible = NewFreeNodeSize >= (i64)sizeof(freelist__header) + Alignment;
    if (Divisible)
    {
        u8 *Ptr = (u8 *)(FreeNode + 1);
        freelist__header *FreeNodeFromSplit = (freelist__header *)(Ptr + AlignedSizeBytes);
        *FreeNodeFromSplit = (freelist__header) {
            .CapacityBytes = NewFreeNodeSize - sizeof(freelist__header),
        };
        FreeList__InsertFreeNode(Allocator, FreeNodeFromSplit);
        FreeNode->CapacityBytes = AlignedSizeBytes;
    }
    FreeNode->SizeBytes = SizeBytes;

    /* link with allocated list */
    {
        FreeNode->Prev = NULL;
        FreeNode->Next = Allocator->Alloced;
        if (Allocator->Alloced)
            Allocator->Alloced->Prev = FreeNode;
        Allocator->Alloced = FreeNode;
    }

    /* return aligned pointer */
    void *Ptr = Memory_AlignPointer(FreeNode + 1, Alignment);
    ASSERT((uintptr_t)Ptr + SizeBytes <= (uintptr_t)FreeNode + sizeof(*FreeNode) + FreeNode->CapacityBytes,
        "SizeBytes: %lli, AlignedSizeBytes: %lli, CapacityBytes: %lli", 
        (long long)SizeBytes, (long long)AlignedSizeBytes, (long long)FreeNode->CapacityBytes
    );
    return Ptr;
}

void FreeList_Free(freelist_alloc *Allocator, void *Ptr)
{
    /* unlink from allocated list */
    freelist__header *FreeNode = FL__HEADER_FROM_PTR(Allocator, Ptr);
    {
        freelist__header *Prev = FreeNode->Prev;
        freelist__header *Next = FreeNode->Next;
        if (Prev)
            Prev->Next = Next;
        else 
            Allocator->Alloced = Next;
        if (Next)
            Next->Prev = Prev;

        FreeNode->Prev = NULL;
        FreeNode->Next = NULL;
    }

    /* insert free node then coalesce */
    FreeList__InsertFreeNode(Allocator, FreeNode);
    freelist__header *Next = FreeNode->Next;
    while (Next 
        && (uintptr_t)(FreeNode + 1) + FreeNode->CapacityBytes == (uintptr_t)Next)
    {
        FreeNode->CapacityBytes += Next->CapacityBytes + sizeof(*Next);
        Next = Next->Next;
    }
    FreeNode->Next = Next;
}

void *FreeList_ReallocNonZero(freelist_alloc *Allocator, void *Ptr, i32 SizeBytes)
{
    if (NULL == Ptr)
    {
        return FreeList_AllocNonZero(Allocator, SizeBytes);
    }

    freelist__header *Header = FL__HEADER_FROM_PTR(Allocator, Ptr);
    if (SizeBytes < Header->CapacityBytes)
    {
        Header->SizeBytes = SizeBytes;
        return Ptr;
    }
    else
    {
        /* must allocate new ptr */
        FreeList_Free(Allocator, Ptr);
        /* NOTE: data in ptr is still valid */
        void *NewPtr = FreeList_AllocNonZero(Allocator, SizeBytes);
        if ((uintptr_t)NewPtr != (uintptr_t)Ptr)
        {
            memcpy(NewPtr, Ptr, Header->SizeBytes);
        }
        return NewPtr;
    }
}

void *FreeList_Realloc(freelist_alloc *Allocator, void *Ptr, i32 SizeBytes)
{
    if (NULL == Ptr)
    {
        return FreeList_Alloc(Allocator, SizeBytes);
    }

    isize NonZeroSize = FL__HEADER_FROM_PTR(Allocator, Ptr)->SizeBytes;
    void *NewPtr = FreeList_ReallocNonZero(Allocator, Ptr, SizeBytes);
    if (SizeBytes > NonZeroSize)
    {
        memset((u8 *)NewPtr + NonZeroSize, 0, SizeBytes - NonZeroSize);
    }
    return NewPtr;
}

void *FreeList_Alloc(freelist_alloc *Allocator, i32 SizeBytes)
{
    void *Ptr = FreeList_AllocNonZero(Allocator, SizeBytes);
    memset(Ptr, 0, SizeBytes);
    return Ptr;
}



#endif /* FREE_LIST_IMPLEMENTATION */


