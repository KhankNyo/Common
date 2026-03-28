
#include <string.h>
#include <stdarg.h>

#include "Common.h"
#include "Platform-Core.h"
#include "Renderer-Core.h"
#include "Profiler.h"

#include "Common-Renderer-Vulkan.h"
#include "Common-Vulkan.h"


#define VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT (VKM_MAX_BUFFER_INDEX+1) /* must be powers of 2 for pointer tagging */
STATIC_ASSERT(sizeof(vk_resource_group) >= VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT, "vk_resource_group needs to be big for pointer tagging");
STATIC_ASSERT(IS_POWER_OF_2(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT), "must be powers of 2 for pointer tagging");

typedef_struct(vk_vertex_description);
typedef_struct(vk_graphics_pipeline_config);
typedef_struct(vk_resource_group_and_index);




typedef enum 
{
    DEVICE_RANK_NONE = 0, 
    DEVICE_RANK_CPU, 
    DEVICE_RANK_INTEGRATED_GPU, 
    DEVICE_RANK_VIRTUAL_GPU,
    DEVICE_RANK_DISCRETE_GPU,
} device_rank;

struct vk_vertex_description
{
    VkVertexInputBindingDescription Binding;
    VkVertexInputAttributeDescription *Attribs;
    int AttribCount;
};

struct vk_graphics_pipeline_config
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
};

struct vk_resource_group_and_index
{
    vk_resource_group *ResourceGroup;
    u32 Handle;
};




force_inline u64 Vulkan_ResourceGroup_MakeHandle(vk_resource_group *ResourceGroup, u32 ItemIndex)
{
    u64 Upper = (u64)ResourceGroup;
    u64 Lower = ItemIndex;
    ASSERT((Upper & (VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT - 1)) == 0, "addr must be aligned");
    ASSERT(Lower < VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT, "too many items");
    return Upper | Lower;
}

force_inline vk_resource_group_and_index Vulkan_ResourceGroup_ExtractHandle(u64 HandleValue)
{
    return (vk_resource_group_and_index) {
        .ResourceGroup = (vk_resource_group *)(HandleValue & ~(VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT - 1)),
        .Handle = HandleValue & (VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT - 1),
    };
}

internal vk_resource_group *Vulkan_GetVkResourceGroup(renderer *Vk, renderer_resource_group_handle Handle)
{
    if (0 == Handle.Value)
    {
        ASSERT(Vk->GlobalResourceGroup);
        return Vk->GlobalResourceGroup;
    }

    vk_resource_group *ResourceGroup = (vk_resource_group *)Handle.Value;
    return ResourceGroup;
}

internal VkSampler Vulkan_ResourceGroup_GetSampler(renderer *Vk, renderer_sampler_handle Handle)
{
    if (0 == Handle.Value)
    {
        ASSERT(Vk->GlobalResourceGroup && Vk->GlobalResourceGroup->Samplers.Count >= 1);
        return Vk->GlobalResourceGroup->Samplers.Data[0];
    }
    vk_resource_group_and_index Result = Vulkan_ResourceGroup_ExtractHandle(Handle.Value);
    ASSERT(Result.Handle < Result.ResourceGroup->Samplers.Count, "Invalid handle");
    return Result.ResourceGroup->Samplers.Data[Result.Handle];
}

internal vk_mesh *Vulkan_ResourceGroup_GetMesh(renderer *Vk, renderer_mesh_handle Handle)
{
    (void)Vk;
    ASSERT(Handle.Value != 0, "no default mesh");

    return (vk_mesh *)Handle.Value;
}

internal vk_graphics_pipeline *Vulkan_ResourceGroup_GetGraphicsPipeline(renderer *Vk, renderer_graphics_pipeline_handle Handle)
{
    (void)Vk;
    ASSERT(Handle.Value != 0, "No default graphics pipeline");

    vk_resource_group_and_index Result = Vulkan_ResourceGroup_ExtractHandle(Handle.Value);
    ASSERT(Result.Handle < Result.ResourceGroup->GraphicsPipelines.Count, "Invalid handle");
    return &Result.ResourceGroup->GraphicsPipelines.Data[Result.Handle];
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
        Vulkan_LogLn("\nERROR: [%s] Code %d : %s", pLayerPrefix, MsgCode, Msg);
    } 
    else if (Flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) 
    {
        Vulkan_LogLn("\nWARNING: [%s] Code %d : %s", pLayerPrefix, MsgCode, Msg);
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

force_inline VkSampleCountFlagBits Vulkan_GetVkSampleCountFlags(renderer_msaa_flags Flag)
{
    switch (Flag)
    {
#define CASE(n) case RENDERER_MSAA_ ## n ## X: return VK_SAMPLE_COUNT_##n##_BIT;
    CASE(1);
    CASE(2);
    CASE(4);
    CASE(8);
    CASE(16);
    CASE(32);
    CASE(64);
#undef CASE
    default: 
        return (VkSampleCountFlagBits)Flag;
    }
}

internal VkDebugReportCallbackEXT Vulkan_CreateDebugCallback(
    VkInstance Instance, 
    PFN_vkDestroyDebugReportCallbackEXT *OutVkDestroyDebugReportCallback
) {
    VkDebugReportCallbackEXT DebugReportCallback = { 0 };

    PFN_vkCreateDebugReportCallbackEXT VkCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
        Instance, "vkCreateDebugReportCallbackEXT"
    );
    *OutVkDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
        Instance, "vkDestroyDebugReportCallbackEXT"
    );

    VkDebugReportCallbackCreateInfoEXT CreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
        .pfnCallback = (PFN_vkDebugReportCallbackEXT) Vulkan_DebugCallback,
        .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT,
    };
    if (VkCreateDebugReportCallback(Instance, &CreateInfo, NULL, &DebugReportCallback) != VK_SUCCESS) 
        Vulkan_LogLn("Failed to create debug callback.");
    else Vulkan_LogLn("Created debug callback.");

    return DebugReportCallback;
}

internal VkInstance Vulkan_CreateInstance(
    const char **ValidationLayers, int ValidationLayerCount,
    VkInstance *Instance, arena_alloc TmpArena, const char *AppName
) {
    NOT_DEBUG_ONLY(
        /* to shut the compiler up */
        (void)g_VkValidationLayerNames;
        (void)Vulkan_CreateDebugCallback;
    );

    vulkan_platform_instance_extensions RequiredExtensions = Vulkan_Platform_GetInstanceExtensions();
    /* query supported instance extensions and print them out */
    {
        u32 Count;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &Count, NULL));
        if (0 == Count)
        {
            VK_FATAL("Vulkan: No instance extension available");
        }

        VkExtensionProperties *SupportedInstanceExtensions;
        Arena_AllocArray(&TmpArena, &SupportedInstanceExtensions, Count); 
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, 
            &Count, SupportedInstanceExtensions
        ));

        Vulkan_LogLn("Supported instance extensions (%d): ", Count);
        for (u32 i = 0; i < Count; i++)
        {
            Vulkan_LogLn("\t%s", SupportedInstanceExtensions[i].extensionName);
        }
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
    Vulkan_LogLn("Used %d extensions to create the Vulkan instance: ", ExtensionCount);
    for (u32 i = 0; i < ExtensionCount; i++)
    {
        Vulkan_LogLn("\t%s", Extensions[i]);
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
        .enabledLayerCount = ValidationLayerCount,
        .ppEnabledLayerNames = ValidationLayers,
    };
    VK_CHECK(vkCreateInstance(&CreateInfo, NULL, Instance));
    return *Instance;
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


