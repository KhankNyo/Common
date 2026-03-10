
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SLICE_H
#define SLICE_H

#include "Common.h"
#include "Memory.h"
#include <string.h> /* memcpy */



/*
    example usage: 
        // arena_alloc *Arena = ...; arena was declared somewhere above...
        // arena_alloc *TmpArena = ...; tmp arena was declared somewhere above...

        slice(i32) Array1, Array2;
        Arena_Scope(TmpArena)
        {
            slice_builder(&Array1, 1024) Array1Builder = { .Arena = TmpArena, }, 
                                         Array2Builder = { .Arena = TmpArena, };
            for (int i = 0; i < ...; i++)
            {
                SliceBuilder_Push(&Array1Builder, i + 1);
                SliceBuilder_Push(&Array2Builder, (int) { i });
            }

            SliceBuilder_CopyToSlice(&Array1Builder, &Array1, Arena);
            SliceBuilder_CopyToSlice(&Array2Builder, &Array2, Arena);
        }
        // use Array1, Array2 here ...


    slice builder function macros:
        // pushes an element into the builder, can be a compound literal
        void SliceBuilder_Push(slice_builder *Builder, element_type Element);
        void SliceBuilder_Push(slice_builder *Builder, (element_type){ ... });

        // copies contents of the builder into a contiguous array allocated by SliceOwner, 
        // Slice->Data contains the block of memory allocated by SliceOwner
        // Slice->Count contains the count of the element
        void SliceBuilder_CopyToSlice(
            const slice_builder *Builder, 
            slice *Slice, 
            arena_alloc *SliceOwner
        );

        // gets the maximum number of element the pool can hold before having to allocate a new pool
        void SliceBuilder_GetPoolCapcaity(const slice_builder *Builder);

        // resets the builder so that the Builder is empty 
        // and the next push will be the first element
        void SliceBuilder_Reset(slice_builder *Builder);


    slice function macros:
        // returns the index of a slice iterator
        isize Slice_GetIteratorIndex(slice *Slice, element_type *iterator_variable_name);

        // iterates through every element within Slice
        Slice_Foreach(slice *Slice, element_type *iterator_variable_name)
        {
            ... for loop body
        }
 */

typedef_struct(slice_builder__node_header);

#define slice_builder(p_slice, block_capacity) slice_builder_typed(typeof((p_slice)->Data[0]), block_capacity)

#define slice_builder_typed(type, block_capacity) struct {\
    arena_alloc *Arena; \
    isize BlockCount; \
    slice_builder__node_header *First; \
    struct { \
        slice_builder__node_header Header; \
        type Pool[block_capacity]; \
    } *Last; \
}

#define slice(type) struct {\
    type *Data;\
    isize Count;\
}

#define Slice_Foreach(p_slice, iterator_name) \
    for (typeof((p_slice)->Data) iterator_name = (p_slice)->Data; \
        iterator_name < (p_slice)->Data + (p_slice)->Count; \
        iterator_name++)

#define Slice_GetIteratorIndex(p_slice, iterator_name) \
    (iterator_name - (p_slice)->Data)


#define SliceBuilder_GetPoolCapacity(p_slice_builder) \
    STATIC_ARRAY_SIZE((SLICE_BUILDER__NODE_TYPE(p_slice_builder)){0}.Pool)

