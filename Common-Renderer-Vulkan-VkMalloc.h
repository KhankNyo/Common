#ifndef COMMON_RENDERER_VULKAN_VKMALLOC_H
#define COMMON_RENDERER_VULKAN_VKMALLOC_H

#include "Common.h"
#include "Common-Vulkan.h"

#define NEW_VKM_API


#ifdef NEW_VKM_API
#include "Arena.h"
#include "FreeList.h"

#ifndef VKM_MIN_ALIGNMENT
#  define VKM_MIN_ALIGNMENT 64
#endif /* VKM_MIN_ALIGNMENT */

#define VKM_MAX_BUFFER_INDEX 255
#define VKM_IMAGE_MEMORY_PROPERTY VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT

typedef_struct(vkm);
typedef_struct(vkm_config);
typedef_struct(vkm_image_config);
typedef_struct(vkm_image_pool_entry);
typedef_struct(vkm_buffer_config);
typedef_struct(vkm_buffer_pool_entry);
typedef_struct(vkm_buffer_info);
typedef_struct(vkm_device_memory_node);
typedef_struct(vkm_device_memory);


/*
 * Value[7:0]   -> u8 IndexIntoImagePool
 * Value[35:8]  -> u28 MemoryOffsetAligned  // offset in bytes / VKM_MIN_BUFFER_ALIGNMENT
 * Value[63:36] -> u28 MemorySizeAligned    // size in bytes / VKM_MIN_BUFFER_ALIGNMENT
 */
typedef handle(u64) vkm_image_handle;

/*
 * Value[7:0]   -> u8 IndexIntoBufferPool
 * Value[35:8]  -> u28 MemoryOffsetAligned  // offset in bytes / VKM_MIN_BUFFER_ALIGNMENT
 * Value[63:36] -> u28 MemorySizeAligned    // size in bytes / VKM_MIN_BUFFER_ALIGNMENT
 */
typedef handle(u64) vkm_buffer_handle;

typedef dynamic_array(vkm_buffer_pool_entry) vkm_buffer_pool;
typedef dynamic_array(vkm_image_pool_entry) vkm_image_pool;
typedef_struct(vkm_image_info);
typedef_struct(vkm_image_pool_entry);


typedef enum 
{
    VKM_BUFFER_TYPE_STAGING = 1,
    VKM_BUFFER_TYPE_UBO = 2,
    VKM_BUFFER_TYPE_VBO = 3,
    VKM_BUFFER_TYPE_EBO = 4,
    VKM_BUFFER_TYPE_SSBO = 5,
#define VKM_BUFFER_TYPE_COUNT 6
} vkm_buffer_type;

struct vkm_device_memory
{
    VkDeviceMemory Handle;
    VkMemoryPropertyFlags MemoryProperties;
    i32 CapacityAligned;
    i8 MemoryTypeIndex;
};
struct vkm_device_memory_node
{
    vkm_device_memory_node *Next;
    i32 RemainAligned;
    i16 DeviceMemoryIndex;
    i8 MemoryTypeIndex;
};
struct vkm_buffer_pool_entry
{
    VkBuffer Buffer;
    vkm_device_memory_node *DeviceMemoryNode;
    VkBufferUsageFlags BufferUsageFlags;
    u32 Alignment;
    i16 DeviceMemoryIndex;
};
struct vkm_image_pool_entry
{
    VkImage Image;
    VkImageView ImageView;
    u32 Alignment;

    VkImageUsageFlags Usage;
    VkFormat Format;
    VkSampleCountFlagBits Samples;
    VkImageTiling Tiling;
    VkImageAspectFlags Aspect;

    i16 DeviceMemoryIndex;
    u16 Width;
    u16 Height;
    u8 PixelSizeBytes;
    u8 MipLevels;
};
struct vkm_config
{
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    i64 DeviceMemoryPoolCapacityBytes;
};
struct vkm
{
    arena_alloc *Arena;
    freelist_alloc FreeList;
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    i64 DeviceMemoryPoolCapacityBytes;

    dynamic_array(vkm_device_memory) DeviceMemory;