/* Out will not be written to if the returned rank is DEVICE_RANK_NONE */
internal device_rank Vulkan_CheckDeviceSuitability(
    const char **DeviceExtensions, int DeviceExtensionCount,
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
        u32 ExtensionCount = 0;
        vkEnumerateDeviceExtensionProperties(Device, NULL, &ExtensionCount, NULL);
        VkExtensionProperties *Properties;
        Arena_AllocArray(&TmpArena, &Properties, ExtensionCount);
        vkEnumerateDeviceExtensionProperties(Device, NULL, &ExtensionCount, Properties);

        bool32 AllExtensionsSupported = true;
        for (int i = 0; i < DeviceExtensionCount; i++)
        {
            bool32 DeviceSupportsThisExtension = false;
            int Length = strlen(DeviceExtensions[i]);
            for (int k = 0; k < DeviceExtensionCount && !DeviceSupportsThisExtension; k++)
            {
                DeviceSupportsThisExtension = (strncmp(DeviceExtensions[i], Properties[k].extensionName, Length) == 0);
            }
            AllExtensionsSupported &= DeviceSupportsThisExtension;
        }
        if (!AllExtensionsSupported)
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
    const char **DeviceExtensions, int DeviceExtensionCount,
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
        Vulkan_LogLn("Found %d devices that have Vulkan support:", Count);
        for (u32 i = 0; i < Count; i++)
        {
            vkGetPhysicalDeviceFeatures(HandleList[i], &FeaturesList[i]);
            vkGetPhysicalDeviceProperties(HandleList[i], &PropertiesList[i]);
            Vulkan_LogLn("Device %d, %s: %s: Supports version %d.%d.%d", 
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
            device_rank Rank = Vulkan_CheckDeviceSuitability(DeviceExtensions, DeviceExtensionCount, Gpus.HandleList[i], WindowSurface, *Arena);
            if (Rank > SelectedDeviceRank) 
            {
                Selected = i;
                SelectedDeviceRank = Rank;
            }
        }

        if (!SelectedDeviceRank)
        {
            VK_FATAL("Vulkan: Could not find a suitable device.");
        }

        bool32 OverrideSelectedDevice = false;
        if (OverrideSelectedDevice)
        {
            // Selected = Selected;
        }

        SelectedGpu = (vk_gpu) {
            .Features = Gpus.FeaturesList[Selected],
            .Properties = Gpus.PropertiesList[Selected],
            .Handle = Gpus.HandleList[Selected],
        };
        Vulkan_LogLn("Selected device %d, %s: %s", 
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
    const char **DeviceExtensions, int DeviceExtensionCount, 
    const char **ValidationLayers, int ValidationLayerCount,
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
            .enabledExtensionCount = DeviceExtensionCount,
            .ppEnabledExtensionNames = DeviceExtensions,
            .enabledLayerCount = ValidationLayerCount,
            .ppEnabledLayerNames = ValidationLayers,
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
        .Width = Extent.width,
        .Height = Extent.height,
        .PresentMode = PresentMode,
    };
    return Swapchain;
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
        ASSERT(PipelineLayout != NULL);

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
        ASSERT(GraphicsPipelineHandle != NULL);
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
    VkSampleCountFlagBits Sample, VkFormat DstImageFormat, VkFormat DepthBufferFormat
) {
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    {
#define COLOR_ATTACHMENT 0
#define DEPTH_ATTACHMENT 1
#define COLOR_RESOLVE_ATTACHMENT 2
#define ATTACHMENT_COUNT 3
        /* NOTE: have to specify array size because TCC would not compile without it */
        VkAttachmentDescription Attachments[ATTACHMENT_COUNT] = { 
            [COLOR_ATTACHMENT] = {
                .format = DstImageFormat,
                .samples = Sample,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, 
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                /* NOTE: set these if we have a stencil buffer */
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            [DEPTH_ATTACHMENT] = {
                .format = DepthBufferFormat,
                .samples = Sample,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, /* clear after done drawing */
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
            [COLOR_RESOLVE_ATTACHMENT] = {
                /* NOTE: to resolve an MSAA image to be presented, a new attachment is needed */
                .format = DstImageFormat, 
                .samples = VK_SAMPLE_COUNT_1_BIT, 
                .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, 
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
        };

        VkAttachmentReference DepthStencilAttachmentRef = {
            .attachment = DEPTH_ATTACHMENT,                                 /* index in Attachments */
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference ColorAttachmentRef = {
            .attachment = COLOR_ATTACHMENT,                                 /* index in Attachments */
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference ColorAttachmentResolveRef = {
            .attachment = COLOR_RESOLVE_ATTACHMENT,                         /* index in Attachments */
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
                bool32 DepthBufferHasStencil = (
                    Format == VK_FORMAT_D32_SFLOAT_S8_UINT 
                    || Format == VK_FORMAT_D24_UNORM_S8_UINT
                );
                if (DepthBufferHasStencil)
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

internal void Vulkan_CreateMainFramebuffers(
    VkDevice Device, 
    VkImageView MSAAImageView, 
    VkImageView DepthBufferImageView, 
    VkImageView *SwapchainImageViews,
    VkRenderPass RenderPass,
    int FramebufferWidth, int FramebufferHeight,
    int FramebufferCount, VkFramebuffer *OutFramebuffers)
{
    for (int i = 0; i < FramebufferCount; i++)
    {
        VkImageView Attachments[] = { 
            MSAAImageView,
            DepthBufferImageView,
            SwapchainImageViews[i]
        };
        VkFramebufferCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .attachmentCount = STATIC_ARRAY_SIZE(Attachments),
            .pAttachments = Attachments,
            .renderPass = RenderPass,
            .width = FramebufferWidth,
            .height = FramebufferHeight,
            .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(Device, &CreateInfo, NULL, &OutFramebuffers[i]));
    }
}

internal vk_render_target Vulkan_CreateRenderTarget(
    vkm *Vkm, 
    arena_alloc *Arena, 
    VkCommandPool CommandPool,
    vk_gpu_context *GpuContext, 
    vk_swapchain *Swapchain, 
    VkSampleCountFlagBits SampleCount
) {
    platform_window_dimensions Monitor = Platform_Get(MonitorDimensions);
    VkDevice Device = GpuContext->Device;

    vkm_image_handle MSAABuffer;
    vkm_image_info MSAABufferInfo;
    {
        MSAABuffer = Vkm_CreateImage(
            Vkm,
            &(vkm_image_config) {
                .Width = Monitor.Width, 
                .Height = Monitor.Height, 
                .MipLevels = 1, 
                .Sample = SampleCount, 
                .Format = Swapchain->ImageFormat, 
                .Tiling = VK_IMAGE_TILING_OPTIMAL, 
                .Usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 
                .Aspect = VK_IMAGE_ASPECT_COLOR_BIT,
            }
        );
        MSAABufferInfo = Vkm_GetImageInfo(Vkm, MSAABuffer);
    }

    vkm_image_handle DepthBuffer;
    vkm_image_info DepthBufferInfo;
    {
        VkFormat DesiredFormats[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT};
        VkFormat DepthBufferImageFormat = Vulkan_FindSupportedFormat(
            GpuContext->PhysicalDevice, 
            DesiredFormats, STATIC_ARRAY_SIZE(DesiredFormats), 
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );

        DepthBuffer = Vkm_CreateImage(
            Vkm,
            &(vkm_image_config) {
                .Width = Monitor.Width, 
                .Height = Monitor.Height, 
                .MipLevels = 1, 
                .Sample = SampleCount, 
                .Format = DepthBufferImageFormat, 
                .Tiling = VK_IMAGE_TILING_OPTIMAL, 
                .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .Aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
            }
        );
        DepthBufferInfo = Vkm_GetImageInfo(Vkm, DepthBuffer);
        Vulkan_TransitionImageLayout(GpuContext, 
            CommandPool, 
            DepthBufferInfo.Image,
            DepthBufferImageFormat,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            DepthBufferInfo.MipLevels
        );
    }

    VkRenderPass RenderPass = Vulkan_CreateRenderPass(GpuContext->Device, 
        SampleCount, Swapchain->ImageFormat, DepthBufferInfo.Format
    );

    u32 ImageCount;
    VkImage *SwapchainImages;
    VkImageView *SwapchainImageViews;
    {
        {
            vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCount, NULL);
            Arena_AllocArray(Arena, &SwapchainImages, ImageCount);
            vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCount, SwapchainImages);
        }

        {
            Arena_AllocArray(Arena, &SwapchainImageViews, ImageCount);
            int MipLevel = 1;
            for (u32 i = 0; i < ImageCount; i++)
            {
                SwapchainImageViews[i] = Vulkan_CreateImageView(Device, 
                    SwapchainImages[i], Swapchain->ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, MipLevel
                );
            }
        }
    }

    VkFramebuffer *Framebuffers;
    Arena_AllocArrayNonZero(Arena, &Framebuffers, ImageCount);
    Vulkan_CreateMainFramebuffers(
        Device, 
        MSAABufferInfo.ImageView,
        DepthBufferInfo.ImageView, 
        SwapchainImageViews,
        RenderPass,
        Swapchain->Width,
        Swapchain->Height,
        ImageCount, 
        Framebuffers
    );

    VkSemaphore *Semaphores;
    Arena_AllocArray(Arena, &Semaphores, ImageCount);
    for (uint i = 0; i < ImageCount; i++)
    {
        VkSemaphoreCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vkCreateSemaphore(Device, &CreateInfo, NULL, &Semaphores[i]);
    }

    vk_render_target RenderTarget = {
        .SampleCount = SampleCount,

        .ImageCount = ImageCount,
        .SwapchainImages = SwapchainImages,
        .SwapchainImageViews = SwapchainImageViews,
        .RenderPass = RenderPass,
        .Framebuffers = Framebuffers,

        .ColorResource = MSAABuffer,
        .DepthResource = DepthBuffer,

        .GpuWaitForRenderFrame = Semaphores,
    };
    return RenderTarget;
}

internal void Vulkan_ResizeRenderTarget(
    vk_render_target *RenderTarget,
    vkm *Vkm, 
    VkCommandPool CommandPool,
    vk_gpu_context *GpuContext, 
    const vk_swapchain *Swapchain
) {
    VkDevice Device = GpuContext->Device;

    for (uint i = 0; i < RenderTarget->ImageCount; i++)
    {
        vkDestroyImageView(Device, RenderTarget->SwapchainImageViews[i], NULL);
        vkDestroyFramebuffer(Device, RenderTarget->Framebuffers[i], NULL);
    }

    RenderTarget->ColorResource = Vkm_ResizeImage(Vkm, 
        RenderTarget->ColorResource, 
        &(vkm_resize_image_config) {
            .Width = Swapchain->Width, 
            .Height = Swapchain->Height,
            .Sample = RenderTarget->SampleCount,
        }
    );
    vkm_image_info MSAABufferInfo = Vkm_GetImageInfo(Vkm, RenderTarget->ColorResource);

    vkm_image_info DepthBufferInfo;
    {
        RenderTarget->DepthResource = Vkm_ResizeImage(Vkm, 
            RenderTarget->DepthResource, 
            &(vkm_resize_image_config) {
                .Width = Swapchain->Width, 
                .Height = Swapchain->Height,
                .Sample = RenderTarget->SampleCount,
            }
        );
        DepthBufferInfo = Vkm_GetImageInfo(Vkm, RenderTarget->DepthResource);
        Vulkan_TransitionImageLayout(GpuContext, 
            CommandPool, 
            DepthBufferInfo.Image,
            DepthBufferInfo.Format,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            DepthBufferInfo.MipLevels
        );
    }

    {
        u32 ImageCountNow;
        vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCountNow, NULL);
        ASSERT(ImageCountNow == RenderTarget->ImageCount, "Unreachable");
        vkGetSwapchainImagesKHR(Device, Swapchain->Handle, &ImageCountNow, RenderTarget->SwapchainImages);

        for (u32 i = 0; i < ImageCountNow; i++)
        {
            int MipLevel = 1;
            RenderTarget->SwapchainImageViews[i] = Vulkan_CreateImageView(Device, 
                RenderTarget->SwapchainImages[i], 
                Swapchain->ImageFormat, 
                VK_IMAGE_ASPECT_COLOR_BIT, 
                MipLevel
            );
        }
    }

    Vulkan_CreateMainFramebuffers(
        Device, 
        MSAABufferInfo.ImageView,
        DepthBufferInfo.ImageView, 
        RenderTarget->SwapchainImageViews,
        RenderTarget->RenderPass,
        Swapchain->Width,
        Swapchain->Height,
        RenderTarget->ImageCount,
        RenderTarget->Framebuffers
    );
}

internal void Vulkan_RecreateSwapchainAndRenderTargetThread(void *UserData)
{
    renderer *Vk = UserData;
    VkDevice Device = Vulkan_GetDevice(Vk);
    vk_gpu_context *GpuContext = &Vk->GpuContext;

    Profiler_Scope(Vk->Profiler, "Vulkan_RecreateSwapchain()")
    {
        Profiler_Scope(Vk->Profiler, "vkDeviceWaitIdle()")
        {
            vkDeviceWaitIdle(Device);
        }

        int Width, Height;
        {
            platform_framebuffer_dimensions FramebufferDimensions = Platform_Get(FramebufferDimensions);
            Width = FramebufferDimensions.Width;
            Height = FramebufferDimensions.Height;
        }

        {
            vk_swapchain NewSwapchain;
            Profiler_Scope(Vk->Profiler, "Vulkan_CreateSwapchain()")
            {
                NewSwapchain = Vulkan_CreateSwapchain(
                    Vk->Profiler, 
                    GpuContext, 
                    Width, Height, 
                    Vk->ForceTripleBuffering, 
                    Vk->WindowSurface, 
                    Vk->Swapchain.Handle,
                    Vk->Arena
                );
            }
            Profiler_Scope(Vk->Profiler, "Vulkan_DestroySwapchain()")
            {
                Vulkan_DestroySwapchain(Device, &Vk->Swapchain);
            }
            Vk->Swapchain = NewSwapchain;
        }
        Profiler_Scope(Vk->Profiler, "Vulkan_ResizeRenderTarget()")
        {
            Vulkan_ResizeRenderTarget(
                &Vk->RenderTarget, 
                &Vk->GlobalResourceGroup->GpuAllocator, 
                Vk->CommandPool, 
                GpuContext, 
                &Vk->Swapchain
            );
        }
    }
}

internal void Vulkan_RecreateSwapchainAndRenderTarget(renderer *Vk)
{
    Vk->IsResized = false;
    Vk->RecreateSwapchainAndRenderTargetThread = Platform_ThreadCreate(Vk, Vulkan_RecreateSwapchainAndRenderTargetThread);
}


internal void Vulkan_RecordCommandBuffer(
    renderer *Vk, 
    VkCommandBuffer CmdBuffer, 
    u32 FramebufferIndex,
    const renderer_draw_pipeline *Pipelines, i32 PipelineCount
) {
    vk_swapchain *Swapchain = &Vk->Swapchain;
    VkCommandBufferBeginInfo BeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pInheritanceInfo = NULL, /* NOTE: for secondary command buffers */
    };
    VK_CHECK(vkBeginCommandBuffer(CmdBuffer, &BeginInfo));
    {
        VkClearValue Clears[] = {
            [0] = {.color.float32 = {.2f, .3f, .4f, 1.0f}},
            [1] = {.depthStencil = {1.0f, 0.0f}},
        };
        VkRenderPassBeginInfo RenderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = Vk->RenderTarget.RenderPass,
            .renderArea = {
                .offset = { 0 },
                .extent = {
                    .width = Swapchain->Width,
                    .height = Swapchain->Height,
                },
            },
            .framebuffer = Vk->RenderTarget.Framebuffers[FramebufferIndex],

            .clearValueCount = STATIC_ARRAY_SIZE(Clears),
            .pClearValues = Clears,
        };
        vkCmdBeginRenderPass(CmdBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            VkViewport FullScreenViewport = { 
                .x = 0, 
                .y = (float)Swapchain->Height, 
                .width = (float)Swapchain->Width, 
                .height = -(float)Swapchain->Height,
                .maxDepth = 1.0f, 
                .minDepth = 0.0f,
            };

            /* bind each pipeline and draw each mesh group */
            for (int i = 0; i < PipelineCount; i++)
            {
                const renderer_draw_pipeline *DrawPipeline = Pipelines + i;
                const vk_graphics_pipeline *Pipeline = Vulkan_ResourceGroup_GetGraphicsPipeline(Vk, DrawPipeline->GraphicsPipelineHandle);
                ASSERT(Pipeline->Handle != NULL);

                vkCmdBindPipeline(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Handle);
                vkCmdSetViewport(CmdBuffer, 0, 1, &FullScreenViewport);

                for (int k = 0; k < DrawPipeline->GroupCount; k++)
                {
                    const renderer_draw_pipeline_group *Group = DrawPipeline->Groups + k;
                    const vk_mesh *Mesh = Vulkan_ResourceGroup_GetMesh(Vk, Group->MeshHandle);
                    const vk_resource_group *MeshOwner = Mesh->Owner;

                    VkRect2D Scissor = {
                        .offset = {
                            .x = Group->Scissor.OffsetX,
                            .y = Group->Scissor.OffsetY,
                        },
                        .extent = {
                            .width = Group->Scissor.Width == 0? Swapchain->Width : Group->Scissor.Width,
                            .height = Group->Scissor.Height == 0? Swapchain->Height : Group->Scissor.Height,
                        },
                    }; 

                    vkCmdSetScissor(CmdBuffer, 0, 1, &Scissor);
                    ASSERT(Mesh->IndexCount, "handle: %llu", (long long unsigned)Group->MeshHandle.Value);

                    vkm_buffer_info VertexBufferInfo = Vkm_GetBufferInfo(&MeshOwner->GpuAllocator, Mesh->VertexBuffer);
                    vkm_buffer_info IndexBufferInfo = Vkm_GetBufferInfo(&MeshOwner->GpuAllocator, Mesh->IndexBuffer);

                    VkBuffer VertexBuffers[] = { VertexBufferInfo.Buffer };
                    VkDeviceSize Offsets[] = { VertexBufferInfo.OffsetBytes };
                    vkCmdBindVertexBuffers(CmdBuffer, 0, 1, VertexBuffers, Offsets);

                    vkCmdBindIndexBuffer(CmdBuffer, IndexBufferInfo.Buffer, IndexBufferInfo.OffsetBytes, VK_INDEX_TYPE_UINT32);

                    VkDescriptorSet *CurrentDescriptorSet = MeshOwner->DescriptorSets + Vk->CurrentFrame;
                    vkCmdBindDescriptorSets(CmdBuffer, 
                        VK_PIPELINE_BIND_POINT_GRAPHICS, 
                        Pipeline->Layout, 
                        0, 1, CurrentDescriptorSet, 
                        0, NULL
                    );

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


/* NOTE: src buffer must have the same format as DstFormat */
internal void Vulkan_CopyBufferToImage(
    const vk_gpu_context *GpuContext, VkCommandPool CommandPool, 
    VkBuffer Src, u32 SrcOffset, 
    VkImage Dst,
    u32 Width, u32 Height, 
    VkImageLayout DstLayout
) {
    VkCommandBuffer CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
    VkBufferImageCopy Region = {
        .bufferImageHeight = 0, 
        .bufferRowLength = 0,
        .bufferOffset = SrcOffset,

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


void Renderer_Draw(renderer *Vk, const renderer_draw_pipeline *Pipelines, i32 PipelineCount)
{
    VkDevice Device = Vulkan_GetDevice(Vk);
    vk_swapchain *Swapchain = &Vk->Swapchain;
    vk_gpu_context *GpuContext = &Vk->GpuContext;

    if (Vk->RecreateSwapchainAndRenderTargetThread.Value != 0)
    {
        Platform_ThreadJoin(Vk->RecreateSwapchainAndRenderTargetThread);
        Vk->RecreateSwapchainAndRenderTargetThread.Value = 0;
    }

    u32 ImageIndex = 0;
    {
        VkSemaphore GpuWaitForRenderTarget = Vk->RenderFrame.GpuWaitForRenderTarget[Vk->CurrentFrame];
        VkFence *Fence = &Vk->RenderFrame.CpuWaitThisFrame[Vk->CurrentFrame];
        vkWaitForFences(Device, 1, Fence, VK_TRUE, UINT64_MAX);
        {
            VkResult Result = vkAcquireNextImageKHR(Device, Swapchain->Handle, UINT64_MAX, 
                GpuWaitForRenderTarget, VK_NULL_HANDLE, &ImageIndex
            );
            if (Result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                Vulkan_RecreateSwapchainAndRenderTarget(Vk);
                return;
            }
            else if (Result == VK_SUBOPTIMAL_KHR || Result == VK_SUCCESS)
            {
                /* continue */
            }
            else
            {
                VK_FATAL("Vulkan: Unable to acquire next swapchain image");
            }
        }
        vkResetFences(Device, 1, Fence);
    }

    /* TODO: record command buffer and transfer in parallel */
    /* record command buffer */
    {
        VK_CHECK(vkResetCommandBuffer(Vk->RenderFrame.CommandBuffers[Vk->CurrentFrame], 0));
        Vulkan_RecordCommandBuffer(Vk, Vk->RenderFrame.CommandBuffers[Vk->CurrentFrame], ImageIndex, Pipelines, PipelineCount);
    }

    /* update resources */
    {
        vk_update_resource *Last = Vk->UpdateResourceHead;
        for (vk_update_resource *i = Vk->UpdateResourceHead; i; Last = i, i = i->Next)
        {
            memcpy(i->UniformBuffersDst[Vk->CurrentFrame], i->UniformBufferSrc, i->UniformBufferSizeBytes);
        }

        if (Last)
        {
            Last->Next = Vk->UpdateResourceFree;
            Vk->UpdateResourceFree = Vk->UpdateResourceHead;
            Vk->UpdateResourceHead = NULL;
        }
    }


    VkSemaphore GraphicsQueueWait[] = { Vk->RenderFrame.GpuWaitForRenderTarget[Vk->CurrentFrame], };
    VkSemaphore GraphicsQueueSignal[] = { Vk->RenderTarget.GpuWaitForRenderFrame[ImageIndex], };

    /* submit to graphics queue */
    {
        VkPipelineStageFlags WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, };
        STATIC_ASSERT(STATIC_ARRAY_SIZE(WaitStages) == STATIC_ARRAY_SIZE(GraphicsQueueWait), "");

        VkSubmitInfo GraphicsQueueSubmitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = STATIC_ARRAY_SIZE(GraphicsQueueWait),
            .pWaitSemaphores = GraphicsQueueWait,
            .pWaitDstStageMask = WaitStages,
            .signalSemaphoreCount = STATIC_ARRAY_SIZE(GraphicsQueueSignal),
            .pSignalSemaphores = GraphicsQueueSignal,
            .commandBufferCount = 1,
            .pCommandBuffers = &Vk->RenderFrame.CommandBuffers[Vk->CurrentFrame],
        };
        VK_CHECK(vkQueueSubmit(GpuContext->GraphicsQueue, 
            1, &GraphicsQueueSubmitInfo,
            Vk->RenderFrame.CpuWaitThisFrame[Vk->CurrentFrame]
        ));
    }

    /* submit to present queue */
    {
        VkSwapchainKHR Swapchains[] = { Vk->Swapchain.Handle };
        u32 ImageIndices[] = { ImageIndex };

        VkPresentInfoKHR PresentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = STATIC_ARRAY_SIZE(GraphicsQueueSignal),
            .pWaitSemaphores = GraphicsQueueSignal,
            .swapchainCount = 1,
            .pSwapchains = Swapchains,
            .pImageIndices = ImageIndices,
        };
        VkResult Result = vkQueuePresentKHR(GpuContext->PresentQueue, &PresentInfo);
        if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR || Vk->IsResized)
        {
            Vulkan_RecreateSwapchainAndRenderTarget(Vk);
        }
        else if (Result != VK_SUCCESS)
        {
            VK_FATAL("Vulkan: Unable to present swapchain image");
        }
    }

    Vk->CurrentFrame++;
    if (Vk->CurrentFrame >= Vk->FramesInFlight)
        Vk->CurrentFrame = 0;
}

renderer_handle Renderer_Create(const renderer_config *Config)
{
    arena_alloc *Arena;
    renderer *Vk;
    {
        arena_alloc TmpArena; 
        memory_alloc_interface Alloc = Platform_Get(Allocator);

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
            .Profiler = Config->Profiler,
            .FramesInFlight = Config->FramesInFlight,
            .ForceTripleBuffering = Config->ForceTripleBuffering,
        };
        Arena = &Vk->Arena;
    }

    const char *ValidationLayers[] = {
        "VK_LAYER_KHRONOS_validation", 
    };
    int ValidationLayerCount = STATIC_ARRAY_SIZE(ValidationLayers);
    NOT_DEBUG_ONLY(ValidationLayerCount = 0);

    VkInstance Instance = Vulkan_CreateInstance(
        ValidationLayers, ValidationLayerCount,
        &Vk->Instance, *Arena, Config->AppName
    );
    DEBUG_ONLY(Vk->DebugReportCallback = Vulkan_CreateDebugCallback(Instance, &Vk->VkDestroyDebugReportCallback)); 
    VK_CHECK(Vulkan_Platform_CreateWindowSurface(Instance, NULL, &Vk->WindowSurface));

    vk_gpu_context *GpuContext;
    VkDevice Device;
    {
        const char *DeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        };
        int DeviceExtensionCount = STATIC_ARRAY_SIZE(DeviceExtensions);

        Vk->Gpus = Vulkan_QueryAndSelectGpu(
            DeviceExtensions, DeviceExtensionCount,
            Instance, 
            Vk->WindowSurface, 
            Arena
        );
        Vk->Gpus.Selected.Features.sampleRateShading = VK_TRUE;
        Vk->GpuContext = Vulkan_CreateGpuContext(
            DeviceExtensions, DeviceExtensionCount,
            ValidationLayers, ValidationLayerCount,
            Vk->Gpus.Selected.Handle, 
            Vk->WindowSurface, 
            Vk->Gpus.Selected.Features, 
            *Arena
        );

        GpuContext = &Vk->GpuContext;
        Device = GpuContext->Device;
        GpuContext->Profiler = Config->Profiler;
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

    /* default/global resource group */
    {
        renderer_resource_group_config ResourceConfig = { 
            .CpuBufferPoolSizeBytes = 1*MB,
            .GpuCpuMemoryPoolSizeBytes = 8*MB,
            .GpuLocalMemoryPoolSizeBytes = 256*MB,
            .GpuBufferPoolSizeBytes = 1*MB,

            .UniformBufferBinding = 0,
            .UniformBufferSizeBytes = 1, /* TODO: put a useful uniform buffer here */

            .TextureArrayBinding = 1,
        };
        renderer_resource_group_handle ResourceGroup = Renderer_CreateResourceGroup(Vk, &ResourceConfig);
        Vk->GlobalResourceGroup = (vk_resource_group *)ResourceGroup.Value;
        ASSERT(Vk->ResourceGroupHead == Vk->GlobalResourceGroup);
    }

    /* create render target  */
    {
        renderer_msaa_flags AvailableMSAAFlags = Renderer_GetAvailableMSAAFlags(Vk);
        VkSampleCountFlagBits MaxSampleCount = 1u << (CountTrailingZeros32(~AvailableMSAAFlags) - 1);
        vk_resource_group *ResourceGroup = Vk->GlobalResourceGroup;
        Vk->RenderTarget = Vulkan_CreateRenderTarget(
            &ResourceGroup->GpuAllocator, 
            Arena, 
            Vk->CommandPool, 
            GpuContext, 
            &Vk->Swapchain, 
            MaxSampleCount
        );

        VkFence *Fences;
        VkSemaphore *Semaphores;
        VkCommandBuffer *CommandBuffers;
        {
            Arena_AllocArray(Arena, &Semaphores, Vk->FramesInFlight);
            Arena_AllocArray(Arena, &Fences, Vk->FramesInFlight);
            Arena_AllocArray(Arena, &CommandBuffers, Vk->FramesInFlight);

            VkSemaphoreCreateInfo SemaphoreCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            };
            VkFenceCreateInfo FenceCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            for (int i = 0; i < Vk->FramesInFlight; i++)
            {
                VK_CHECK(vkCreateSemaphore(Device, &SemaphoreCreateInfo, NULL, &Semaphores[i]));
                VK_CHECK(vkCreateFence(Device, &FenceCreateInfo, NULL, &Fences[i]));
            }

            VkCommandBufferAllocateInfo CommandBufferAllocateInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = Vk->CommandPool,
                .commandBufferCount = Vk->FramesInFlight,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            };
            VK_CHECK(vkAllocateCommandBuffers(Device, &CommandBufferAllocateInfo, CommandBuffers));
        }
        Vk->RenderFrame = (vk_render_frame) {
            .CommandBuffers = CommandBuffers,
            .CpuWaitThisFrame = Fences,
            .GpuWaitForRenderTarget = Semaphores,
        };
    }

    Renderer__InitDefaultResources(Vk);
    ASSERT_EXPRESSION_TYPE(Vk, renderer_handle, "invalid type");
    return Vk;
}

renderer_msaa_flags Renderer_GetAvailableMSAAFlags(renderer_handle Vk)
{
    STATIC_ASSERT(RENDERER_MSAA_1X == (int)VK_SAMPLE_COUNT_1_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_2X == (int)VK_SAMPLE_COUNT_2_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_4X == (int)VK_SAMPLE_COUNT_4_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_8X == (int)VK_SAMPLE_COUNT_8_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_16X == (int)VK_SAMPLE_COUNT_16_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_32X == (int)VK_SAMPLE_COUNT_32_BIT, "");
    STATIC_ASSERT(RENDERER_MSAA_64X == (int)VK_SAMPLE_COUNT_64_BIT, "");
    renderer_msaa_flags Flags = Vk->Gpus.Selected.Properties.limits.framebufferColorSampleCounts;
    return Flags;
}

void Renderer_SetScreenMSAA(renderer_handle Vk, renderer_msaa_flags OneFlag)
{
    ASSERT(IS_POWER_OF_2(OneFlag), "Must specify only 1 RENDERER_MSAA_xxx flag to %s(): %02x", __func__, OneFlag);
    VkSampleCountFlagBits MSAASample = Vulkan_GetVkSampleCountFlags(OneFlag);
    if (MSAASample == Vk->RenderTarget.SampleCount)
        return;

    VkDevice Device = Vulkan_GetDevice(Vk);
    vkm_image_info DepthBufferInfo = Vkm_GetImageInfo(&Vk->GlobalResourceGroup->GpuAllocator, Vk->RenderTarget.DepthResource);

    Vk->RenderTarget.SampleCount = MSAASample;
    vkDestroyRenderPass(Device, Vk->RenderTarget.RenderPass, NULL);
    Vk->RenderTarget.RenderPass = Vulkan_CreateRenderPass(
        Device, 
        MSAASample,
        Vk->Swapchain.ImageFormat, 
        DepthBufferInfo.Format
    );
    Vulkan_ResizeRenderTarget(
        &Vk->RenderTarget, 
        &Vk->GlobalResourceGroup->GpuAllocator, 
        Vk->CommandPool, 
        &Vk->GpuContext, 
        &Vk->Swapchain
    );
}

internal u32 Vulkan_ResourceGroup_PushSampler(vk_resource_group *ResourceGroup, VkSampler Sampler)
{
    VkDynamicArray_Push(&ResourceGroup->CpuAllocator, &ResourceGroup->Samplers, Sampler);
    u32 Index = ResourceGroup->Samplers.Count - 1;
    return Index;
}

internal u32 Vulkan_ResourceGroup_PushTexture(vk_resource_group *ResourceGroup, const vk_texture *Texture)
{
    VkDynamicArray_Push(&ResourceGroup->CpuAllocator, &ResourceGroup->Textures, *Texture);
    u32 Index = ResourceGroup->Textures.Count - 1;
    return Index;
}

internal u32 Vulkan_ResourceGroup_PushGraphicsPipeline(vk_resource_group *ResourceGroup, const vk_graphics_pipeline *GraphicsPipeline)
{
    VkDynamicArray_Push(&ResourceGroup->CpuAllocator, &ResourceGroup->GraphicsPipelines, *GraphicsPipeline);
    u32 Index = ResourceGroup->GraphicsPipelines.Count - 1;
    return Index;
}

internal void Vulkan_ResourceGroup_ResizeStagingBuffer(vk_resource_group *ResourceGroup, isize SizeBytes)
{
    if (SizeBytes > ResourceGroup->StagingBufferInfo.CapacityBytes)
    {
        if (ResourceGroup->StagingBufferInfo.CapacityBytes == 0)
        {
            ResourceGroup->StagingBuffer = Vkm_CreateBuffer(
                &ResourceGroup->GpuAllocator, 
                &(vkm_buffer_config) {
                    .BufferType = VKM_BUFFER_TYPE_STAGING,
                    .MemoryCapacityBytes = SizeBytes,
                }
            );
        }
        else
        {
            Vkm_UnmapBufferMemory(&ResourceGroup->GpuAllocator, ResourceGroup->StagingBuffer, ResourceGroup->StagingBufferMapped);
            ResourceGroup->StagingBuffer = Vkm_ResizeBuffer(&ResourceGroup->GpuAllocator, ResourceGroup->StagingBuffer, SizeBytes);
        }
        ResourceGroup->StagingBufferInfo = Vkm_GetBufferInfo(&ResourceGroup->GpuAllocator, ResourceGroup->StagingBuffer);
        ResourceGroup->StagingBufferMapped = Vkm_MapBufferMemory(&ResourceGroup->GpuAllocator, ResourceGroup->StagingBuffer);
    }
}

internal void Vulkan_ResourceGroup_Init(renderer *Vk, vk_resource_group *ResourceGroup, const renderer_resource_group_config *Config)
{
    VkDevice Device = Vulkan_GetDevice(Vk);

    isize GpuLocalMemoryPoolSizeBytes = Config->GpuLocalMemoryPoolSizeBytes
        ? Config->GpuLocalMemoryPoolSizeBytes : RENDERER_DEFAULT_GPU_LOCAL_MEMORY_POOL_SIZE;
    isize GpuCpuMemoryPoolSizeBytes = Config->GpuCpuMemoryPoolSizeBytes
        ? Config->GpuCpuMemoryPoolSizeBytes : RENDERER_DEFAULT_GPU_CPU_MEMORY_POOL_SIZE;
    isize GpuBufferPoolSizeBytes = Config->GpuBufferPoolSizeBytes
        ? Config->GpuBufferPoolSizeBytes : RENDERER_DEFAULT_GPU_BUFFER_POOL_SIZE;
    isize CpuBufferPoolSizeBytes = Config->CpuBufferPoolSizeBytes
        ? Config->CpuBufferPoolSizeBytes : RENDERER_DEFAULT_CPU_BUFFER_POOL_SIZE;
    isize UniformBufferSizeBytes = Config->UniformBufferSizeBytes
        ? Config->UniformBufferSizeBytes : RENDERER_DEFAULT_UNIFORM_BUFFER_SIZE;

    /* allocators */
    {
        int Alignment = 8;
        Arena_Create(&ResourceGroup->CpuArena, Vk->Arena.UserAlloc, CpuBufferPoolSizeBytes, Alignment);
        FreeList_Create(
            &ResourceGroup->CpuAllocator, 
            Arena_AsAllocInterface(&ResourceGroup->CpuArena), 
            CpuBufferPoolSizeBytes/2,
            Alignment
        );

        Vkm_Create(
            &ResourceGroup->GpuAllocator,
            &ResourceGroup->CpuArena,
            &(vkm_config) {
                .Device = Device,
                .PhysicalDevice = Vulkan_GetPhysicalDevice(Vk),
                .LocalDeviceMemoryPoolCapacityBytes = GpuLocalMemoryPoolSizeBytes,
                .TransDeviceMemoryPoolCapacityBytes = GpuCpuMemoryPoolSizeBytes,
                .BufferPoolCapacityBytes = GpuBufferPoolSizeBytes,
            }
        );
    }

    if (Vk->GlobalResourceGroup)
    {
        ASSERT(Vk->GlobalResourceGroup->Samplers.Count >= 1);
        ASSERT(Vk->GlobalResourceGroup->Textures.Count >= 1);
        /* NOTE: handle 0 is always the first global handle */
        Vulkan_ResourceGroup_PushSampler(ResourceGroup, Vk->GlobalResourceGroup->Samplers.Data[0]);
        Vulkan_ResourceGroup_PushTexture(ResourceGroup, &Vk->GlobalResourceGroup->Textures.Data[0]);
    }

    /* uniform buffer */
    {
        FreeList_ReallocArray(&ResourceGroup->CpuAllocator, &ResourceGroup->UniformBuffers, Vk->FramesInFlight);
        FreeList_ReallocArray(&ResourceGroup->CpuAllocator, &ResourceGroup->UniformBuffersMapped, Vk->FramesInFlight);
        FreeList_ReallocArray(&ResourceGroup->CpuAllocator, &ResourceGroup->UniformBufferTmp, UniformBufferSizeBytes);
        ResourceGroup->UniformBufferTmpCapacity = UniformBufferSizeBytes;
        for (int i = 0; i < Vk->FramesInFlight; i++)
        {
            ResourceGroup->UniformBuffers[i] = Vkm_CreateBuffer(
                &ResourceGroup->GpuAllocator, 
                &(vkm_buffer_config) {
                    .BufferType = VKM_BUFFER_TYPE_UBO,
                    .MemoryCapacityBytes = UniformBufferSizeBytes,
                }
            );
            vkm_buffer_info BufferInfo = Vkm_GetBufferInfo(
                &ResourceGroup->GpuAllocator, 
                ResourceGroup->UniformBuffers[i]
            );
            ASSERT(BufferInfo.CapacityBytes <= Vk->Gpus.Selected.Properties.limits.maxUniformBufferRange, 
                "rounded: %ld, provided: %ld, max allowed by device: %d", BufferInfo.CapacityBytes, UniformBufferSizeBytes, Vk->Gpus.Selected.Properties.limits.maxUniformBufferRange
            );
            ResourceGroup->UniformBuffersMapped[i] = Vkm_MapBufferMemory(
                &ResourceGroup->GpuAllocator, 
                ResourceGroup->UniformBuffers[i]
            );
        }
    }


    /* descriptor pool */
    VkDescriptorPool DescriptorPool;
    {
        int UniformBufferDescriptorMaxCount = 1;
        int TextureDescriptorMaxCount = VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT;
        VkDescriptorPoolSize PoolSizes[] = {
            [0] = {
                .descriptorCount = Vk->FramesInFlight * UniformBufferDescriptorMaxCount,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            },
            [1] = {
                .descriptorCount = Vk->FramesInFlight * TextureDescriptorMaxCount,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            },
        };

        int MaxDescriptorCount = (UniformBufferDescriptorMaxCount + TextureDescriptorMaxCount) * Vk->FramesInFlight;
        VkDescriptorPoolCreateInfo CreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .poolSizeCount = STATIC_ARRAY_SIZE(PoolSizes),
            .pPoolSizes = PoolSizes,
            .maxSets = MaxDescriptorCount,
            .flags = 0,
        };
        VK_CHECK(vkCreateDescriptorPool(Device, &CreateInfo, NULL, &DescriptorPool));
    }

    /* descriptor sets and layout */
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorSet *DescriptorSets;
    arena_alloc *Arena = &ResourceGroup->CpuArena;
    Arena_AllocArray(Arena, &DescriptorSets, Vk->FramesInFlight);
    Arena_Scope(Arena)
    {
        {
            /* TODO: make this dynamic? */
            VkDescriptorSetLayoutBinding Bindings[] = {
                [0] = {
                    .binding = Config->UniformBufferBinding,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                [1] = {
                    .binding = Config->TextureArrayBinding,
                    .descriptorCount = ResourceGroup->Textures.Count,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            };
            VkDescriptorSetLayoutCreateInfo CreateInfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = STATIC_ARRAY_SIZE(Bindings),
                .pBindings = Bindings,
            };
            VK_CHECK(vkCreateDescriptorSetLayout(Device, &CreateInfo, NULL, &DescriptorSetLayout));
        }

        /* alloc descriptor set */

        VkDescriptorSetLayout *DescriptorSetLayouts;
        Arena_AllocArray(Arena, &DescriptorSetLayouts, Vk->FramesInFlight);
        for (int i = 0; i < Vk->FramesInFlight; i++)
        {
            DescriptorSetLayouts[i] = DescriptorSetLayout;
        }
        VkDescriptorSetAllocateInfo AllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = DescriptorPool,
            .descriptorSetCount = Vk->FramesInFlight,
            .pSetLayouts = DescriptorSetLayouts,
        };
        VK_CHECK(vkAllocateDescriptorSets(Device, &AllocInfo, DescriptorSets));
    }

    ResourceGroup->TextureArrayBinding = Config->TextureArrayBinding;
    ResourceGroup->UniformBufferBinding = Config->UniformBufferBinding;
    ResourceGroup->DescriptorPool = DescriptorPool;
    ResourceGroup->DescriptorSetLayout = DescriptorSetLayout;
    ResourceGroup->DescriptorSets = DescriptorSets;
}

internal void Vulkan_ResourceGroup_Deinit(renderer *Vk, vk_resource_group *ResourceGroup, bool32 ShouldDestroyDefaultResources)
{
    /* TODO: wait for resources in use? */
    VkDevice Device = Vulkan_GetDevice(Vk);

    isize Start = ShouldDestroyDefaultResources? 0 : 1;
    for (isize i = Start; i < ResourceGroup->Samplers.Count; i++)
    {
        vkDestroySampler(Device, ResourceGroup->Samplers.Data[i], NULL);
    }
    for (isize i = 0; i < ResourceGroup->GraphicsPipelines.Count; i++)
    {
        Vulkan_DestroyGraphicsPipeline(Device, &ResourceGroup->GraphicsPipelines.Data[i]);
    }
    vkDestroyDescriptorSetLayout(Device, ResourceGroup->DescriptorSetLayout, NULL);
    vkDestroyDescriptorPool(Device, ResourceGroup->DescriptorPool, NULL);

    /* NOTE: don't need to destroy the free list since the arena owns it */
    Vkm_Destroy(&ResourceGroup->GpuAllocator);
    Arena_Destroy(&ResourceGroup->CpuArena);
}


renderer_resource_group_handle Renderer_CreateResourceGroup(
    renderer *Vk, 
    const renderer_resource_group_config *Config
) {
    vk_resource_group *ResourceGroup = Vk->ResourceGroupFreeSlots;
    if (NULL == Vk->ResourceGroupFreeSlots)
    {
        /* create a new group */
        Arena_ScopedAlignment(&Vk->Arena, VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT)
        {
            ResourceGroup = Arena_Alloc(&Vk->Arena, sizeof *ResourceGroup);
        }
    }
    else
    {
        /* use one from the free list */
        vk_resource_group *Next = ResourceGroup->Next;
        vk_resource_group *Prev = ResourceGroup->Prev;
        if (Prev)
            Prev->Next = Next;
        else
            Vk->ResourceGroupFreeSlots = Next;
        if (Next)
            Next->Prev = Prev;
    }

    Vulkan_ResourceGroup_Init(Vk, ResourceGroup, Config);

    /* link list */
    if (NULL == Vk->ResourceGroupHead)
    {
        Vk->ResourceGroupHead = ResourceGroup;
    }
    else
    {
        vk_resource_group *Head = Vk->ResourceGroupHead;

        ResourceGroup->Next = Head;
        Head->Prev = ResourceGroup;
        Vk->ResourceGroupHead = ResourceGroup;
    }
    renderer_resource_group_handle Handle = { (u64)ResourceGroup };
    return Handle;
}

void Renderer_DestroyResourceGroup(
    renderer *Vk, 
    renderer_resource_group_handle ResourceGroupHandle
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);
    vk_resource_group *Next = ResourceGroup->Next;
    vk_resource_group *Prev = ResourceGroup->Prev;
    ASSERT(ResourceGroup != Vk->GlobalResourceGroup, "Cannot destroy global resource group.");

    /* unlink */
    if (Prev)
    {
        Prev->Next = Next;
    }
    else 
    {
        Vk->ResourceGroupHead = Next;
    }
    if (Next)
    {
        Next->Prev = Prev;
    }

    bool32 ShouldDestroyDefaultResources = false;
    Vulkan_ResourceGroup_Deinit(Vk, ResourceGroup, ShouldDestroyDefaultResources);

    /* put in free list */
    {
        ResourceGroup->Prev = NULL;
        ResourceGroup->Next = Vk->ResourceGroupFreeSlots;
        if (Vk->ResourceGroupFreeSlots)
            Vk->ResourceGroupFreeSlots->Prev = ResourceGroup;
        Vk->ResourceGroupFreeSlots = ResourceGroup;
    }
}

void Renderer_BindResourceGroup(
    renderer *Vk, 
    renderer_resource_group_handle ResourceGroupHandle
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);
    VkDevice Device = Vulkan_GetDevice(Vk);
    /* TODO: not wait until the gpu is in idle state? */
    vkDeviceWaitIdle(Device);

    /* udpate descriptor set */
    arena_alloc *Arena = &ResourceGroup->CpuArena;
    Arena_Scope(Arena)
    {
        ASSERT(ResourceGroup->Textures.Count <= VULKAN_RESOURCE_GROUP_MAX_ELEM_COUNT);
        /* update/write descriptor sets */

        VkDescriptorImageInfo *TextureDescriptors;
        Arena_AllocArray(Arena, &TextureDescriptors, ResourceGroup->Textures.Count);
        for (int i = 0; i < ResourceGroup->Textures.Count; i++)
        {
            vk_texture *Texture = &ResourceGroup->Textures.Data[i];
            TextureDescriptors[i] = (VkDescriptorImageInfo) {
                .sampler = Texture->SamplerReference,
                .imageView = Texture->ImageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        }

        VkDescriptorBufferInfo *UniformBufferDescriptors;
        Arena_AllocArray(Arena, &UniformBufferDescriptors, Vk->FramesInFlight);
        for (int i = 0; i < Vk->FramesInFlight; i++)
        {
            vkm_buffer_info BufferInfo = Vkm_GetBufferInfo(&ResourceGroup->GpuAllocator, ResourceGroup->UniformBuffers[i]);
            UniformBufferDescriptors[i] = (VkDescriptorBufferInfo) {
                .buffer = BufferInfo.Buffer,
                .offset = BufferInfo.OffsetBytes,
                .range = BufferInfo.CapacityBytes,
            };
            UNREACHABLE_IF(BufferInfo.CapacityBytes > 64*KB, "uni");
        }

        for (int i = 0; i < Vk->FramesInFlight; i++)
        {
            VkWriteDescriptorSet Writes[] = {
                [0] = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &UniformBufferDescriptors[i],
                    .dstSet = ResourceGroup->DescriptorSets[i],
                    .dstBinding = ResourceGroup->UniformBufferBinding,
                    .dstArrayElement = 0,
                },
                [1] = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = ResourceGroup->Textures.Count,
                    .pImageInfo = TextureDescriptors,
                    .dstSet = ResourceGroup->DescriptorSets[i],
                    .dstBinding = ResourceGroup->TextureArrayBinding,
                    .dstArrayElement = 0,
                },
            };
            vkUpdateDescriptorSets(Device, STATIC_ARRAY_SIZE(Writes), Writes, 0, NULL);
        }
    }
}



