
#include "Common-Renderer-Vulkan-VkMalloc.h"
#include "Common-Renderer-Vulkan.h"
#include "Common-Vulkan.h"
#include "Memory.h"



typedef struct
{
    i64 SizeBytes;
    i64 OffsetBytes;
    u8 Index;
} vkm__unpacked_handle;

force_inline u64 Vkm__PackHandle(
    u32 Index, 
    i64 SizeBytes,
    i64 OffsetBytes
) {
    ASSERT(Index <= VKM_MAX_BUFFER_INDEX, "Index too big");
    ASSERT(SizeBytes < (1ll << 28)*VKM_MIN_ALIGNMENT, "Size too big");
    ASSERT(OffsetBytes < (1ll << 28)*VKM_MIN_ALIGNMENT, "Offset too big");
    ASSERT(Memory_AlignSize(SizeBytes, VKM_MIN_ALIGNMENT) == SizeBytes, "Size must be aligned: %llu != %llu", 
        (long long unsigned)Memory_AlignSize(SizeBytes, VKM_MIN_ALIGNMENT), (long long unsigned)SizeBytes
    );
    ASSERT(Memory_AlignSize(OffsetBytes, VKM_MIN_ALIGNMENT) == OffsetBytes, "Offset must be aligned");

    u64 SizeAligned = SizeBytes / VKM_MIN_ALIGNMENT;
    u64 OffsetAligned = OffsetBytes / VKM_MIN_ALIGNMENT;
    u64 Result = 0
        | ((u64)Index)
        | ((u64)OffsetAligned << 8)
        | ((u64)SizeAligned << 36)
    ;
    return Result;
}

force_inline vkm__unpacked_handle Vkm__UnpackHandle(u64 Handle) 
{
    vkm__unpacked_handle Unpacked = {
        .Index = (Handle) & 0xFF,
        .OffsetBytes = ((Handle >> 8) & ((1 << 28) - 1)) * VKM_MIN_ALIGNMENT,
        .SizeBytes = ((Handle >> 36) & ((1 << 28) - 1)) * VKM_MIN_ALIGNMENT,
    };
    return Unpacked;
}


force_inline VkMemoryRequirements Vkm__GetImageMemoryRequirements(const vkm *Vkm, VkImage Image, i64 Capacity)
{
    VkMemoryRequirements Required;
    vkGetImageMemoryRequirements(Vkm->Device, Image, &Required);
    Required.alignment = MAXIMUM(Required.alignment, VKM_MIN_ALIGNMENT);
    Required.size = MAXIMUM(Capacity, (i64)Required.size);
    Required.size = Memory_AlignSize(Required.size, Required.alignment);
    return Required;
}

force_inline VkMemoryRequirements Vkm__GetBufferMemoryRequirements(const vkm *Vkm, VkBuffer Buffer)
{
    VkMemoryRequirements Required;
    vkGetBufferMemoryRequirements(Vkm->Device, Buffer, &Required);
    Required.alignment = MAXIMUM(Required.alignment, VKM_MIN_ALIGNMENT);
    Required.size = Memory_AlignSize(Required.size, Required.alignment);
    return Required;
}


internal i32 Vkm__FindMemoryType(VkPhysicalDevice PhysDevice, u32 Filter, VkMemoryPropertyFlags Flags)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(PhysDevice, &MemoryProperties);
    for (u32 i = 0; i < MemoryProperties.memoryTypeCount; i++)
    {
        if (Filter & (1llu << i) && MemoryProperties.memoryTypes[i].propertyFlags & Flags)
            return i;
    }
    return -1;
}

internal VkBufferUsageFlags Vkm__GetBufferUsageFlags(vkm_buffer_type BufferType)
{
    VkBufferUsageFlags Result = 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    switch (BufferType)
    {
    case VKM_BUFFER_TYPE_STAGING:
    case VKM_BUFFER_TYPE_UBO:
        Result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    case VKM_BUFFER_TYPE_VBO:
    case VKM_BUFFER_TYPE_EBO:
    case VKM_BUFFER_TYPE_SSBO:
        break;
    }
    return Result;
}

internal VkMemoryPropertyFlags Vkm__GetBufferMemoryPropertyFlags(vkm_buffer_type BufferType)
{
    VkMemoryPropertyFlags Result = 0;
    switch (BufferType)
    {
    case VKM_BUFFER_TYPE_STAGING:
    case VKM_BUFFER_TYPE_UBO:
        Result = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case VKM_BUFFER_TYPE_VBO:
    case VKM_BUFFER_TYPE_EBO:
    case VKM_BUFFER_TYPE_SSBO:
        Result = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    }
    return Result;
}

internal int Vkm__GetPixelSizeBytes(VkFormat Format)
{
    switch (Format)
    {
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_B8G8R8A8_SNORM:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 4;
    default:
    {
        ASSERT(false, "Format not supported: %d", Format);
        return 0;
    } break;
    }
}


internal vkm_buffer_chunk *Vkm__CreateChunk(vkm *Vkm, i32 EntryIndex, i32 SizeBytes, i32 OffsetBytes) 
{
    vkm_buffer_chunk *Chunk = Vkm->BufferChunkUnused;
    if (!Chunk)
    {
        Chunk = FreeList_AllocNonZero(&Vkm->FreeList, sizeof *Chunk);
    }
    else
    {
        Vkm->BufferChunkUnused = Chunk->Next;
    }
    *Chunk = (vkm_buffer_chunk) {
        .EntryIndex = EntryIndex,
        .OffsetAligned = OffsetBytes / VKM_MIN_ALIGNMENT,
        .SizeAligned = SizeBytes / VKM_MIN_ALIGNMENT,
    };
    return Chunk;
}