    vkm_device_memory_node *DeviceMemoryNodeFree;
    vkm_device_memory_node *DeviceMemoryPoolHead[VK_MAX_MEMORY_TYPES];
    vkm_buffer_pool BufferPool;     /* owns VkBuffer */
    vkm_image_pool ImagePool;       /* owns VkImage, VkImageView */
};


struct vkm_image_info
{
    VkImage Image;
    VkImageView ImageView;

    VkDeviceMemory DeviceMemory;
    i64 OffsetBytes;
    i64 CapacityBytes;
    u32 Alignment;
    i32 MemoryTypeIndex;

    VkImageUsageFlags Usage;
    VkFormat Format;
    VkSampleCountFlagBits Samples;
    VkImageTiling Tiling;
    VkImageAspectFlags Aspect;

    u16 PixelSizeBytes;
    u16 Width;
    u16 Height;
    u16 MipLevels;
};
struct vkm_buffer_info 
{
    VkBuffer Buffer;
    VkDeviceMemory DeviceMemory;
    VkBufferUsageFlags BufferUsage;
    VkMemoryPropertyFlags MemoryProperties;
    int MemoryTypeIndex;
    i64 OffsetBytes;
    i64 CapacityBytes;
};


struct vkm_image_config
{
    i64 MemoryCapacityPixels;       /* 0 implies that MemoryCapacityPixels = Width*Height */
    u16 Width;
    u16 Height;
    u16 MipLevels;                  /* 0 implies default mip level (1) */
    VkSampleCountFlags Samples;
    VkFormat Format;
    VkImageUsageFlags Usage;
    VkImageTiling Tiling;
    VkImageAspectFlags Aspect;
};
struct vkm_buffer_config
{
    i64 MemoryCapacityBytes;
    vkm_buffer_type BufferType;
};


void Vkm_Create(vkm *Vkm, arena_alloc *Arena, const vkm_config *Config);
void Vkm_Destroy(vkm *Vkm);
void Vkm_Reset(vkm *Vkm);

vkm_buffer_handle Vkm_CreateBuffer(vkm *Vkm, const vkm_buffer_config *Config);
vkm_image_handle Vkm_CreateImage(vkm *Vkm, const vkm_image_config *Config);

vkm_image_handle Vkm_ResizeImage(vkm *Vkm, vkm_image_handle ImageHandle, u16 NewWidth, u16 NewHeight);

/* map will only succeed if the buffer is VKM_BUFFER_TYPE_STAGING or VKM_BUFFER_TYPE_UBO */
void *Vkm_MapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle);
void Vkm_UnmapBufferMemory(vkm *Vkm, void *MappedMemory);

vkm_buffer_info Vkm_GetBufferInfo(const vkm *Vkm, vkm_buffer_handle BufferHandle);
vkm_image_info Vkm_GetImageInfo(const vkm *Vkm, vkm_image_handle ImageHandle);

#else


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

    /* NOTE: for VkImage only */
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


void Vkm_Create(
    vkm *Vkm, 
    VkDevice Device, VkPhysicalDevice PhysicalDevice, 
    i64 ImageMemoryPoolCapacityBytes,
    i64 BufferMemoryPoolCapacityBytes[VKM_MEMORY_TYPE_COUNT],
    VkBufferUsageFlags DefaultUsages[VKM_MEMORY_TYPE_COUNT],
    VkMemoryPropertyFlags DefaultMemoryProperties[VKM_MEMORY_TYPE_COUNT]
);
void Vkm_Destroy(vkm *Vkm);
void Vkm_Reset(vkm *Vkm);

vkm_buffer Vkm_CreateBuffer(vkm *Vkm, vkm_memory_type MemoryType, isize BufferSizeBytes);
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

VkDeviceMemory Vkm_Buffer_GetVkDeviceMemory(const vkm *Vkm, vkm_buffer Buffer);
VkBuffer Vkm_Buffer_GetVkBuffer(const vkm *Vkm, vkm_buffer Buffer);
void *Vkm_Buffer_GetMappedMemory(vkm *Vkm, vkm_buffer Buffer);

vkm_image_and_view Vkm_ImageAndView_Resize(vkm *Vkm, vkm_image_and_view Iav, u32 NewWidth, u32 NewHeight);
#endif

#endif /* COMMON_RENDERER_VULKAN_VKMALLOC_H */