internal VkFilter Vulkan_RendererFilterTypeToVkFilter(renderer_filter_type Type)
{
    switch (Type)
    {
    case RENDERER_FILTER_NEAREST:
        return VK_FILTER_NEAREST;
    case RENDERER_FILTER_LINEAR:
        return VK_FILTER_LINEAR;
    }
    UNREACHABLE();
}


renderer_sampler_handle Renderer_CreateSampler(
    renderer *Vk, 
    renderer_resource_group_handle ResourceGroupHandle,
    const renderer_sampler_config *TextureSamplerConfig
) {
    VkDevice Device = Vulkan_GetDevice(Vk);
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);

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

        UNREACHABLE_IF(
            !AnisotropyFilteringSupported && TextureSamplerConfig->EnableAnisotropyFiltering, 
            "TODO: query anisotrophy support"
        );

        VkSamplerCreateInfo SamplerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            /* bilinear filtering:
             *  minification:  texels are bigger than screen pixels 
             *  magnification: screen pixels are bigger than texels */
            .magFilter = Vulkan_RendererFilterTypeToVkFilter(TextureSamplerConfig->MagFilter),
            .minFilter = Vulkan_RendererFilterTypeToVkFilter(TextureSamplerConfig->MinFilter),
            /* for u,v,(w) coords that are outside of the texture (default 0..1) */
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            /* when sampling out of bound for VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER */
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
            /* anisotrophy */
            .anisotropyEnable = TextureSamplerConfig->EnableAnisotropyFiltering,
            .maxAnisotropy = AnisotropyFilteringMax,
            /* for filtering */
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            /* mipmapping */
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .mipLodBias = 0.0f,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VK_CHECK(vkCreateSampler(
            Device,
            &SamplerCreateInfo,
            NULL, 
            &Sampler
        ));
    }

    /* save the sampler and return it */
    u32 Index = Vulkan_ResourceGroup_PushSampler(ResourceGroup, Sampler);
    renderer_sampler_handle Handle = {
        .Value = Vulkan_ResourceGroup_MakeHandle(ResourceGroup, Index),
    };
    return Handle;
}


