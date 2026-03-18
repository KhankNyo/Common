
#if defined(COMMON_UNITY_BUILD_WITH_PLATFORM_AND_RENDERER)
#  include "Common-Glfw-UnityBuild.c"
#  include "Common-Platform-Glfw.c"
#  include "Common-Renderer-Vulkan.c"
#  include "Common-Renderer-Vulkan-VkMalloc.c"
#endif

#include "Common.h"
#include "Memory.h"
#define ARENA_IMPLEMENTATION
#include "Arena.h"
#define FREE_LIST_IMPLEMENTATION
#include "FreeList.h"
