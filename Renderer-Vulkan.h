#ifndef RENDERER_VULKAN_H
#define RENDERER_VULKAN_H

#include "Common.h"
#include "Memory.h"
#include "Renderer-Core.h"
#include "Profiler.h"
#include "Vulkan.h"


#define QUEUE_FAMILY_INVALID_INDEX -1
typedef i64 vk_queue_family_index;
#define BINDING_COUNT 2
#define BINDING_TYPE_UNIFORM_BUFFER 0
#define BINDING_TYPE_COMBINED_IMAGE_SAMPLER 1

typedef_struct(vk_gpu);
typedef_struct(vk_gpu_list);
typedef_struct(vk_gpu_context);
typedef_struct(vk_physical_devices);
typedef_struct(vk_queue_family_indices);
typedef_struct(vk_swapchain_support_config);
typedef_struct(vk_swapchain);
typedef_struct(vk_graphics_pipeline);

typedef_struct(vk_texture);
typedef dynamic_array(VkSurfaceFormatKHR) vk_surface_format_array;
typedef dynamic_array(VkPresentModeKHR) vk_present_mode_array;
typedef dynamic_array(VkImage) vk_image_array;
typedef dynamic_array(VkImageView) vk_image_view_array;
typedef dynamic_array(VkFramebuffer) vk_framebuffer_array; 
typedef dynamic_array(vk_texture) vk_texture_array;
typedef_struct(vk_frame_data);
typedef_struct(vk_device_memory_image);
typedef_struct(vk_depth_buffer);
typedef_struct(vk_swapchain_image);
typedef_struct(vk_color_resource);
typedef_struct(vk_mesh);
typedef_struct(vk_uniform_buffer);


typedef_struct(vkm);
typedef_struct(vkm_buffer);
typedef_struct(vkm_buffer_pool);
typedef_struct(vkm_buffer_pool_slot);


#define VKM_POOL_SLOT_COUNT (1 << 6)
#define VKM_BUFFER_MIN_ALIGNMENT 4096
#define VKM_BUFFER_MAX_OFFSET (((1llu << 28) - 1) * VKM_BUFFER_MIN_ALIGNMENT)
#define VKM_BUFFER_MAX_SIZEBYTES (((1llu << 28) - 1) * VKM_BUFFER_MIN_ALIGNMENT)
struct vkm_buffer
{
    /* Info[63:62] -> unsigned 2-bit vkm_memory_type tag
     * Info[62:56] -> unsigend 6-bit pool index
     * Info[55:28] -> unsigned 28-bit buffer offset, aligned by 4096 boundary
     * Info[27: 0] -> unsigned 28-bit buffer size, aligned by 4096 boundary */
    u64 Info;
    /* max offset: 2^28 * 4096 -> 0..2^40 -> 1024GB (1TB) offset */
    /* max size:   2^28 * 4096 -> 0..2^40 -> 1024GB (1TB) memory */
    /* max pool index: 2^8 -> 256 */


    /* init */
#define Vkm__Buffer_Init(memory_type, pool_index, offset_bytes, size_bytes) (vkm_buffer) {.Info = \
        (u64)(memory_type) << 62\
        | (u64)(pool_index) << 56\
        | (((u64)(offset_bytes)/VKM_BUFFER_MIN_ALIGNMENT) & 0x0FFFFFFF) << 28\
        | (((u64)(size_bytes)/VKM_BUFFER_MIN_ALIGNMENT) & 0x0FFFFFFF) << 0\
    }
    /* getters, you'll need 'em */
#define Vkm_Buffer_GetMemoryType(vkm_buf) ((vkm_buf).Info >> 62)
#define Vkm_Buffer_GetPoolIndex(vkm_buf) (((vkm_buf).Info >> 56) & 0x3F)
#define Vkm_Buffer_GetOffsetBytes(vkm_buf) ((((vkm_buf).Info >> 28) & 0x0FFFFFFFu) * VKM_BUFFER_MIN_ALIGNMENT)
#define Vkm_Buffer_GetSizeBytes(vkm_buf) (((vkm_buf).Info & 0x0FFFFFFFu) * VKM_BUFFER_MIN_ALIGNMENT)
};

typedef handle(u32) vkm_image_handle;
#define Vkm_Image_Get(p_vkm, vkm_image_handle_) \
    (p_vkm)->Images[(vkm_image_handle_).Value]

typedef_struct(vkm_image);
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

typedef_struct(vkm_image_and_view);
struct vkm_image_and_view
{
    vkm_image_handle ImageHandle;
    VkImageView ImageView;
    VkImageAspectFlags Aspect;
};