internal void Vulkan_TransferDataToGpuLocalMemory(
    renderer *Vk, 
    VkCommandPool CommandPool, 
    const vk_resource_group *DstOwner, vkm_buffer_handle Dst, 
    const void *Src, isize SrcSizeBytes
) {
    /* TODO: use a dedicated transfer queue */
    /* TODO: better way to do staging buffer, having to create an entire allocator just to use a staging buffer is ridiculous */
    vk_resource_group *Global = Vk->GlobalResourceGroup;
    const vk_gpu_context *GpuContext = &Vk->GpuContext;
    {
        vkm_buffer_info DstBufferInfo = Vkm_GetBufferInfo(&DstOwner->GpuAllocator, Dst);
        Vulkan_ResourceGroup_ResizeStagingBuffer(Global, SrcSizeBytes);
        memcpy(Global->StagingBufferMapped, Src, SrcSizeBytes);

        VkCommandBuffer CmdBuf = Vulkan_BeginSingleTimeCommandBuffer(GpuContext, CommandPool);
        vkCmdCopyBuffer(CmdBuf,
            Global->StagingBufferInfo.Buffer,
            DstBufferInfo.Buffer,
            1, &(VkBufferCopy) {
                .dstOffset = DstBufferInfo.OffsetBytes,
                .srcOffset = Global->StagingBufferInfo.OffsetBytes,
                .size = SrcSizeBytes,
            }
        );
        Vulkan_EndSingleTimeCommandBuffer(GpuContext, CommandPool, CmdBuf);
    }
}

