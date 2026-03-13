
#include <string.h>

#include "Common.h"
#include "Platform-Core.h"
#include "Renderer-Core.h"
#include "Renderer-Vulkan.h"
#include "Profiler.h"


#define FATAL(...) do {\
    (void)eprintfln("FATAL: "__VA_ARGS__);\
    exit(1);\
} while (0)
#define VK_CHECK(call) do {\
    VkResult res = call;\
    if (res != VK_SUCCESS) {\
        FATAL("%s -- '"#call, Vulkan_VkResultToString(res));\
    }\
} while (0)


internal const char *g_VkValidationLayerNames[] = {
    "VK_LAYER_KHRONOS_validation", 
};
internal const char *g_VkDeviceExtensionNames[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
internal PFN_vkCreateDebugReportCallbackEXT g_VkCreateDebugReportCallbackEXT;
internal PFN_vkDestroyDebugReportCallbackEXT g_VkDestroyDebugReportCallbackEXT;

internal const char *Vulkan_VkResultToString(VkResult Result);



typedef_struct(vk_vertex_description);
struct vk_vertex_description
{
    VkVertexInputBindingDescription Binding;
    VkVertexInputAttributeDescription *Attribs;
    int AttribCount;
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

void Vkm_Init(
    vkm *Vkm, 
    VkDevice Device, VkPhysicalDevice PhysicalDevice, 
    i64 ImageMemoryPoolCapacityBytes,
    i64 ResetCapacity[VKM_MEMORY_TYPE_COUNT],
    VkBufferUsageFlags DefaultUsages[VKM_MEMORY_TYPE_COUNT],
    VkMemoryPropertyFlags DefaultMemoryProperties[VKM_MEMORY_TYPE_COUNT]
) {
    ASSERT_EXPRESSION_TYPE((vkm_buffer){0}.Info, u64, "invalid type");
    STATIC_ASSERT(STATIC_ARRAY_SIZE(Vkm->BufferPool[0].Slot) == VKM_POOL_SLOT_COUNT, "");

    /* prints memory types */
    {
        VkPhysicalDeviceMemoryProperties MemoryProperties;
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);

        eprintfln("\nHeapCount: %d", MemoryProperties.memoryHeapCount);
        for (u32 i = 0; i < MemoryProperties.memoryHeapCount; i++)
        {
            VkMemoryHeap Heap = MemoryProperties.memoryHeaps[i];
            eprintfln("    Heap %d: F:%08x, size:%zimb", i, Heap.flags, (isize)Heap.size/MB);
        }
        eprintfln("\nTypeCount: %d", MemoryProperties.memoryTypeCount);
#define PRINT_MEM_TYPE(t) eprintfln("  "#t": %d", t);
        PRINT_MEM_TYPE(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        PRINT_MEM_TYPE(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        PRINT_MEM_TYPE(VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        PRINT_MEM_TYPE(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        for (u32 i = 0; i < MemoryProperties.memoryTypeCount; i++)
        {
            VkMemoryType Type = MemoryProperties.memoryTypes[i];
            VkMemoryHeap Heap = MemoryProperties.memoryHeaps[Type.heapIndex];
            eprintfln("    Type %d: F:%08x, heapidx:%d, heapsize: %zigb", i, Type.propertyFlags, Type.heapIndex, Heap.size/(KB*MB));
        }
#undef PRINT_MEM_TYPE
    }

    *Vkm = (vkm) {
        .Device = Device,
        .PhysicalDevice = PhysicalDevice,
        .NewDeviceMemoryCapacity = ImageMemoryPoolCapacityBytes,
    };
    for (int i = 0; i < VKM_MEMORY_TYPE_COUNT; i++)
    {
        Vkm->BufferPool[i].ResetCapacity = ResetCapacity[i];
        Vkm->BufferPool[i].DefaultUsages = DefaultUsages[i];
        Vkm->BufferPool[i].DefaultMemoryProperties = DefaultMemoryProperties[i];
    }
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
bool32 Vkm__BufferFits(const vkm_buffer_pool_slot *Slot, i64 BufferSizeBytes)
{
    /* TODO: turn this into a pool-style allocator */
    i64 AlignedSize = Arena_AlignSize(BufferSizeBytes, Slot->Alignment);
    bool32 Fits = Slot->SizeBytesRemain - AlignedSize >= 0;
    return Fits;
}
typedef struct 
{
    i64 Offset;
    VkDeviceMemory Handle;
} vkm__allocate_device_memory_result;
vkm__allocate_device_memory_result Vkm__AllocateDeviceMemory(vkm *Vkm, VkMemoryRequirements Requirements, VkMemoryPropertyFlags Flags)
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
        i64 Offset = Arena_AlignSize(PoolSlot->Offset, Requirements.alignment);
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

vkm_buffer Vkm_CreateBuffer(vkm *Vkm, vkm_memory_type MemoryType, isize BufferSizeBytes)
{
    vkm_buffer_pool *Pool = &Vkm->BufferPool[MemoryType];

    /* find fit pool */
    vkm_buffer_pool_slot *Slot = NULL;
    u64 Offset = 0;
    for (int i = Pool->SlotCount - 1; i >= 0; i--)
    {
        vkm_buffer_pool_slot *CurrSlot = &Pool->Slot[i];

        /* TODO: this is currently an arena-style allocator, 
         * maybe modify Vkm__BufferFits() so that it's more like a pool allocator */
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
            NewBufferSize = Arena_AlignSize(MAXIMUM(NewBufferSize, (isize)MemoryRequirements.size), NewBufferAlignment);

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
        Slot->SizeBytesRemain, Slot->Alignment, BufferSizeBytes, Arena_AlignSize(BufferSizeBytes, Slot->Alignment)
    );

    u64 PoolIndex = Slot - Pool->Slot;
    u64 AlignedOffset = Arena_AlignSize(Offset, Slot->Alignment);
    u64 AlignedSize = Arena_AlignSize(BufferSizeBytes, Slot->Alignment);
    ASSERT(PoolIndex <= VKM_POOL_SLOT_COUNT, "Invalid pool slot");
    ASSERT(AlignedOffset <= VKM_BUFFER_MAX_OFFSET, "Invalid pool slot alignment");
    ASSERT(AlignedSize <= VKM_BUFFER_MAX_SIZEBYTES, "Invalid pool slot size");

    Slot->SizeBytesRemain -= AlignedSize;
    vkm_buffer Buffer = Vkm__Buffer_Init(MemoryType, PoolIndex, AlignedOffset, AlignedSize); 
    return Buffer;
}


VkImage Vkm__CreateVkImage(
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

VkImageView Vkm__CreateVkImageView(vkm *Vkm, vkm_image_handle ImageHandle, VkImageAspectFlags Aspect)
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

/* NOTE: defaults:
   memory property: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
   image layout: VK_IMAGE_LAYOUT_UNDEFINED
   image type: VK_IMAGE_TYPE_2D
       extent.depth = 1
   sharing mode: VK_SHARING_MODE_EXCLUSIVE
   flags = 0
*/
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





internal const char *Vulkan_PhysicalDeviceTypeToString(VkPhysicalDeviceType type) 
{
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "Other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Unknown Type";
    }
}

internal const char *Vulkan_VkResultToString(VkResult Result)
{
	switch (Result) 
    {
#define CASE(x) case VK_##x: return #x;
        CASE(SUCCESS)                       CASE(NOT_READY)
        CASE(TIMEOUT)                       CASE(EVENT_SET)
        CASE(EVENT_RESET)                   CASE(INCOMPLETE)
        CASE(ERROR_OUT_OF_HOST_MEMORY)      CASE(ERROR_OUT_OF_DEVICE_MEMORY)
        CASE(ERROR_INITIALIZATION_FAILED)   CASE(ERROR_DEVICE_LOST)
        CASE(ERROR_MEMORY_MAP_FAILED)       CASE(ERROR_LAYER_NOT_PRESENT)
        CASE(ERROR_EXTENSION_NOT_PRESENT)   CASE(ERROR_FEATURE_NOT_PRESENT)
        CASE(ERROR_INCOMPATIBLE_DRIVER)     CASE(ERROR_TOO_MANY_OBJECTS)
        CASE(ERROR_FORMAT_NOT_SUPPORTED)    CASE(ERROR_FRAGMENTED_POOL)
        CASE(ERROR_UNKNOWN)                 CASE(ERROR_OUT_OF_POOL_MEMORY)
        CASE(ERROR_INVALID_EXTERNAL_HANDLE) CASE(ERROR_FRAGMENTATION)
        CASE(ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS)
        CASE(PIPELINE_COMPILE_REQUIRED)      CASE(ERROR_SURFACE_LOST_KHR)
        CASE(ERROR_NATIVE_WINDOW_IN_USE_KHR) CASE(SUBOPTIMAL_KHR)
        CASE(ERROR_OUT_OF_DATE_KHR)          CASE(ERROR_INCOMPATIBLE_DISPLAY_KHR)
        CASE(ERROR_VALIDATION_FAILED_EXT)    CASE(ERROR_INVALID_SHADER_NV)
#ifdef VK_ENABLE_BETA_EXTENSIONS
        CASE(ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR)
        CASE(ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR)
        CASE(ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR)
        CASE(ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR)
        CASE(ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR)
        CASE(ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR)
#endif
        CASE(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT)
        CASE(ERROR_NOT_PERMITTED_KHR)
        CASE(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
        CASE(THREAD_IDLE_KHR)        CASE(THREAD_DONE_KHR)
        CASE(OPERATION_DEFERRED_KHR) CASE(OPERATION_NOT_DEFERRED_KHR)
        default: return "unknown";
#undef CASE
    }
}

internal VkBool32 Vulkan_DebugCallback(
    VkDebugReportFlagsEXT Flags, 
    VkDebugReportObjectTypeEXT ObjType, 
    uint64_t SrcObject, size_t Location, 
    int32_t MsgCode, const char* pLayerPrefix, 
    const char* Msg, void* UserData
) {
    (void)ObjType, (void)Location, (void)SrcObject, (void)UserData;
    if (Flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) 
    {
        (void)eprintfln("\nERROR: [%s] Code %d : %s", pLayerPrefix, MsgCode, Msg);
    } 
    else if (Flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) 
    {
        (void)eprintfln("\nWARNING: [%s] Code %d : %s", pLayerPrefix, MsgCode, Msg);
    }
    return VK_FALSE;
}

force_inline VkPhysicalDevice Vulkan_GetPhysicalDevice(const renderer *Vk)
{
    return Vk->Gpus.Selected.Handle;
}

force_inline VkDevice Vulkan_GetDevice(const renderer *Vk)
{
    return Vk->GpuContext.Device;
}

internal VkInstance Vulkan_CreateInstance(VkInstance *Instance, arena_alloc TmpArena, const char *AppName)
{
    vulkan_platform_instance_extensions RequiredExtensions = Vulkan_Platform_GetInstanceExtensions();
    /* query supported instance extensions and print them out */
    {
        u32 Count;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &Count, NULL));
        if (0 == Count)
        {
            FATAL("Vulkan: No instance extension available");
        }

        VkExtensionProperties *SupportedInstanceExtensions;
        Arena_AllocArray(&TmpArena, &SupportedInstanceExtensions, Count); 
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, 
                &Count, SupportedInstanceExtensions
            ));

        (void)eprintfln("Supported instance extensions (%d): ", Count);
        for (u32 i = 0; i < Count; i++)
        {
            (void)eprintfln("\t%s", SupportedInstanceExtensions[i].extensionName);
        }
        VK_KHR_dynamic_rendering;
    }

    /* NOTE: extensions for instance creation */
    u32 ExtensionCount = RequiredExtensions.Count;
    DEBUG_ONLY(ExtensionCount++);
    const char **Extensions;
    Arena_AllocArray(&TmpArena, &Extensions, ExtensionCount);
    {
        memcpy(Extensions, RequiredExtensions.StringPtrArray, RequiredExtensions.Count * sizeof(Extensions[0]));
        DEBUG_ONLY(
            Extensions[ExtensionCount - 1] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        );
    }
    (void)eprintfln("Used %d extensions to create the Vulkan_ instance: ", ExtensionCount);
    for (u32 i = 0; i < ExtensionCount; i++)
    {
        (void)eprintfln("\t%s", Extensions[i]);
    }

    /* create the vulkan instance */
    VkApplicationInfo AppInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "No Engine",
        .pApplicationName = AppName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &AppInfo,
        .enabledExtensionCount = ExtensionCount,
        .ppEnabledExtensionNames = Extensions,
        DEBUG_ONLY(
            .enabledLayerCount = STATIC_ARRAY_SIZE(g_VkValidationLayerNames),
            .ppEnabledLayerNames = g_VkValidationLayerNames,
        )
    };
    VK_CHECK(vkCreateInstance(&CreateInfo, NULL, Instance));
    return *Instance;
}

internal VkDebugReportCallbackEXT Vulkan_CreateDebugCallback(VkInstance Instance)
{
    VkDebugReportCallbackEXT DebugReportCallback = { 0 };

    g_VkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
            Instance, "vkCreateDebugReportCallbackEXT"
        );
    g_VkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
            Instance, "vkDestroyDebugReportCallbackEXT"
        );

    VkDebugReportCallbackCreateInfoEXT CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .pfnCallback = (PFN_vkDebugReportCallbackEXT) Vulkan_DebugCallback,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
    };
    if (g_VkCreateDebugReportCallbackEXT(Instance, &CreateInfo, NULL, &DebugReportCallback) != VK_SUCCESS) 
        (void)eprintfln("Failed to create debug callback.");
    else (void)eprintfln("Created debug callback.");

    return DebugReportCallback;
}

