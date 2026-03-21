
#include "Common-Renderer-Vulkan-VkMalloc.h"
#include "Common-Vulkan.h"
#include "Memory.h"

/* init */
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