renderer_texture_handle Renderer_CreateStaticTexture(
    renderer *Vk,
    renderer_resource_group_handle ResourceGroupHandle,
    const renderer_texture_config *TextureConfig,
    const void *ImageData
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);

    VkFormat ImageFormat = Vulkan_GetVkFormat(TextureConfig->Format);

    vkm_image_handle TextureImageHandle;
    vkm_image_info TextureImageInfo;
    {
        vk_gpu_context *GpuContext = &Vk->GpuContext;
        VkCommandPool CommandPool = Vk->CommandPool;
        isize ImageSizeBytes = TextureConfig->Width * TextureConfig->Height * sizeof(u32);

        Vulkan_ResourceGroup_ResizeStagingBuffer(ResourceGroup, ImageSizeBytes);
        memcpy(ResourceGroup->StagingBufferMapped, ImageData, ImageSizeBytes);

        TextureImageHandle = Vkm_CreateImage(
            &ResourceGroup->GpuAllocator, 
            &(vkm_image_config) {
                .Width = TextureConfig->Width, 
                .Height = TextureConfig->Height, 
                .MipLevels = TextureConfig->MipLevels,
                .Sample = VK_SAMPLE_COUNT_1_BIT,
                .Format = ImageFormat,
                .Tiling = VK_IMAGE_TILING_OPTIMAL,
                .Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                .Aspect = VK_IMAGE_ASPECT_COLOR_BIT,
            }
        );
        TextureImageInfo = Vkm_GetImageInfo(&ResourceGroup->GpuAllocator, TextureImageHandle);

        /* copy image data to gpu memory */
        Vulkan_TransitionImageLayout(GpuContext, CommandPool,
            TextureImageInfo.Image, 
            ImageFormat, 
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            TextureConfig->MipLevels
        );
        Vulkan_CopyBufferToImage(GpuContext, CommandPool,
            ResourceGroup->StagingBufferInfo.Buffer,
            ResourceGroup->StagingBufferInfo.OffsetBytes,
            TextureImageInfo.Image, TextureConfig->Width, TextureConfig->Height, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
#if 1
        Vulkan_TransitionImageLayout(GpuContext, CommandPool,
            TextureImageInfo.Image, 
            ImageFormat,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            TextureConfig->MipLevels
        );
        /* to shut the compiler up */
        (void)Vulkan_GenerateMipmap;
#else
        Vulkan_GenerateMipmap(GpuContext, CommandPool, 
            ImageFormat, TextureImageInfo.Image, TextureConfig->Width, TextureConfig->Height, TextureConfig->MipLevels
        );
#endif
    }

    VkSampler Sampler = Vulkan_ResourceGroup_GetSampler(Vk, TextureConfig->SamplerHandle);

    u32 Index = Vulkan_ResourceGroup_PushTexture(ResourceGroup, &(vk_texture) {
        .Image = TextureImageHandle,
        .ImageView = TextureImageInfo.ImageView,
        .SamplerReference = Sampler,
    });
    renderer_texture_handle Handle = {
        .Value = Vulkan_ResourceGroup_MakeHandle(ResourceGroup, Index),
    };
    return Handle;
}

