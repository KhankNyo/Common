
#include "Common-Renderer-Vulkan-VkMalloc.h"
#include "Common-Renderer-Vulkan.h"
#include "Common-Vulkan.h"
#include "Memory.h"



/* init */
#ifdef NEW_VKM_API

typedef struct
{
    vkm_buffer_type BufferType;
    u32 BufferIndex;
    u32 SizeBytes;
    u32 OffsetBytes;
} vkm__unpacked_buffer_handle;

force_inline vkm_buffer_handle Vkm__PackBufferHandle(
    vkm_buffer_type BufferType, 
    u32 BufferIndex, 
    u32 BufferSizeBytes,
    u32 BufferOffsetBytes
) {
    ASSERT(BufferIndex <= VKM_MAX_BUFFER_COUNT, "Buffer index too big");

    u64 BufferSizeAligned = BufferSizeBytes / VKM_MIN_BUFFER_ALIGNMENT;
    u64 BufferOffsetAligned = BufferOffsetBytes / VKM_MIN_BUFFER_ALIGNMENT;
    vkm_buffer_handle Result = {
        .Value = (u64)BufferType
                | (((u64)BufferIndex << 3))
                | BufferOffsetAligned << 10
                | BufferSizeAligned << 37
    };
    return Result;
}

force_inline vkm__unpacked_buffer_handle Vkm__UnpackBufferHandle(
    vkm_buffer_handle Handle
) {
    vkm__unpacked_buffer_handle Unpacked = {
        .BufferType = Handle.Value & 0x07,
        .BufferIndex = (Handle.Value >> 3) & 0x7F,
        .OffsetBytes = ((Handle.Value >> 10) & ((1 << 27) - 1)) * VKM_MIN_BUFFER_ALIGNMENT,
        .SizeBytes = ((Handle.Value >> 37) & ((1 << 27) - 1)) * VKM_MIN_BUFFER_ALIGNMENT,
    };
    return Unpacked;
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

internal VkBufferUsageFlagBits Vkm__GetBufferUsageFlagBits(vkm_buffer_type BufferType)
{
    VkBufferUsageFlagBits Result = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    switch (BufferType)
    {
    case VKM_BUFFER_TYPE_STAGING:
        Result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    case VKM_BUFFER_TYPE_UBO:
        Result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case VKM_BUFFER_TYPE_VBO:
        Result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        break;
    case VKM_BUFFER_TYPE_EBO:
        Result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        break;
    case VKM_BUFFER_TYPE_SSBO:
        Result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    }
    return Result;
}

internal VkMemoryPropertyFlags Vkm__GetMemoryPropertyFlagsForBuffer(vkm_buffer_type BufferType)
{
    VkMemoryPropertyFlagBits Result = 0;
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

internal vkm_device_memory *Vkm__CreateDeviceMemory(vkm *Vkm, VkMemoryPropertyFlagBits MemoryProperties, int MemoryTypeIndex, i64 CapacityBytes)
{
    CapacityBytes = MAXIMUM(Vkm->DeviceMemoryPoolCapacityBytes, CapacityBytes);

    VkDeviceMemory DeviceMemory;
    VkMemoryAllocateInfo AllocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .memoryTypeIndex = MemoryTypeIndex,
        .allocationSize = CapacityBytes,
    };
    VK_CHECK(vkAllocateMemory(Vkm->Device, &AllocateInfo, NULL, &DeviceMemory));

    vkm_device_memory *Result = Arena_Alloc(Vkm->Arena, sizeof(*Result));
    *Result = (vkm_device_memory) {
        .RemainBytes = CapacityBytes,
        .CapacityBytes = CapacityBytes,
        .Handle = DeviceMemory,
        .MemoryProperties = MemoryProperties,
        .MemoryTypeIndex = MemoryTypeIndex,

        .Next = NULL,
    };
    return Result;
}

force_inline bool32 Vkm__IsNodeSuitable(const vkm_device_memory *Node, i64 SizeBytes, u32 Alignment)
{
    if (Node->RemainBytes < Alignment)
        return false;
    i64 Remain = Memory_AlignSize(Node->RemainBytes - Alignment, Alignment);
    return Remain >= SizeBytes;
}

internal vkm_device_memory *Vkm__GetDeviceMemory(vkm *Vkm, VkMemoryRequirements Requirements, VkMemoryPropertyFlags MemoryProperties)
{
    int MemoryTypeIndex = Vkm__FindMemoryType(
        Vkm->PhysicalDevice,
        Requirements.memoryTypeBits,
        MemoryProperties
    );
    ASSERT(-1 != MemoryTypeIndex, "Unable to locate suitable memory type");

    vkm_device_memory *Node = Vkm->DeviceMemoryPoolHead[MemoryTypeIndex];
    {
        /* NOTE: since the device memory pool is sorted by decreasing order, if the first node is not suitable, none in the pool will */
        if (!Node || !Vkm__IsNodeSuitable(Node, Requirements.size, Requirements.alignment))
        {
            /* create a new node, insert at the beginning of the list */
            Node = Vkm__CreateDeviceMemory(Vkm, MemoryProperties, MemoryTypeIndex, Requirements.size);
            vkm_device_memory *Next = Vkm->DeviceMemoryPoolHead[MemoryTypeIndex];
            Node->Next = Next;
            Vkm->DeviceMemoryPoolHead[MemoryTypeIndex] = Node;
        }
    }
    return Node;
}

internal void Vkm__InsertDeviceMemory(vkm_device_memory **Head, vkm_device_memory *Node)
{
    ASSERT(Node == *Head, "Node must be at the beginning of the list");
    /* unlink node from head */
    {
        vkm_device_memory *Next = Node->Next;
        *Head = Next;
    }
    vkm_device_memory *Prev = NULL;
    vkm_device_memory *Curr = Node->Next;
    while (Curr && Node->RemainBytes < Curr->RemainBytes)
    {
        Prev = Curr;
        Curr = Curr->Next;
    }

    if (Prev)
        Prev->Next = Node;
    else *Head = Node;
    Node->Next = Curr;
}

internal vkm_buffer_pool_entry *Vkm__GetBufferEntry(vkm *Vkm, vkm_buffer_handle BufferHandle)
{
    vkm__unpacked_buffer_handle Buffer = Vkm__UnpackBufferHandle(BufferHandle);
    vkm_buffer_pool_entry *Entry = &Vkm->BufferPool[Buffer.BufferType].Data[Buffer.BufferIndex];
    return Entry;
}




void Vkm_Create(vkm *Vkm, arena_alloc *Arena, const vkm_config *Config)
{
    *Vkm = (vkm) { 
        .Arena = Arena,
        .Device = Config->Device,
        .PhysicalDevice = Config->PhysicalDevice,
        .DeviceMemoryPoolCapacityBytes = Config->DeviceMemoryPoolCapacityBytes,
    };
    u32 FreeListAlignment = 8;
    isize FreeListPoolSize = 64*KB;
    FreeList_Create(&Vkm->FreeList, Arena_AsAllocInterface(Arena), FreeListPoolSize, FreeListAlignment);
}

void Vkm_Destroy(vkm *Vkm)
{
    /* deallocate all memory */
    for (uint k = 0; k < VK_MAX_MEMORY_TYPES; k++)
    {
        vkm_device_memory *Head = Vkm->DeviceMemoryPoolHead[k];
        for (vkm_device_memory *i = Head; i != NULL; i = i->Next)
        {
            vkFreeMemory(Vkm->Device, i->Handle, NULL);
        }
    }

    /* deallocate all buffers */
    for (int k = 0; k < VKM_BUFFER_TYPE_COUNT; k++)
    {
        dynamic_array_foreach(&Vkm->BufferPool[k], i)
        {
            vkDestroyBuffer(Vkm->Device, i->Buffer, NULL);
        }
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
    vkm_buffer_pool *Pool = &Vkm->BufferPool[Config->BufferType];
    int Index = -1;
    for (int i = 0; i < Pool->Count; i++)
    {
        const vkm_buffer_pool_entry *Entry = &Pool->Data[i];
        i64 AlignedSize = Memory_AlignSize(Config->MemoryCapacityBytes, Entry->Alignment);
        bool32 Fits = Pool->Data[i].DeviceMemory->RemainBytes >= AlignedSize;
        if (Fits)
        {
            Index = i;
            break;
        }
    }
    if (Index == -1)
    {
        /* create new buffer pool entry */
        VkBuffer Buffer;
        {
            VkBufferUsageFlagBits BufferUsage = Vkm__GetBufferUsageFlagBits(Config->BufferType);
            VkBufferCreateInfo CreateInfo = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .usage = BufferUsage,
                .size = Config->MemoryCapacityBytes,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

                .flags = 0,
                .pNext = NULL,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = NULL, /* NOTE: must be filled when sharingMode is VK_SHARING_MODE_CONCURRENT */
            };
            vkCreateBuffer(Vkm->Device, &CreateInfo, NULL, &Buffer);
        }

        VkMemoryRequirements MemoryRequirements;
        vkGetBufferMemoryRequirements(Vkm->Device, Buffer, &MemoryRequirements);

        vkm_device_memory *DeviceMemory;
        {
            VkMemoryPropertyFlags MemoryProperties = Vkm__GetMemoryPropertyFlagsForBuffer(Config->BufferType);
            DeviceMemory = Vkm__GetDeviceMemory(Vkm, MemoryRequirements, MemoryProperties);
        }

        u32 Alignment = MAXIMUM(MemoryRequirements.alignment, VKM_MIN_BUFFER_ALIGNMENT);
        VkDynamicArray_Push(&Vkm->FreeList, Pool, (vkm_buffer_pool_entry) {
            .Buffer = Buffer,
            .DeviceMemory = DeviceMemory,
            .Alignment = Alignment,
        });
        Index = Pool->Count - 1;
    }

    ASSERT(IN_RANGE(0, Index, Pool->Count - 1));

    vkm_buffer_pool_entry *Entry = &Pool->Data[Index];
    vkm_device_memory *DeviceMemory = Entry->DeviceMemory;

    i64 SizeBytes = Memory_AlignSize(Config->MemoryCapacityBytes, Entry->Alignment);
    i64 OffsetBytes = Memory_AlignSize(DeviceMemory->CapacityBytes - DeviceMemory->RemainBytes, Entry->Alignment);
    ASSERT(SizeBytes < (i64)UINT32_MAX);
    ASSERT(OffsetBytes < (i64)UINT32_MAX);
    vkm_buffer_handle Handle = Vkm__PackBufferHandle(Config->BufferType, Index, SizeBytes, OffsetBytes);

    Entry->DeviceMemory->RemainBytes -= SizeBytes;
    Vkm__InsertDeviceMemory(&Vkm->DeviceMemoryPoolHead[DeviceMemory->MemoryTypeIndex], DeviceMemory);
    return Handle;
}

vkm_image_handle Vkm_CreateImage(vkm *Vkm, const vkm_image_config *Config)
{
}



void Vkm_ResizeImage(vkm *Vkm, vkm_image_handle ImageHandle, u32 NewWidth, u32 NewHeight);

/* map will only succeed if the buffer is VKM_BUFFER_TYPE_STAGING or VKM_BUFFER_TYPE_UBO */
void *Vkm_MapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle)
{
    vkm_buffer_info BufferInfo = Vkm_GetBufferInfo(Vkm, BufferHandle);
    ASSERT(BufferInfo.BufferType == VKM_BUFFER_TYPE_STAGING 
        || BufferInfo.BufferType == VKM_BUFFER_TYPE_UBO, "Can only map staging/uniform buffer memory");
    void *Ptr;
    vkMapMemory(Vkm->Device, BufferInfo.DeviceMemory, BufferInfo.OffsetBytes, BufferInfo.CapacityBytes, 0, &Ptr);
    return Ptr;
}

void Vkm_UnmapBufferMemory(vkm *Vkm, void *MappedMemory)
{
    vkUnmapMemory(Vkm->Device, MappedMemory);
}



vkm_buffer_info Vkm_GetBufferInfo(vkm *Vkm, vkm_buffer_handle BufferHandle)
{
    vkm__unpacked_buffer_handle Buffer = Vkm__UnpackBufferHandle(BufferHandle);
    vkm_buffer_pool_entry *Entry = Vkm__GetBufferEntry(Vkm, BufferHandle);
    vkm_device_memory *DeviceMemory = Entry->DeviceMemory;
    vkm_buffer_info BufferInfo = {
        .Buffer = Entry->Buffer,
        .BufferType = Buffer.BufferType,
        .MemoryTypeIndex = DeviceMemory->MemoryTypeIndex,
        .OffsetBytes = Buffer.OffsetBytes,
        .CapacityBytes = Buffer.SizeBytes,
        .DeviceMemory = DeviceMemory->Handle,
    };
    return BufferInfo;
}

vkm_image_info Vkm_GetImageInfo(vkm *Vkm, vkm_image_handle ImageHandle)
{
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
