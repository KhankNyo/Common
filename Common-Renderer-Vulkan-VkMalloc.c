
#include "Common-Renderer-Vulkan-VkMalloc.h"
#include "Common-Renderer-Vulkan.h"
#include "Common-Vulkan.h"
#include "Memory.h"



/* init */
#ifdef NEW_VKM_API



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
        Chunk = Arena_AllocNonZero(Vkm->Arena, sizeof *Chunk);
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

internal bool32 Vkm__UpdateLargest(vkm_buffer_pool_entry *Entry, vkm_buffer_chunk *Largest)
{
    if (!Largest)
        Largest = Entry->Freed;
    if (!Entry->Freed)
    {
        Entry->LargestFree = NULL;
        return false;
    }
    vkm_buffer_chunk *Curr = Largest;
    while (Curr)
    {
        if (Largest->SizeAligned > Curr->SizeAligned)
            Largest = Curr;
        Curr = Curr->Next;
    }
    Entry->LargestFree = Largest;
    return true;
}

internal void Vkm__UnlinkChunk(vkm_buffer_chunk **Head, vkm_buffer_chunk *Chunk)
{
    if (Chunk->Prev)
        Chunk->Prev->Next = Chunk->Next;
    else *Head = Chunk->Next;
    if (Chunk->Next)
        Chunk->Next->Prev = Chunk->Prev;

    Chunk->Next = NULL;
    Chunk->Prev = NULL;
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

    if (Prev)
        Prev->Next = Chunk;
    else Entry->Freed = Chunk;
    Chunk->Prev = Prev;
    Chunk->Next = Curr;
    if (Curr)
        Curr->Prev = Chunk;

    /* coalesce */
    for (vkm_buffer_chunk *i = Entry->Freed; i && i->Next; i = i->Next)
    {
        vkm_buffer_chunk *Next = i->Next;
        while (i && Next 
            && i->OffsetAligned + i->SizeAligned == Next->OffsetAligned)
        {
            vkm_buffer_chunk *Skip = Next;
            i->Next = Skip->Next;
            i->SizeAligned += Skip->SizeAligned;
            Next = Skip->Next;

            Skip->Next = Vkm->BufferChunkUnused;
            Vkm->BufferChunkUnused = Skip;
        }
    }

    Vkm__UpdateLargest(Entry, Largest);
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
        Node = Arena_AllocNonZero(Vkm->Arena, sizeof(vkm_device_memory_node));
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
    vkm_device_memory_node *Prev = Node->Prev;
    vkm_device_memory_node *Next = Node->Next;
    if (Prev)
        Prev->Next = Next;
    else *Head = Next;
    if (Next)
        Next->Prev = Prev;
    Node->Next = NULL;
    Node->Prev = NULL;
}

force_inline bool32 Vkm__IsNodeSuitable(const vkm_device_memory_node *Node, i64 SizeBytes, u32 Alignment)
{
    i64 SizeRemainBytes = Node->SizeAligned*VKM_MIN_ALIGNMENT;
    if (SizeRemainBytes < (i64)Alignment)
        return false;
    return SizeBytes <= Memory_AlignSize(SizeRemainBytes, Alignment);
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
            i64 CapacityBytes = MAXIMUM(Vkm->DeviceMemoryPoolCapacityBytes, (i64)Requirements.size);
            ASSERT(CapacityBytes <= ((i64)INT32_MAX) * VKM_MIN_ALIGNMENT, "memory size too big");

            VkDeviceMemory DeviceMemory;
            VkMemoryAllocateInfo AllocateInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .memoryTypeIndex = MemoryTypeIndex,
                .allocationSize = CapacityBytes,
            };
            VK_CHECK(vkAllocateMemory(Vkm->Device, &AllocateInfo, NULL, &DeviceMemory));
            VkDynamicArray_Push(&Vkm->FreeList, &Vkm->DeviceMemory, (vkm_device_memory) {
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
        }
    }
    return FreeNode;
}