internal bool32 Vkm__UpdateLargest(vkm_buffer_pool_entry *Entry)
{
    vkm_buffer_chunk *Largest = Entry->Freed;
    LinkedList_Foreach(Entry->Freed, Curr)
    {
        if (Largest->SizeAligned > Curr->SizeAligned)
            Largest = Curr;
    }
    Entry->LargestFree = Largest;
    return Largest != NULL;
}

internal void Vkm__InsertFreeChunk(vkm *Vkm, vkm_buffer_pool_entry *Entry, vkm_buffer_chunk *Chunk)
{
    vkm_buffer_chunk *Prev = NULL;
    vkm_buffer_chunk *Curr = Entry->Freed;
    vkm_buffer_chunk *Largest = Entry->LargestFree;
    /* insert by increasing offset */
    while (Curr && Chunk->OffsetAligned < Curr->OffsetAligned)
    {
        if (!Largest || Largest->SizeAligned < Curr->SizeAligned)
            Largest = Curr;
        Prev = Curr;
        Curr = Curr->Next;
    }

    DoubleLink_Link(&Entry->Freed, Prev, Chunk, Curr);

    /* coalesce */
    for (vkm_buffer_chunk *i = Prev? Prev : Entry->Freed; 
        i && i->Next; 
        i = i->Next
    ) {
        for (vkm_buffer_chunk *Next = i->Next; 
            Next;
        ) {
            bool32 Coalesceable = i->OffsetAligned + i->SizeAligned == Next->OffsetAligned;
            if (Coalesceable)
            {
                vkm_buffer_chunk *Skip = Next;
                i->SizeAligned += Skip->SizeAligned;

                Next = Skip->Next;
                i->Next = Next;
                if (Next)
                    Next->Prev = i;

                SingleLink_Push(&Vkm->BufferChunkUnused, Skip);
            }
            else break;
        }
    }

    Vkm__UpdateLargest(Entry);
}


internal vkm_device_memory_node *Vkm__CreateNode(
    vkm *Vkm, 
    i32 SizeBytes,
    i32 OffsetBytes,
    i16 DeviceMemoryIndex,
    i8 MemoryTypeIndex
) {
    vkm_device_memory_node *Node = Vkm->DeviceMemoryNodeUnused;
    if (!Node)
    {
        Node = FreeList_AllocNonZero(&Vkm->FreeList, sizeof(vkm_device_memory_node));
    }
    else
    {
        Vkm->DeviceMemoryNodeUnused = Node->Next;
    }
    *Node = (vkm_device_memory_node) {
        .SizeAligned = SizeBytes / VKM_MIN_ALIGNMENT,
        .OffsetAligned = OffsetBytes / VKM_MIN_ALIGNMENT,
        .DeviceMemoryIndex = DeviceMemoryIndex,
        .MemoryTypeIndex = MemoryTypeIndex,
    };
    return Node;
}

internal void Vkm__UnlinkNode(vkm_device_memory_node **Head, vkm_device_memory_node *Node)
{
    DoubleLink_Unlink(Head, Node);
}

force_inline bool32 Vkm__IsNodeSuitable(const vkm_device_memory_node *Node, i64 SizeBytes, u32 Alignment)
{
    i64 SizeRemainBytes = Memory_AlignSizeDown(Node->SizeAligned*VKM_MIN_ALIGNMENT, Alignment);
    if (SizeRemainBytes < (i64)Alignment)
        return false;
    return SizeBytes <= SizeRemainBytes;
}

internal vkm_device_memory_node *Vkm__GetFreeNode(vkm *Vkm, VkMemoryRequirements Requirements, VkMemoryPropertyFlags MemoryProperties)
{
    int MemoryTypeIndex = Vkm__FindMemoryType(
        Vkm->PhysicalDevice,
        Requirements.memoryTypeBits,
        MemoryProperties
    );
    ASSERT(-1 != MemoryTypeIndex, "Unable to locate suitable memory type");

    vkm_device_memory_node **Head = &Vkm->DeviceMemoryNodeFree[MemoryTypeIndex];
    vkm_device_memory_node *FreeNode = *Head;
    {
        /* first fit */
        while (FreeNode && !Vkm__IsNodeSuitable(FreeNode, Requirements.size, Requirements.alignment))
        {
            FreeNode = FreeNode->Next;
        }

        if (!FreeNode)
        {
            /* allocate new one */
            i64 PoolCapacity = Vkm->LocalDeviceMemoryPoolCapacityBytes;
            if (MemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            {
                PoolCapacity = Vkm->TransDeviceMemoryPoolCapacityBytes;
            }

            i64 CapacityBytes = MAXIMUM(PoolCapacity, (i64)Requirements.size);
            ASSERT(CapacityBytes <= ((i64)INT32_MAX) * VKM_MIN_ALIGNMENT, "memory size too big");

            VkDeviceMemory DeviceMemory;
            VkMemoryAllocateInfo AllocateInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .memoryTypeIndex = MemoryTypeIndex,
                .allocationSize = CapacityBytes,
            };
            VK_CHECK(vkAllocateMemory(Vkm->Device, &AllocateInfo, NULL, &DeviceMemory));
            DynamicArray_Push(&Vkm->FreeList, &Vkm->DeviceMemory, (vkm_device_memory) {
                .MemoryProperties = MemoryProperties,
                .MemoryTypeIndex = MemoryTypeIndex,
                .CapacityAligned = CapacityBytes / VKM_MIN_ALIGNMENT,
                .Handle = DeviceMemory,
            });

            FreeNode = Vkm__CreateNode(Vkm,
                CapacityBytes,
                0,
                Vkm->DeviceMemory.Count - 1,
                MemoryTypeIndex
            );
        }
        else
        {
            Vkm__UnlinkNode(Head, FreeNode);
            /* make sure offset is aligned to the alignment given by the requirement */
            i64 NewOffsetAligned = Memory_AlignSize(
                FreeNode->OffsetAligned, Requirements.alignment / VKM_MIN_ALIGNMENT
            );
            FreeNode->SizeAligned -= NewOffsetAligned - FreeNode->OffsetAligned;
            FreeNode->OffsetAligned = NewOffsetAligned;
        }
    }
    return FreeNode;
}