internal vk_swapchain_support_config Vulkan_QuerySwapchainSupportConfig(
    VkPhysicalDevice PhysDevice, VkSurfaceKHR WindowSurface, arena_alloc *Arena
) {
    vk_swapchain_support_config Config = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysDevice, WindowSurface, &Config.Capabilities);
    (void)Config.Capabilities.supportedUsageFlags;

    u32 FormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(PhysDevice, WindowSurface, &FormatCount, NULL);
    if (FormatCount)
    {
        Arena_AllocDynamicArray(Arena, &Config.Formats, FormatCount, FormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysDevice, WindowSurface, &FormatCount, Config.Formats.Data);
    }

    u32 PresentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(PhysDevice, WindowSurface, &PresentModeCount, NULL);
    if (PresentModeCount)
    {
        Arena_AllocDynamicArray(Arena, &Config.PresentModes, PresentModeCount, PresentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            PhysDevice, WindowSurface, &PresentModeCount, Config.PresentModes.Data
        );
    }

    return Config;
}

internal vk_queue_family_indices Vulkan_GetQueueFamilies(
    VkPhysicalDevice PhysicalDevice, VkSurfaceKHR WindowSurface, arena_alloc TmpArena
) {
    vk_queue_family_indices QueueFamilyIndices = { 0 };
    for (int i = 0; i < QUEUE_FAMILY_TYPE_COUNT; i++) 
        QueueFamilyIndices.Type[i] = QUEUE_FAMILY_INVALID_INDEX;

    u32 Count = 0;
    VkQueueFamilyProperties *QueueFamily;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &Count, NULL);
    Arena_AllocArray(&TmpArena, &QueueFamily, Count);
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &Count, QueueFamily);
    for (u32 i = 0; i < Count; i++)
    {
        if (QueueFamily[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            QueueFamilyIndices.Type[QUEUE_FAMILY_TYPE_GRAPHICS] = i;
        }
        bool32 IsPresentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, i, WindowSurface, &IsPresentSupported);
        if (IsPresentSupported)
        {
            QueueFamilyIndices.Type[QUEUE_FAMILY_TYPE_PRESENT] = i;
        }

        bool8 Full = true;
        for (int i = 0; i < QUEUE_FAMILY_TYPE_COUNT; i++)
        {
            if (QueueFamilyIndices.Type[i] == QUEUE_FAMILY_INVALID_INDEX)
                Full = false;
        }
        if (Full)
            break;
    }
    return QueueFamilyIndices;
}


typedef enum 
{
    DEVICE_RANK_NONE = 0, 
    DEVICE_RANK_CPU, 
    DEVICE_RANK_INTEGRATED_GPU, 
    DEVICE_RANK_VIRTUAL_GPU,
    DEVICE_RANK_DISCRETE_GPU,
} device_rank;
/* Out will not be written to if the returned rank is DEVICE_RANK_NONE */
internal device_rank Vulkan_CheckDeviceSuitability(
    VkPhysicalDevice Device, VkSurfaceKHR WindowSurface, arena_alloc TmpArena
) {
    /* rank device */
    device_rank Rank = DEVICE_RANK_NONE;
    {
        VkPhysicalDeviceProperties Prop;
        VkPhysicalDeviceFeatures Feat;
        vkGetPhysicalDeviceProperties(Device, &Prop);
        vkGetPhysicalDeviceFeatures(Device, &Feat);

        switch (Prop.deviceType)
        {
#define CASE(name) case VK_PHYSICAL_DEVICE_TYPE_##name: Rank = DEVICE_RANK_##name; break
        CASE(CPU);
        CASE(INTEGRATED_GPU);
        CASE(DISCRETE_GPU);
        CASE(VIRTUAL_GPU);
        default: 
            Rank = DEVICE_RANK_NONE;
            break;
#undef  CASE
        }

        if (!Feat.geometryShader)
            Rank = DEVICE_RANK_NONE;
        if (!Feat.samplerAnisotropy)
            Rank = DEVICE_RANK_NONE;
    }
    if (!Rank)
    {
        return Rank;
    }

    /* check queue family support */
    {
        vk_queue_family_indices QueueFamilyIndices = Vulkan_GetQueueFamilies(Device, WindowSurface, TmpArena);
        for (int i = 0; i < QUEUE_FAMILY_TYPE_COUNT; i++)
        {
            if (QueueFamilyIndices.Type[i] == QUEUE_FAMILY_INVALID_INDEX)
                return DEVICE_RANK_NONE;
        }
    }

    /* check for device level required extensions */
    {
        u32 DevExtCount = 0;
        vkEnumerateDeviceExtensionProperties(Device, NULL, &DevExtCount, NULL);
        VkExtensionProperties *DevExts;
        Arena_AllocArray(&TmpArena, &DevExts, DevExtCount);
        vkEnumerateDeviceExtensionProperties(Device, NULL, &DevExtCount, DevExts);
        bool8 AreRequiredExtensionsSupported = false;
        for (u32 i = 0; i < STATIC_ARRAY_SIZE(g_VkDeviceExtensionNames); i++)
        {
            AreRequiredExtensionsSupported = false;
            for (u32 k = 0; k < DevExtCount && !AreRequiredExtensionsSupported; k++)
            {
                AreRequiredExtensionsSupported = 
                    (strcmp(DevExts[k].extensionName, g_VkDeviceExtensionNames[i]) == 0);
            }
        }
        if (!AreRequiredExtensionsSupported)
            return DEVICE_RANK_NONE;
    }

    /* check for swapchain support */
    {
        vk_swapchain_support_config Config = Vulkan_QuerySwapchainSupportConfig(
            Device, WindowSurface, &TmpArena
        );
        if (Config.Formats.Count == 0 || Config.PresentModes.Count == 0)
            return DEVICE_RANK_NONE;
    }
    return Rank;
}

/* NOTE: memory in the returned array is allocated using the given arena */
internal vk_physical_devices Vulkan_QueryAndSelectGpu(
    VkInstance Instance, VkSurfaceKHR WindowSurface, arena_alloc *Arena) 
{
    /* query and report gpus */
    vk_gpu_list Gpus;
    {
        u32 Count;
        VkPhysicalDevice *HandleList;
        VkPhysicalDeviceProperties *PropertiesList;
        VkPhysicalDeviceFeatures *FeaturesList;

        VK_CHECK(vkEnumeratePhysicalDevices(Instance, &Count, NULL));
        Arena_AllocArray(Arena, &HandleList, Count);
        Arena_AllocArray(Arena, &FeaturesList, Count);
        Arena_AllocArray(Arena, &PropertiesList, Count);
        VK_CHECK(vkEnumeratePhysicalDevices(Instance, &Count, HandleList));

        /* report the devices found */
        (void)eprintfln("Found %d devices that have Vulkan support:", Count);
        for (u32 i = 0; i < Count; i++)
        {
            vkGetPhysicalDeviceFeatures(HandleList[i], &FeaturesList[i]);
            vkGetPhysicalDeviceProperties(HandleList[i], &PropertiesList[i]);
            (void)eprintfln("Device %d, %s: %s: Supports version %d.%d.%d", 
                i, Vulkan_PhysicalDeviceTypeToString(PropertiesList[i].deviceType), PropertiesList[i].deviceName,
                VK_VERSION_MAJOR(PropertiesList[i].apiVersion),
                VK_VERSION_MINOR(PropertiesList[i].apiVersion),
                VK_VERSION_PATCH(PropertiesList[i].apiVersion)
            );
        }

        Gpus = (vk_gpu_list) {
            .Count = Count,
            .HandleList = HandleList,
            .FeaturesList = FeaturesList,
            .PropertiesList = PropertiesList,
        };
    }

    /* select suitable gpu */
    vk_gpu SelectedGpu;
    {
        u32 Selected = 0;
        device_rank SelectedDeviceRank = DEVICE_RANK_NONE;
        for (u32 i = 0; i < Gpus.Count; i++)
        {
            device_rank Rank = Vulkan_CheckDeviceSuitability(Gpus.HandleList[i], WindowSurface, *Arena);
            if (Rank > SelectedDeviceRank) 
            {
                Selected = i;
                SelectedDeviceRank = Rank;
            }
        }

        if (!SelectedDeviceRank)
        {
            FATAL("Vulkan: Could not find a suitable device.");
        }
        //Selected = 1;
        SelectedGpu = (vk_gpu) {
            .Features = Gpus.FeaturesList[Selected],
            .Properties = Gpus.PropertiesList[Selected],
            .Handle = Gpus.HandleList[Selected],
        };
        (void)eprintfln("Selected device %d, %s: %s", 
            Selected, 
            Vulkan_PhysicalDeviceTypeToString(SelectedGpu.Properties.deviceType), 
            SelectedGpu.Properties.deviceName
        );
    }
    return (vk_physical_devices) {
        .Selected = SelectedGpu,
        .List = Gpus,
    };
}

internal vk_gpu_context Vulkan_CreateGpuContext(
    VkPhysicalDevice Gpu, VkSurfaceKHR WindowSurface, VkPhysicalDeviceFeatures EnabledFeatures, arena_alloc TmpArena
) {
    /* get queue families */
    u32 QueueFamilyIndex[QUEUE_FAMILY_TYPE_COUNT] = { 0 };
    {
        vk_queue_family_indices Indices = Vulkan_GetQueueFamilies(Gpu, WindowSurface, TmpArena);
        for (int i = 0; i < QUEUE_FAMILY_TYPE_COUNT; i++)
        {
            ASSERT(Indices.Type[i] != QUEUE_FAMILY_INVALID_INDEX, "%d", i);
            QueueFamilyIndex[i] = Indices.Type[i];
        }
    }

    /* get unique queues */
    int UniqueQueueCount = 0;
    u32 UniqueQueueFamilies[QUEUE_FAMILY_TYPE_COUNT] = { 
        QueueFamilyIndex[QUEUE_FAMILY_TYPE_GRAPHICS], 
        QueueFamilyIndex[QUEUE_FAMILY_TYPE_PRESENT] 
    };
    float UniqueQueuePriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo UniqueQueueCreateInfo[QUEUE_FAMILY_TYPE_COUNT] = { 0 };
    {
        UniqueQueueCount = 2;
        if (QueueFamilyIndex[QUEUE_FAMILY_TYPE_PRESENT] == QueueFamilyIndex[QUEUE_FAMILY_TYPE_GRAPHICS])
            UniqueQueueCount = 1;

        /* initialize the QueueCreateInfo struct */
        for (int i = 0; i < UniqueQueueCount; i++)
        {
            UniqueQueueCreateInfo[i] = (VkDeviceQueueCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 
                .queueCount = 1, 
                .pQueuePriorities = UniqueQueuePriorities,
                .queueFamilyIndex = UniqueQueueFamilies[i],
            };
        }
    }

    VkDevice Device;
    VkQueue GraphicsQueue, PresentQueue;
    {
        VkDeviceCreateInfo DeviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = UniqueQueueCount,
            .pQueueCreateInfos = UniqueQueueCreateInfo, 
            .pEnabledFeatures = &EnabledFeatures,
            .enabledExtensionCount = STATIC_ARRAY_SIZE(g_VkDeviceExtensionNames),
            .ppEnabledExtensionNames = g_VkDeviceExtensionNames,
            DEBUG_ONLY(
                .enabledLayerCount = STATIC_ARRAY_SIZE(g_VkValidationLayerNames),
                .ppEnabledLayerNames = g_VkValidationLayerNames,
            )
        };
        VK_CHECK(vkCreateDevice(Gpu, &DeviceCreateInfo, NULL, &Device));
        vkGetDeviceQueue(Device, QueueFamilyIndex[QUEUE_FAMILY_TYPE_GRAPHICS], 0, &GraphicsQueue);
        vkGetDeviceQueue(Device, QueueFamilyIndex[QUEUE_FAMILY_TYPE_PRESENT], 0, &PresentQueue);
    }
    vk_gpu_context GpuContext = {
        .GraphicsQueue = GraphicsQueue,
        .PresentQueue = PresentQueue,
        .Device = Device,
        .PhysicalDevice = Gpu,
    };
    memcpy(GpuContext.QueueFamilyIndex, QueueFamilyIndex, sizeof(GpuContext.QueueFamilyIndex));
    vkGetPhysicalDeviceMemoryProperties(Gpu, &GpuContext.MemoryProperties);
    return GpuContext;
}

internal VkSampleCountFlagBits Vulkan_GetMaxSupportedSampleCount(const vk_gpu *Gpu)
{
    VkSampleCountFlags Bits = Gpu->Properties.limits.framebufferColorSampleCounts;
#define PICK_IF_AVAILABLE(s) if (Bits & s) return s
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_64_BIT);
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_32_BIT);
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_16_BIT);
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_8_BIT);
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_4_BIT);
    PICK_IF_AVAILABLE(VK_SAMPLE_COUNT_2_BIT);
#undef PICK_IF_AVAILABLE
    return VK_SAMPLE_COUNT_1_BIT;
}