internal void Vkm__InsertNode(vkm *Vkm, vkm_device_memory_node **Head, vkm_device_memory_node *Node)
{
    vkm_device_memory_node *Prev = NULL;
    vkm_device_memory_node *Curr = *Head;
    while (Curr && Node->OffsetAligned < Curr->OffsetAligned) 
    {
        Prev = Curr;
        Curr = Curr->Next;
    }

    if (Prev)
        Prev->Next = Node;
    else *Head = Node;
    Node->Prev = Prev;
    Node->Next = Curr;
    if (Curr)
        Curr->Prev = Node;

    /* coalesce */
    for (vkm_device_memory_node *i = *Head; i && i->Next; i = i->Next)
    {
        vkm_device_memory_node *Next = i->Next;
        while (i && Next
            && i->OffsetAligned + i->SizeAligned == Next->OffsetAligned)
        {
            vkm_device_memory_node *Skip = Next;
            i->Next = Skip->Next;
            i->SizeAligned += Skip->SizeAligned;
            Next = Skip->Next;

            Skip->Next = Vkm->DeviceMemoryNodeUnused;
            Vkm->DeviceMemoryNodeUnused = Skip;
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
    if (NULL == Entry->LargestFree && !Vkm__UpdateLargest(Entry, NULL))
    {
        Vulkan_LogLn("Unsuitable");
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

        /* split image memory */
        bool32 Splitable = (i64)FreeNode->SizeAligned*VKM_MIN_ALIGNMENT >= (i64)Requirements.size*2;
        if (Splitable)
        {
            AllocatedNode = Vkm__CreateNode(Vkm, 
                Requirements.size, 
                FreeNode->OffsetAligned*VKM_MIN_ALIGNMENT, 
                FreeNode->DeviceMemoryIndex, 
                FreeNode->MemoryTypeIndex
            );
            FreeNode->SizeAligned -= AllocatedNode->SizeAligned;
            FreeNode->OffsetAligned += AllocatedNode->SizeAligned;
            Vkm__InsertNode(Vkm, &Vkm->DeviceMemoryNodeFree[FreeNode->MemoryTypeIndex], FreeNode);
            ASSERT(Vkm->DeviceMemoryNodeFree[FreeNode->MemoryTypeIndex], "");
            Vulkan_LogLn("Node Split: %d KB, %d KB", AllocatedNode->SizeAligned * VKM_MIN_ALIGNMENT/KB, FreeNode->SizeAligned * VKM_MIN_ALIGNMENT/KB);
        }
        else
        {
            AllocatedNode = FreeNode;
            Vulkan_LogLn("Node Not Split: %d KB", AllocatedNode->SizeAligned * VKM_MIN_ALIGNMENT/KB);
        }

        /* insert allocated node */
        vkm_device_memory_node **Head = &Vkm->DeviceMemoryNodeAllocated[AllocatedNode->MemoryTypeIndex];
        AllocatedNode->Prev = NULL;
        AllocatedNode->Next = *Head;
        if (*Head)
            (*Head)->Prev = AllocatedNode;

        *Head = AllocatedNode;
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
    Vulkan_LogLn("alloc: %ldmb", CapacityBytes/MB);

    int Index = -1;
    vkm_buffer_chunk *SuitableChunk = NULL;
    for (int i = Pool->Count - 1; i >= 0; i--)
    {
        vkm_buffer_pool_entry *Entry = &Pool->Data[i];
        i64 SizeAligned = Memory_AlignSize(CapacityBytes, Entry->Alignment) / VKM_MIN_ALIGNMENT;
        Vulkan_LogLn("  checking %d/%d", i, Pool->Count);
        if (Vkm__IsBufferSuitable(Vkm, Entry, BufferUsage, BufferMemoryProperties, SizeAligned))
        {
            Index = i;
            /* unlink */
            /* NOTE: largest free will get updated */
            SuitableChunk = Entry->LargestFree;
            Entry->LargestFree = NULL;
            Vkm__UnlinkChunk(&Entry->Freed, SuitableChunk);
            break;
        }
    }
    if (!SuitableChunk)
    {
        Vulkan_LogLn("  New buffer", CapacityBytes/MB);
        /* create new buffer pool entry */
        i64 BufferSize = MAXIMUM(CapacityBytes, Vkm->BufferPoolCapacityBytes);

        VkBuffer Buffer;
        {
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
        i64 OffsetBytes = Node->OffsetAligned * VKM_MIN_ALIGNMENT;
        vkBindBufferMemory(Vkm->Device, Buffer, Vkm__GetDeviceMemoryHandle(Vkm, Node), OffsetBytes);

        VkDynamicArray_Push(&Vkm->FreeList, Pool, (vkm_buffer_pool_entry) {
            .Buffer = Buffer,
            .BufferUsageFlags = BufferUsage,
            .Alignment = Required.alignment,
            .DeviceMemoryIndex = Node->DeviceMemoryIndex,
            .DeviceMemoryNode = Node,
        });
        Index = Pool->Count - 1;
        SuitableChunk = Vkm__CreateChunk(Vkm, Index, Required.size, 0);
    }

    ASSERT(IN_RANGE(0, Index, Pool->Count - 1));
    ASSERT(SuitableChunk);

    vkm_buffer_pool_entry *Entry = &Pool->Data[Index];
    CapacityBytes = Memory_AlignSize(CapacityBytes, Entry->Alignment);
    {
        if (SuitableChunk->SizeAligned*VKM_MIN_ALIGNMENT >= CapacityBytes*2)
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
            Vulkan_LogLn("  Splitting chunk");
        }
        else
        {
            /* keep chunk */
            CapacityBytes = SuitableChunk->SizeAligned*VKM_MIN_ALIGNMENT;
            Vulkan_LogLn("  Keeping chunk");
        }

        /* update allocated list */
        if (Entry->Allocated)
            Entry->Allocated->Prev = SuitableChunk;
        SuitableChunk->Prev = NULL;
        SuitableChunk->Next = Entry->Allocated;
        Entry->Allocated = SuitableChunk;
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





void Vkm_Create(vkm *Vkm, arena_alloc *Arena, const vkm_config *Config)
{
    *Vkm = (vkm) { 
        .Arena = Arena,
        .Device = Config->Device,
        .PhysicalDevice = Config->PhysicalDevice,
        .DeviceMemoryPoolCapacityBytes = Config->DeviceMemoryPoolCapacityBytes,
        .BufferPoolCapacityBytes = Config->BufferPoolCapacityBytes,
    };
    u32 FreeListAlignment = 8;
    isize FreeListPoolSize = 64*KB;
    FreeList_Create(&Vkm->FreeList, Arena_AsAllocInterface(Arena), FreeListPoolSize, FreeListAlignment);
}

void Vkm_Destroy(vkm *Vkm)
{
    /* print alloc stats */
    Vulkan_LogLn("==== vkm alloc stat: %p ====", Vkm);
    Vulkan_LogLn("    VkDeviceMemory: %d", Vkm->DeviceMemory.Count);
    for (int i = 0; i < Vkm->DeviceMemory.Count; i++)
    {
        vkm_device_memory *Entry = &Vkm->DeviceMemory.Data[i];
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
            Vulkan_LogLn("      size, offset, idx: %ld KB, %ld Bytes, %d", i->SizeAligned * VKM_MIN_ALIGNMENT/KB, i->OffsetAligned*VKM_MIN_ALIGNMENT, i->DeviceMemoryIndex);
        }
        Vulkan_LogLn("    FreeNode %d: ", k);
        for (vkm_device_memory_node *i = Vkm->DeviceMemoryNodeFree[k]; i; i = i->Next)
        {
            Vulkan_LogLn("      size, offset, idx: %ld KB, %ld Bytes, %d", i->SizeAligned * VKM_MIN_ALIGNMENT/KB, i->OffsetAligned*VKM_MIN_ALIGNMENT, i->DeviceMemoryIndex);
        }
    }
    Vulkan_LogLn("    vkm_buffer (pool): %d", (int)Vkm->BufferPool.Count);
    for (int i = 0; i < Vkm->BufferPool.Count; i++)
    {
        vkm_buffer_pool_entry *Entry = Vkm->BufferPool.Data + i;
        isize LargestFree = Vkm__UpdateLargest(Entry, Entry->LargestFree)
            ? Entry->LargestFree->SizeAligned * VKM_MIN_ALIGNMENT
            : 0;

        Vulkan_LogLn("      Group %d: VkDeviceMemory %d, largest: %ld KB", i, Entry->DeviceMemoryIndex, LargestFree / KB);
        Vulkan_LogLn("        alloced:"); 
        vkm_buffer_chunk *Chunk = Entry->Allocated;
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
        vkm_image_pool_entry *Entry = Vkm->ImagePool.Data + i;
        /* TODO: log image allocation  */
        Vulkan_LogLn("      VkDeviceMemory: %d; size: %lld KB; Offset: %lld Bytes, w: %d, h: %d", 
            i, 
            Entry->DeviceMemoryNode->SizeAligned * VKM_MIN_ALIGNMENT/KB,
            Entry->DeviceMemoryNode->OffsetAligned * VKM_MIN_ALIGNMENT,
            Entry->Width, Entry->Height
        );
    }



    /* deallocate all memory */
    dynamic_array_foreach(&Vkm->DeviceMemory, i)
    {
        vkFreeMemory(Vkm->Device, i->Handle, NULL);
    }

    /* deallocate all buffers */
    dynamic_array_foreach(&Vkm->BufferPool, i)
    {
        vkDestroyBuffer(Vkm->Device, i->Buffer, NULL);
    }

    /* deallocate all images */
    dynamic_array_foreach(&Vkm->ImagePool, i)
    {
        vkDestroyImage(Vkm->Device, i->Image, NULL);
        vkDestroyImageView(Vkm->Device, i->ImageView, NULL);
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
    ASSERT(Config->Samples, "Samples cannot be 0");
    ASSERT(Config->Format, "Format cannot be 0 (VK_FORMAT_UNDEFINED)");
    ASSERT(Config->Usage, "Usage cannot be 0");
    ASSERT(Config->Aspect, "Aspect cannot be 0 (VK_ASPECT_NONE)");

    VkImage Image = Vkm__CreateVkImage(Vkm, 
        Config->Tiling, Config->Usage, Config->Samples, Config->Format, 
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
    VkDynamicArray_Push(&Vkm->FreeList, &Vkm->ImagePool, (vkm_image_pool_entry) {
        .Image = Image,
        .ImageView = ImageView, 
        .DeviceMemoryNode = AllocatedNode,
        .Alignment = Required.alignment,

        .Usage = Config->Usage,
        .Format = Config->Format,
        .Samples = Config->Samples,
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
        NewSizeBytes = MAXIMUM(NewSizeBytes, Chunk->SizeAligned*VKM_MIN_ALIGNMENT*2);
        NewSizeBytes = Memory_AlignSize(NewSizeBytes, Entry->Alignment);


        Vkm__UnlinkChunk(&Entry->Allocated, Chunk);
        Vkm__InsertFreeChunk(Vkm, Entry, Chunk);
        BufferHandle = Vkm__CreateBufferRaw(Vkm, 
            Entry->BufferUsageFlags, 
            Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex].MemoryProperties,
            NewSizeBytes
        );
        UNREACHABLE();
    }
    return BufferHandle;
}

vkm_image_handle Vkm_ResizeImage(vkm *Vkm, vkm_image_handle ImageHandle, u16 NewWidth, u16 NewHeight)
{
    vkm__unpacked_handle Unpacked = Vkm__UnpackHandle(ImageHandle.Value);
    ASSERT(Unpacked.Index < Vkm->ImagePool.Count, "Invalid handle");

    vkm_image_pool_entry *Entry = &Vkm->ImagePool.Data[Unpacked.Index];
    vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Entry->DeviceMemoryIndex];
    VkImage OldImage = Entry->Image;
    VkImageView OldImageView = Entry->ImageView;

    VkImage NewImage = Vkm__CreateVkImage(Vkm, 
        Entry->Tiling, Entry->Usage, Entry->Samples, Entry->Format, 
        NewWidth, NewHeight, Entry->MipLevels
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
        vkm_device_memory_node *NodeToDeallocate = Entry->DeviceMemoryNode;
        Vkm__InsertNode(Vkm, &Vkm->DeviceMemoryNodeFree[NodeToDeallocate->MemoryTypeIndex], NodeToDeallocate);

        /* create new device memory */
        Required.size *= 2;
        ASSERT(Required.size < (u64)INT32_MAX*VKM_MIN_ALIGNMENT);
        vkm_device_memory_node *NewNode = Vkm__AllocateNode(Vkm, Required, VKM_IMAGE_MEMORY_PROPERTY);
        i64 OffsetBytes = NewNode->OffsetAligned * VKM_MIN_ALIGNMENT;
        vkBindImageMemory(Vkm->Device, NewImage, Vkm__GetDeviceMemoryHandle(Vkm, NewNode), OffsetBytes);

        Entry->DeviceMemoryNode = NewNode;
        Entry->DeviceMemoryIndex = NewNode->DeviceMemoryIndex;
        ImageHandle.Value = Vkm__PackHandle(Unpacked.Index, Required.size, OffsetBytes);
    }

    VkImageView NewImageView = Vkm__CreateImageView(Vkm, 
        NewImage, Entry->Format, Entry->Aspect, Entry->MipLevels
    );
    Entry->Width = NewWidth;
    Entry->Height = NewHeight;
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

    vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Chunk->EntryIndex];
    if (DeviceMemory->MapCount == 0)
    {
        void *Ptr;
        vkMapMemory(Vkm->Device, DeviceMemory->Handle, 0, DeviceMemory->CapacityAligned*VKM_MIN_ALIGNMENT, 0, &Ptr);
        DeviceMemory->MappedMemory = Ptr;
    }
    ASSERT(DeviceMemory->MappedMemory);
    DeviceMemory->MapCount++;
    return (u8 *)DeviceMemory->MappedMemory + Chunk->OffsetAligned;
}

void Vkm_UnmapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle, void *MappedMemory)
{
    (void)MappedMemory;
    vkm_buffer_chunk *Chunk = BufferHandle.Value;
    ASSERT(IN_RANGE(0, Chunk->EntryIndex, Vkm->BufferPool.Count - 1), "Invalid handle");

    vkm_device_memory *DeviceMemory = &Vkm->DeviceMemory.Data[Chunk->EntryIndex];
    DeviceMemory->MapCount -= (DeviceMemory->MapCount != 0);
    if (DeviceMemory->MapCount == 0)
    {
        vkUnmapMemory(Vkm->Device, DeviceMemory->MappedMemory);
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
        .Samples = Entry->Samples,
        .Tiling = Entry->Tiling,
        .Aspect = Entry->Aspect,

        .PixelSizeBytes = Entry->PixelSizeBytes,
        .Width = Entry->Width,
        .Height = Entry->Height,
        .MipLevels = Entry->MipLevels,
    };
    return ImageInfo;
}


#else

#define Vkm__Buffer_Init(memory_type, pool_index, offset_bytes, size_bytes) (vkm_buffer) {.Info = \
        (u64)(memory_type) << 62\
        | (u64)(pool_index) << 56\
        | (((u64)(offset_bytes)/VKM_BUFFER_MIN_ALIGNMENT) & 0x0FFFFFFF) << 28\
        | (((u64)(size_bytes)/VKM_BUFFER_MIN_ALIGNMENT) & 0x0FFFFFFF) << 0\
    }


typedef_struct(vkm__allocate_device_memory_result);
struct vkm__allocate_device_memory_result
{
    i64 Offset;
    VkDeviceMemory Handle;
};


internal i32 Vulkan_FindMemoryType(VkPhysicalDevice PhysDevice, u32 Filter, VkMemoryPropertyFlags Flags)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(PhysDevice, &MemoryProperties);
    for (u32 i = 0; i < MemoryProperties.memoryTypeCount; i++)
    {
        // zzz
        if (Filter & (1llu << i) && MemoryProperties.memoryTypes[i].propertyFlags & Flags)
            return i;
    }
    return -1;
}

internal bool32 Vkm__BufferFits(const vkm_buffer_pool_slot *Slot, i64 BufferSizeBytes)
{
    /* TODO: turn this into a pool-style allocator */
    i64 AlignedSize = Memory_AlignSize(BufferSizeBytes, Slot->Alignment);
    bool32 Fits = Slot->SizeBytesRemain - AlignedSize >= 0;
    return Fits;
}

internal vkm__allocate_device_memory_result Vkm__AllocateDeviceMemory(vkm *Vkm, VkMemoryRequirements Requirements, VkMemoryPropertyFlags Flags)
{
    int MemoryTypeIndex = Vulkan_FindMemoryType(
        Vkm->PhysicalDevice, 
        Requirements.memoryTypeBits, 
        Flags
    );
    ASSERT(MemoryTypeIndex > -1);
    ASSERT(Vkm->DeviceMemoryCount <= (i64)STATIC_ARRAY_SIZE(Vkm->DeviceMemory));

    for (int i = Vkm->DeviceMemoryCount - 1; i >= 0; i--)
    {
        vkm_device_memory *PoolSlot = &Vkm->DeviceMemory[i];
        i64 Offset = Memory_AlignSize(PoolSlot->Offset, Requirements.alignment);
        if (PoolSlot->TypeIndex == MemoryTypeIndex
        && Offset + (isize)Requirements.size <= PoolSlot->Capacity)
        {
            /* found appropriate pool slot, allocate from it */
            PoolSlot->Offset = Offset + Requirements.size;
            return (vkm__allocate_device_memory_result) {
                .Handle = PoolSlot->Handle,
                .Offset = Offset,
            };
        }
    }

    /* create a new pool and allocate from it */
    ASSERT(Vkm->DeviceMemoryCount + 1 <= (i64)STATIC_ARRAY_SIZE(Vkm->DeviceMemory));
    u32 i = Vkm->DeviceMemoryCount;
    VkDeviceMemory Memory;
    {
        i64 MaxMemoryCapacity = 
            MAXIMUM((i64)Requirements.size, Vkm->NewDeviceMemoryCapacity);
        VkMemoryAllocateInfo AllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .memoryTypeIndex = MemoryTypeIndex, 
            .allocationSize = MaxMemoryCapacity,
        };
        // zzz
        VK_CHECK(vkAllocateMemory(Vkm->Device, &AllocateInfo, NULL, &Memory));
        Vkm->DeviceMemory[i] = (vkm_device_memory) {
            .Handle = Memory,
            .Capacity = MaxMemoryCapacity,
            .Offset = Requirements.size,
            .TypeIndex = MemoryTypeIndex,
        };
        Vkm->DeviceMemoryCount++;
        Vkm->NewDeviceMemoryCapacity = MaxMemoryCapacity;
    }
    return (vkm__allocate_device_memory_result) {
        .Handle = Memory,
        .Offset = 0,
    };
}