internal void Vkm__InsertFreeNode(vkm *Vkm, vkm_device_memory_node **Head, vkm_device_memory_node *Node)
{
    vkm_device_memory_node *Prev = NULL;
    vkm_device_memory_node *Curr = *Head;
    while (Curr && Node->OffsetAligned > Curr->OffsetAligned) 
    {
        Prev = Curr;
        Curr = Curr->Next;
    }

    DoubleLink_Link(Head, Prev, Node, Curr);

    /* coalesce */
    for (vkm_device_memory_node *i = Prev? Prev : *Head; 
        i && i->Next; 
        i = i->Next
    ) {
        for (vkm_device_memory_node *Next = i->Next; 
            Next;
        ) {
            bool32 Coalesceable = i->OffsetAligned + i->SizeAligned == Next->OffsetAligned;
            if (Coalesceable)
            {
                vkm_device_memory_node *Skip = Next;
                i->SizeAligned += Skip->SizeAligned;

                Next = Skip->Next;
                i->Next = Next;
                if (Next)
                    Next->Prev = i;

                DoubleLink_Push(&Vkm->DeviceMemoryNodeUnused, Skip);
            }
            else break;
        }
    }
}

internal bool32 Vkm__IsBufferSuitable(
    const vkm *Vkm,
    vkm_buffer_pool_entry *Entry, 
    VkBufferUsageFlags BufferUsage, 
    VkMemoryPropertyFlags MemoryProperties, 
    i64 SizeAligned
) {
    if (NULL == Entry->LargestFree && !Vkm__UpdateLargest(Entry))
    {
        return false;
    }
    i64 LargestSizeAligned = Entry->LargestFree->SizeAligned;

    const vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];
    bool32 Suitable = 
        Entry->BufferUsageFlags == BufferUsage
        && DeviceMemory->MemoryProperties == MemoryProperties
        && LargestSizeAligned >= SizeAligned;
    return Suitable;
}

force_inline VkDeviceMemory Vkm__GetDeviceMemoryHandle(const vkm *Vkm, const vkm_device_memory_node *Node)
{
    return Vkm->DeviceMemory.Data[Node->DeviceMemoryIndex].Handle;
}

internal VkImageView Vkm__CreateImageView(vkm *Vkm, VkImage Image, VkFormat Format, VkImageAspectFlags Aspect, int MipLevels)
{
    VkImageView ImageView;
    VkImageViewCreateInfo ImageViewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = Image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = Format,
        .components = { /* default mapping, use channel values */
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = Aspect, 
            .baseMipLevel = 0, 
            .levelCount = MipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1, 
        },
    };
    VK_CHECK(vkCreateImageView(Vkm->Device, &ImageViewCreateInfo, NULL, &ImageView));
    return ImageView;
}

internal vkm_device_memory_node *Vkm__AllocateNode(vkm *Vkm, VkMemoryRequirements Requirements, VkMemoryPropertyFlags MemoryProperties)
{
    vkm_device_memory_node *AllocatedNode;
    {
        vkm_device_memory_node *FreeNode = Vkm__GetFreeNode(Vkm, Requirements, MemoryProperties);

        i64 AllocationCapacityBytes = MINIMUM(Requirements.size + Vkm->BufferPoolCapacityBytes, Requirements.size*2);
        bool32 ShouldSplit = FreeNode->SizeAligned*VKM_MIN_ALIGNMENT >= AllocationCapacityBytes;
        if (ShouldSplit)
        {
            /* split image memory */
            AllocatedNode = Vkm__CreateNode(Vkm, 
                Requirements.size, 
                FreeNode->OffsetAligned*VKM_MIN_ALIGNMENT, 
                FreeNode->DeviceMemoryIndex, 
                FreeNode->MemoryTypeIndex
            );
            FreeNode->SizeAligned -= AllocatedNode->SizeAligned;
            FreeNode->OffsetAligned += AllocatedNode->SizeAligned;
            Vkm__InsertFreeNode(Vkm, &Vkm->DeviceMemoryNodeFree[FreeNode->MemoryTypeIndex], FreeNode);
            ASSERT(Vkm->DeviceMemoryNodeFree[FreeNode->MemoryTypeIndex], "");
        }
        else
        {
            AllocatedNode = FreeNode;
        }

        /* insert allocated node */
        vkm_device_memory_node **Head = &Vkm->DeviceMemoryNodeAllocated[AllocatedNode->MemoryTypeIndex];
        DoubleLink_Push(Head, AllocatedNode);
    }
    return AllocatedNode;
}