internal VkImageView Vulkan_CreateImageView(VkDevice Device, VkImage Image, VkFormat Format, VkImageAspectFlags Aspect, u32 MipLevels)
{
    VkImageViewCreateInfo CreateInfo = {
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
    VkImageView ImageView;
    VK_CHECK(vkCreateImageView(Device, &CreateInfo, NULL, &ImageView));
    return ImageView;
}

internal void Vulkan_DestroySwapchain(VkDevice Device, vk_swapchain *Swapchain)
{
    vkDestroySwapchainKHR(Device, Swapchain->Handle, NULL);
}

/* NOTE: framebuffer must be manually created after */
internal vk_swapchain Vulkan_CreateSwapchain(
    profiler *Profiler,
    const vk_gpu_context *GpuContext, 
    int Width, int Height,
    bool32 ForceTripleBuffering,
    VkSurfaceKHR WindowSurface,
    VkSwapchainKHR OldSwapchainHandle,
    arena_alloc TmpArena
) {
    VkDevice Device = GpuContext->Device;
    VkPhysicalDevice PhysicalDevice = GpuContext->PhysicalDevice;

    VkSwapchainKHR SwapchainHandle = NULL;
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR; /* FIFO is guaranteed by the Vulkan spec */
    VkSurfaceFormatKHR SelectedFormat = { 0 };
    VkExtent2D Extent = { 0 };
    Arena_Scope(&TmpArena)
    {
        vk_swapchain_support_config Config = { 0 };
        Profiler_Scope(Profiler, "Vulkan_QuerySwapchainSupportConfig()")
        {
            Config = Vulkan_QuerySwapchainSupportConfig(
                PhysicalDevice, WindowSurface, &TmpArena
            );
        }
        VkSurfaceCapabilitiesKHR *Cap = &Config.Capabilities;

        /* NOTE: choose surface format */
        Profiler_Scope(Profiler, "CreateSwapchain -- choosing config")
        {
            {
                VkSurfaceFormatKHR *Found = NULL;
                dynamic_array_foreach(&Config.Formats, i)
                {
                    if (i->format == VK_FORMAT_B8G8R8A8_SRGB 
                    && i->colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
                    {
                        Found = i;
                        break;
                    }
                }
                if (Found)
                {
                    SelectedFormat = *Found;
                }
                else
                {
                    ASSERT(Config.Formats.Count > 0, "No surface format to choose from");
                    SelectedFormat = Config.Formats.Data[0];
                }
            }

            /* NOTE: choose present mode */
            if (ForceTripleBuffering)
            {
                PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
            else
            {
                dynamic_array_foreach(&Config.PresentModes, i)
                {
                    /* triple buffering my beloved */
                    if (*i == VK_PRESENT_MODE_MAILBOX_KHR)
                    {
                        PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                        break;
                    }
                }
            }

            /* NOTE: choose extent, resolution of swapchain image */
            Extent = Config.Capabilities.currentExtent;
            if (Config.Capabilities.currentExtent.width == UINT32_MAX)
            {
                Extent = (VkExtent2D) {
                    .width = CLAMP(Cap->minImageExtent.width, (u32)Width, Cap->maxImageExtent.width),
                    .height = CLAMP(Cap->minImageExtent.height, (u32)Height, Cap->maxImageExtent.height),
                };
            }
        }

        /* NOTE: create the swapchain */
        {
            VkSwapchainCreateInfoKHR CreateInfo;
            Profiler_Scope(Profiler, "vkCreateSwapchainKHR() -- parameters")
            {
                u32 ImageCount = Cap->minImageCount + 1;
                if (Cap->maxImageCount > 0 && ImageCount > Cap->maxImageCount)
                    ImageCount = Cap->maxImageCount;
                if (IN_RANGE(Cap->minImageCount, 3, Cap->maxImageCount))
                    ImageCount = 3;

                CreateInfo = (VkSwapchainCreateInfoKHR) {
                    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                    .surface = WindowSurface,
                    .minImageCount = ImageCount, 
                    .imageExtent = Extent, 
                    .imageColorSpace = SelectedFormat.colorSpace, 
                    .imageFormat = SelectedFormat.format,
                    .imageArrayLayers = 1,
                    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, /* directly rendering into the swapchain */
                    .preTransform = Cap->currentTransform, /* no transform */
                    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, /* no alpha blend with external windows */
                    .presentMode = PresentMode,
                    .clipped = VK_TRUE,
                    .oldSwapchain = OldSwapchainHandle,
                };
                if (GpuContext->QueueFamilyIndex[QUEUE_FAMILY_TYPE_GRAPHICS] 
                != GpuContext->QueueFamilyIndex[QUEUE_FAMILY_TYPE_PRESENT])
                {
                    STATIC_ASSERT(QUEUE_FAMILY_TYPE_GRAPHICS == 0 && QUEUE_FAMILY_TYPE_PRESENT == 1, "");
                    CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                    CreateInfo.queueFamilyIndexCount = 2; /* NOTE: we only use 2 queues here */
                    CreateInfo.pQueueFamilyIndices = GpuContext->QueueFamilyIndex;
                }
                else
                {
                    CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                }
            }

            /* TODO: why is this so slow on Nvidia? Nvidia Linux drivers are bad?
             * 3080: 17~20ms average, 
             * 7600x igpu: 2ms average
             * Almost an order of magnitude!!!!!!! */
            /* NOTE:
             * On the 7600x igpu: The slow part of recreating the swapchain was in vkDestroySwapchainKHR(), 
             * On the 3080      : The slow part of recreating the swapchain was here (vkCreateSwapchainKHR()),
             * Both gpu's drivers appeared to do slow things during swapchain recreation, but at different time
             * */
            Profiler_Scope(Profiler, "vkCreateSwapchainKHR()")
            {
                VK_CHECK(vkCreateSwapchainKHR(Device, &CreateInfo, NULL, &SwapchainHandle));
            }
        }
    }

    vk_swapchain Swapchain = (vk_swapchain) {
        .Handle = SwapchainHandle,
        .ImageFormat = SelectedFormat.format, 
        .Extent = Extent, 
        .PresentMode = PresentMode,
    };
    return Swapchain;
}

internal void Vulkan_DestroySwapchainImageAndFramebuffer(
    VkDevice Device, 
    vk_swapchain_image *SwapchainImage, 
    bool32 ShouldDistroySyncObjects
) {
    for (u32 i = 0; i < SwapchainImage->Count; i++)
    {
        vkDestroyImageView(Device, SwapchainImage->ViewArray[i], NULL);
        vkDestroyFramebuffer(Device, SwapchainImage->FramebufferArray[i], NULL);
        if (ShouldDistroySyncObjects)
        {
            vkDestroySemaphore(Device, SwapchainImage->RenderFinishedSemaphoreArray[i], NULL);
        }
    }
}

internal void Vulkan_CreateSwapchainImageViewAndFramebuffers(
    vk_swapchain_image *SwapchainImage,
    const vk_swapchain *Swapchain, 
    const vk_gpu_context *GpuContext, 
    VkRenderPass RenderPass, VkImageView ColorImageView, VkImageView DepthImageView,
    platform_framebuffer_dimensions Dimensions,
    bool32 ShouldCreateSyncObjects,
    arena_alloc *Arena
) {
    VkDevice Device = GpuContext->Device;
    u32 ImageCount;
    VkImage *SwapchainImages;
    {
        vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCount, NULL);
        Arena_AllocArray(Arena, &SwapchainImages, ImageCount);
        vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCount, SwapchainImages);
    }

    VkImageView *SwapchainImageViews;
    {
        Arena_AllocArray(Arena, &SwapchainImageViews, ImageCount);
        int MipLevel = 1;
        for (u32 i = 0; i < ImageCount; i++)
        {
            SwapchainImageViews[i] = Vulkan_CreateImageView(Device, SwapchainImages[i], Swapchain->ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, MipLevel);
        }
    }

    VkFramebuffer *Framebuffers;
    {
        Arena_AllocArray(Arena, &Framebuffers, ImageCount);
        for (u32 i = 0; i < ImageCount; i++)
        {
            VkImageView Attachments[] = { 
                ColorImageView,
                DepthImageView,
                SwapchainImageViews[i],
            };
            /* NOTE: framebuffer must be compatible with swapchain and render pass */
            VkFramebufferCreateInfo CreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .attachmentCount = STATIC_ARRAY_SIZE(Attachments),
                .pAttachments = Attachments,
                .renderPass = RenderPass,
                .width = Dimensions.Width,
                .height = Dimensions.Height,
                .layers = 1,
            };
            VK_CHECK(vkCreateFramebuffer(Device, &CreateInfo, NULL, &Framebuffers[i]));
        }
    }

    if (ShouldCreateSyncObjects)
    {
        VkSemaphore *RenderFinishedSemaphores;
        {
            Arena_AllocArray(Arena, &RenderFinishedSemaphores, ImageCount);
            VkSemaphoreCreateInfo SemaphoreCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            };
            for (u32 i = 0; i < ImageCount; i++)
            {
                VK_CHECK(vkCreateSemaphore(Device, &SemaphoreCreateInfo, NULL, &RenderFinishedSemaphores[i]));
            }
        }

        SwapchainImage->RenderFinishedSemaphoreArray = RenderFinishedSemaphores;
    }

    SwapchainImage->Count = ImageCount;
    SwapchainImage->Array = SwapchainImages;
    SwapchainImage->ViewArray = SwapchainImageViews;
    SwapchainImage->FramebufferArray = Framebuffers;
}

internal VkShaderModule Vulkan_CreateShaderModule(
    VkDevice Device, const u8 *ShaderCode, isize CodeSize
) {
    VkShaderModule Module;
    VkShaderModuleCreateInfo CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = CodeSize,
        .pCode = (u32 *)ShaderCode,
    };
    VK_CHECK(vkCreateShaderModule(Device, &CreateInfo, NULL, &Module));
    return Module;
}

