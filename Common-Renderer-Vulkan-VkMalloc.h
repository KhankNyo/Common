#ifndef COMMON_RENDERER_VULKAN_VKMALLOC_H
#define COMMON_RENDERER_VULKAN_VKMALLOC_H

#include "Common.h"
#include "Common-Vulkan.h"
#include "Arena.h"
#include "FreeList.h"

#include "Containers.h"


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
typedef_struct(vkm_image_info);
typedef_struct(vkm_image_pool_entry);
typedef_struct(vkm_buffer_chunk);
typedef_struct(vkm_resize_image_config);
typedef dynamic_array(vkm_buffer_pool_entry) vkm_buffer_pool;
typedef dynamic_array(vkm_image_pool_entry) vkm_image_pool;



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
typedef handle(vkm_buffer_chunk *) vkm_buffer_handle;

typedef enum 
{
    VKM_BUFFER_TYPE_STAGING = 1,
    VKM_BUFFER_TYPE_UBO = 2,
    VKM_BUFFER_TYPE_VBO = 3,
    VKM_BUFFER_TYPE_EBO = 4,
    VKM_BUFFER_TYPE_SSBO = 5,
#define VKM_BUFFER_TYPE_COUNT 5
} vkm_buffer_type;

struct vkm_device_memory
{
    VkDeviceMemory Handle;
    VkMemoryPropertyFlags MemoryProperties;
    void *MappedMemory; /* for staging and UBO only */
    i32 CapacityAligned;
    u16 MapCount;
    i8 MemoryTypeIndex;
};
struct vkm_device_memory_node
{
    double_link(vkm_device_memory_node);
    i32 SizeAligned;
    i32 OffsetAligned;
    i16 DeviceMemoryIndex;
    i8 MemoryTypeIndex;
};
struct vkm_buffer_chunk
{
    double_link(vkm_buffer_chunk);
    i32 SizeAligned;
    i32 OffsetAligned;
    i32 EntryIndex;
};
struct vkm_buffer_pool_entry
{
    VkBuffer Buffer;
    vkm_device_memory_node *DeviceMemoryNode;
    i64 DeviceMemoryOffsetBytes; /* for vkMapMemory */
    i64 DeviceMemoryCapacityBytes;
    vkm_buffer_chunk *Allocated;
    vkm_buffer_chunk *Freed;
    vkm_buffer_chunk *LargestFree;
    u32 Alignment;
    VkBufferUsageFlags BufferUsageFlags;
    i16 DeviceMemoryIndex;
};
struct vkm_image_pool_entry
{
    VkImage Image;
    VkImageView ImageView;
    vkm_device_memory_node *DeviceMemoryNode;
    u32 Alignment;

    VkImageUsageFlags Usage;
    VkFormat Format;
    VkSampleCountFlagBits Sample;
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
    i64 LocalDeviceMemoryPoolCapacityBytes; /* gpu local */
    i64 TransDeviceMemoryPoolCapacityBytes; /* gpu-cpu */
    i64 BufferPoolCapacityBytes;
};
struct vkm
{
    arena_alloc *Arena;
    freelist_alloc FreeList;
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    i64 LocalDeviceMemoryPoolCapacityBytes;
    i64 TransDeviceMemoryPoolCapacityBytes;
    i64 BufferPoolCapacityBytes;

    dynamic_array(vkm_device_memory) DeviceMemory;

    vkm_device_memory_node *DeviceMemoryNodeUnused;
    vkm_buffer_chunk *BufferChunkUnused;
    vkm_device_memory_node *DeviceMemoryNodeAllocated[VK_MAX_MEMORY_TYPES];
    vkm_device_memory_node *DeviceMemoryNodeFree[VK_MAX_MEMORY_TYPES];
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
    VkSampleCountFlagBits Sample;
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
    VkSampleCountFlags Sample;
    VkFormat Format;
    VkImageUsageFlags Usage;
    VkImageTiling Tiling;
    VkImageAspectFlags Aspect;
};
struct vkm_resize_image_config
{
    VkSampleCountFlagBits Sample;   /* 0 implies the image's old sample */
    VkFormat Format;                /* VK_FORMAT_UNDEFINED (0) implies the image's old format */
    u16 MipLevels;                  /* 0 implies the image's old mip levels */
    u16 Width;
    u16 Height;
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

vkm_image_handle Vkm_ResizeImage(vkm *Vkm, vkm_image_handle ImageHandle, const vkm_resize_image_config *Config);
vkm_buffer_handle Vkm_ResizeBuffer(vkm *Vkm, vkm_buffer_handle BufferHandle, i64 NewSizeBytes);

/* map will only succeed if the buffer is VKM_BUFFER_TYPE_STAGING or VKM_BUFFER_TYPE_UBO */
void *Vkm_MapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle);
void Vkm_UnmapBufferMemory(vkm *Vkm, vkm_buffer_handle BufferHandle, void *MappedMemory);

vkm_buffer_info Vkm_GetBufferInfo(const vkm *Vkm, vkm_buffer_handle BufferHandle);
vkm_image_info Vkm_GetImageInfo(const vkm *Vkm, vkm_image_handle ImageHandle);

#endif /* COMMON_RENDERER_VULKAN_VKMALLOC_H */

