#ifndef ARENA_H
#define ARENA_H

#include "Memory.h"
#include "Common.h"

typedef_struct(arena_context);
typedef_struct(arena_alloc);
typedef_struct(arena_snapshot);
typedef_struct(arena__node);

/* NOTE: use big capacity, 64mb x 64 nodes = 4096mb = 4gb */
struct arena_alloc 
{
    memory_alloc_interface UserAlloc;
    arena__node *Begin;
    arena__node *Curr;
    u32 Alignment;
};

struct arena_snapshot
{
    arena__node *Node;
    u8 *Ptr;
};

struct arena_context
{
    arena_snapshot RestorePoint;
    u8 *ArenaEndPtr;
};

struct arena__scope_data
{
    arena_snapshot Snapshot;
    int i;
};

struct arena__node 
{
    arena__node *Next;
    u8 *Ptr;
    u8 *End;
    /* data after */
};


force_inline void Arena_Create(arena_alloc *Arena, memory_alloc_interface UserAlloc, i64 PoolSizeBytes, u32 Alignment);
force_inline void Arena_Destroy(arena_alloc *Arena);
force_inline void Arena_Reset(arena_alloc *Arena);

memory_alloc_interface Arena_AsAllocInterface(arena_alloc *Arena);

header_function isize Arena_GetAllocatedSize(const arena_alloc *Arena);
header_function isize Arena_GetAllocatedCapacity(const arena_alloc *Arena);
void *Arena_Alloc(arena_alloc *Arena, i64 SizeBytes);
void *Arena_AllocNonZero(arena_alloc *Arena, i64 SizeBytes);

#define Arena_SetAlignment(p_arena, alignment) ((p_arena)->Alignment = (alignment))
#define Arena_AllocArray(p_arena, p_array, count) (*(p_array) = Arena_Alloc(p_arena, sizeof((*(p_array))[0]) * (count)))

/* allocate an array without zeroing memory */
#define Arena_AllocArrayNonZero(p_arena, p_array, count) (*(p_array) = Arena_AllocNonZero(p_arena, sizeof((*(p_array))[0]) * (count)))

/* USAGE: 
 *
 * arena_alloc Arena = Arena_Create(...);
 * Arena_Scope(&Arena) 
 * {
 *      ... temporary resources that use the arena
 * }
 * // resources will be automatically freed after the block
 * */
#define Arena_Scope(p_arena) for (\
    struct arena__scope_data arena__scope_data = { .Snapshot = Arena_SaveSnapshot(p_arena), .i = 1}; \
    arena__scope_data.i; \
    (arena__scope_data.i = 0), Arena_RestoreSnapshot(p_arena, arena__scope_data.Snapshot))

#define Arena_ScopedAlignment(p_arena, u32_alignment) for (\
    u32 i = 1, \
        scoped_alignment_restore = (p_arena)->Alignment, \
        scoped_alignment_dummy = Arena_SetAlignment(p_arena, u32_alignment); \
    i;\
    (i = 0), \
    (void)scoped_alignment_dummy, \
    Arena_SetAlignment(p_arena, scoped_alignment_restore))

force_inline arena_snapshot Arena_SaveSnapshot(arena_alloc *Arena);
force_inline void Arena_RestoreSnapshot(arena_alloc *Arena, arena_snapshot Snapshot);

force_inline arena_context Arena_BeginContext(arena_alloc *Arena);
force_inline void Arena_EndContext(arena_alloc *Arena, arena_context *Context);
force_inline bool32 Arena_TryPopContext(arena_alloc *Arena, arena_context *Context);



force_inline i64 Arena_GetNodeCapacity(const arena__node *Node)
{
    return Node->End - (u8 *)(Node + 1);
}

force_inline i64 Arena_GetNodeRemainingSize(const arena__node *Node)
{
    return Node->End - Node->Ptr;
}

force_inline void Arena__ResetNode(arena__node *Node, arena__node *Next, i64 TotalSizeBytes)
{
    u8 *Ptr = (u8 *)(Node + 1);
    u8 *End = (u8 *)Node + TotalSizeBytes;
    *Node = (arena__node) {
        .Next = Next,
        .Ptr = Ptr,
        .End = End,
    };
}