internal vk_vertex_description Vulkan_ConvertRendererVertexDescription(const renderer_vertex_description *Desc, arena_alloc *Arena)
{
    vk_vertex_description Result = {
        .AttribCount = Desc->AttribCount,
        .Binding = {
            .binding = Desc->Binding,
            .stride = Desc->Stride,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };
    Arena_AllocArray(Arena, &Result.Attribs, Result.AttribCount);
    for (int i = 0; i < Result.AttribCount; i++)
    {
        renderer_vertex_attributes *Attrib = Desc->Attribs + i;
        VkFormat Format = 0;
        switch (Attrib->Type)
        {
        case RENDERER_TYPE_F32x1: Format = VK_FORMAT_R32_SFLOAT; break;
        case RENDERER_TYPE_F32x2: Format = VK_FORMAT_R32G32_SFLOAT; break;
        case RENDERER_TYPE_F32x3: Format = VK_FORMAT_R32G32B32_SFLOAT; break;
        case RENDERER_TYPE_F32x4: Format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
        case RENDERER_TYPE_U32x1: Format = VK_FORMAT_R32_UINT; break;
        }
        Result.Attribs[i] = (VkVertexInputAttributeDescription) {
            .binding = Attrib->Binding,
            .offset = Attrib->Offset,
            .location = Attrib->Location,
            .format = Format,
        };
    }
    return Result;
}


internal void Vulkan_DestroyGraphicsPipeline(VkDevice Device, vk_graphics_pipeline *GraphicsPipeline)
{
    vkDestroyPipelineLayout(Device, GraphicsPipeline->Layout, NULL);
    vkDestroyPipeline(Device, GraphicsPipeline->Handle, NULL);
}

typedef struct 
{
    bool8 EnableMSAA;
    VkSampleCountFlags MSAASampleCount;

    bool8 EnableDepthTesting;

    VkCullModeFlags CullMode;
    VkFrontFace CullModeFrontFace;

    bool8 EnableSampleShading;
    float SampleShadingMin;

    bool8 EnableDepthBoundsTesting;
    float DepthBoundsTestingMin, DepthBoundsTestingMax;

    bool8 EnableBlending;
} vk_graphics_pipeline_config;
internal vk_graphics_pipeline Vulkan_CreateGraphicsPipeline(
    VkDevice Device, 
    VkRenderPass RenderPass, 
    const vk_graphics_pipeline_config *Config,
    VkDescriptorSetLayout *SetLayouts, int SetLayoutCount, 
    const u8 *FragShaderSpvCode, isize FragShaderSpvSizeBytes,
    const u8 *VertShaderSpvCode, isize VertShaderSpvSizeBytes,
    const renderer_vertex_description *Desc,
    arena_alloc TmpArena
) {
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline GraphicsPipelineHandle = VK_NULL_HANDLE;
    VkShaderModule FragShaderModule = Vulkan_CreateShaderModule(Device, FragShaderSpvCode, FragShaderSpvSizeBytes);
    VkShaderModule VertShaderModule = Vulkan_CreateShaderModule(Device, VertShaderSpvCode, VertShaderSpvSizeBytes);
    vk_vertex_description VertexDesc = Vulkan_ConvertRendererVertexDescription(Desc, &TmpArena);
    {
        /* NOTE: create programmable stages (fragment and vertex) */
        VkPipelineShaderStageCreateInfo ShaderStages[] = {
            [0] = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = VertShaderModule, 
                .pName = "main",
            },
            [1] = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = FragShaderModule, 
                .pName = "main",
            },
        };
        /* NOTE: dynamic states */
        VkDynamicState DynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, 
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo DynamicStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = STATIC_ARRAY_SIZE(DynamicStates),
            .pDynamicStates = DynamicStates,
        };
        /* NOTE: create viewport */
        VkPipelineViewportStateCreateInfo ViewportStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        /* NOTE: vertex attributes */
        VkPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexAttributeDescriptionCount = VertexDesc.AttribCount,
            .pVertexAttributeDescriptions = VertexDesc.Attribs,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &VertexDesc.Binding,
        };
        /* NOTE: input assembly state */
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .primitiveRestartEnable = VK_FALSE,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 
        };
        /* NOTE: rasterization state */
        VkPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE, /* NOTE: other modes requires GPU ffeature to be enabled */
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL, /* NOTE: other modes requires GPU feature to be enabled */
            .lineWidth = 1.0f, /* NOTE: other line with requires GPU feature to be enabled */
            .cullMode = Config->CullMode,
            .frontFace = Config->CullModeFrontFace,
            .depthBiasEnable = VK_FALSE,
        };
        /* NOTE: multisample (anti-aliasing) state */
        VkPipelineMultisampleStateCreateInfo MultisamplingStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = Config->EnableMSAA? Config->MSAASampleCount : VK_SAMPLE_COUNT_1_BIT,
            /* NOTE: sampling shading: is anti aliasing within a texture (normally AA is only enabled for primitives) */
            .sampleShadingEnable = Config->EnableSampleShading,
            .minSampleShading = Config->SampleShadingMin,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        /* NOTE: depth and stencil test state */
        VkPipelineDepthStencilStateCreateInfo DepthStencilStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = Config->EnableDepthTesting,
            .depthWriteEnable = Config->EnableDepthTesting,
            .depthCompareOp = VK_COMPARE_OP_LESS,

            .depthBoundsTestEnable = Config->EnableDepthBoundsTesting,
            .minDepthBounds = Config->DepthBoundsTestingMin,
            .maxDepthBounds = Config->DepthBoundsTestingMax,

            .stencilTestEnable = VK_FALSE,
            .back = { 0 },
            .front = { 0 },
        };
        /* NOTE: color blending state */
        VkPipelineColorBlendAttachmentState ColorBlendAttachment = {
            .colorWriteMask = 
                VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = Config->EnableBlending,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        };
        VkPipelineColorBlendStateCreateInfo ColorBlendingStateCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &ColorBlendAttachment,
            .blendConstants = {
                0.0f, 0.0f, 0.0f, 0.0f,
            },
        };
        /* NOTE: pipeline layout */
        VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = SetLayoutCount,
            .pSetLayouts = SetLayouts,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = NULL,
        };
        VK_CHECK(vkCreatePipelineLayout(Device, &PipelineLayoutCreateInfo, NULL, &PipelineLayout));

        STATIC_ASSERT(STATIC_ARRAY_SIZE(ShaderStages) == 2, "");
        VkGraphicsPipelineCreateInfo GraphicsPipelineCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, 
            .stageCount = STATIC_ARRAY_SIZE(ShaderStages),
            .pStages = ShaderStages,
            .pViewportState = &ViewportStateCreateInfo,
            .pVertexInputState = &VertexInputStateCreateInfo,
            .pInputAssemblyState = &InputAssemblyCreateInfo, 
            .pRasterizationState = &RasterizationStateCreateInfo,
            .pMultisampleState = &MultisamplingStateCreateInfo,
            .pDepthStencilState = &DepthStencilStateCreateInfo,
            .pColorBlendState = &ColorBlendingStateCreateInfo, 
            .pDynamicState = &DynamicStateCreateInfo,
            .layout = PipelineLayout,
            .renderPass = RenderPass,
            /* index to subpass in VkRenderPassCreateInfo.pSubpasses */
            /* idx = GraphicsPipelineCreateInfo.subpass 
             * -> idx = RenderPassCreateInfo.pSubpasses[idx].pColorAttachments[0..Count].attchment
             *  -> RenderPassCreateInfo.pAttachments[idx]
             *   -> VkAttachmentDescription
             */
            .subpass = 0, 
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        VK_CHECK(vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &GraphicsPipelineCreateInfo, NULL, &GraphicsPipelineHandle
        ));
    }
    vkDestroyShaderModule(Device, FragShaderModule, NULL);
    vkDestroyShaderModule(Device, VertShaderModule, NULL);

    vk_graphics_pipeline GraphicsPipeline = {
        .Layout = PipelineLayout,
        .Handle = GraphicsPipelineHandle,
    };
    return GraphicsPipeline;
}

internal VkRenderPass Vulkan_CreateRenderPass(VkDevice Device, 
    VkSampleCountFlagBits Samples, VkFormat DstImageFormat, VkFormat DepthBufferFormat
) {
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    {
#define ATTACHMENT_COUNT 3
#define COLOR_ATTACHMENT 0
#define DEPTH_ATTACHMENT 1
#define COLOR_RESOLVE_ATTACHMENT 2
        VkAttachmentDescription DepthStencilAttachmentDesc = {
            .format = DepthBufferFormat,
            .samples = Samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, /* clear after done drawing */
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference DepthStencilAttachmentRef = {
            .attachment = DEPTH_ATTACHMENT,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        VkAttachmentDescription ColorAttachmentDesc = {
            .format = DstImageFormat,
            .samples = Samples,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, 
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            /* NOTE: set these if we have a stencil buffer */
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference ColorAttachmentRef = {
            .attachment = COLOR_ATTACHMENT, /* [in pSubpasses] points to the index in pAttachments */
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        /* NOTE: to resolve an MSAA image to be presented, a new attachment is needed */
        VkAttachmentDescription ColorAttachmentResolveDesc = {
            .format = DstImageFormat, 
            .samples = VK_SAMPLE_COUNT_1_BIT, 
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, 
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        VkAttachmentReference ColorAttachmentResolveRef = {
            .attachment = COLOR_RESOLVE_ATTACHMENT, /* [in pSubpasses] points to the index in pAttachments */
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        VkSubpassDescription Subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &ColorAttachmentRef,
            .pDepthStencilAttachment = &DepthStencilAttachmentRef,
            .pResolveAttachments = &ColorAttachmentResolveRef,
        };

        VkSubpassDependency Dep = {
            .srcSubpass = VK_SUBPASS_EXTERNAL, 
            .dstSubpass = 0, /* subpass index */

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };

        /* NOTE: have to specify array size because TCC would not compile without it */
        VkAttachmentDescription Attachments[ATTACHMENT_COUNT] = { 
            [COLOR_ATTACHMENT] = ColorAttachmentDesc,
            [DEPTH_ATTACHMENT] = DepthStencilAttachmentDesc,
            [COLOR_RESOLVE_ATTACHMENT] = ColorAttachmentResolveDesc,
        };
        VkRenderPassCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = STATIC_ARRAY_SIZE(Attachments),
            .pAttachments = Attachments,
            .subpassCount = 1,
            .pSubpasses = &Subpass,
            .dependencyCount = 1, 
            .pDependencies = &Dep,
        };
        VK_CHECK(vkCreateRenderPass(Device, &CreateInfo, NULL, &RenderPass));
#undef COLOR_ATTACHMENT
#undef DEPTH_ATTACHMENT
#undef COLOR_RESOLVE_ATTACHMENT
#undef ATTACHMENT_COUNT
    }
    return RenderPass;
}

internal VkCommandPool Vulkan_CreateCommandPool(const vk_gpu_context *GpuContext) 
{
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    {
        VkCommandPoolCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = GpuContext->QueueFamilyIndex[QUEUE_FAMILY_TYPE_GRAPHICS],
        };
        VK_CHECK(vkCreateCommandPool(GpuContext->Device, &CreateInfo, NULL, &CommandPool));
    }
    return CommandPool;
}

internal VkFormat Vulkan_FindSupportedFormat(
    VkPhysicalDevice Gpu,
    const VkFormat *DesiredFormatList, isize DesiredFormatCount, 
    VkImageTiling Tiling, 
    VkFormatFeatureFlags DesiredFeatures
) {
    for (int i = 0; i < DesiredFormatCount; i++)
    {
        VkFormat Format = DesiredFormatList[i];
        VkFormatProperties Properties;
        vkGetPhysicalDeviceFormatProperties(Gpu, Format, &Properties);
        if (Tiling == VK_IMAGE_TILING_LINEAR
        && DesiredFeatures == (DesiredFeatures & Properties.linearTilingFeatures))
        {
            return Format;
        }
        if (Tiling == VK_IMAGE_TILING_OPTIMAL
        && DesiredFeatures == (DesiredFeatures & Properties.optimalTilingFeatures))
        {
            return Format;
        }
    }
    UNREACHABLE();
}

internal VkCommandBuffer Vulkan_BeginSingleTimeCommandBuffer(const vk_gpu_context *GpuContext, VkCommandPool CommandPool)
{
    VkCommandBuffer CmdBuf;
    VkCommandBufferAllocateInfo CmdBufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
        .commandPool = CommandPool,
    };
    VK_CHECK(vkAllocateCommandBuffers(GpuContext->Device, &CmdBufAllocInfo, &CmdBuf));

    VkCommandBufferBeginInfo BeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(CmdBuf, &BeginInfo));
    return CmdBuf;
}

internal void Vulkan_EndSingleTimeCommandBuffer(const vk_gpu_context *GpuContext, VkCommandPool CommandPool, VkCommandBuffer CmdBuf)
{
    VK_CHECK(vkEndCommandBuffer(CmdBuf));
    VkSubmitInfo SubmitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &CmdBuf,
    };
    /* TODO: async transfers */
    VK_CHECK(vkQueueSubmit(GpuContext->GraphicsQueue, 1, &SubmitInfo, VK_NULL_HANDLE));
    /* FIXME: this is very bad performance wise, and that is an understatement */
    /* NOTE: this is locked to vsync time, DON'T DO THIS */
    vkQueueWaitIdle(GpuContext->GraphicsQueue);
    vkFreeCommandBuffers(GpuContext->Device, CommandPool, 1, &CmdBuf);
}

internal bool32 Vulkan_DepthBufferFormatHasStencil(VkFormat Format)
{
    return Format == VK_FORMAT_D32_SFLOAT_S8_UINT || Format == VK_FORMAT_D24_UNORM_S8_UINT;
}

