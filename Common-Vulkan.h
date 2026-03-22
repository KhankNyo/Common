#ifndef COMMON_VULKAN_H
#define COMMON_VULKAN_H

#include <vulkan/vulkan.h>
#include "deps/glfw/include/GLFW/glfw3.h"

#include "Common.h"
#include <stdarg.h>


#define VK_FATAL(...) UNREACHABLE_IF(false, __VA_ARGS__)

#define VK_CHECK(call) do {\
    VkResult res = call;\
    if (res != VK_SUCCESS) {\
        const char *fatal_msg = Vulkan_VkResultToString(res);\
        VK_FATAL("%s -- '"#call" -> %08x", fatal_msg, res);\
    }\
} while (0)



header_function void Vulkan_LogLn(const char *Fmt, ...)
{
    va_list Args;
    va_start(Args, Fmt);
    bool32 ShouldLog;
    DEBUG_ONLY(ShouldLog = true);
    NOT_DEBUG_ONLY(ShouldLog = false);
    if (ShouldLog)
    {
        vfprintf(stderr, Fmt, Args);
        fprintf(stderr, "\n");
    }
    va_end(Args);
}

header_function const char *Vulkan_PhysicalDeviceTypeToString(VkPhysicalDeviceType type) 
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

header_function const char *Vulkan_VkResultToString(VkResult Result)
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

#endif /* COMMON_VULKAN_H  */