typedef enum 
{
    VKM_MEMORY_TYPE_CPU_VISIBLE = 0,
    VKM_MEMORY_TYPE_GPU_LOCAL = 1,
    VKM_MEMORY_TYPE_COUNT = 2,
} vkm_memory_type;
typedef_struct(vkm_device_memory);
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


struct vk_mesh
{
    vkm_buffer VertexBuffer;
    vkm_buffer IndexBuffer;
    isize VertexBufferSizeBytes, IndexBufferSizeBytes;
    isize VertexCount;
    isize IndexCount;
};

typedef enum 
{
    QUEUE_FAMILY_TYPE_GRAPHICS = 0,
    QUEUE_FAMILY_TYPE_PRESENT,
    QUEUE_FAMILY_TYPE_COUNT,
} vk_queue_family_indices_type;
struct vk_queue_family_indices 
{
    vk_queue_family_index Type[QUEUE_FAMILY_TYPE_COUNT];
};
struct vk_gpu
{
    VkPhysicalDevice Handle;
    VkPhysicalDeviceProperties Properties;
    VkPhysicalDeviceFeatures Features;
};
struct vk_gpu_list
{
    u32 Count; 
    VkPhysicalDevice *HandleList; 
    VkPhysicalDeviceProperties *PropertiesList;
    VkPhysicalDeviceFeatures *FeaturesList;
};


struct vk_swapchain_support_config
{
    VkSurfaceCapabilitiesKHR Capabilities;
    vk_surface_format_array Formats;
    vk_present_mode_array PresentModes;
};
struct vk_texture
{
    vkm_image_handle Image;
    VkImageView ImageView;
    VkSampler Sampler;
};
struct vk_graphics_pipeline
{
    VkPipelineLayout Layout;
    VkPipeline Handle;
};
struct renderer
{
    arena_alloc Arena; /* owns the renderer */

    VkInstance Instance;
    VkDebugReportCallbackEXT DebugReportCallback;
    VkSurfaceKHR WindowSurface;
    struct vk_physical_devices {
        vk_gpu_list List;
        vk_gpu Selected;
    } Gpus;
    struct vk_gpu_context {
        u32 QueueFamilyIndex[QUEUE_FAMILY_TYPE_COUNT];
        VkPhysicalDevice PhysicalDevice;
        VkPhysicalDeviceMemoryProperties MemoryProperties;
        VkDevice Device;
        VkQueue GraphicsQueue;
        VkQueue PresentQueue;

        vkm VkMalloc;
        vkm_buffer StagingBuffer;
        void *StagingBufferPtr;

        profiler *Profiler;
    } GpuContext;
    struct vk_swapchain { 
        VkSwapchainKHR Handle;
        VkExtent2D Extent;
        VkFormat ImageFormat;
        VkPresentModeKHR PresentMode;

        arena_context ArenaContext;
    } Swapchain;
    struct vk_swapchain_image {
        u32 Count;
        VkImage *Array;
        VkImageView *ViewArray;
        VkFramebuffer *FramebufferArray;

        VkSemaphore *RenderFinishedSemaphoreArray;
    } SwapchainImage;

    VkRenderPass RenderPass;

    /* renderer_resource_handle */
    dynamic_array(VkDescriptorSetLayout) DescriptorSetLayouts;

    /* renderer_graphics_pipeline_handle */
    dynamic_array(vk_graphics_pipeline) GraphicsPipelines;

    VkCommandPool CommandPool;
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorPool DescriptorPool;
    struct vk_frame_data
    {
        VkFence *InFlightFenceArray;
        VkSemaphore *ImageAvailableSemaphoreArray;
        VkCommandBuffer *CommandBufferArray;
        VkDescriptorSet *DescriptorSetArray;
        vkm_buffer *UniformBufferArray;
        void **UniformMappedMemoryArray;
    } FrameData;
    int CurrentFrame;
    int FramesInFlight;

    vkm_image_and_view DepthBuffer;
    vkm_image_and_view ColorResource;

    VkSampleCountFlagBits MSAASample;

    vk_texture_array TextureArray;
    /* renderer_mesh_handle */
    dynamic_array(vk_mesh) MeshArray;
    dynamic_array(u8) UniformBuffer;
    bool8 ShouldUpdateUniformBuffer;
    bool8 IsResized;
    bool8 ForceTripleBuffering;

    profiler *Profiler;
};

/* NOTE: platform must implement these functions: */
VkResult Vulkan_Platform_CreateWindowSurface(VkInstance Instance, VkAllocationCallbacks *AllocCallback, VkSurfaceKHR *OutWindowSurface);
typedef_struct(vulkan_platform_instance_extensions);
struct vulkan_platform_instance_extensions {
    u32 Count;
    const char **StringPtrArray;
};
vulkan_platform_instance_extensions Vulkan_Platform_GetInstanceExtensions(void);

#endif /* RENDERER_VULKAN_H */