internal void Vulkan_TransitionImageLayout(
    const vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    VkImage Image, VkFormat Format, VkImageLayout Old, VkImageLayout New, u32 MipLevel
) {
    VkCommandBuffer CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
    {
        /* image memory barrier */
        {
            VkAccessFlags SrcAccessMask;
            VkAccessFlags DstAccessMask;
            VkPipelineStageFlags SrcStageFlags;
            VkPipelineStageFlags DstStageFlags;
            VkImageAspectFlags Aspect;
            if (Old == VK_IMAGE_LAYOUT_UNDEFINED && New == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                /* texture creation */
                Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                SrcAccessMask = 0;
                DstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                SrcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                DstStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (Old == VK_IMAGE_LAYOUT_UNDEFINED && New == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                /* depth/stencil */
                Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                SrcAccessMask = 0;
                DstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                SrcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                DstStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                if (Vulkan_DepthBufferFormatHasStencil(Format))
                {
                    Aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }
            }
            else if (Old == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && New == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                /* texture save */
                Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                SrcStageFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
                DstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                SrcAccessMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                DstStageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else
            {
                RUNTIME_TODO("%s", __func__);
            }

            VkImageMemoryBarrier Barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .image = Image, 
                .oldLayout = Old,
                .newLayout = New,
                .subresourceRange = {
                    .aspectMask = Aspect,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                    .baseMipLevel = 0,
                    .levelCount = MipLevel,
                },
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .srcAccessMask = SrcAccessMask, 
                .dstAccessMask = DstAccessMask, 
            };
            /* NOTE: all barriers are submitted by this function, 
               other barriers: 
                    VkBufferMemoryBarrier
                    VkMemoryBarrier
            */
            vkCmdPipelineBarrier(CmdBuf, 
                SrcStageFlags, 
                DstStageFlags, 
                0, 
                0, NULL, 
                0, NULL, 
                1, &Barrier
            );
        }
    }
    Vulkan_EndSingleTimeCommandBuffer(GpuContext, CommandPool, CmdBuf);
}


internal vkm_image_and_view Vulkan_CreateDepthBuffer(
    vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    VkSampleCountFlagBits Samples, u32 Width, u32 Height
) {
    vkm_image_and_view DepthBuffer;
    {
        VkFormat DepthFormat;
        {
            VkFormat DesiredFormat[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
            DepthFormat = Vulkan_FindSupportedFormat(GpuContext->PhysicalDevice,
                DesiredFormat, STATIC_ARRAY_SIZE(DesiredFormat),
                VK_IMAGE_TILING_OPTIMAL,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
            );
        }

        int MipLevel = 1;
        vkm *Vkm = &GpuContext->VkMalloc;
        DepthBuffer = Vkm_CreateImageAndView(
            &GpuContext->VkMalloc, 
            Width, Height, MipLevel, 
            Samples, 
            DepthFormat, 
            VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        Vulkan_TransitionImageLayout(GpuContext, CommandPool, 
            Vkm_Image_Get(Vkm, DepthBuffer.ImageHandle).Handle, 
            DepthFormat, 
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 
            MipLevel
        );
    }
    return DepthBuffer;
}

internal vkm_image_and_view Vulkan_ResizeDepthBuffer(
    vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    vkm_image_and_view DepthBuffer, 
    u32 NewWidth, u32 NewHeight
) {
    vkm_image_and_view NewDepthBuffer;
    {
        vkm *Vkm = &GpuContext->VkMalloc;
        NewDepthBuffer = Vkm_ImageAndView_Resize(Vkm, DepthBuffer, NewWidth, NewHeight);
        Vulkan_TransitionImageLayout(GpuContext, CommandPool, 
            Vkm_Image_Get(Vkm, DepthBuffer.ImageHandle).Handle, 
            Vkm_Image_Get(Vkm, DepthBuffer.ImageHandle).Format, 
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 
            Vkm_Image_Get(Vkm, DepthBuffer.ImageHandle).MipLevels 
        );
    }
    return NewDepthBuffer;
}

internal vkm_image_and_view Vulkan_CreateColorResource(
    vk_gpu_context *GpuContext, 
    VkSampleCountFlagBits Samples, VkFormat Format, u32 Width, u32 Height)
{
    int MipLevel = 1;
    vkm_image_and_view MSAAResolve = Vkm_CreateImageAndView(
        &GpuContext->VkMalloc, 
        Width, Height, MipLevel, 
        Samples, 
        Format, 
        VK_IMAGE_TILING_OPTIMAL, 
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    return MSAAResolve;
}



internal void Vulkan_RecreateSwapchain(renderer *Vk, arena_alloc *Arena)
{
    Vk->IsResized = false;
    profiler *Profiler = Vk->Profiler;
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    Profiler_Scope(Profiler, "Vulkan_RecreateSwapchain()")
    {
        VkDevice Device = Vk->GpuContext.Device;

        /* TODO: move this out of here */
#if 0
        Platform_ProfileBlock(WaitEventTime)
        {
            int Width, Height;
            glfwGetFramebufferSize(Vk->Window, &Width, &Height);
            while (Width == 0 || Height == 0)
            {
                glfwGetFramebufferSize(Vk->Window, &Width, &Height);
                glfwWaitEvents();
                if (glfwWindowShouldClose(Vk->Window))
                    break;
            }
        }
#endif

        Profiler_Scope(Profiler, "vkDeviceWaitIdle()")
        {
            vkDeviceWaitIdle(Device);
        }

        arena_context Context;
        Profiler_Scope(Profiler, "arena context creation")
        {
            Arena_TryPopContext(Arena, &Vk->Swapchain.ArenaContext);
            Context = Arena_BeginContext(Arena);
        }

        {
            platform_framebuffer_dimensions FramebufferDimensions = Platform_Get(FramebufferDimensions);
            int Width = FramebufferDimensions.Width;
            int Height = FramebufferDimensions.Height;
            vk_swapchain NewSwapchain = { 0 };
            vkm_image_and_view NewDepthBuffer = { 0 };
            vkm_image_and_view NewColorBuffer = { 0 };
            vk_swapchain_image NewSwapchainImage = Vk->SwapchainImage; /* NOTE: need to keep sync objects */
            {
                bool32 DontTouchSyncObjects = false;
                vk_swapchain *OldSwapchain = &Vk->Swapchain;
                vkm_image_and_view *OldDepthBuffer = &Vk->DepthBuffer;
                vkm_image_and_view *OldColorBuffer = &Vk->ColorResource;
                vk_swapchain_image *OldSwapchainImage = &Vk->SwapchainImage;
                vkm *Vkm = &GpuContext->VkMalloc;

                /* NOTE: DO NOT REORDER */
                Profiler_Scope(Profiler, "Vulkan_DestroySwapchainImageAndFramebuffer()")
                {
                    Vulkan_DestroySwapchainImageAndFramebuffer(
                        Device, OldSwapchainImage, DontTouchSyncObjects
                    );
                }
                Profiler_Scope(Profiler, "Vulkan_CreateSwapchain()")
                {
                    NewSwapchain = Vulkan_CreateSwapchain(
                        Profiler,
                        GpuContext,
                        Width, Height,
                        Vk->ForceTripleBuffering,
                        Vk->WindowSurface, 
                        OldSwapchain->Handle,
                        *Arena
                    );
                    Width = NewSwapchain.Extent.width;
                    Height = NewSwapchain.Extent.height;
                }
                Profiler_Scope(Profiler, "Vulkan_CreateColorResource()")
                {
                    NewColorBuffer = Vkm_ImageAndView_Resize(Vkm, *OldColorBuffer, Width, Height);
                }
                Profiler_Scope(Profiler, "Vulkan_CreateDepthBuffer()")
                {
                    NewDepthBuffer = Vulkan_ResizeDepthBuffer(GpuContext, Vk->CommandPool, *OldDepthBuffer, Width, Height);
                }
                Profiler_Scope(Profiler, "Vulkan_CreateSwapchainImageViewAndFramebuffers()")
                {
                    Vulkan_CreateSwapchainImageViewAndFramebuffers(
                        &NewSwapchainImage, 
                        &NewSwapchain, 
                        GpuContext, 
                        Vk->RenderPass, 
                        NewColorBuffer.ImageView, NewDepthBuffer.ImageView, 
                        FramebufferDimensions, 
                        DontTouchSyncObjects,
                        Arena
                    );
                }
                Profiler_Scope(Profiler, "Vulkan_DestroySwapchain()")
                {
                    Vulkan_DestroySwapchain(Device, OldSwapchain);
                }
            }
            Vk->Swapchain = NewSwapchain;
            Vk->SwapchainImage = NewSwapchainImage;
            Vk->DepthBuffer = NewDepthBuffer;
            Vk->ColorResource = NewColorBuffer;
        }
        Profiler_Scope(Profiler, "arena context end")
        {
            Arena_EndContext(Arena, &Context);
            Vk->Swapchain.ArenaContext = Context;
        }
    }
}


internal void Vulkan_RecordCommandBuffer(
    renderer *Vk, VkCommandBuffer CmdBuffer, u32 FramebufferIndex,
    const renderer_draw_pipeline *Pipelines, i32 PipelineCount
) {
    vkm *Vkm = &Vk->GpuContext.VkMalloc;
    VkDevice Device = Vulkan_GetDevice(Vk);
    vk_swapchain *Swapchain = &Vk->Swapchain;
    vk_swapchain_image *SwapchainImage = &Vk->SwapchainImage;
    VkCommandBufferBeginInfo BeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pInheritanceInfo = NULL, /* NOTE: for secondary command buffers */
    };
    VkDescriptorSet *CurrentDescriptorSet = Vk->FrameData.DescriptorSetArray + Vk->CurrentFrame;

    VK_CHECK(vkBeginCommandBuffer(CmdBuffer, &BeginInfo));
    {
        VkClearValue Clears[] = {
            [0] = {.color.float32 = {.2f, .3f, .4f, 1.0f}},
            [1] = {.depthStencil = {1.0f, 0.0f}},
        };
        VkRenderPassBeginInfo RenderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = Vk->RenderPass,
            .renderArea = {
                .offset = { 0 },
                .extent = Swapchain->Extent,
            },
            .framebuffer = SwapchainImage->FramebufferArray[FramebufferIndex],
            .clearValueCount = STATIC_ARRAY_SIZE(Clears),
            .pClearValues = Clears,
        };
        vkCmdBeginRenderPass(CmdBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            VkViewport FullScreenViewport = { 
                .x = 0, 
                .y = (float)Swapchain->Extent.height, 
                .width = (float)Swapchain->Extent.width, 
                .height = -(float)Swapchain->Extent.height,
                .maxDepth = 1.0f, 
                .minDepth = 0.0f,
            };
            VkRect2D FullScreenScissor = {
                .offset = { 0 },
                .extent = {
                    .width = Swapchain->Extent.width,
                    .height = Swapchain->Extent.height,
                }
            };

            /* bind each pipeline and draw each mesh group */
            for (int i = 0; i < PipelineCount; i++)
            {
                const renderer_draw_pipeline *DrawPipeline = Pipelines + i;
                const vk_graphics_pipeline *Pipeline = &Vk->GraphicsPipelines.Data[
                    DrawPipeline->GraphicsPipelineHandle.Value
                ];

                vkCmdBindPipeline(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Handle);
                vkCmdSetViewport(CmdBuffer, 0, 1, &FullScreenViewport);

                for (int k = 0; k < DrawPipeline->GroupCount; k++)
                {
                    const renderer_draw_pipeline_group *Group = DrawPipeline->Groups + k;
                    const vk_mesh *Mesh = &Vk->MeshArray.Data[Group->MeshHandle.Value];

                    VkRect2D Scissor = {
                        .offset = {
                            .x = Group->Scissor.OffsetX,
                            .y = Group->Scissor.OffsetY,
                        },
                        .extent = {
                            .width = Group->Scissor.Width == 0? FullScreenScissor.extent.width : Group->Scissor.Width,
                            .height = Group->Scissor.Height == 0? FullScreenScissor.extent.height : Group->Scissor.Height,
                        },
                    }; 

                    vkCmdSetScissor(CmdBuffer, 0, 1, &Scissor);
                    ASSERT(Mesh->IndexCount, "handle: %d", Scissor->Mesh.Value);

                    VkBuffer VertexBuffers[] = { Vkm_Buffer_GetVkBuffer(Vkm, Mesh->VertexBuffer) };
                    VkDeviceSize Offsets[] = { Vkm_Buffer_GetOffsetBytes(Mesh->VertexBuffer) };
                    vkCmdBindVertexBuffers(CmdBuffer, 0, 1, VertexBuffers, Offsets);

                    vkCmdBindIndexBuffer(CmdBuffer, 
                        Vkm_Buffer_GetVkBuffer(Vkm, Mesh->IndexBuffer), 
                        Vkm_Buffer_GetOffsetBytes(Mesh->IndexBuffer), 
                        VK_INDEX_TYPE_UINT32
                    );

                    vkCmdBindDescriptorSets(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Layout, 0, 1, CurrentDescriptorSet, 0, NULL);

                    /* draw call */
                    u32 IndexCount = Group->Mesh_IndexBuffer_ElementCount;
                    if (IndexCount == 0)
                        IndexCount = Mesh->IndexCount;
                    vkCmdDrawIndexed(CmdBuffer, IndexCount, 1, Group->Mesh_IndexBuffer_FirstElement, 0, 0);
                }
            }
        }
        vkCmdEndRenderPass(CmdBuffer);
    }
    VK_CHECK(vkEndCommandBuffer(CmdBuffer));
}

internal void Vulkan_TransferDataToGpuLocalMemory(
    const vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    vkm_buffer GpuLocalDst, const void *Src, isize SrcSizeBytes
) {
    /* because the staging buffer has a fixed size, we'll transfer the data by blocks.
     * TODO: use a dedicated transfer queue */
    Profiler_Scope(GpuContext->Profiler, "Vulkan_TransferDataToGpuLocalMemory()")
    {
        const vkm *Vkm = &GpuContext->VkMalloc;
        VkBuffer DstBuffer = Vkm_Buffer_GetVkBuffer(Vkm, GpuLocalDst);
        vkm_buffer StagingBuffer = GpuContext->StagingBuffer;
        VkBuffer StagingBufferHandle = Vkm_Buffer_GetVkBuffer(Vkm, StagingBuffer);
        void *StagingBufferPtr = GpuContext->StagingBufferPtr;
        i64 StagingBufferSizeBytes = Vkm_Buffer_GetSizeBytes(StagingBuffer);

        const u8 *SrcPtr = Src;
        i64 TransferedByteCount = 0;
        Profiler_Scope(GpuContext->Profiler, "Copy Loop")
        {
            while (SrcSizeBytes > 0)
            {
                /* copy to staging */
                i64 TransferSize = MINIMUM(StagingBufferSizeBytes, SrcSizeBytes);
                Profiler_Scope(GpuContext->Profiler, "memcpy()")
                    memcpy(StagingBufferPtr, SrcPtr, TransferSize);

                /* copy to dst */
                i64 DstOffset = Vkm_Buffer_GetOffsetBytes(GpuLocalDst);
                {
                    VkCommandBuffer CmdBuf; 
                    Profiler_Scope(GpuContext->Profiler, "Vulkan_BeginSingleTimeCommandBuffer()")
                    {
                        CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
                    }
                    VkBufferCopy Region = {
                        .dstOffset = DstOffset + TransferedByteCount,
                        .srcOffset = 0,
                        .size = TransferSize,
                    };
                    vkCmdCopyBuffer(CmdBuf, StagingBufferHandle, DstBuffer, 1, &Region);
                    Profiler_Scope(GpuContext->Profiler, "Vulkan_EndSingleTimeCommandBuffer()")
                    {
                        Vulkan_EndSingleTimeCommandBuffer(GpuContext, CommandPool, CmdBuf);
                    }
                }

                SrcPtr += TransferSize;
                SrcSizeBytes -= TransferSize;
                TransferedByteCount += TransferSize;
            }
        }
    }
}


internal VkDescriptorSetLayout Vulkan_CreateDescriptorSetLayout(VkDevice Device, int UniformBufferDescriptorCount, int ImageSamplerDescriptorCount)
{
    VkDescriptorSetLayout Layout;
    {
        VkDescriptorSetLayoutBinding Bindings[] = {
            [0] = {
                .binding = BINDING_TYPE_UNIFORM_BUFFER,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = UniformBufferDescriptorCount,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = NULL,
            },
            [1] = {
                .binding = BINDING_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = ImageSamplerDescriptorCount,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = NULL,
            },
        };

        VkDescriptorSetLayoutCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = STATIC_ARRAY_SIZE(Bindings),
            .pBindings = Bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(Device, &CreateInfo, NULL, &Layout));
    }
    return Layout;
}

internal VkDescriptorPool Vulkan_CreateDescriptorPool(VkDevice Device, int FramesInFlight, int UniformBufferDescriptorCount, int ImageSamplerDescriptorCount)
{
    VkDescriptorPoolSize PoolSizes[] = {
        [0] = {
            .descriptorCount = UniformBufferDescriptorCount,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        },
        [1] = {
            .descriptorCount = ImageSamplerDescriptorCount,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        },
    };

    int MaxDescriptorCount = (UniformBufferDescriptorCount + ImageSamplerDescriptorCount) * FramesInFlight;
    VkDescriptorPoolCreateInfo CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = STATIC_ARRAY_SIZE(PoolSizes),
        .pPoolSizes = PoolSizes,
        .maxSets = MaxDescriptorCount,
        .flags = 0,
    };
    VkDescriptorPool DescriptorPool;
    VK_CHECK(vkCreateDescriptorPool(Device, &CreateInfo, NULL, &DescriptorPool));
    return DescriptorPool;
}