internal vkm_buffer_handle Vkm__CreateBufferRaw(
    vkm *Vkm, 
    VkBufferUsageFlags BufferUsage, 
    VkMemoryPropertyFlags BufferMemoryProperties, 
    i64 CapacityBytes
) {
    vkm_buffer_pool *Pool = &Vkm->BufferPool;

    int Index = -1;
    vkm_buffer_chunk *SuitableChunk = NULL;
    for (int i = Pool->Count - 1; i >= 0; i--)
    {
        vkm_buffer_pool_entry *Entry = &Pool->Data[i];
        i64 SizeAligned = Memory_AlignSize(CapacityBytes, Entry->Alignment) / VKM_MIN_ALIGNMENT;
        if (Vkm__IsBufferSuitable(Vkm, Entry, BufferUsage, BufferMemoryProperties, SizeAligned))
        {
            Index = i;
            /* unlink */
            /* NOTE: largest free will get updated */
            SuitableChunk = Entry->LargestFree;
            Entry->LargestFree = NULL;

            DoubleLink_Unlink(&Entry->Freed, SuitableChunk);
            break;
        }
    }
    if (!SuitableChunk)
    {
        /* create new buffer pool entry */
        VkBuffer Buffer;
        {
            i64 BufferSize = MAXIMUM(CapacityBytes, Vkm->BufferPoolCapacityBytes);
            VkBufferCreateInfo CreateInfo = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .usage = BufferUsage,
                .size = BufferSize,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

                .flags = 0,
                .pNext = NULL,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = NULL, /* NOTE: must be filled when sharingMode is VK_SHARING_MODE_CONCURRENT */
            };
            VK_CHECK(vkCreateBuffer(Vkm->Device, &CreateInfo, NULL, &Buffer));
        }

        VkMemoryRequirements Required = Vkm__GetBufferMemoryRequirements(Vkm, Buffer);
        vkm_device_memory_node *Node = Vkm__AllocateNode(Vkm, Required, BufferMemoryProperties);
        i64 DeviceMemoryOffsetBytes = Node->OffsetAligned * VKM_MIN_ALIGNMENT;
        /* NOTE: don't use '%' since the expr will be in format string */
        ASSERT((i64)(DeviceMemoryOffsetBytes / Required.alignment * Required.alignment) == DeviceMemoryOffsetBytes); 
        vkBindBufferMemory(Vkm->Device, Buffer, Vkm__GetDeviceMemoryHandle(Vkm, Node), DeviceMemoryOffsetBytes);

        DynamicArray_Push(&Vkm->FreeList, Pool, (vkm_buffer_pool_entry) {
            .Buffer = Buffer,
            .BufferUsageFlags = BufferUsage,
            .Alignment = Required.alignment,
            .DeviceMemoryIndex = Node->DeviceMemoryIndex,
            .DeviceMemoryNode = Node,
            .DeviceMemoryOffsetBytes = DeviceMemoryOffsetBytes,
            .DeviceMemoryCapacityBytes = Required.size,
        });
        Index = Pool->Count - 1;
        i64 BufferOffsetBytes = 0;
        SuitableChunk = Vkm__CreateChunk(Vkm, Index, Required.size, BufferOffsetBytes);
    }

    ASSERT(IN_RANGE(0, Index, Pool->Count - 1));
    ASSERT(SuitableChunk);

    vkm_buffer_pool_entry *Entry = &Pool->Data[Index];
    CapacityBytes = Memory_AlignSize(CapacityBytes, Entry->Alignment);
    {
        /* NOTE: buffers may have very strict size, can't use Vkm__ShouldSplitMemory() */
        bool32 ShouldSplitChunk = SuitableChunk->SizeAligned*VKM_MIN_ALIGNMENT - CapacityBytes > VKM_MIN_ALIGNMENT;
        if (ShouldSplitChunk)
        {
            /* split suitable chunk into free and allocated part */
            vkm_buffer_chunk *FreeChunk = Vkm__CreateChunk(Vkm, 
                Index,
                SuitableChunk->SizeAligned*VKM_MIN_ALIGNMENT - CapacityBytes, 
                SuitableChunk->OffsetAligned*VKM_MIN_ALIGNMENT + CapacityBytes
            );
            ASSERT(FreeChunk->OffsetAligned != SuitableChunk->OffsetAligned);
            Vkm__InsertFreeChunk(Vkm, Entry, FreeChunk);
            SuitableChunk->SizeAligned = CapacityBytes/VKM_MIN_ALIGNMENT;
        }
        else
        {
            /* keep chunk */
            CapacityBytes = SuitableChunk->SizeAligned*VKM_MIN_ALIGNMENT;
        }

        /* update allocated list */
        DoubleLink_Push(&Entry->Allocated, SuitableChunk);
    }
    vkm_buffer_handle Handle = { 
        .Value = SuitableChunk,
    };
    return Handle;
}


internal VkImage Vkm__CreateVkImage(
    vkm *Vkm, 
    VkImageTiling Tiling, 
    VkImageUsageFlags Usage, 
    VkSampleCountFlagBits Samples, 
    VkFormat Format, 
    u16 Width, u16 Height, int MipLevels)
{
    VkImage Image;
    VkImageCreateInfo ImageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .tiling = Tiling, 
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = Usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .samples = Samples,
        .format = Format,
        .extent = {
            .depth = 1,
            .width = Width, 
            .height = Height,
        },
        .mipLevels = MipLevels,
        .arrayLayers = 1,
        .flags = 0,
    };
    VK_CHECK(vkCreateImage(Vkm->Device, &ImageCreateInfo, NULL, &Image));
    return Image;
}




