#ifndef COMMON_RENDERER_VULKAN_H
#define COMMON_RENDERER_VULKAN_H

#include "Common.h"
#include "Renderer-Core.h"
#include "Profiler.h"

#include "Memory.h"
#include "FreeList.h"
#include "Arena.h"
#include "Containers.h"

#include "Common-Vulkan.h"
#include "Common-Renderer-Vulkan-VkMalloc.h"

#include "Platform-Core.h"



#define QUEUE_FAMILY_INVALID_INDEX -1
typedef i64 vk_queue_family_index;

typedef_struct(vk_gpu);
typedef_struct(vk_gpu_list);
typedef_struct(vk_gpu_context);
typedef_struct(vk_physical_devices);
typedef_struct(vk_queue_family_indices);
typedef_struct(vk_swapchain_support_config);
typedef_struct(vk_swapchain);
typedef_struct(vk_graphics_pipeline);

typedef_struct(vk_texture);
typedef_struct(vk_mesh);
typedef_struct(vk_render_target);
typedef_struct(vk_render_frame);
typedef_struct(vk_resource_group);

typedef_struct(vk_update_resource);
typedef_struct(vk_update_ubo);
typedef_struct(vk_update_texture);


typedef enum 
{
    VULKAN_TEXTURE_STATE_UNDEFINED = 0,
    VULKAN_TEXTURE_STATE_SHADER_READONLY = 1,
    VULKAN_TEXTURE_STATE_TRANSFER_DST = 2,
} vk_texture_state;

struct vk_texture
{
    vk_texture_state State;
    vkm_image_handle Image;
    VkImageView ImageView;
    VkSampler SamplerReference;
    void *ImageBuffer;
    isize ImageBufferSizeBytes;
};

struct vk_graphics_pipeline
{
    VkPipelineLayout Layout;
    VkPipeline Handle;
};



struct vk_mesh
{
    vk_resource_group *Owner;
    vkm_buffer_handle VertexBuffer;
    vkm_buffer_handle IndexBuffer;
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
    slice(VkSurfaceFormatKHR) Formats;
    slice(VkPresentModeKHR) PresentModes;
};
/* NOTE: every item (texture, samplers, graphics pipelines) allocated using vk_resource_group is a u64 consisting of: 
 *      Value[63:log2(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT)] -> bottom bits are zeroed to get a pointer to vk_resource_group
 *      Value[log2(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT)-1:0] -> index of the item */
struct vk_resource_group
{
    double_link(vk_resource_group);
    vkm GpuAllocator;
    arena_alloc CpuArena;
    freelist_alloc CpuAllocator; /* NOTE: free list is owned by the arena */

    /* samplers and textures are owned by the GpuAllocator */
    dynamic_array(VkSampler) Samplers;
    dynamic_array(vk_texture) Textures;
    dynamic_array(vk_graphics_pipeline) GraphicsPipelines;

    u32 UniformBufferBinding;
    u32 TextureArrayBinding;

    /* NOTE: there are Vk->FramesInFlight amount of uniform buffers */
    vkm_buffer_handle *UniformBuffers;
    void **UniformBuffersMapped;
    u8 *UniformBufferTmp;
    i32 UniformBufferTmpCapacity;

    vkm_buffer_handle StagingBuffer;
    vkm_buffer_info StagingBufferInfo;
    void *StagingBufferMapped;

    VkDescriptorPool DescriptorPool;
    VkDescriptorSetLayout DescriptorSetLayout;
    /* NOTE: there are Vk->FramesInFlight amount of DescriptorSets */
    VkDescriptorSet *DescriptorSets;
};

typedef enum 
{
    VULKAN_UPDATE_RESOURCE_UBO = 1,
    VULKAN_UPDATE_RESOURCE_TEXTURE = 2,
} vk_update_resource_type;

struct vk_update_resource
{
    single_link(vk_update_resource);
    vk_update_resource_type Type;

    union {
        struct vk_update_ubo {
            void **Dsts;
            void *Src;
            isize SizeBytes;
            int FramesUpdatedCount;
        } UniformBuffer;
        struct vk_update_texture {
            renderer_texture_handle Handle;
            u32 NewWidth, NewHeight;
            int MipLevels;
            bool8 GenerateMipmap;
        } Texture;
    } As;
};


struct renderer
{
    arena_alloc Arena; /* owns the renderer */

    VkInstance Instance;

    PFN_vkDestroyDebugReportCallbackEXT VkDestroyDebugReportCallback;
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

        profiler *Profiler;
    } GpuContext;
    bool8 IsResized;

    vk_resource_group *ResourceGroupHead, 
                      *ResourceGroupFreeSlots, 
                      *GlobalResourceGroup;
    vk_update_resource *UpdateResourceHead,
                       *UpdateResourceFree;
    i64 StagingBufferRequiredSize;

    platform_thread_handle RecreateSwapchainAndRenderTargetThread;
    VkSurfaceFormatKHR SwapchainSurfaceFormat;
    VkPresentModeKHR SwapchainPresentMode;
    struct vk_swapchain { 
        VkSwapchainKHR Handle;
        int Width;
        int Height;
        VkFormat ImageFormat;
        VkPresentModeKHR PresentMode;
    } Swapchain;

    struct vk_render_target {
        VkSampleCountFlagBits SampleCount;

        u32 ImageCount;
        VkFramebuffer *Framebuffers;
        VkImage *SwapchainImages;
        VkImageView *SwapchainImageViews;
        VkSemaphore *GpuWaitForRenderFrame;

        VkRenderPass RenderPass;
        vkm_image_handle DepthResource;
        vkm_image_handle ColorResource;
    } RenderTarget;

    VkCommandPool CommandPool;
    struct vk_render_frame {
        VkCommandBuffer *CommandBuffers;
        VkFence *CpuWaitThisFrame;
        VkSemaphore *GpuWaitForRenderTarget;
    } RenderFrame;
    int CurrentFrame;
    int FramesInFlight;

    profiler *Profiler;
};

/* NOTE: platform must implement these functions: */
VkResult Vulkan_Platform_CreateWindowSurface(VkInstance Instance, VkAllocationCallbacks *AllocCallback, VkSurfaceKHR *OutWindowSurface);
typedef_struct(vulkan_platform_instance_extensions);
struct vulkan_platform_instance_extensions 
{
    u32 Count;
    const char **StringPtrArray;
};
vulkan_platform_instance_extensions Vulkan_Platform_GetInstanceExtensions(void);

#endif /* COMMON_RENDERER_VULKAN_H */