internal VkImage Vkm__CreateVkImage(
    VkDevice Device, 
    u32 Width, u32 Height, u32 MipLevels, 
    VkSampleCountFlagBits Samples,
    VkFormat Format,
    VkImageTiling Tiling, 
    VkImageUsageFlags Usage
) {
    VkImage Image;
    {
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
        VK_CHECK(vkCreateImage(Device, &ImageCreateInfo, NULL, &Image));
    }
    return Image;
}

internal VkImageView Vkm__CreateVkImageView(vkm *Vkm, vkm_image_handle ImageHandle, VkImageAspectFlags Aspect)
{
    VkImageViewCreateInfo ImageViewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = Vkm_Image_Get(Vkm, ImageHandle).Handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = Vkm_Image_Get(Vkm, ImageHandle).Format,
        .components = { /* default mapping, use channel values */
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = Aspect, 
            .baseMipLevel = 0, 
            .levelCount = Vkm_Image_Get(Vkm, ImageHandle).MipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1, 
        },
    };
    VkImageView ImageView;
    VK_CHECK(vkCreateImageView(Vkm->Device, &ImageViewCreateInfo, NULL, &ImageView));
    return ImageView;

}





void Vkm_Create(
    vkm *Vkm, 
    VkDevice Device, VkPhysicalDevice PhysicalDevice, 
    i64 ImageMemoryPoolCapacityBytes,
    i64 BufferMemoryPoolCapacityBytes[VKM_MEMORY_TYPE_COUNT],
    VkBufferUsageFlags DefaultUsages[VKM_MEMORY_TYPE_COUNT],
    VkMemoryPropertyFlags DefaultMemoryProperties[VKM_MEMORY_TYPE_COUNT]
) {
    ASSERT_EXPRESSION_TYPE((vkm_buffer){0}.Info, u64, "invalid type");
    STATIC_ASSERT(STATIC_ARRAY_SIZE(Vkm->BufferPool[0].Slot) == VKM_POOL_SLOT_COUNT, "");

    *Vkm = (vkm) {
        .Device = Device,
        .PhysicalDevice = PhysicalDevice,
        .NewDeviceMemoryCapacity = ImageMemoryPoolCapacityBytes,
    };
    for (int i = 0; i < VKM_MEMORY_TYPE_COUNT; i++)
    {
        Vkm->BufferPool[i].ResetCapacity = BufferMemoryPoolCapacityBytes[i];
        Vkm->BufferPool[i].DefaultUsages = DefaultUsages[i];
        Vkm->BufferPool[i].DefaultMemoryProperties = DefaultMemoryProperties[i];
    }
}