internal void Vkm__PrintAllocStats(const vkm *Vkm)
{
    Vulkan_LogLn("==== vkm alloc stat: %p ====", Vkm);
    Vulkan_LogLn("    VkDeviceMemory: %d", Vkm->DeviceMemory.Count);
    for (int i = 0; i < Vkm->DeviceMemory.Count; i++)
    {
        const vkm_device_memory *Entry = &Vkm->DeviceMemory.Data[i];
        Vulkan_LogLn("        %d: capacity: %lld KB, type index: %d", i, Entry->CapacityAligned*VKM_MIN_ALIGNMENT/KB, Entry->MemoryTypeIndex);
    }
    for (uint k = 0; k < VK_MAX_MEMORY_TYPES; k++)
    {
        if (!Vkm->DeviceMemoryNodeAllocated[k] && !Vkm->DeviceMemoryNodeFree[k])
        {
            continue;
        }

        Vulkan_LogLn("    AllocNode %d: ", k);
        for (vkm_device_memory_node *i = Vkm->DeviceMemoryNodeAllocated[k]; i; i = i->Next)
        {
            Vulkan_LogLn("      size, offset, idx: %4.6f MB, %4.6f MB, %d", 
                (double)i->SizeAligned * VKM_MIN_ALIGNMENT/MB, 
                (double)i->OffsetAligned*VKM_MIN_ALIGNMENT/MB, 
                i->DeviceMemoryIndex
            );
        }
        Vulkan_LogLn("    FreeNode %d: ", k);
        for (vkm_device_memory_node *i = Vkm->DeviceMemoryNodeFree[k]; i; i = i->Next)
        {
            Vulkan_LogLn("      size, offset, idx: %4.6f MB, %4.6f MB, %d", 
                (double)i->SizeAligned * VKM_MIN_ALIGNMENT/MB, 
                (double)i->OffsetAligned*VKM_MIN_ALIGNMENT/MB, 
                i->DeviceMemoryIndex
            );
        }
    }
    Vulkan_LogLn("    vkm_buffer (pool): %d", (int)Vkm->BufferPool.Count);
    for (int i = 0; i < Vkm->BufferPool.Count; i++)
    {
        vkm_buffer_pool_entry *Entry = Vkm->BufferPool.Data + i;
        isize LargestFree = Vkm__UpdateLargest(Entry)
            ? Entry->LargestFree->SizeAligned * VKM_MIN_ALIGNMENT
            : 0;

        Vulkan_LogLn("      Group %d: VkDeviceMemory %d, largest: %ld KB", i, Entry->DeviceMemoryIndex, LargestFree / KB);
        Vulkan_LogLn("        alloced:"); 
        const vkm_buffer_chunk *Chunk = Entry->Allocated;
        while (Chunk)
        {
            Vulkan_LogLn("          size: %g KB, offset: %ld Bytes", (double)Chunk->SizeAligned*VKM_MIN_ALIGNMENT/KB, Chunk->OffsetAligned*VKM_MIN_ALIGNMENT);
            Chunk = Chunk->Next;
        }

        Vulkan_LogLn("        freed :", i); 
        Chunk = Vkm->BufferPool.Data[i].Freed;
        while (Chunk)
        {
            Vulkan_LogLn("          size: %g KB, offset: %ld Bytes", (double)Chunk->SizeAligned*VKM_MIN_ALIGNMENT/KB, Chunk->OffsetAligned*VKM_MIN_ALIGNMENT);
            Chunk = Chunk->Next;
        }
    }
    Vulkan_LogLn("    vkm_image (pool head, allocated):");
    for (int i = 0; i < Vkm->ImagePool.Count; i++)
    {
        const vkm_image_pool_entry *Entry = Vkm->ImagePool.Data + i;
        /* TODO: log image allocation  */
        Vulkan_LogLn("      %d: size: %lld KB; Offset: %lld KB; w: %d, h: %d; VkDeviceMemory index: %d", 
            i, 
            Entry->DeviceMemoryNode->SizeAligned * VKM_MIN_ALIGNMENT/KB,
            Entry->DeviceMemoryNode->OffsetAligned * VKM_MIN_ALIGNMENT/KB,
            Entry->Width, Entry->Height,
            Entry->DeviceMemoryIndex
        );
    }
}


void Vkm_Create(vkm *Vkm, arena_alloc *Arena, const vkm_config *Config)
{
    *Vkm = (vkm) { 
        .Arena = Arena,
        .Device = Config->Device,
        .PhysicalDevice = Config->PhysicalDevice,
        .LocalDeviceMemoryPoolCapacityBytes = Config->LocalDeviceMemoryPoolCapacityBytes,
        .TransDeviceMemoryPoolCapacityBytes = Config->TransDeviceMemoryPoolCapacityBytes,
        .BufferPoolCapacityBytes = Config->BufferPoolCapacityBytes,
    };
    u32 FreeListAlignment = 8;
    isize FreeListPoolSize = 32*KB;
    FreeList_Create(&Vkm->FreeList, Arena_AsAllocInterface(Arena), FreeListPoolSize, FreeListAlignment);
}