internal vk_frame_data Vulkan_CreateFrameData(
    vk_gpu_context *GpuContext,
    VkCommandPool CommandPool,
    VkDescriptorPool DescriptorPool,
    VkDescriptorSetLayout DescriptorSetLayout, 
    int FramesInFlight,
    vk_texture_array *TextureArray,
    VkDeviceSize UniformBufferSize,
    arena_alloc *Arena
) {
    VkDevice Device = GpuContext->Device;

    vkm_buffer *UniformBufferArray;
    void **UniformBufferPtrArray;
    {
        Arena_AllocArray(Arena, &UniformBufferArray, FramesInFlight);
        Arena_AllocArray(Arena, &UniformBufferPtrArray, FramesInFlight);
        for (int i = 0; i < FramesInFlight; i++)
        {
            vkm_buffer UniformBuffer = Vkm_CreateBuffer(&GpuContext->VkMalloc, VKM_MEMORY_TYPE_CPU_VISIBLE, UniformBufferSize);
            UniformBufferPtrArray[i] = Vkm_Buffer_GetMappedMemory(&GpuContext->VkMalloc, UniformBuffer);
            UniformBufferArray[i] = UniformBuffer;
        }
    }

    /* allocate a descriptor set for each frame in flight */
    VkDescriptorSet *DescriptorSets;
    {
        Arena_AllocArray(Arena, &DescriptorSets, FramesInFlight);
        arena_snapshot Snapshot = Arena_SaveSnapshot(Arena);

        VkDescriptorSetLayout *DescriptorSetLayouts;
        Arena_AllocArrayNonZero(Arena, &DescriptorSetLayouts, FramesInFlight);
        for (int i = 0; i < FramesInFlight; i++)
        {
            DescriptorSetLayouts[i] = DescriptorSetLayout;
        }

        VkDescriptorImageInfo *TextureImageInfos;
        Arena_AllocArray(Arena, &TextureImageInfos, TextureArray->Count);
        for (int i = 0; i < TextureArray->Count; i++)
        {
            TextureImageInfos[i] = (VkDescriptorImageInfo) {
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = TextureArray->Data[i].ImageView,
                .sampler = TextureArray->Data[i].Sampler,
            };
        }

        VkDescriptorSetAllocateInfo AllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = DescriptorPool, 
            .descriptorSetCount = FramesInFlight,
            .pSetLayouts = DescriptorSetLayouts,
        };
        VK_CHECK(vkAllocateDescriptorSets(Device, &AllocInfo, DescriptorSets));
        for (int i = 0; i < FramesInFlight; i++)
        {
            VkDescriptorBufferInfo BufferInfo = {
                .buffer = Vkm_Buffer_GetVkBuffer(&GpuContext->VkMalloc, UniformBufferArray[i]),
                .offset = Vkm_Buffer_GetOffsetBytes(UniformBufferArray[i]),
                .range = UniformBufferSize,
            };
            VkWriteDescriptorSet Write[] = {
                [0] = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = DescriptorSets[i],
                    .dstBinding = BINDING_TYPE_UNIFORM_BUFFER,
                    .dstArrayElement = 0, /* if uniform is an array, index to update */
                    .descriptorCount = 1, /* amount of descriptor to update starting from dstArrayElement */
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &BufferInfo,     /* if descriptor was for buffer data */
                    .pImageInfo = NULL,             /* if descriptor was for image data */
                    .pTexelBufferView = NULL,       /* if descriptor was for buffer view */
                },
                [1] = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = DescriptorSets[i],
                    .dstBinding = BINDING_TYPE_COMBINED_IMAGE_SAMPLER,
                    .dstArrayElement = 0, /* if uniform is an array, index to update */
                    .descriptorCount = TextureArray->Count, /* amount of descriptor to update starting from dstArrayElement */
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pBufferInfo = NULL,     /* if descriptor was for buffer data */
                    .pImageInfo = TextureImageInfos,             /* if descriptor was for image data */
                    .pTexelBufferView = NULL,       /* if descriptor was for buffer view */
                },
            };
            VkCopyDescriptorSet *Copy = NULL;
            vkUpdateDescriptorSets(Device, STATIC_ARRAY_SIZE(Write), Write, 0, Copy);
        }
        Arena_RestoreSnapshot(Arena, Snapshot);
    }

    VkCommandBuffer *CmdBufArray;
    {
        Arena_AllocArray(Arena, &CmdBufArray, FramesInFlight);
        VkCommandBufferAllocateInfo AllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = CommandPool,
            .commandBufferCount = FramesInFlight,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        };
        VK_CHECK(vkAllocateCommandBuffers(Device, &AllocateInfo, CmdBufArray));
    }

    VkFence *InFlightFenceArray;
    VkSemaphore *ImageAvailableSemaphoreArray;
    {
        Arena_AllocArray(Arena, &InFlightFenceArray, FramesInFlight);
        Arena_AllocArray(Arena, &ImageAvailableSemaphoreArray, FramesInFlight);

        VkFenceCreateInfo InFlightFenceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT, /* fence initted in signaled state */
        };
        VkSemaphoreCreateInfo SemaphoreCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        for (int i = 0; i < FramesInFlight; i++)
        {
            VK_CHECK(vkCreateFence(Device, &InFlightFenceCreateInfo, NULL, &InFlightFenceArray[i]));
            VK_CHECK(vkCreateSemaphore(Device, &SemaphoreCreateInfo, NULL, &ImageAvailableSemaphoreArray[i]));
        }
    }

    return (vk_frame_data) {
        .InFlightFenceArray = InFlightFenceArray,
        .ImageAvailableSemaphoreArray = ImageAvailableSemaphoreArray,
        .CommandBufferArray = CmdBufArray,
        .DescriptorSetArray = DescriptorSets,
        .UniformBufferArray = UniformBufferArray,
        .UniformMappedMemoryArray = UniformBufferPtrArray,
    };
}

/* NOTE: src buffer must have the same format as DstFormat */
internal void Vulkan_CopyBufferToImage(
    const vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    VkBuffer Src, VkImage Dst,
    u32 Width, u32 Height, 
    VkImageLayout DstLayout
) {
    VkCommandBuffer CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
    VkBufferImageCopy Region = {
        .bufferImageHeight = 0, 
        .bufferRowLength = 0,
        .bufferOffset = 0,

        .imageOffset = { 0 },
        .imageExtent = {
            .width = Width,
            .height = Height,
            .depth = 1,
        },
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseArrayLayer = 0, 
            .mipLevel = 0,
            .layerCount = 1,
        },
    };
    vkCmdCopyBufferToImage(CmdBuf, Src, Dst, DstLayout, 1, &Region);
    Vulkan_EndSingleTimeCommandBuffer(GpuContext, CommandPool, CmdBuf);
}

internal VkFormat Vulkan_GetVkFormat(renderer_image_format Format)
{
    switch (Format)
    {
    case RENDERER_IMAGE_FORMAT_BGRA: return VK_FORMAT_B8G8R8A8_SRGB;
    case RENDERER_IMAGE_FORMAT_RGBA: return VK_FORMAT_R8G8B8A8_SRGB;
    case RENDERER_IMAGE_FORMAT_BC1: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case RENDERER_IMAGE_FORMAT_BC2: return VK_FORMAT_BC2_SRGB_BLOCK;
    case RENDERER_IMAGE_FORMAT_BC3: return VK_FORMAT_BC3_SRGB_BLOCK;
    }
    UNREACHABLE_IF(true, "format: %d", Format);
}


void Renderer_Draw(renderer *Vk, const renderer_draw_pipeline *Pipelines, i32 PipelineCount)
{
    arena_alloc *Arena = &Vk->Arena;
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    vk_frame_data *Frame = &Vk->FrameData;
    vk_swapchain_image *ScImage = &Vk->SwapchainImage;
    VkDevice Device = Vulkan_GetDevice(Vk);
    VkCommandBuffer CmdBuffer = Frame->CommandBufferArray[Vk->CurrentFrame];
    VkSwapchainKHR Swapchain = Vk->Swapchain.Handle;

    /* wait for gpu to finish displaying */
    u32 ImageIndex = 0;
    {
        vkWaitForFences(Device, 1, &Frame->InFlightFenceArray[Vk->CurrentFrame], VK_TRUE, UINT64_MAX);
        VkResult Result = vkAcquireNextImageKHR(
            Device, Swapchain, UINT64_MAX, 
            Frame->ImageAvailableSemaphoreArray[Vk->CurrentFrame], 
            VK_NULL_HANDLE, &ImageIndex
        );
        if (Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            Vulkan_RecreateSwapchain(Vk, Arena);
            return;
        }
        else if (Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR)
        {
            FATAL("Vulkan: Unable to acquire next swapchain image.");
        }
    }
    vkResetFences(Device, 1, &Frame->InFlightFenceArray[Vk->CurrentFrame]);


    VkCommandBufferResetFlags Flags = 0;
    vkResetCommandBuffer(CmdBuffer, Flags);
    Vulkan_RecordCommandBuffer(Vk, CmdBuffer, ImageIndex, Pipelines, PipelineCount);
    if (Vk->ShouldUpdateUniformBuffer)
    {
        memcpy(Frame->UniformMappedMemoryArray[Vk->CurrentFrame], Vk->UniformBuffer.Data, Vk->UniformBuffer.Count);
        Vk->ShouldUpdateUniformBuffer = false;
    }


    VkPipelineStageFlags WaitStages[] = { 
        //VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 
    };
    VkSemaphore WaitSemaphores[] = { Frame->ImageAvailableSemaphoreArray[Vk->CurrentFrame], };
    VkSemaphore SignalSemaphores[] = { ScImage->RenderFinishedSemaphoreArray[ImageIndex], };
    VkSubmitInfo SubmitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = STATIC_ARRAY_SIZE(WaitSemaphores), 
        .pWaitSemaphores = WaitSemaphores,
        .pWaitDstStageMask = WaitStages,
        .commandBufferCount = 1, 
        .pCommandBuffers = &CmdBuffer,
        .signalSemaphoreCount = STATIC_ARRAY_SIZE(SignalSemaphores),
        .pSignalSemaphores = SignalSemaphores,
    };
    VK_CHECK(vkQueueSubmit(GpuContext->GraphicsQueue, 1, &SubmitInfo, 
            Frame->InFlightFenceArray[Vk->CurrentFrame]
        ));


    u32 ImageIndices[] = { ImageIndex };
    VkSwapchainKHR Swapchains[] = { Swapchain };
    VkPresentInfoKHR PresentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = STATIC_ARRAY_SIZE(SignalSemaphores),
        .pWaitSemaphores = SignalSemaphores,
        .swapchainCount = 1,
        .pImageIndices = ImageIndices,
        .pSwapchains = Swapchains,
        .pResults = NULL,
    };
    {
        VkResult Result = vkQueuePresentKHR(GpuContext->PresentQueue, &PresentInfo);
        if (Result == VK_SUBOPTIMAL_KHR || Result == VK_ERROR_OUT_OF_DATE_KHR || Vk->IsResized)
        {
            Vulkan_RecreateSwapchain(Vk, Arena);
        }
        else if (Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR)
        {
            FATAL("Vulkan: Unable to present swapchain image.");
        }
    }

    Vk->CurrentFrame++;
    if (Vk->CurrentFrame >= Vk->FramesInFlight)
        Vk->CurrentFrame = 0;
}