renderer_mesh_handle Renderer_CreateStaticMesh(
    renderer *Vk,
    renderer_resource_group_handle ResourceGroupHandle,
    const renderer_mesh_config *MeshConfig,
    const void *VertexBufferPtr,
    const u32 *IndexBufferPtr
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);

    isize VertexBufferSizeBytes = MeshConfig->VertexCount * MeshConfig->VertexBufferElementSizeBytes;
    isize IndexBufferSizeBytes = MeshConfig->IndexCount * sizeof(u32);
    /*
     * So apparently, the VkBufferUsageFlags is just a suggestion, 
     * but for certain vkCmd* to work, you'd need to just *HAVE* the appropriate flags
     *
     * eg: vkCmdBindIndexBuffers needs VK_BUFFER_USAGE_INDEX_BUFFER_BIT, though it can have other flags, even VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
     * */
    vk_mesh *MeshPtr = FreeList_AllocNonZero(&ResourceGroup->CpuAllocator, sizeof(*MeshPtr));
    *MeshPtr = (vk_mesh) {
        .Owner = ResourceGroup,
        .IndexBufferSizeBytes = IndexBufferSizeBytes,
        .VertexBufferSizeBytes = VertexBufferSizeBytes,
        .VertexCount = MeshConfig->VertexCount,
        .IndexCount = MeshConfig->IndexCount,

        .VertexBuffer = Vkm_CreateBuffer(&ResourceGroup->GpuAllocator, &(vkm_buffer_config) {
            .BufferType = VKM_BUFFER_TYPE_VBO,
            .MemoryCapacityBytes = VertexBufferSizeBytes,
        }),
        .IndexBuffer = Vkm_CreateBuffer(&ResourceGroup->GpuAllocator, &(vkm_buffer_config) {
            .BufferType = VKM_BUFFER_TYPE_EBO,
            .MemoryCapacityBytes = IndexBufferSizeBytes,
        }), 
    };
    Vulkan_TransferDataToGpuLocalMemory(Vk, Vk->CommandPool, ResourceGroup, MeshPtr->VertexBuffer, VertexBufferPtr, VertexBufferSizeBytes);
    Vulkan_TransferDataToGpuLocalMemory(Vk, Vk->CommandPool, ResourceGroup, MeshPtr->IndexBuffer, IndexBufferPtr, IndexBufferSizeBytes);
    return (renderer_mesh_handle) { (u64)MeshPtr };
}