#define SliceBuilder_Push(p_slice_builder, ...) do { \
    typeof(p_slice_builder) builder_ = p_slice_builder;\
    \
    isize pool_capacity_ = SliceBuilder_GetPoolCapacity(builder_);\
    bool32 should_allocate_new_node_ = \
        NULL == builder_->Last \
        || (builder_->Last->Header.Count >= pool_capacity_ \
            && NULL == builder_->Last->Header.Next);\
    \
    if (should_allocate_new_node_) {\
        \
        slice_builder__node_header *BasePtr = Arena_AllocNonZero(builder_->Arena, \
            sizeof(SLICE_BUILDER__NODE_TYPE(builder_))\
        );\
        *BasePtr = (slice_builder__node_header) { 0 };\
        \
        if (!builder_->Last) {\
            builder_->First = BasePtr;\
            builder_->Last = (SLICE_BUILDER__NODE_TYPE(builder_) *)BasePtr;\
        } else {\
            builder_->Last->Header.Next = BasePtr;\
            builder_->Last = (SLICE_BUILDER__NODE_TYPE(builder_) *)BasePtr;\
        }\
        builder_->BlockCount++;\
    } else if (builder_->Last->Header.Count >= pool_capacity_) {\
        /* reuse next node */\
        builder_->Last = (SLICE_BUILDER__NODE_TYPE(builder_) *)builder_->Last->Header.Next;\
        builder_->Last->Header.Count = 0;\
        builder_->BlockCount++;\
    }\
    ASSERT(builder_->Last && builder_->Last->Header.Count < pool_capacity_);\
    \
    isize top_ = builder_->Last->Header.Count++;\
    ((SLICE_BUILDER__NODE_TYPE(builder_) *)builder_->Last)->Pool[top_] = __VA_ARGS__;\
} while (0)

#define SliceBuilder_CopyToSlice(p_slice_builder, p_slice, p_arena_slice_owner) do {\
    typeof(p_slice_builder) builder_ = p_slice_builder;\
    typeof(p_slice) slice_ = p_slice;\
    typeof(p_arena_slice_owner) arena_slice_owner = p_arena_slice_owner;\
    if (0 == builder_->BlockCount) {\
        *slice_ = (typeof(*slice_)) { 0 };\
        break;\
    }\
    \
    ASSERT_EXPRESSION_TYPE(slice_->Data[0], SLICE_BUILDER__POOL_ELEM_TYPE(builder_), "Mismatching types between slice and slice builder.");\
    SLICE_BUILDER__POOL_ELEM_TYPE(builder_) *slice_ptr_;\
    isize slice_elem_count_ = 0;\
    {\
        isize BlockElemCapacity = SliceBuilder_GetPoolCapacity(builder_);\
        slice_elem_count_ = BlockElemCapacity * (builder_->BlockCount - 1) + builder_->Last->Header.Count;\
        \
        Arena_AllocArrayNonZero(arena_slice_owner, &slice_ptr_, slice_elem_count_);\
        SLICE_BUILDER__POOL_ELEM_TYPE(builder_) *Ptr = slice_ptr_;\
        \
        SLICE_BUILDER__NODE_TYPE(builder_) *Curr = (typeof(Curr))builder_->First;\
        for (isize i = 0; i < builder_->BlockCount - 1; i++) {\
            ASSERT(Curr);\
            memcpy(Ptr, Curr->Pool, sizeof Curr->Pool);\
            Ptr += BlockElemCapacity;\
            Curr = (typeof(Curr))Curr->Header.Next;\
        }\
        ASSERT(Curr);\
        memcpy(Ptr, Curr->Pool, Curr->Header.Count * sizeof Ptr[0]);\
    }\
    *slice_ = (typeof(slice_[0])) { \
        .Count = slice_elem_count_,\
        .Data = slice_ptr_,\
    };\
} while (0)

#define SliceBuilder_Reset(p_slice_builder) do {\
    typeof(p_slice_builder) slice_builder_ = p_slice_builder;\
    slice_builder_->BlockCount = 1;\
    slice_builder_->Last = (SLICE_BUILDER__NODE_TYPE(slice_builder_) *)slice_builder_->First;\
    if (slice_builder_->First) \
        slice_builder_->First->Count = 0; \
} while (0)


#define SLICE_BUILDER__NODE_TYPE(p_slice_builder) \
    typeof((p_slice_builder)->Last[0])

#define SLICE_BUILDER__POOL_ELEM_TYPE(p_slice_builder) \
    typeof((p_slice_builder)->Last[0].Pool[0])

struct slice_builder__node_header
{
    slice_builder__node_header *Next;
    isize Count;
};



#endif /* SLICE_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