void Vkm_Destroy(vkm *Vkm)
{
    for (int i = 0; i < VKM_MEMORY_TYPE_COUNT; i++)
    {
        vkm_buffer_pool *Pool = &Vkm->BufferPool[i];
        for (int k = 0; k < Pool->SlotCount; k++)
        {
            vkDestroyBuffer(Vkm->Device, Pool->Slot[k].BufferHandle, NULL);
            vkFreeMemory(Vkm->Device, Pool->Slot[k].MemoryHandle, NULL);
        }
    }

    for (int i = 0; i < Vkm->ImageCount; i++)
    {
        vkDestroyImage(Vkm->Device, Vkm->Images[i].Handle, NULL);
    }
    for (int i = 0; i < Vkm->DeviceMemoryCount; i++)
    {
        vkFreeMemory(Vkm->Device, Vkm->DeviceMemory[i].Handle, NULL);
    }
}

void Vkm_Reset(vkm *Vkm)
{
    for (int i = 0; i < VKM_MEMORY_TYPE_COUNT; i++)
    {
        vkm_buffer_pool *Pool = &Vkm->BufferPool[i];
        for (int k = 0; k < Pool->SlotCount; k++)
        {
            Pool->Slot[k].SizeBytesRemain = Pool->Slot[k].Capacity;
        }
    }

    /* NOTE: must deallocate all images since we can't use them anymore */
    for (int i = 0; i < Vkm->ImageCount; i++)
    {
        vkDestroyImage(Vkm->Device, Vkm->Images[i].Handle, NULL);
    }
    for (int i = 0; i < Vkm->DeviceMemoryCount; i++)
    {
        vkFreeMemory(Vkm->Device, Vkm->DeviceMemory[i].Handle, NULL);
    }
    Vkm->ImageCount = 0;
    Vkm->DeviceMemoryCount = 0;
}