force_inline arena__node *Arena__NewNode(arena_alloc *Arena, arena__node *Next, i64 PoolSizeBytes, u32 Alignment)
{
    arena__node *Node = NULL;
    {
        i64 TotalSizeBytes = Memory_AlignSize(PoolSizeBytes + sizeof(Node[0]), Alignment);
        Node = (typeof(Node))Memory_Alloc(&Arena->UserAlloc, TotalSizeBytes, alignment_of(arena__node));
        ASSERT(Node);
        Arena__ResetNode(Node, Next, TotalSizeBytes);
    }
    return Node;
}

force_inline void Arena_Reset(arena_alloc *Arena)
{
    Arena->Curr = Arena->Begin;
    i64 TotalSizeBytes = Arena_GetNodeCapacity(Arena->Curr) + sizeof(*Arena->Curr);
    Arena__ResetNode(Arena->Curr, Arena->Curr->Next, TotalSizeBytes);
}

force_inline void Arena_Create(arena_alloc *Arena, memory_alloc_interface UserAlloc, i64 PoolSizeBytes, u32 Alignment)
{
    ASSERT(PoolSizeBytes > 0);
    *Arena = (arena_alloc) {
        .Alignment = Alignment,
        .UserAlloc = UserAlloc,
    };
    arena__node *Head = Arena__NewNode(Arena, NULL, PoolSizeBytes, Alignment);
    Arena->Begin = Head;
    Arena->Curr = Head;
}

force_inline void Arena_Destroy(arena_alloc *Arena)
{
    memory_alloc_interface UserAlloc = Arena->UserAlloc;
    for (arena__node *i = Arena->Begin; i;)
    {
        arena__node *Next = i->Next;
        Memory_Free(&UserAlloc, i);
        i = Next;
    }
    /* NOTE: arena might be located inside one of the blocks, 
     * so we shouldn't touch the 'Arena' pointer after deleting all blocks, 

        *Arena = (arena_alloc) { 0 };
     */
}

header_function isize Arena_GetAllocatedSize(const arena_alloc *Arena)
{
    isize BytesAllocated = 0;
    arena__node *i = Arena->Begin;
    while (i && i != Arena->Curr)
    {
        BytesAllocated += Arena_GetNodeCapacity(i) - Arena_GetNodeRemainingSize(i);
        i = i->Next;
    }
    if (i)
        BytesAllocated += Arena_GetNodeCapacity(i) - Arena_GetNodeRemainingSize(i);
    return BytesAllocated;
}

header_function isize Arena_GetAllocatedCapacity(const arena_alloc *Arena)
{
    isize Capacity = 0;
    arena__node *i = Arena->Begin;
    while (i)
    {
        Capacity += Arena_GetNodeCapacity(i);
        i = i->Next;
    }
    return Capacity;
}

force_inline arena_snapshot Arena_SaveSnapshot(arena_alloc *Arena)
{
    return (arena_snapshot) {
        .Node = Arena->Curr,
        .Ptr = Arena->Curr->Ptr,
    };
}

force_inline void Arena_RestoreSnapshot(arena_alloc *Arena, arena_snapshot Snapshot)
{
    Snapshot.Node->Ptr = Snapshot.Ptr;
    Arena->Curr = Snapshot.Node;
}

force_inline arena_context Arena_BeginContext(arena_alloc *Arena)
{
    arena_context Context = {
        .RestorePoint = Arena_SaveSnapshot(Arena),
    };
    return Context;
}

force_inline void Arena_EndContext(arena_alloc *Arena, arena_context *Context)
{
    Context->ArenaEndPtr = (typeof(Context->ArenaEndPtr))Arena_Alloc(Arena, 0); /* ensure alignment */
}

force_inline bool32 Arena_TryPopContext(arena_alloc *Arena, arena_context *Context)
{
    if (Arena->Curr->Ptr == Context->ArenaEndPtr)
    {
        Arena_RestoreSnapshot(Arena, Context->RestorePoint);
        return true;
    }
    return false;
}

#endif /* ARENA_H */



#if defined(ARENA_IMPLEMENTATION) && !defined(ARENA_ALREADY_IMPLEMENTED)
#define ARENA_ALREADY_IMPLEMENTED

#include <string.h> /* memset */


