#ifndef COMMON_RENDERER_VULKAN_H
#define COMMON_RENDERER_VULKAN_H

#include "Common.h"
#include "Renderer-Core.h"
#include "Profiler.h"
#include "Slice.h"

#include "Memory.h"
#include "FreeList.h"
#include "Arena.h"

#include "Common-Vulkan.h"
#include "Common-Renderer-Vulkan-VkMalloc.h"

#include "Platform-Core.h"


/* TODO: move this somewhere else */
#define VkDynamicArray_ResizeCapacity(p_freelist, p_da, isize_new_capacity) do {\
    (p_da)->Capacity = isize_new_capacity;\
    FreeList_ReallocArray(p_freelist, &(p_da)->Data, (p_da)->Capacity);\
} while (0)
#define VkDynamicArray_Push(p_freelist, p_da, ...) do {\
    typeof(p_freelist) freelist_ = p_freelist;\
    typeof(p_da) dynamic_array_ = p_da;\
    if (dynamic_array_->Count >= dynamic_array_->Capacity) {\
        VkDynamicArray_ResizeCapacity(freelist_, dynamic_array_, dynamic_array_->Capacity == 0? 32 : dynamic_array_->Capacity * 2);\
    }\
    dynamic_array_->Data[dynamic_array_->Count] = __VA_ARGS__;\
    dynamic_array_->Count++;\
} while (0)



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
typedef dynamic_array(vk_texture) vk_texture_array;
typedef dynamic_array(VkSurfaceFormatKHR) vk_surface_format_array;
typedef dynamic_array(VkPresentModeKHR) vk_present_mode_array;
typedef dynamic_array(VkImage) vk_image_array;
typedef dynamic_array(VkImageView) vk_image_view_array;
typedef dynamic_array(VkFramebuffer) vk_framebuffer_array; 
typedef_struct(vk_frame_data);
typedef_struct(vk_device_memory_image);
typedef_struct(vk_swapchain_image);
typedef_struct(vk_mesh);
typedef_struct(vk_uniform_buffer);
typedef_struct(vk_render_target);
typedef_struct(vk_render_frame);
typedef_struct(vk_resource_group);


struct vk_texture
{
    vkm_image_handle Image;
    VkImageView ImageView;
    VkSampler SamplerReference;
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
    vk_surface_format_array Formats;
    vk_present_mode_array PresentModes;
};
/* NOTE: every item (texture, samplers, graphics pipelines) allocated using vk_resource_group is a u64 consisting of: 
 *      Value[63:log2(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT)] -> bottom bits are zeroed to get a pointer to vk_resource_group
 *      Value[log2(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT)-1:0] -> index of the item */
struct vk_resource_group
{
    vk_resource_group *Next, *Prev;
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

typedef_struct(vk_update_resource);
struct vk_update_resource
{
    vk_update_resource *Next;
    void **UniformBuffersDst;
    void *UniformBufferSrc;
    isize UniformBufferSizeBytes;
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
    bool8 ForceTripleBuffering;

    vk_resource_group *ResourceGroupHead, 
                      *ResourceGroupFreeSlots, 
                      *GlobalResourceGroup;
    vk_update_resource *UpdateResourceHead,
                       *UpdateResourceFree;

    platform_thread_handle RecreateSwapchainAndRenderTargetThread;
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