VkDeviceMemory Vkm_Buffer_GetVkDeviceMemory(const vkm *Vkm, vkm_buffer Buffer)
{
    return Vkm->BufferPool[Vkm_Buffer_GetMemoryType(Buffer)]
                    .Slot[Vkm_Buffer_GetPoolIndex(Buffer)].MemoryHandle;
}

VkBuffer Vkm_Buffer_GetVkBuffer(const vkm *Vkm, vkm_buffer Buffer)
{
    return Vkm->BufferPool[Vkm_Buffer_GetMemoryType(Buffer)]
                    .Slot[Vkm_Buffer_GetPoolIndex(Buffer)].BufferHandle;
}

void *Vkm_Buffer_GetMappedMemory(vkm *Vkm, vkm_buffer Buffer)
{
    ASSERT(Vkm_Buffer_GetMemoryType(Buffer) == VKM_MEMORY_TYPE_CPU_VISIBLE, "Cannot map non-cpu visible memory");
    vkm_buffer_pool_slot *Slot = 
        &Vkm->BufferPool[Vkm_Buffer_GetMemoryType(Buffer)]
                .Slot[Vkm_Buffer_GetPoolIndex(Buffer)];

    u8 *Ptr = Slot->MappedMemory + Vkm_Buffer_GetOffsetBytes(Buffer);
    return Ptr;
}