renderer_handle Renderer_Init(const char *AppName, int FramesInFlight, bool32 ForceTripleBuffering, profiler *Profiler) 
{
    arena_alloc *Arena;
    renderer *Vk;
    {
        arena_alloc TmpArena; 
        platform_allocator Alloc = Platform_Get(Allocator);

        // FUCK YOU GCC, why movaps????
        // movaps and movups have the SAME PERFORMANCE on pretty much all modern x86 processors
        //STATIC_ASSERT(get_type_alignment(renderer) == 16, "Why the fuck is this aligned on 16 byte boundary?");
        //Arena_Create(&TmpArena, Alloc, 1*MB, sizeof(uintptr_t));

        // fuck you gcc, I will align every piece of shit on cache line size, go fuck yourself
        int AlignOnCacheLineSize = 64;
        Arena_Create(&TmpArena, Alloc, 512 * KB, AlignOnCacheLineSize);

        Vk = Arena_Alloc(&TmpArena, sizeof(renderer));
        *Vk = (renderer) {
            .Arena = TmpArena,
            .Profiler = Profiler,
            .FramesInFlight = FramesInFlight,
            .ForceTripleBuffering = ForceTripleBuffering,
        };
        Arena = &Vk->Arena;
    }


    VkInstance Instance = Vulkan_CreateInstance(&Vk->Instance, *Arena, AppName);
    DEBUG_ONLY(Vk->DebugReportCallback = Vulkan_CreateDebugCallback(Instance)); 
    VK_CHECK(Vulkan_Platform_CreateWindowSurface(Instance, NULL, &Vk->WindowSurface));

    vk_gpu_context *GpuContext;
    VkDevice Device;
    {
        Vk->Gpus = Vulkan_QueryAndSelectGpu(Instance, Vk->WindowSurface, Arena);
        Vk->Gpus.Selected.Features.sampleRateShading = VK_TRUE;
        Vk->GpuContext = Vulkan_CreateGpuContext(Vk->Gpus.Selected.Handle, Vk->WindowSurface, Vk->Gpus.Selected.Features, *Arena);

        GpuContext = &Vk->GpuContext;
        Device = GpuContext->Device;
        GpuContext->Profiler = Profiler;
    }

    /* initialize custom memory allocator */
    {
        {
            i64 ResetCapacity[] = {
                [VKM_MEMORY_TYPE_GPU_LOCAL] = 32*MB,
                [VKM_MEMORY_TYPE_CPU_VISIBLE] = 64*MB,
            };
            VkBufferUsageFlags Usages[] = {
                [VKM_MEMORY_TYPE_GPU_LOCAL] = VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                [VKM_MEMORY_TYPE_CPU_VISIBLE] = VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            };
            VkMemoryPropertyFlags MemoryProperties[] = {
                [VKM_MEMORY_TYPE_GPU_LOCAL] = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                [VKM_MEMORY_TYPE_CPU_VISIBLE] = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            };
            i64 ImagePoolCapacityBytes = 64*MB;
            Vkm_Init(&GpuContext->VkMalloc, 
                Device, GpuContext->PhysicalDevice, 
                ImagePoolCapacityBytes,
                ResetCapacity, Usages, MemoryProperties
            );
        }

        vkm_buffer StagingBuffer = Vkm_CreateBuffer(&GpuContext->VkMalloc, VKM_MEMORY_TYPE_CPU_VISIBLE, 64*MB);
        void *StagingBufferPtr;
        {
            StagingBuffer = Vkm_CreateBuffer(&GpuContext->VkMalloc, VKM_MEMORY_TYPE_CPU_VISIBLE, 64*MB);
            StagingBufferPtr = Vkm_Buffer_GetMappedMemory(&GpuContext->VkMalloc, StagingBuffer);
        }
        GpuContext->StagingBuffer = StagingBuffer;
        GpuContext->StagingBufferPtr = StagingBufferPtr;
    }


    /* NOTE: command pool must be created before attempting to load resources onto the gpu */
    Vk->CommandPool = Vulkan_CreateCommandPool(GpuContext);

    platform_framebuffer_dimensions FramebufferDimensions = Platform_Get(FramebufferDimensions);
    Vk->Swapchain = Vulkan_CreateSwapchain(
        Vk->Profiler, GpuContext, 
        FramebufferDimensions.Width, FramebufferDimensions.Height, 
        Vk->ForceTripleBuffering,
        Vk->WindowSurface, 
        VK_NULL_HANDLE, 
        Vk->Arena
    );

    {
        /* texture handle 0 contains a pink 1x1 texture */
        Arena_AllocDynamicArray(Arena, &Vk->TextureArray, 0, 256);
        renderer_texture_handle Texture = Renderer_UploadTexture(Vk, (u32[]) { 0xFF00FF00 }, 1, 1, 1, RENDERER_IMAGE_FORMAT_BGRA);
        ASSERT(Texture.Value == 0, "First texture");
    }
    /* initialize mesh array */
    {
        /* NOTE: mesh handle 0 does not contain anything */
        Arena_AllocDynamicArray(Arena, &Vk->MeshArray, 1, 256);
    }


    ASSERT_EXPRESSION_TYPE(Vk, renderer_handle, "invalid type");
    return Vk;
}



renderer_mesh_handle Renderer_UploadStaticMesh(
    renderer *Vk, 
    const void *VertexBuffer, isize VertexCount, isize VertexSizeBytes,
    const u32 *Indices, isize IndexCount
) {
    arena_alloc TmpArena = Vk->Arena;
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    VkDevice Device = GpuContext->Device;

    isize VertexBufferSizeBytes = VertexCount * VertexSizeBytes;
    isize IndexBufferSizeBytes = IndexCount * sizeof(Indices[0]);
    /*
     * So apparently, the VkBufferUsageFlags is just a suggestion, 
     * but for certain vkCmd* to work, you'd need to just *HAVE* the appropriate flags
     *
     * eg: vkCmdBindIndexBuffers needs VK_BUFFER_USAGE_INDEX_BUFFER_BIT, though it can have other flags, even VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
     * */

    vk_mesh Mesh = {
        .IndexBufferSizeBytes = IndexBufferSizeBytes,
        .VertexBufferSizeBytes = VertexBufferSizeBytes,
        .VertexCount = VertexCount,
        .IndexCount = IndexCount,

        .VertexBuffer = Vkm_CreateBuffer(&GpuContext->VkMalloc, VKM_MEMORY_TYPE_GPU_LOCAL, VertexBufferSizeBytes),
        .IndexBuffer = Vkm_CreateBuffer(&GpuContext->VkMalloc, VKM_MEMORY_TYPE_GPU_LOCAL, IndexBufferSizeBytes),
    };
    Vulkan_TransferDataToGpuLocalMemory(GpuContext, Vk->CommandPool, Mesh.VertexBuffer, VertexBuffer, VertexBufferSizeBytes);
    Vulkan_TransferDataToGpuLocalMemory(GpuContext, Vk->CommandPool, Mesh.IndexBuffer, Indices, IndexBufferSizeBytes);

    ASSERT(Vk->MeshArray.Count < Vk->MeshArray.Capacity, "TODO: more meshes");
    u32 Handle = Vk->MeshArray.Count++;
    Vk->MeshArray.Data[Handle] = Mesh;
    return (renderer_mesh_handle) { Handle };
}


void Renderer_UpdateUniformBuffer(renderer *Vk, const void *Data, isize SizeBytes) 
{
    /* don't actually update now because the uniform might be in use by the gpu, 
     * wait until draw time to update, for now copy it into a tmp buffer */
    ASSERT(SizeBytes <= Vk->UniformBuffer.Capacity, "Data too big for uniform buffer");
    memcpy(Vk->UniformBuffer.Data, Data, SizeBytes);
    Vk->UniformBuffer.Count = SizeBytes;
    Vk->ShouldUpdateUniformBuffer = true;
}

renderer_mesh_handle Renderer_CreateMesh(renderer *Vk, isize VertexBufferSizeBytes, isize IndexBufferSizeBytes)
{
    vkm_buffer VertexBuffer = Vkm_CreateBuffer(&Vk->GpuContext.VkMalloc, VKM_MEMORY_TYPE_GPU_LOCAL, VertexBufferSizeBytes);
    vkm_buffer IndexBuffer = Vkm_CreateBuffer(&Vk->GpuContext.VkMalloc, VKM_MEMORY_TYPE_GPU_LOCAL, IndexBufferSizeBytes);

    ASSERT(Vk->MeshArray.Count < Vk->MeshArray.Capacity, "TODO: more meshes");
    u32 Handle = Vk->MeshArray.Count++;
    Vk->MeshArray.Data[Handle] = (vk_mesh) {
        .VertexBuffer = VertexBuffer,
        .IndexBuffer = IndexBuffer,
        .VertexBufferSizeBytes = VertexBufferSizeBytes,
        .IndexBufferSizeBytes = IndexBufferSizeBytes,
        .VertexCount = 0,
        .IndexCount = 0,
    };
    return (renderer_mesh_handle) { Handle };
}

renderer_result Renderer_UpdateMesh(
    renderer *Vk, 
    renderer_mesh_handle Handle,
    const void *VertexBuffer, isize VertexCount, isize VertexSizeBytes,
    const u32 *Indices, isize IndexCount
) {
    if (Vk->MeshArray.Count <= Handle.Value)
    {
        return RENDERER_ERROR_INVALID_HANDLE;
    }

    vk_mesh *Mesh = Vk->MeshArray.Data + Handle.Value;
    isize VertexBufferSizeBytes = VertexCount * VertexSizeBytes;
    isize IndexBufferSizeBytes = IndexCount * sizeof(Indices[0]);
    if (Mesh->IndexBufferSizeBytes < IndexBufferSizeBytes
    || Mesh->VertexBufferSizeBytes < VertexBufferSizeBytes)
    {
        return RENDERER_ERROR_OUT_OF_MEMORY;
    }

    Mesh->IndexCount = IndexCount;
    Mesh->VertexCount = VertexCount;
    Vulkan_TransferDataToGpuLocalMemory(&Vk->GpuContext, Vk->CommandPool, Mesh->VertexBuffer, VertexBuffer, VertexBufferSizeBytes);
    Vulkan_TransferDataToGpuLocalMemory(&Vk->GpuContext, Vk->CommandPool, Mesh->IndexBuffer, Indices, IndexBufferSizeBytes);
    return RENDERER_SUCCESS;
}

/* NOTE: Image layout must be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL */
internal void Vulkan_GenerateMipmap(
    const vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    VkFormat ImageFormat,
    VkImage Image, i32 TextureWidth, i32 TextureHeight, i32 MipLevels
) {
    if (MipLevels != 1)
    {
        VkFormatProperties FormatProperties;
        vkGetPhysicalDeviceFormatProperties(GpuContext->PhysicalDevice, ImageFormat, &FormatProperties);
        if (!(FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            RUNTIME_TODO("Implement software image resizing at runtime");
        }
    }

    VkCommandBuffer CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
    {
        VkImageLayout SourceImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        VkImageMemoryBarrier Barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .image = Image, 
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
                .levelCount = 1,
            },

        };
        /* generating mipmapping from base level (0) */
        i32 MipWidth = TextureWidth;
        i32 MipHeight = TextureHeight;
        for (int i = 1; i < MipLevels; i++)
        {
            int SourceBaseMipLevel = i - 1;
            int DestinationBaseMipLevel = i;
            int DestinationMipWidth = MAXIMUM(1, MipWidth/2);
            int DestinationMipHeight = MAXIMUM(1, MipHeight/2);

            /* Transfer mip level */
            VkImageLayout ImageLayoutForMipLevelTransition = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            {
                /* NOTE: transferring from DST_OPTIMAL to SRC_OPTIMAL via pipeline barrier,
                 * temporary transfer to begin mipmapping process */

                Barrier.subresourceRange.baseMipLevel = SourceBaseMipLevel;
                Barrier.oldLayout = SourceImageLayout;
                Barrier.newLayout = ImageLayoutForMipLevelTransition;
                Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(CmdBuf, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 
                    0, NULL, 
                    0, NULL, 
                    1, &Barrier
                );

                /* mipmap via blit image command */
                VkImageBlit BlitRegions = {
                    .srcOffsets = {
                        [0] = { 0 }, 
                        [1] = { .x = MipWidth, .y = MipHeight, .z = 1 }
                    },
                    .srcSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = SourceBaseMipLevel,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                    .dstOffsets = {
                        [0] = { 0 },
                        [1] = { .x = DestinationMipWidth, .y = DestinationMipHeight, .z = 1 }
                    },
                    .dstSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = DestinationBaseMipLevel,
                        .baseArrayLayer = 0, 
                        .layerCount = 1,
                    },
                };
                /* NOTE: if using dedicated transfer queue, the queue must have graphics capabilities for vkCmdBlitImage() to work */
                vkCmdBlitImage(CmdBuf, 
                    /* NOTE: src same as dst is ok to transition between mip levels */
                    Image, ImageLayoutForMipLevelTransition,
                    Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    1, 
                    &BlitRegions,
                    VK_FILTER_LINEAR
                );
            }

            /* final transfer for optimal shader access */
            {
                Barrier.oldLayout = ImageLayoutForMipLevelTransition;
                Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(CmdBuf, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 
                    0, NULL, 
                    0, NULL, 
                    1, &Barrier
                );
            }

            MipWidth = DestinationMipWidth;
            MipHeight = DestinationMipHeight;
        }

        /* NOTE: handle final mip level */
        {
            Barrier.subresourceRange.baseMipLevel = MipLevels - 1;
            Barrier.oldLayout = SourceImageLayout;
            Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(CmdBuf, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 
                0, NULL, 
                0, NULL, 
                1, &Barrier
            );
        }
    }
    Vulkan_EndSingleTimeCommandBuffer(GpuContext, CommandPool, CmdBuf);
}