internal void *Arena__InterfaceAllocRoutine(void *UserData, const memory_alloc_parameter *Param)
{
    arena_alloc *Arena = UserData;
    switch (Param->Mode)
    {
    case ALLOCATOR_ALLOCATE:
    {
        void *Result;
        Arena_ScopedAlignment(Arena, Param->As.Allocate.Alignment)
        {
            Result = Arena_Alloc(Arena, Param->As.Allocate.SizeBytes);
        }
        return Result;
    } break;
    case ALLOCATOR_FREE:
    {
        /* nop */
    } break;
    }
    return NULL;
}

memory_alloc_interface Arena_AsAllocInterface(arena_alloc *Arena)
{
    memory_alloc_interface Interface = {
        .UserData = Arena,
        .Routine = Arena__InterfaceAllocRoutine,
    };
    return Interface;
}

internal void *Arena__PushNewNodeAndAllocateBuffer(arena_alloc *Arena, arena__node *Next, i64 BufferSizeBytes, u32 Alignment)
{
    /* not ideal, but not bad: allocate new node */
    i64 PoolSize = Arena_GetNodeCapacity(Arena->Curr);
    i64 Max = MAXIMUM(PoolSize, BufferSizeBytes);
    Max += sizeof(arena__node);

    arena__node *New = Arena__NewNode(Arena, Next, Max, Alignment);
    Arena->Curr->Next = New;
    Arena->Curr = New;

    u8 *Buffer = Memory_AlignPointer(Arena->Curr->Ptr, Alignment);
    Arena->Curr->Ptr = Buffer + BufferSizeBytes;
    ASSERT(Buffer + BufferSizeBytes <= Arena->Curr->End, 
        "%p + %zikb = (%p) <= %p", Buffer, BufferSizeBytes / KB, Buffer + BufferSizeBytes, Arena->Curr->End
    );
    return Buffer;
}

void *Arena_AllocNonZero(arena_alloc *Arena, i64 SizeBytes)
{
    u32 Alignment = Arena->Alignment;
    {
        arena__node *Curr = Arena->Curr;
        u8 *End = Arena->Curr->End;

        SizeBytes = Memory_AlignSize(SizeBytes, Alignment);
        u8 *Buffer = Memory_AlignPointer(Curr->Ptr, Alignment);
        u8 *PtrNext = Buffer + SizeBytes;
        if (PtrNext <= End)
        {
            /* fast case: arena has enough space */
            Curr->Ptr = PtrNext;
            return Buffer;
        }

        if (NULL == Curr->Next)
        {
            return Arena__PushNewNodeAndAllocateBuffer(Arena, NULL, SizeBytes, Alignment);
        }
    }

    /* search for optimal node */
    /* NOTE: all nodes from Curr->Next onwards are considered empty */

    arena__node *BeforeEmptyList = Arena->Curr;
    arena__node *Prev_i = Arena->Curr;
    arena__node *i = Prev_i->Next;
    do {
        i64 Capacity = Arena_GetNodeCapacity(i);
        Arena__ResetNode(i, i->Next, Capacity + sizeof(*i));
        if (Capacity >= SizeBytes)
        {
            /* check for alignment */
            u8 *Buffer = Memory_AlignPointer(i->Ptr, Alignment);
            u8 *PtrNext = Buffer + SizeBytes;
            if (PtrNext <= i->End)
            {
                arena__node *EmptyListHead = BeforeEmptyList->Next;
                if (i != EmptyListHead)
                {
                    /* found existing node with matching capacity, 
                     * rearrange the nodes so that the one that fit is right after Arena->Curr */
                    Prev_i->Next = i->Next;
                    i->Next = EmptyListHead;
                    Arena->Curr->Next = i;
                    Arena->Curr = i;

                }
                else
                {
                    /* the first node fits */
                    Arena->Curr = i;
                }

                /* allocate the buffer */
                i->Ptr = PtrNext;
                return Buffer;
            }
            /* enough capacity but does not fit due to alignment, discard */
        }
        Prev_i = i;
        i = i->Next;
    } while (i);

    /* exhausted list, no node with bigger/same capacity */
    ASSERT(Arena->Curr == BeforeEmptyList);
    return Arena__PushNewNodeAndAllocateBuffer(Arena, BeforeEmptyList->Next, SizeBytes, Alignment);
}

void *Arena_Alloc(arena_alloc *Arena, i64 SizeBytes)
{
    void *Buffer = Arena_AllocNonZero(Arena, SizeBytes);
    memset(Buffer, 0, SizeBytes);
    return Buffer;
}



#endif /* ARENA_IMPLEMENTATION */