vkm_buffer Vkm_CreateBuffer(vkm *Vkm, vkm_memory_type MemoryType, isize BufferSizeBytes)
{
    vkm_buffer_pool *Pool = &Vkm->BufferPool[MemoryType];

    /* find fit pool */
    vkm_buffer_pool_slot *Slot = NULL;
    u64 Offset = 0;
    for (int i = Pool->SlotCount - 1; i >= 0; i--)
    {
        vkm_buffer_pool_slot *CurrSlot = &Pool->Slot[i];

        if (Vkm__BufferFits(CurrSlot, BufferSizeBytes))
        {
            /* NOTE: alignment is already taken care of when pool is allocated */
            Slot = CurrSlot;
            Offset = Slot->Capacity - Slot->SizeBytesRemain;
            break;
        }
    }

    if (NULL == Slot)
    {
        isize NewBufferSize = MAXIMUM(BufferSizeBytes, Pool->ResetCapacity);
        usize NewBufferAlignment = VKM_BUFFER_MIN_ALIGNMENT;
        Pool->ResetCapacity = NewBufferSize;

        VkBuffer Buffer;
        VkMemoryRequirements MemoryRequirements;
        {
            VkBufferCreateInfo CreateInfo = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .usage = Pool->DefaultUsages | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .size = NewBufferSize,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

                .flags = 0,
                .pNext = NULL,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = NULL, /* NOTE: must be filled when sharingMode is VK_SHARING_MODE_CONCURRENT */
            };
            VK_CHECK(vkCreateBuffer(Vkm->Device, &CreateInfo, NULL, &Buffer));
            vkGetBufferMemoryRequirements(Vkm->Device, Buffer, &MemoryRequirements);
        }

        VkDeviceMemory Memory = VK_NULL_HANDLE;
        void *MappedMemory = NULL;
        {
            int MemoryTypeIndex = Vulkan_FindMemoryType(Vkm->PhysicalDevice, MemoryRequirements.memoryTypeBits, Pool->DefaultMemoryProperties);
            NewBufferAlignment = MAXIMUM(NewBufferAlignment, MemoryRequirements.alignment);
            NewBufferSize = Memory_AlignSize(MAXIMUM(NewBufferSize, (isize)MemoryRequirements.size), NewBufferAlignment);

            VkMemoryAllocateInfo AllocInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .memoryTypeIndex = MemoryTypeIndex,
                .allocationSize = NewBufferSize,

                .pNext = NULL,
            };
            VK_CHECK(vkAllocateMemory(Vkm->Device, &AllocInfo, NULL, &Memory));
            vkBindBufferMemory(Vkm->Device, Buffer, Memory, 0);

            if (MemoryType == VKM_MEMORY_TYPE_CPU_VISIBLE)
            {
                VK_CHECK(vkMapMemory(Vkm->Device, Memory, 0, NewBufferSize, 0, &MappedMemory));
                ASSERT(MappedMemory);
            }
        }

        /* create a new pool slot */
        ASSERT(Pool->SlotCount < VKM_POOL_SLOT_COUNT, "Out of pool");
        Pool->Slot[Pool->SlotCount] = (vkm_buffer_pool_slot) {
            .BufferHandle = Buffer,
            .MemoryHandle = Memory,
            .MappedMemory = MappedMemory,
            .Alignment = NewBufferAlignment,
            .SizeBytesRemain = NewBufferSize,
            .Capacity = NewBufferSize,
        };
        Slot = &Pool->Slot[Pool->SlotCount];
        Pool->SlotCount++;
    }

    ASSERT(Slot != NULL);
    ASSERT(Vkm__BufferFits(Slot, BufferSizeBytes), "Slot: %zi, alignment: %zi, BufferSizeBytes: %zi, aligned size: %zi", 
        Slot->SizeBytesRemain, Slot->Alignment, BufferSizeBytes, Memory_AlignSize(BufferSizeBytes, Slot->Alignment)
    );

    u64 PoolIndex = Slot - Pool->Slot;
    u64 AlignedOffset = Memory_AlignSize(Offset, Slot->Alignment);
    u64 AlignedSize = Memory_AlignSize(BufferSizeBytes, Slot->Alignment);
    ASSERT(PoolIndex <= VKM_POOL_SLOT_COUNT, "Invalid pool slot");
    ASSERT(AlignedOffset <= VKM_BUFFER_MAX_OFFSET, "Invalid pool slot alignment");
    ASSERT(AlignedSize <= VKM_BUFFER_MAX_SIZEBYTES, "Invalid pool slot size");

    Slot->SizeBytesRemain -= AlignedSize;
    vkm_buffer Buffer = Vkm__Buffer_Init(MemoryType, PoolIndex, AlignedOffset, AlignedSize); 
    return Buffer;
}