void Vkm_Destroy(vkm *Vkm)
{
    if (0)
    {
        Vkm__PrintAllocStats(Vkm);
    }

    /* deallocate all memory */
    DynamicArray_Foreach(&Vkm->DeviceMemory, i)
    {
        if (i->MappedMemory)
        {
            vkUnmapMemory(Vkm->Device, i->Handle);
        }
        vkFreeMemory(Vkm->Device, i->Handle, NULL);
    }

    /* deallocate all buffers */
    DynamicArray_Foreach(&Vkm->BufferPool, i)
    {
        vkDestroyBuffer(Vkm->Device, i->Buffer, NULL);
    }

    /* deallocate all images */
    DynamicArray_Foreach(&Vkm->ImagePool, i)
    {
        vkDestroyImage(Vkm->Device, i->Image, NULL);
        vkDestroyImageView(Vkm->Device, i->ImageView, NULL);
    }
}

void Vkm_Reset(vkm *Vkm)
{
    /* deallocate all buffers */
    DynamicArray_Foreach(&Vkm->BufferPool, i)
    {
        vkDestroyBuffer(Vkm->Device, i->Buffer, NULL);
    }

    /* deallocate all images */
    DynamicArray_Foreach(&Vkm->ImagePool, i)
    {
        vkDestroyImage(Vkm->Device, i->Image, NULL);
        vkDestroyImageView(Vkm->Device, i->ImageView, NULL);
    }

    /* reset free list */
    if (0 == Vkm->DeviceMemory.Count)
    {
        memset(&Vkm->DeviceMemory, 0, sizeof Vkm->DeviceMemory);
        FreeList_Reset(&Vkm->FreeList);
    }
    else
    {
        /* retain device memory while resetting free list */
        Arena_Scope(Vkm->Arena)
        {
            isize Count = Vkm->DeviceMemory.Count;
            vkm_device_memory *Tmp;
            Arena_AllocArrayNonZero(Vkm->Arena, &Tmp, Count);
            memcpy(Tmp, Vkm->DeviceMemory.Data, sizeof(Tmp[0]) * Count);

            FreeList_Reset(&Vkm->FreeList);

            Vkm->DeviceMemory.Count = Count;
            Vkm->DeviceMemory.Capacity = Count;
            FreeList_AllocArrayNonZero(&Vkm->FreeList, &Vkm->DeviceMemory.Data, Count);
            memcpy(Vkm->DeviceMemory.Data, Tmp, sizeof(Tmp[0]) * Count);
        }
    }

    /* re-initialize all resources */
    Vkm->BufferPool = (vkm_buffer_pool) { 0 };
    Vkm->ImagePool = (vkm_image_pool) { 0 };
    memset(Vkm->DeviceMemoryNodeAllocated, 0, sizeof(Vkm->DeviceMemoryNodeAllocated));
    memset(Vkm->DeviceMemoryNodeFree, 0, sizeof(Vkm->DeviceMemoryNodeFree));
    Vkm->BufferChunkUnused = NULL;
    Vkm->DeviceMemoryNodeUnused = NULL;
    for (int i = 0; i < Vkm->DeviceMemory.Count; i++)
    {
        vkm_device_memory *Curr = Vkm->DeviceMemory.Data + i;
        if (Curr->MappedMemory)
        {
            vkUnmapMemory(Vkm->Device, Curr->Handle);
            Curr->MappedMemory = NULL;
            Curr->MapCount = 0;
        }

        vkm_device_memory_node *Node = Vkm__CreateNode(Vkm, Curr->CapacityAligned * VKM_MIN_ALIGNMENT, 0, i, Curr->MemoryTypeIndex);
        Vkm__InsertFreeNode(Vkm, &Vkm->DeviceMemoryNodeFree[Curr->MemoryTypeIndex], Node);
    }
}

vkm_buffer_handle Vkm_CreateBuffer(vkm *Vkm, const vkm_buffer_config *Config)
{
    ASSERT(Config->BufferType, "BufferType cannot be 0");
    ASSERT(Config->MemoryCapacityBytes, "MemoryCapacityBytes cannot be 0");
    VkBufferUsageFlags BufferUsage = Vkm__GetBufferUsageFlags(Config->BufferType);
    VkMemoryPropertyFlags BufferMemoryProperties = Vkm__GetBufferMemoryPropertyFlags(Config->BufferType);
    return Vkm__CreateBufferRaw(Vkm, BufferUsage, BufferMemoryProperties, Config->MemoryCapacityBytes);
}

