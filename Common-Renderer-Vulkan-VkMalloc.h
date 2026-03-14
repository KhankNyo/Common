#ifndef COMMON_RENDERER_VULKAN_VKMALLOC_H
#define COMMON_RENDERER_VULKAN_VKMALLOC_H

#include "Common.h"
#include "Common-Vulkan.h"

typedef_struct(vkm);
typedef_struct(vkm_buffer);
typedef_struct(vkm_buffer_pool);
typedef_struct(vkm_buffer_pool_slot);
typedef_struct(vkm_image);
typedef_struct(vkm_image_and_view);
typedef_struct(vkm_device_memory);
typedef handle(u32) vkm_image_handle;


#define VKM_POOL_SLOT_COUNT (1 << 6)
#define VKM_BUFFER_MIN_ALIGNMENT (1u << 8)
#define VKM_BUFFER_MAX_OFFSET (((1llu << 28) - 1) * VKM_BUFFER_MIN_ALIGNMENT)
#define VKM_BUFFER_MAX_SIZEBYTES (((1llu << 28) - 1) * VKM_BUFFER_MIN_ALIGNMENT)

/* vkm_buffer getters, you'll need 'em */
#define Vkm_Buffer_GetMemoryType(vkm_buf) ((vkm_buf).Info >> 62)
#define Vkm_Buffer_GetPoolIndex(vkm_buf) (((vkm_buf).Info >> 56) & 0x3F)
#define Vkm_Buffer_GetOffsetBytes(vkm_buf) ((((vkm_buf).Info >> 28) & 0x0FFFFFFFu) * VKM_BUFFER_MIN_ALIGNMENT)
#define Vkm_Buffer_GetSizeBytes(vkm_buf) (((vkm_buf).Info & 0x0FFFFFFFu) * VKM_BUFFER_MIN_ALIGNMENT)

/* vkm_image_handle getter */
#define Vkm_Image_Get(p_vkm, vkm_image_handle_) \
    (p_vkm)->Images[(vkm_image_handle_).Value]


typedef enum 
{
    VKM_MEMORY_TYPE_CPU_VISIBLE = 0,
    VKM_MEMORY_TYPE_GPU_LOCAL = 1,
#define VKM_MEMORY_TYPE_COUNT 2
} vkm_memory_type;


struct vkm_buffer
{
    /* Info[63:62] -> unsigned 2-bit vkm_memory_type tag
     * Info[62:56] -> unsigend 6-bit pool index
     * Info[55:28] -> unsigned 28-bit buffer offset, aligned by VKM_BUFFER_MIN_ALIGNMENT boundary
     * Info[27: 0] -> unsigned 28-bit buffer size, aligned by VKM_BUFFER_MIN_ALIGNMENT boundary */
    u64 Info;
};

struct vkm_image 
{
    i64 MemoryOffset;
    i64 Capacity;
    VkImage Handle;
    VkDeviceMemory MemoryHandle;
    VkFormat Format;
    VkSampleCountFlagBits Samples;
    VkImageTiling Tiling;
    VkImageUsageFlags Usage;
    u32 Width, Height;
    u32 MipLevels;
};

struct vkm_image_and_view
{
    vkm_image_handle ImageHandle;
    VkImageView ImageView;
    VkImageAspectFlags Aspect;
};

struct vkm
{
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;

    isize NewDeviceMemoryCapacity;
    i32 DeviceMemoryCount;
    struct vkm_device_memory {
        VkDeviceMemory Handle;
        isize Capacity;
        isize Offset;
        int TypeIndex;
    } DeviceMemory[256];

    i32 ImageCount;
    vkm_image Images[256];

    struct vkm_buffer_pool {
        /* default flags for all buffers specified during Vkm_Init() */
        VkBufferUsageFlags DefaultUsages;
        VkMemoryPropertyFlags DefaultMemoryProperties;

        /* Capacity of VkBuffer in BufferPool[x] when it is created,
         * may be overriden if the allocator needs to 
         * allocate a buffer with a bigger size in Vkm_AllocateBuffer() */
        i64 ResetCapacity;

        /* This should be a small value since 
         * individual buffers should be big (>=128mb). */
        i32 SlotCount;

        /* The pool that holds handles to the underlying 
         * buffer and device memory for each allocation. 
         * Capacity <= BufferPoolResetCapacity */
        struct vkm_buffer_pool_slot
        {
            /* TODO: decouple VkBuffer from VkDeviceMemory, use an indirect handle
             * so that VkImage can share VkDeviceMemory */
            VkBuffer BufferHandle;
            VkDeviceMemory MemoryHandle;
            u8 *MappedMemory; /* NOTE: only for VKM_MEMORY_TYPE_CPU_VISIBLE */
            i64 SizeBytesRemain;
            i64 Capacity;
            u64 Alignment;
        } Slot[VKM_POOL_SLOT_COUNT];
    } BufferPool[VKM_MEMORY_TYPE_COUNT];
};


void Vkm_Init(
    vkm *Vkm, 
    VkDevice Device, VkPhysicalDevice PhysicalDevice, 
    i64 ImageMemoryPoolCapacityBytes,
    i64 ResetCapacity[VKM_MEMORY_TYPE_COUNT],
    VkBufferUsageFlags DefaultUsages[VKM_MEMORY_TYPE_COUNT],
    VkMemoryPropertyFlags DefaultMemoryProperties[VKM_MEMORY_TYPE_COUNT]
);
void Vkm_Destroy(vkm *Vkm);

vkm_buffer Vkm_CreateBuffer(vkm *Vkm, vkm_memory_type MemoryType, isize BufferSizeBytes);
VkDeviceMemory Vkm_Buffer_GetVkDeviceMemory(const vkm *Vkm, vkm_buffer Buffer);
VkBuffer Vkm_Buffer_GetVkBuffer(const vkm *Vkm, vkm_buffer Buffer);
void *Vkm_Buffer_GetMappedMemory(vkm *Vkm, vkm_buffer Buffer);

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
);
vkm_image_and_view Vkm_CreateImageAndView(
    vkm *Vkm, 
    u32 Width, u32 Height, u32 MipLevels,
    VkSampleCountFlagBits Samples,
    VkFormat Format, 
    VkImageTiling Tiling, 
    VkImageUsageFlags Usage, 
    VkImageAspectFlags Aspect
);
vkm_image_and_view Vkm_ImageAndView_Resize(vkm *Vkm, vkm_image_and_view Iav, u32 NewWidth, u32 NewHeight);

#endif /* COMMON_RENDERER_VULKAN_VKMALLOC_H */