vkm_image_handle Vkm_CreateImage(
    vkm *Vkm, 
    u32 Width, u32 Height, u32 MipLevels,
    VkSampleCountFlagBits Samples,
    VkFormat Format, 
    VkImageTiling Tiling, 
    VkImageUsageFlags Usage
) {
    VkDevice Device = Vkm->Device;
    VkImage ImageHandle = Vkm__CreateVkImage(Vkm->Device, 
        Width, Height, MipLevels, Samples, Format, Tiling, Usage
    );
    VkMemoryRequirements ImageMemoryRequirements;
    vkGetImageMemoryRequirements(Vkm->Device, ImageHandle, &ImageMemoryRequirements);

    i64 MemoryOffset;
    VkDeviceMemory MemoryHandle;
    {
        VkMemoryPropertyFlags ImageMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        vkm__allocate_device_memory_result Result = Vkm__AllocateDeviceMemory(
            Vkm, ImageMemoryRequirements, ImageMemoryProperties
        );
        MemoryHandle = Result.Handle;
        MemoryOffset = Result.Offset;
        VK_CHECK(vkBindImageMemory(Device, ImageHandle, MemoryHandle, MemoryOffset));
    }

    vkm_image_handle Handle = { 0 };
    if (Vkm->ImageCount < (i32)STATIC_ARRAY_SIZE(Vkm->Images))
    {
        Handle.Value = Vkm->ImageCount;
        Vkm->Images[Handle.Value] = (vkm_image) {
            .Handle = ImageHandle,
            .MemoryHandle = MemoryHandle,
            .MemoryOffset = MemoryOffset,
            .Width = Width,
            .Height = Height,
            .Samples = Samples,
            .Capacity = ImageMemoryRequirements.size,
            .MipLevels = MipLevels,
            .Format = Format,
            .Tiling = Tiling, 
            .Usage = Usage,
        };
        Vkm->ImageCount++;
    }
    else
    {
        ASSERT(false, "Out of memory for image allocation");
    }
    return Handle;
}