vkm_image_handle Vkm_CreateImage(vkm *Vkm, const vkm_image_config *Config)
{
    int PixelSizeBytes = Vkm__GetPixelSizeBytes(Config->Format);
    int MipLevels = Config->MipLevels == 0? 1 : Config->MipLevels;
    ASSERT(Config->Width, "Width cannot be 0");
    ASSERT(Config->Height, "Height cannot be 0");
    ASSERT(Config->Sample, "Samples cannot be 0");
    ASSERT(Config->Format, "Format cannot be 0 (VK_FORMAT_UNDEFINED)");
    ASSERT(Config->Usage, "Usage cannot be 0");
    ASSERT(Config->Aspect, "Aspect cannot be 0 (VK_ASPECT_NONE)");

    VkImage Image = Vkm__CreateVkImage(Vkm, 
        Config->Tiling, Config->Usage, Config->Sample, Config->Format, 
        Config->Width, Config->Height, MipLevels
    );

    VkMemoryRequirements Required;
    {
        i32 CapacityBytes = Config->MemoryCapacityPixels == 0
            ? (i32)Config->Width * Config->Height
            : Config->MemoryCapacityPixels;
        CapacityBytes *= PixelSizeBytes;
        Required = Vkm__GetImageMemoryRequirements(Vkm, Image, CapacityBytes);
    }

    /* get allocated node */
    vkm_device_memory_node *AllocatedNode = Vkm__AllocateNode(Vkm, Required, VKM_IMAGE_MEMORY_PROPERTY);
    i64 OffsetBytes = AllocatedNode->OffsetAligned * VKM_MIN_ALIGNMENT;
    vkBindImageMemory(Vkm->Device, Image, Vkm__GetDeviceMemoryHandle(Vkm, AllocatedNode), OffsetBytes);

    VkImageView ImageView = Vkm__CreateImageView(Vkm, 
        Image, Config->Format, Config->Aspect, MipLevels
    );
    DynamicArray_Push(&Vkm->FreeList, &Vkm->ImagePool, (vkm_image_pool_entry) {
        .Image = Image,
        .ImageView = ImageView, 
        .DeviceMemoryNode = AllocatedNode,
        .Alignment = Required.alignment,

        .Usage = Config->Usage,
        .Format = Config->Format,
        .Sample = Config->Sample,
        .Tiling = Config->Tiling,
        .Aspect = Config->Aspect,

        .DeviceMemoryIndex = AllocatedNode->DeviceMemoryIndex,
        .Width = Config->Width,
        .Height = Config->Height,
        .PixelSizeBytes = PixelSizeBytes,
        .MipLevels = MipLevels,
    });
    vkm_image_handle Handle = {
        .Value = Vkm__PackHandle(Vkm->ImagePool.Count - 1, Required.size, OffsetBytes),
    };
    return Handle;
}



vkm_buffer_handle Vkm_ResizeBuffer(vkm *Vkm, vkm_buffer_handle BufferHandle, i64 NewSizeBytes)
{
    vkm_buffer_chunk *Chunk = BufferHandle.Value;
    ASSERT(Chunk->EntryIndex < Vkm->BufferPool.Count, "Invalid handle");

    if (NewSizeBytes > Chunk->SizeAligned*VKM_MIN_ALIGNMENT)
    {
        vkm_buffer_pool_entry *Entry = &Vkm->BufferPool.Data[Chunk->EntryIndex];
        vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];
        NewSizeBytes = MAXIMUM(NewSizeBytes, Chunk->SizeAligned*VKM_MIN_ALIGNMENT*2);
        NewSizeBytes = Memory_AlignSize(NewSizeBytes, Entry->Alignment);

        DoubleLink_Unlink(&Entry->Allocated, Chunk);
        Vkm__InsertFreeChunk(Vkm, Entry, Chunk);
        BufferHandle = Vkm__CreateBufferRaw(Vkm, 
            Entry->BufferUsageFlags, 
            DeviceMemory->MemoryProperties,
            NewSizeBytes
        );
    }
    return BufferHandle;
}

vkm_image_handle Vkm_ResizeImage(vkm *Vkm, vkm_image_handle ImageHandle, const vkm_resize_image_config *Config)
{
    vkm__unpacked_handle Unpacked = Vkm__UnpackHandle(ImageHandle.Value);
    ASSERT(Unpacked.Index < Vkm->ImagePool.Count, "Invalid handle");

    vkm_image_pool_entry *Entry = &Vkm->ImagePool.Data[Unpacked.Index];
    vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];
    VkImage OldImage = Entry->Image;
    VkImageView OldImageView = Entry->ImageView;
    VkSampleCountFlagBits Sample = Config->Sample? Config->Sample : Entry->Sample;
    VkFormat Format = Config->Format? Config->Format : Entry->Format;
    uint MipLevels = Config->MipLevels? Config->MipLevels : Entry->MipLevels;

    VkImage NewImage = Vkm__CreateVkImage(Vkm, 
        Entry->Tiling, Entry->Usage, Sample, Format, 
        Config->Width, Config->Height, MipLevels
    );

    VkMemoryRequirements Required = Vkm__GetImageMemoryRequirements(Vkm, NewImage, Unpacked.SizeBytes);
    bool32 CanReuseDeviceMemory = 
        (i64)Required.size <= Unpacked.SizeBytes
        && Required.alignment <= Entry->Alignment;
    if (CanReuseDeviceMemory)
    {
        vkBindImageMemory(Vkm->Device, NewImage, DeviceMemory->Handle, Unpacked.OffsetBytes);
    }
    else
    {
        /* deallocate old memory */
        {
            vkm_device_memory_node *NodeToDeallocate = Entry->DeviceMemoryNode;
            vkm_device_memory_node **Head = &Vkm->DeviceMemoryNodeFree[NodeToDeallocate->MemoryTypeIndex];
            Vkm__UnlinkNode(Head, NodeToDeallocate);
            Vkm__InsertFreeNode(Vkm, Head, NodeToDeallocate);
        }

        /* create new device memory */
        vkm_device_memory_node *NewNode;
        i64 OffsetBytes;
        {
            Required.size = Required.size * 1.5;
            ASSERT(Required.size < (u64)INT32_MAX*VKM_MIN_ALIGNMENT);

            NewNode = Vkm__AllocateNode(Vkm, Required, VKM_IMAGE_MEMORY_PROPERTY);
            OffsetBytes = NewNode->OffsetAligned * VKM_MIN_ALIGNMENT;
            vkBindImageMemory(Vkm->Device, NewImage, Vkm__GetDeviceMemoryHandle(Vkm, NewNode), OffsetBytes);
        }

        Entry->DeviceMemoryNode = NewNode;
        Entry->DeviceMemoryIndex = NewNode->DeviceMemoryIndex;
        ImageHandle.Value = Vkm__PackHandle(Unpacked.Index, Required.size, OffsetBytes);
    }

    VkImageView NewImageView = Vkm__CreateImageView(Vkm, 
        NewImage, Format, Entry->Aspect, MipLevels
    );
    Entry->Sample = Sample;
    Entry->Format = Format;
    Entry->MipLevels = MipLevels;
    Entry->Width = Config->Width;
    Entry->Height = Config->Height;
    Entry->Image = NewImage;
    Entry->ImageView = NewImageView;
    vkDestroyImage(Vkm->Device, OldImage, NULL);
    vkDestroyImageView(Vkm->Device, OldImageView, NULL);
    return ImageHandle;
}