renderer_texture_handle Renderer_UploadTexture(
    renderer *Vk, 
    const void *Data, u32 Width, u32 Height, u32 MipLevels,
    renderer_image_format Format
) {
    VkFormat ImageFormat = Vulkan_GetVkFormat(Format);
    isize ImageSize = Width * Height * sizeof(u32);
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    VkDevice Device = GpuContext->Device;
    VkPhysicalDevice PhysicalDevice = GpuContext->PhysicalDevice;
    VkCommandPool CommandPool = Vk->CommandPool;

    /* create texture on the gpu */
    vkm_image_handle TextureImageHandle;
    VkImage Image;
    {
        vkm_buffer StagingBuffer = GpuContext->StagingBuffer;
        isize StagingBufferSize = Vkm_Buffer_GetSizeBytes(StagingBuffer);
        ASSERT(ImageSize < StagingBufferSize, "Image too large");
        memcpy(GpuContext->StagingBufferPtr, Data, ImageSize);

        TextureImageHandle = Vkm_CreateImage(
            &GpuContext->VkMalloc,
            Width, Height, MipLevels,
            VK_SAMPLE_COUNT_1_BIT,
            ImageFormat,
            VK_IMAGE_TILING_OPTIMAL, /* optimal for shaders to access */
            VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT
        );
        Image = Vkm_Image_Get(&GpuContext->VkMalloc, TextureImageHandle).Handle;
        Vulkan_TransitionImageLayout(GpuContext, CommandPool,
            Image, 
            ImageFormat, 
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            MipLevels
        );
        Vulkan_CopyBufferToImage(GpuContext, CommandPool,
            Vkm_Buffer_GetVkBuffer(&GpuContext->VkMalloc, StagingBuffer),
            Image, Width, Height, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
#if 1
        Vulkan_TransitionImageLayout(GpuContext, CommandPool,
            Image, 
            ImageFormat,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            MipLevels
        );
#else
        Vulkan_GenerateMipmap(GpuContext, CommandPool, 
            ImageFormat, Image, Width, Height, MipLevels
        );
#endif
    }

    /* create an image view to the texture */
    VkImageView TextureImageView = Vulkan_CreateImageView(Device, Image, ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, MipLevels);

    /* create a sampler */
    VkSampler Sampler;
    {
        VkBool32 AnisotropyFilteringSupported = VK_FALSE;
        float AnisotropyFilteringMax = 1.0f;
        {
            VkPhysicalDevice Gpu = Vulkan_GetPhysicalDevice(Vk);
            VkPhysicalDeviceFeatures Features;
            VkPhysicalDeviceProperties Properties;
            vkGetPhysicalDeviceFeatures(Gpu, &Features);
            vkGetPhysicalDeviceProperties(Gpu, &Properties);
            AnisotropyFilteringSupported = Features.samplerAnisotropy;
            AnisotropyFilteringMax = Properties.limits.maxSamplerAnisotropy;
        }

        VkSamplerCreateInfo SamplerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            /* bilinear filtering:
             *  minification:  texels are bigger than screen pixels 
             *  magnification: screen pixels are bigger than texels */
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            /* for u,v,(w) coords that are outside of the texture (default 0..1) */
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .anisotropyEnable = AnisotropyFilteringSupported,
            .maxAnisotropy = AnisotropyFilteringMax,
            /* when sampling out of bound for VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER */
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
            /* for filtering */
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            /* mipmapping */
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .mipLodBias = 0.0f,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VK_CHECK(vkCreateSampler(Device, &SamplerCreateInfo, NULL, &Sampler));
    }

    /* push texture */
    renderer_texture_handle TextureHandle = { 0 };
    if (Vk->TextureArray.Count < Vk->TextureArray.Capacity)
    {
        TextureHandle.Value = Vk->TextureArray.Count;
        Vk->TextureArray.Data[TextureHandle.Value] = (vk_texture) {
            .Image = TextureImageHandle,
            .ImageView = TextureImageView,
            .Sampler = Sampler,
            /* TODO: store this width and height as the width and height capacity of this vkm_image in the texture */
        };
        Vk->TextureArray.Count++;
    }
    else
    {
        ASSERT(false, "Run out of memory for texture");
    }
    return TextureHandle;
}

bool32 Renderer_IsMSAASampleCountSupported(renderer_handle Vk, int SampleCount)
{
    switch (SampleCount)
    {
#define CASE(n) case n: if (Vk->Gpus.Selected.Properties.limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_##n##_BIT) return true; break
    CASE(1);
    CASE(2);
    CASE(4);
    CASE(8);
    CASE(16);
    CASE(32);
    CASE(64);
#undef CASE
    }
    return false;
}

internal VkSampleCountFlags Vulkan_GetVkSampleCountFlags(const vk_gpu *Gpu, int SampleCount)
{
    switch (SampleCount)
    {
#define CASE(n) case n: if (Gpu->Properties.limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_##n##_BIT) return VK_SAMPLE_COUNT_##n##_BIT; break
    CASE(1);
    CASE(2);
    CASE(4);
    CASE(8);
    CASE(16);
    CASE(32);
    CASE(64);
#undef CASE
    default: break;
    }
    UNREACHABLE_IF(true, "Invalid sample count");
}

void Renderer_CreateGraphicsPipelines(
    renderer *Vk, 
    isize UniformBufferCapacity, 
    int MSAASampleCount,
    int GraphicsPipelineCount,
    const renderer_graphics_pipeline_config *GraphicsPipelineConfigs, 
    renderer_graphics_pipeline_handle *OutGraphicsPipelineHandle
) {
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    VkDevice Device = GpuContext->Device;


    /* swapchain init */
    {
        int Width = Vk->Swapchain.Extent.width;
        int Height = Vk->Swapchain.Extent.height;

        Vk->MSAASample = Vulkan_GetVkSampleCountFlags(&Vk->Gpus.Selected, MSAASampleCount);
        {
            /* HACK: initially, we create depth and msaa resolve buffers with width and height of the monitor 
             *  then resize them to the current swapchain's size. This makes sure that when the window is resized, 
             *  the ColorResource and DepthBuffer will reuse its underlying VkDeviceMemory. 
             *  This might not work if the user has multiple monitors.
             * */
            platform_window_dimensions Max = Platform_Get(MonitorDimensions);
            Vk->ColorResource = Vulkan_CreateColorResource(GpuContext, Vk->MSAASample, Vk->Swapchain.ImageFormat, Max.Width, Max.Height);
            Vk->DepthBuffer = Vulkan_CreateDepthBuffer(GpuContext, Vk->CommandPool, Vk->MSAASample, Max.Width, Max.Height);
            Vk->ColorResource = Vkm_ImageAndView_Resize(&GpuContext->VkMalloc, Vk->ColorResource, Width, Height);
            Vk->DepthBuffer = Vulkan_ResizeDepthBuffer(GpuContext, Vk->CommandPool, Vk->DepthBuffer, Width, Height);
        }
        Vk->RenderPass = Vulkan_CreateRenderPass(Device, Vk->MSAASample, Vk->Swapchain.ImageFormat, 
            Vkm_Image_Get(&GpuContext->VkMalloc, Vk->DepthBuffer.ImageHandle).Format
        );
        Vulkan_CreateSwapchainImageViewAndFramebuffers(
            &Vk->SwapchainImage, 
            &Vk->Swapchain, 
            GpuContext, 
            Vk->RenderPass, 
            Vk->ColorResource.ImageView, 
            Vk->DepthBuffer.ImageView, 
            (platform_framebuffer_dimensions) { 
                .Width = Width,
                .Height = Height,
            },
            true,
            &Vk->Arena
        );
    }

    {
        int UniformBufferCount = 1;
        int TextureCount = Vk->TextureArray.Count;
        int UniformDescriptorPoolSize = Vk->FramesInFlight * UniformBufferCount;
        int TextureDescriptorPoolSize = Vk->FramesInFlight * TextureCount;
        Vk->DescriptorSetLayout = Vulkan_CreateDescriptorSetLayout(Device, UniformBufferCount, TextureCount);
        Vk->DescriptorPool = Vulkan_CreateDescriptorPool(Device, Vk->FramesInFlight, UniformDescriptorPoolSize, TextureDescriptorPoolSize);
    }

    Arena_AllocDynamicArray(&Vk->Arena, &Vk->UniformBuffer, 0, UniformBufferCapacity);
    Vk->FrameData = Vulkan_CreateFrameData(
        GpuContext, 
        Vk->CommandPool,
        Vk->DescriptorPool, 
        Vk->DescriptorSetLayout, 
        Vk->FramesInFlight, 
        &Vk->TextureArray,
        UniformBufferCapacity,
        &Vk->Arena
    );

    Arena_AllocDynamicArray(&Vk->Arena, &Vk->GraphicsPipelines, GraphicsPipelineCount + 1, GraphicsPipelineCount + 1);
    {
        for (int i = 0; i < GraphicsPipelineCount; i++)
        {
            const renderer_graphics_pipeline_config *Config = GraphicsPipelineConfigs + i;
            renderer_graphics_pipeline_handle Handle = { i + 1 };

            VkCullModeFlags CullMode = VK_CULL_MODE_NONE; 
            VkFrontFace FrontFace = 0;
            if (IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_BACKFACE_CULLING))
            {
                CullMode = VK_CULL_MODE_BACK_BIT;
                switch (Config->CullingDirection)
                {
                case RENDERER_CULLING_CLOCKWISE: FrontFace = VK_FRONT_FACE_CLOCKWISE; break;
                case RENDERER_CULLING_COUNTER_CLOCKWISE: FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; break;
                }
            }

            OutGraphicsPipelineHandle[i] = Handle;
            Vk->GraphicsPipelines.Data[Handle.Value] = Vulkan_CreateGraphicsPipeline(
                Vk->GpuContext.Device, 
                Vk->RenderPass, 
                &(vk_graphics_pipeline_config) {
                    .CullMode = CullMode,
                    .CullModeFrontFace = FrontFace,
                    .EnableBlending = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_BLENDING),
                    .EnableDepthTesting = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_Z_BUFFER), 
                    .EnableMSAA = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_MSAA), 
                    .MSAASampleCount = Vk->MSAASample,
                    .EnableSampleShading = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_SAMPLE_SHADING), 
                    .SampleShadingMin = Config->SampleShadingMin,
                },
                &Vk->DescriptorSetLayout, 1, 
                Config->FragShaderCode, Config->FragShaderCodeSizeBytes, 
                Config->VertShaderCode, Config->VertShaderCodeSizeBytes, 
                Config->VertexDescription, 
                Vk->Arena
            );
        }
    }
}

void Renderer_Destroy(renderer *Vk)
{
    VkInstance Instance = Vk->Instance;
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    VkDevice Device = GpuContext->Device;
    vk_frame_data *Frame = &Vk->FrameData;
    vkDeviceWaitIdle(Device);

    for (int i = 0; i < Vk->TextureArray.Count; i++)
    {
        vk_texture *Texture = Vk->TextureArray.Data + i;
        /* TODO: proper cleanup */
#if 0
        Vulkan_DestroyImage(Device, &Texture->DeviceMemory);
#endif
        vkDestroyImageView(Device, Texture->ImageView, NULL);
        vkDestroySampler(Device, Texture->Sampler, NULL);
    }
    for (int i = 0; i < Vk->FramesInFlight; i++)
    {
        /* nothing to do, Vkm_Destroy will deallocate all buffers */
        (void)Frame->UniformBufferArray;
        vkDestroyFence(Device, Frame->InFlightFenceArray[i], NULL);
        vkDestroySemaphore(Device, Frame->ImageAvailableSemaphoreArray[i], NULL);
    }
    vkDestroyDescriptorPool(Device, Vk->DescriptorPool, NULL);
    vkDestroyDescriptorSetLayout(Device, Vk->DescriptorSetLayout, NULL);
    /* nothing to do, Vkm_Destroy will deallocate all buffers */
    (void)Vk->MeshArray;
    Vulkan_DestroySwapchain(Device, &Vk->Swapchain); 
    Vulkan_DestroySwapchainImageAndFramebuffer(Device, &Vk->SwapchainImage, true);
    /* TODO: proper cleanup, defer this to the vkm */
    {
        vkDestroyImageView(Device, Vk->DepthBuffer.ImageView, NULL);
        vkDestroyImageView(Device, Vk->ColorResource.ImageView, NULL);
    }
    vkDestroyCommandPool(Device, Vk->CommandPool, NULL);
    dynamic_array_foreach(&Vk->GraphicsPipelines, i)
    {
        Vulkan_DestroyGraphicsPipeline(Device, i);
    }
    vkDestroyRenderPass(Device, Vk->RenderPass, NULL);
    vkDestroySurfaceKHR(Instance, Vk->WindowSurface, NULL);
    Vkm_Destroy(&GpuContext->VkMalloc);
    vkDestroyDevice(Device, NULL);
    DEBUG_ONLY(
        g_VkDestroyDebugReportCallbackEXT(Instance, Vk->DebugReportCallback, NULL);
    );
    vkDestroyInstance(Instance, NULL);

    /* arena owns Vk */
    Arena_Destroy(&Vk->Arena);
}

void Renderer_OnFramebufferResize(renderer *Renderer, int Width, int Height)
{
    (void)Width, (void)Height;
    Renderer->IsResized = true;
}

