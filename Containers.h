#ifndef COMMON_CONTAINERS_H
#define COMMON_CONTAINERS_H

#include "Common.h"
#include "FreeList.h"

#define dynamic_array(type) struct {\
    type *Data;\
    isize Count, Capacity;\
}

#define slice(type) struct {\
    type *Data;\
    isize Count;\
}

#define double_link(type) struct {\
    type *Next, *Prev;\
}
#define single_link(type) struct {\
    type *Next;\
}



#define DynamicArray_Foreach(p_dynamic_array, iterator_name) for (\
        typeof((p_dynamic_array)->Data) iterator_name = (p_dynamic_array)->Data;\
        iterator_name < (p_dynamic_array)->Data + (p_dynamic_array)->Count;\
        iterator_name++)

#define DynamicArray_ResizeCapacity(p_freelist, p_da, isize_new_capacity) do {\
    (p_da)->Capacity = isize_new_capacity;\
    FreeList_ReallocArray(p_freelist, &(p_da)->Data, (p_da)->Capacity);\
} while (0)

#define DynamicArray_Push(p_freelist, p_da, ...) do {\
    typeof(p_freelist) freelist_ = p_freelist;\
    typeof(p_da) dynamic_array_ = p_da;\
    if (dynamic_array_->Count >= dynamic_array_->Capacity) {\
        DynamicArray_ResizeCapacity(freelist_, dynamic_array_, dynamic_array_->Capacity == 0? 32 : dynamic_array_->Capacity * 2);\
    }\
    dynamic_array_->Data[dynamic_array_->Count] = __VA_ARGS__;\
    dynamic_array_->Count++;\
} while (0)



#define Slice_Foreach(p_slice, iterator_name) for (\
        typeof((p_slice)->Data) iterator_name = (p_slice)->Data;\
        iterator_name < (p_slice)->Data + (p_slice)->Count; \
        iterator_name++)


#define LinkedList_Foreach(p_struct_with_link, iterator_name) for (\
        typeof(p_struct_with_link) iterator_name = p_struct_with_link; \
        iterator_name != NULL; \
        iterator_name = iterator_name->Next)

#define DoubleLink_Link(pp_head, p_prev, p_curr, p_next) do {\
    typeof(p_curr) curr_ = p_curr, \
                   prev_ = p_prev, \
                   next_ = p_next; \
    if (prev_) prev_->Next = curr_;\
    else *(pp_head) = curr_;\
    curr_->Prev = prev_;\
    curr_->Next = next_;\
    if (next_) next_->Prev = curr_;\
} while (0)

#define DoubleLink_Unlink(pp_head, p_struct_with_link) do {\
    typeof(p_struct_with_link) curr_ = (p_struct_with_link), \
                               prev_ = curr_->Prev, \
                               next_ = curr_->Next; \
    if (prev_) prev_->Next = next_;\
    else *(pp_head) = next_;\
    if (next_) next_->Prev = prev_;\
    curr_->Prev = NULL;\
    curr_->Next = NULL;\
} while (0)

#define DoubleLink_Push(pp_head, p_struct_with_link) \
        DoubleLink_Link(pp_head, NULL, p_struct_with_link, *(pp_head))

#define DoubleLink_Pop(pp_head, out_pp_struct_with_link) do {\
    typeof(*(pp_head)) result_ = *(pp_head);\
    if (result_) {\
        *(pp_head) = result_->Next;\
        result_->Next = NULL;\
    }\
    if (*(pp_head)) (*(pp_head))->Prev = NULL;\
    STATIC_ASSERT(\
        TYPE_EQUAL(out_pp_struct_with_link, void *) \
        || TYPE_EQUAL(out_pp_struct_with_link, typeof(result_) *),\
        "Invalid type"\
    );\
    if ((out_pp_struct_with_link) != NULL) *((typeof(result_) *)(out_pp_struct_with_link)) = result_;\
} while (0)


#define SingleLink_Push(pp_head, p_struct_with_link) do {\
    (p_struct_with_link)->Next = *(pp_head);\
    *(pp_head) = (p_struct_with_link);\
} while (0)

#define SingleLink_Pop(pp_head, out_pp_struct_with_link) do {\
    typeof(*(pp_head)) result_ = *(pp_head);\
    if (result_) {\
        *(pp_head) = result_->Next;\
        result_->Next = NULL;\
    }\
    STATIC_ASSERT(\
        TYPE_EQUAL(out_pp_struct_with_link, void *) \
        || TYPE_EQUAL(out_pp_struct_with_link, typeof(result_) *),\
        "Invalid type"\
    );\
    if ((out_pp_struct_with_link) != NULL) *(typeof(result_) *)(out_pp_struct_with_link) = result_;\
} while (0)





#endif /* COMMON_CONTAINERS_H */