void *Vkm_MapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle)
{
    vkm_buffer_chunk *Chunk = (BufferHandle.Value);
    ASSERT(IN_RANGE(0, Chunk->EntryIndex, Vkm->BufferPool.Count - 1), "Invalid handle");

    vkm_buffer_pool_entry *Entry = Vkm->BufferPool.Data + Chunk->EntryIndex;
    vkm_device_memory *DeviceMemory = Vkm->DeviceMemory.Data + Entry->DeviceMemoryIndex;
    if (DeviceMemory->MapCount == 0)
    {
        /* NOTE: map the entire VkBuffer to a region in VkDeviceMemory */
        vkMapMemory(Vkm->Device, 
            DeviceMemory->Handle, 
            Entry->DeviceMemoryOffsetBytes, 
            Entry->DeviceMemoryCapacityBytes, 
            0, 
            &DeviceMemory->MappedMemory
        );
    }
    ASSERT(DeviceMemory->MappedMemory);
    DeviceMemory->MapCount++;
    return (u8 *)DeviceMemory->MappedMemory + Entry->DeviceMemoryOffsetBytes + Chunk->OffsetAligned * VKM_MIN_ALIGNMENT;
}

void Vkm_UnmapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle, void *MappedMemory)
{
    (void)MappedMemory;
    vkm_buffer_chunk *Chunk = BufferHandle.Value;
    ASSERT(IN_RANGE(0, Chunk->EntryIndex, Vkm->BufferPool.Count - 1), "Invalid handle");

    vkm_buffer_pool_entry *Entry = Vkm->BufferPool.Data + Chunk->EntryIndex;
    vkm_device_memory *DeviceMemory = Vkm->DeviceMemory.Data + Entry->DeviceMemoryIndex;
    DeviceMemory->MapCount -= (DeviceMemory->MapCount != 0);
    if (DeviceMemory->MapCount == 0)
    {
        vkUnmapMemory(Vkm->Device, DeviceMemory->Handle);
        DeviceMemory->MappedMemory = NULL;
    }
}



vkm_buffer_info Vkm_GetBufferInfo(const vkm *Vkm, vkm_buffer_handle BufferHandle)
{
    vkm_buffer_chunk *Chunk = (BufferHandle.Value);
    ASSERT(IN_RANGE(0, Chunk->EntryIndex, Vkm->BufferPool.Count - 1), "Invalid buffer handle");

    const vkm_buffer_pool_entry *Entry = &Vkm->BufferPool.Data[Chunk->EntryIndex];
    const vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];

    vkm_buffer_info BufferInfo = {
        .Buffer = Entry->Buffer,
        .DeviceMemory = DeviceMemory->Handle, 
        .BufferUsage = Entry->BufferUsageFlags,
        .MemoryProperties = DeviceMemory->MemoryProperties,
        .MemoryTypeIndex = DeviceMemory->MemoryTypeIndex,
        .OffsetBytes = Chunk->OffsetAligned * VKM_MIN_ALIGNMENT,
        .CapacityBytes = Chunk->SizeAligned * VKM_MIN_ALIGNMENT,
    };
    return BufferInfo;
}

vkm_image_info Vkm_GetImageInfo(const vkm *Vkm, vkm_image_handle ImageHandle)
{
    vkm__unpacked_handle Unpacked = Vkm__UnpackHandle(ImageHandle.Value);
    ASSERT(IN_RANGE(0, Unpacked.Index, Vkm->ImagePool.Count - 1), "Invalid image handle");

    const vkm_image_pool_entry *Entry = &Vkm->ImagePool.Data[Unpacked.Index];
    const vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];

    vkm_image_info ImageInfo = {
        .Image = Entry->Image,
        .ImageView = Entry->ImageView,

        .DeviceMemory = DeviceMemory->Handle,
        .OffsetBytes = Unpacked.OffsetBytes,
        .CapacityBytes = Unpacked.SizeBytes,
        .MemoryTypeIndex = DeviceMemory->MemoryTypeIndex,

        .Usage = Entry->Usage,
        .Format = Entry->Format,
        .Sample = Entry->Sample,
        .Tiling = Entry->Tiling,
        .Aspect = Entry->Aspect,

        .PixelSizeBytes = Entry->PixelSizeBytes,
        .Width = Entry->Width,
        .Height = Entry->Height,
        .MipLevels = Entry->MipLevels,
    };
    return ImageInfo;
}