renderer_graphics_pipeline_handle Renderer_CreateGraphicsPipeline(
    renderer *Vk,
    renderer_resource_group_handle ResourceGroupHandle,
    const renderer_graphics_pipeline_config *Config
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);
    VkDevice Device = Vulkan_GetDevice(Vk);

    vk_graphics_pipeline GraphicsPipeline;
    Arena_Scope(&Vk->Arena)
    {
        bool32 EnableMSAA = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_MSAA);
        UNREACHABLE_IF(!EnableMSAA && Vk->RenderTarget.SampleCount != VK_SAMPLE_COUNT_1_BIT, 
            "Must enable MSAA if MSAA is not 1X"
        );

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
        vk_graphics_pipeline_config VkConfig = {
            .CullMode = CullMode,
            .CullModeFrontFace = FrontFace,
            .EnableBlending = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_BLENDING),
            .EnableDepthTesting = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_Z_BUFFER), 
            .EnableMSAA = EnableMSAA, 
            .MSAASampleCount = Vk->RenderTarget.SampleCount,
            .EnableSampleShading = IS_SET(Config->EnabledGraphicsFeatures, RENDERER_GFXFT_SAMPLE_SHADING), 
            .SampleShadingMin = Config->SampleShadingMin,
        };
        GraphicsPipeline = Vulkan_CreateGraphicsPipeline(
            Device, 
            Vk->RenderTarget.RenderPass, 
            &VkConfig, 
            &ResourceGroup->DescriptorSetLayout, 1,
            Config->FragShaderCode,
            Config->FragShaderCodeSizeBytes,
            Config->VertShaderCode,
            Config->VertShaderCodeSizeBytes, 
            Config->VertexDescription,
            Vk->Arena
        );
    }
    u32 Index = Vulkan_ResourceGroup_PushGraphicsPipeline(ResourceGroup, &GraphicsPipeline);
    renderer_graphics_pipeline_handle Handle = { 
        .Value = Vulkan_ResourceGroup_MakeHandle(ResourceGroup, Index),
    };
    return Handle;
}