vkm_image_and_view Vkm_CreateImageAndView(
    vkm *Vkm, 
    u32 Width, u32 Height, u32 MipLevels,
    VkSampleCountFlagBits Samples,
    VkFormat Format, 
    VkImageTiling Tiling, 
    VkImageUsageFlags Usage, 
    VkImageAspectFlags Aspect
) {
    vkm_image_handle ImageHandle = Vkm_CreateImage(Vkm, 
        Width, Height, MipLevels, Samples, Format, Tiling, Usage
    );
    VkImageView ImageView = Vkm__CreateVkImageView(Vkm, ImageHandle, Aspect);
    return (vkm_image_and_view) {
        .ImageHandle = ImageHandle,
        .ImageView = ImageView,
        .Aspect = Aspect,
    };
}

/* NOTE: we assumed that the underlying image is not in use */
vkm_image_and_view Vkm_ImageAndView_Resize(vkm *Vkm, vkm_image_and_view Iav, u32 NewWidth, u32 NewHeight)
{
    ASSERT((i32)Iav.ImageHandle.Value < Vkm->ImageCount, "%d", Iav.ImageHandle.Value);
    vkm_image *Image = &Vkm->Images[Iav.ImageHandle.Value];
    VkImage NewImage = Vkm__CreateVkImage(Vkm->Device, 
        NewWidth, NewHeight, Image->MipLevels, Image->Samples, Image->Format, Image->Tiling, Image->Usage
    );
    VkMemoryRequirements MemoryRequirements;
    vkGetImageMemoryRequirements(Vkm->Device, NewImage, &MemoryRequirements);

    vkm_image_and_view NewIav;
    if ((i64)MemoryRequirements.size > Image->Capacity)
    {
        /* image does not fit, add to free list and allocate new one */
        vkDestroyImage(Vkm->Device, NewImage, NULL);
        RUNTIME_TODO("resizing image that does not fit");
    }
    else
    {
        /* image with new dimensions fit, replace the old one with it */
        vkBindImageMemory(Vkm->Device, NewImage, Image->MemoryHandle, Image->MemoryOffset);
        vkDestroyImage(Vkm->Device, Image->Handle, NULL);
        Image->Width = NewWidth;
        Image->Height = NewHeight;
        Image->Handle = NewImage;

        vkDestroyImageView(Vkm->Device, Iav.ImageView, NULL);
        VkImageView NewImageView = Vkm__CreateVkImageView(Vkm, Iav.ImageHandle, Iav.Aspect);

        NewIav = (vkm_image_and_view) {
            .ImageHandle = Iav.ImageHandle, /* same old handle */
            .ImageView = NewImageView,
            .Aspect = Iav.Aspect,
        };
    }
    return NewIav;
}



#endif