void Renderer_UpdateUniformBuffer(
    renderer *Vk,
    renderer_resource_group_handle ResourceGroupHandle,
    const void *Data, isize SizeBytes
) {
    vk_resource_group *ResourceGroup = Vulkan_GetVkResourceGroup(Vk, ResourceGroupHandle);
    UNREACHABLE_IF(SizeBytes > ResourceGroup->UniformBufferTmpCapacity, 
        "Uniform buffer size larger than capacity on creation: %lld > %lld",
        (long long)SizeBytes, (long long)ResourceGroup->UniformBufferTmpCapacity
    );

    /* NOTE: copy to tmp buffer, delay update to draw time */
    memcpy(ResourceGroup->UniformBufferTmp, Data, SizeBytes);

    /* push uniform to be updated */
    {
        vk_update_resource *Node = Vk->UpdateResourceFree;
        if (Node)
        {
            Vk->UpdateResourceFree = Node->Next;
            Node->Next = NULL;
        }
        else
        {
            Node = Arena_Alloc(&Vk->Arena, sizeof *Node);
        }

        *Node = (vk_update_resource) {
            .Next = Vk->UpdateResourceHead,
            .UniformBuffersDst = ResourceGroup->UniformBuffersMapped,
            .UniformBufferSrc = ResourceGroup->UniformBufferTmp,
            .UniformBufferSizeBytes = SizeBytes,
        };
        Vk->UpdateResourceHead = Node;
    }
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


void Renderer_Destroy(renderer *Vk)
{
    vk_gpu_context *GpuContext = &Vk->GpuContext;
    VkDevice Device = GpuContext->Device;
    vkDeviceWaitIdle(Device);

    for (vk_resource_group *ResourceGroup = Vk->ResourceGroupHead;
        ResourceGroup;
        ResourceGroup = ResourceGroup->Next)
    {
        bool32 ShouldDestroyDefaultResources = ResourceGroup == Vk->GlobalResourceGroup;
        Vulkan_ResourceGroup_Deinit(Vk, ResourceGroup, ShouldDestroyDefaultResources);
    }
    for (int i = 0; i < Vk->FramesInFlight; i++)
    {
        vkDestroySemaphore(Device, Vk->RenderFrame.GpuWaitForRenderTarget[i], NULL);
        vkDestroyFence(Device, Vk->RenderFrame.CpuWaitThisFrame[i], NULL);
    }

    for (uint i = 0; i < Vk->RenderTarget.ImageCount; i++)
    {
        vkDestroySemaphore(Device, Vk->RenderTarget.GpuWaitForRenderFrame[i], NULL);
        vkDestroyImageView(Device, Vk->RenderTarget.SwapchainImageViews[i], NULL);
        vkDestroyFramebuffer(Device, Vk->RenderTarget.Framebuffers[i], NULL);
    }
    vkDestroyRenderPass(Device, Vk->RenderTarget.RenderPass, NULL);

    vkDestroySwapchainKHR(Device, Vk->Swapchain.Handle, NULL);
    vkDestroyCommandPool(Device, Vk->CommandPool, NULL);
    vkDestroyDevice(Device, NULL);
    vkDestroySurfaceKHR(Vk->Instance, Vk->WindowSurface, NULL);
    if (Vk->VkDestroyDebugReportCallback)
    {
        Vk->VkDestroyDebugReportCallback(Vk->Instance, Vk->DebugReportCallback, NULL);
    }
    vkDestroyInstance(Vk->Instance, NULL);

    /* arena owns Vk */
    Arena_Destroy(&Vk->Arena);
}

void Renderer_OnFramebufferResize(renderer *Renderer, int Width, int Height)
{
    (void)Width, (void)Height;
    Renderer->IsResized = true;
}

