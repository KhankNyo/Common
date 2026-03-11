

/* the fuck is this shit?? */
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <unistd.h> /* usleep */
#include <pthread.h>
#include <sys/mman.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h> /* aligned_alloc, free */
#include <string.h>


/* NOTE: using our own fork of GLFW here, 
 *  the GLFW installed on my system does not have 
 *  GLFW_RESIZE_NWSE_CURSOR and GLFW_RESIZE_NESW_CURSOR defined. 
 *  Fuck Debian and their outdated packages
 */
#define GLFW_INCLUDE_VULKAN
#include "deps/glfw/include/GLFW/glfw3.h"

#include "Platform-Core.h"
#include "Common.h"
#include "Renderer-Vulkan.h"

/* NOTE: application must define app.h and the struct 'app' */
#include "App.h"




typedef_struct(platform__memory);
typedef_struct(platform__allocator_data);

struct platform__allocator_data
{
    pthread_mutex_t Mutex;

    isize MemSizeAlloced;
    platform_allocator_flags Flags;
    i32 AllocCount;
    i32 FreeCount;

    i32 PoolSize;
    i32 Log2OfPageSize;
    struct platform__memory {
        u8 *MMapPtr;
        u8 *AlignedPtr;
        isize SizeBytes;
    } Pool[256];
};

internal double g_TargetFrameTime = 0;
internal GLFWwindow *g_Window;
internal double g_FrameTime;
internal app g_App;
internal platform_mouse_button g_MouseButtonDown = 0;

internal platform__allocator_data *g_AllocatorData;

internal platform_mouse_shape g_MouseShape = PLATFORM_MOUSE_SHAPE_ARROW;
internal GLFWcursor *g_CursorShapes[PLATFORM_MOUSE_SHAPE_COUNT];


internal double GetElapsedTime(void)
{
    return glfwGetTime();
}

internal void OnMouseMove(GLFWwindow *Window, double X, double Y)
{
    (void)Window;
    platform_event Event = {
        .Type = PLATFORM_EVENT_MOUSE,
        .As.Mouse = {
            .ButtonDownMask = g_MouseButtonDown,
            .X = X, 
            .Y = Y,
        },
    };
    App_OnEvent(&g_App, Event);
}

internal void OnMouseButton(GLFWwindow *Window, int Button, int Action, int Mods)
{
    (void)Mods;
    if (Action == GLFW_PRESS)
    {
        if (Button == GLFW_MOUSE_BUTTON_LEFT)
            g_MouseButtonDown |= PLATFORM_MOUSE_BUTTON_LEFT;
        else if (Button == GLFW_MOUSE_BUTTON_RIGHT)
            g_MouseButtonDown |= PLATFORM_MOUSE_BUTTON_RIGHT;
        else if (Button == GLFW_MOUSE_BUTTON_MIDDLE)
            g_MouseButtonDown |= PLATFORM_MOUSE_BUTTON_MIDDLE;
    }
    else if (Action == GLFW_RELEASE)
    {
        if (Button == GLFW_MOUSE_BUTTON_LEFT)
            g_MouseButtonDown &= ~PLATFORM_MOUSE_BUTTON_LEFT;
        else if (Button == GLFW_MOUSE_BUTTON_RIGHT)
            g_MouseButtonDown &= ~PLATFORM_MOUSE_BUTTON_RIGHT;
        else if (Button == GLFW_MOUSE_BUTTON_MIDDLE)
            g_MouseButtonDown &= ~PLATFORM_MOUSE_BUTTON_MIDDLE;
    }

    double X, Y;
    glfwGetCursorPos(Window, &X, &Y);
    platform_event Event = {
        .Type = PLATFORM_EVENT_MOUSE,
        .As.Mouse = {
            .ButtonDownMask = g_MouseButtonDown,
            .X = X, 
            .Y = Y,
        },

    };
    App_OnEvent(&g_App, Event);
}

internal void OnWindowResize(GLFWwindow *Window, int Width, int Height)
{
    (void)Window;
    platform_event Event = {
        .Type = PLATFORM_EVENT_WINDOW_RESIZE,
        .As.WindowResize = {
            .Width = Width, 
            .Height = Height,
        },
    };
    App_OnEvent(&g_App, Event);
}

internal void OnKeyInput(GLFWwindow *Window, int Key, int Scancode, int Action, int Mod)
{
    (void)Window, (void)Scancode, (void)Mod;
    platform_key_type KeyType = 0;
    switch (Key)
    {
    case GLFW_KEY_LEFT_SHIFT: KeyType = PLATFORM_KEY_LSHIFT; break;
    case GLFW_KEY_RIGHT_SHIFT: KeyType = PLATFORM_KEY_RSHIFT; break;
    case GLFW_KEY_SPACE: KeyType = PLATFORM_KEY_SPACE; break;
    case GLFW_KEY_ENTER: KeyType = PLATFORM_KEY_ENTER; break;
    case GLFW_KEY_TAB: KeyType = PLATFORM_KEY_TAB; break;
    case GLFW_KEY_RIGHT_CONTROL: KeyType = PLATFORM_KEY_RCONTROL; break;
    case GLFW_KEY_LEFT_CONTROL: KeyType = PLATFORM_KEY_LCONTROL; break;
    case GLFW_KEY_BACKSPACE: KeyType = PLATFORM_KEY_BACKSPACE; break;
    case GLFW_KEY_CAPS_LOCK: KeyType = PLATFORM_KEY_CAPSLOCK; break;
    case GLFW_KEY_ESCAPE: KeyType = PLATFORM_KEY_ESCAPE; break;
    case GLFW_KEY_LEFT: KeyType = PLATFORM_KEY_LEFT_ARROW; break;
    case GLFW_KEY_RIGHT: KeyType = PLATFORM_KEY_RIGHT_ARROW; break;
    case GLFW_KEY_UP: KeyType = PLATFORM_KEY_UP_ARROW; break;
    case GLFW_KEY_DOWN: KeyType = PLATFORM_KEY_DOWN_ARROW; break;
    default:
    {
        STATIC_ASSERT(GLFW_KEY_0 == '0', "");
        STATIC_ASSERT(GLFW_KEY_9 == '9', "");
        STATIC_ASSERT(GLFW_KEY_A == 'A', "");
        STATIC_ASSERT(GLFW_KEY_Z == 'Z', "");
        if (IN_RANGE(GLFW_KEY_0, Key, GLFW_KEY_9) || IN_RANGE(GLFW_KEY_A, Key, GLFW_KEY_Z))
            KeyType = Key;
    } break;
    }
    if (KeyType)
    {
        platform_event Event = {
            .Type = PLATFORM_EVENT_KEY,
            .As.Key = {
                .Type = KeyType,
                .IsDown = Action == GLFW_PRESS || Action == GLFW_REPEAT,
            },
        };
        App_OnEvent(&g_App, Event);
    }
}

internal void OnMouseWheel(GLFWwindow *Window, double ScrollX, double ScrollY)
{
    double X, Y;
    glfwGetCursorPos(Window, &X, &Y);
    platform_event Event = {
        .Type = PLATFORM_EVENT_MOUSE,
        .As.Mouse = {
            .ButtonDownMask = g_MouseButtonDown,
            .WheelScrollY = ScrollY,
            .WheelScrollX = ScrollX,
            .X = X, 
            .Y = Y,
        },
    };
    App_OnEvent(&g_App, Event);
}

internal void OnFramebufferResize(GLFWwindow *Window, int Width, int Height)
{
    (void)Window;
    platform_event Event = {
        .Type = PLATFORM_EVENT_FRAMEBUFFER_RESIZE,
        .As.FramebufferResize = {
            .Width = Width, 
            .Height = Height,
        },
    };
    App_OnEvent(&g_App, Event);
}

int main(void)
{
    /* initialize allocator */
    platform__allocator_data AllocatorData = { 0 };
    {
        pthread_mutex_init(&AllocatorData.Mutex, NULL);

        i64 PageSize = sysconf(_SC_PAGESIZE);
        ASSERT(IS_POWER_OF_2(PageSize), "Fuck you");
        AllocatorData.Log2OfPageSize = CountTrailingZeros64(PageSize);

        g_AllocatorData = &AllocatorData;
    }

    if (!glfwInit())
    {
        Platform_FatalErrorNoReturn("Unable to initialize GLFW.");
    }

    {
        int GlfwCursorShapes[PLATFORM_MOUSE_SHAPE_COUNT] = {
            [PLATFORM_MOUSE_SHAPE_ARROW] = GLFW_ARROW_CURSOR,
            [PLATFORM_MOUSE_SHAPE_RESIZE_VERTICAL] = GLFW_VRESIZE_CURSOR,
            [PLATFORM_MOUSE_SHAPE_RESIZE_HORIZONTAL] = GLFW_HRESIZE_CURSOR,
            [PLATFORM_MOUSE_SHAPE_RESIZE_TOP_LEFT_AND_BOTTOM_RIGHT] = GLFW_RESIZE_NWSE_CURSOR,
            [PLATFORM_MOUSE_SHAPE_RESIZE_TOP_RIGHT_AND_BOTTOM_LEFT] = GLFW_RESIZE_NESW_CURSOR,
        };
        for (int i = 0; i < PLATFORM_MOUSE_SHAPE_COUNT; i++)
        {
            g_CursorShapes[i] = glfwCreateStandardCursor(GlfwCursorShapes[i]);
        }
    }

    int Width = 1080, Height = 720;
    const char *Title = "Hello, glfw!";

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    g_Window = glfwCreateWindow(Width, Height, Title, NULL, NULL);
    if (!g_Window)
    {
        Platform_FatalErrorNoReturn("Unable to initialize GLFW.");
    }
    glfwMakeContextCurrent(g_Window);
    glfwSetMouseButtonCallback(g_Window, OnMouseButton);
    glfwSetCursorPosCallback(g_Window, OnMouseMove);
    glfwSetWindowSizeCallback(g_Window, OnWindowResize);
    glfwSetKeyCallback(g_Window, OnKeyInput);
    glfwSetFramebufferSizeCallback(g_Window, OnFramebufferResize);
    glfwSetScrollCallback(g_Window, OnMouseWheel);

    App_OnInit(&g_App);
    double FrameBegin = GetElapsedTime();
    while (1)
    {
        double WorkBegin = GetElapsedTime();
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(g_Window))
                break;

            App_OnLoop(&g_App);
            App_OnRender(&g_App);
            glfwSwapBuffers(g_Window);
        }
        double Now = GetElapsedTime();
        double WorkTime = Now - WorkBegin;
        g_FrameTime = Now - FrameBegin;
        FrameBegin = Now;

        u64 SleepTime = 0;
        if (WorkTime < g_TargetFrameTime)
        {
            SleepTime = (g_TargetFrameTime - WorkTime) * 1000000 + 0.5;
            usleep(SleepTime);
        }
    }
    App_OnDeinit(&g_App);
    {
        (void)eprintfln("Free: %d, Alloc: %d, Alloced: %zdmb", AllocatorData.FreeCount, AllocatorData.AllocCount, AllocatorData.MemSizeAlloced / MB);
        pthread_mutex_destroy(&AllocatorData.Mutex);
    }

    glfwDestroyWindow(g_Window);
    for (int i = 0; i < PLATFORM_MOUSE_SHAPE_COUNT; i++)
    {
        glfwDestroyCursor(g_CursorShapes[i]);
    }
    glfwTerminate();
    return 0;
}

void *Platform_AllocateMemory(void *UserData, isize SizeBytes, usize Alignment)
{
    (void)UserData;
    platform__allocator_data *Allocator = UserData;
    isize TotalSizeBytes = SizeBytes + Alignment;
    isize PoolCapacity = STATIC_ARRAY_SIZE(Allocator->Pool); 

    STATIC_ASSERT(IS_POWER_OF_2(STATIC_ARRAY_SIZE(Allocator->Pool)), "Capacity must be a power of 2");
    ASSERT(IS_POWER_OF_2(Alignment), "Alignment must be a power of 2: %zu", Alignment);
    ASSERT(Allocator->PoolSize < PoolCapacity, "Out of memory");

    int MapFlags = MAP_ANONYMOUS|MAP_PRIVATE;
    if (Allocator->Flags & PLATFORM_ALLOCATOR_FLAG_COMMIT_ON_ALLOCATE)
    {
        MapFlags |= MAP_ANONYMOUS;
    }

    u8 *Ptr = mmap(
        NULL, 
        TotalSizeBytes, 
        PROT_READ|PROT_WRITE, 
        MapFlags,
        -1,
        0
    );
    UNREACHABLE_IF(Ptr == (void *)-1ll, "mmap() %fmb", (double)TotalSizeBytes / MB);
    u8 *AlignedPtr = (u8 *)((uintptr_t)Ptr & ~(Alignment - 1));

    pthread_mutex_lock(&Allocator->Mutex);
    {
        /* insert the newly allocated memory into the pool */
        u32 Index = ((uintptr_t)Ptr >> Allocator->Log2OfPageSize) & (PoolCapacity - 1);
        i32 i = 0;
        for (; i < PoolCapacity; i++)
        {
            bool32 IsPoolSlotEmpty = (NULL == Allocator->Pool[Index].AlignedPtr);
            if (IsPoolSlotEmpty)
            {
                Allocator->Pool[Index] = (platform__memory) {
                    .MMapPtr = Ptr,
                    .AlignedPtr = AlignedPtr,
                    .SizeBytes = TotalSizeBytes,
                };
                break;
            }
            else
            {
                /* slot taken, move on */
                Index += 1;
                Index &= PoolCapacity - 1;
            }
        }
        UNREACHABLE_IF(i == PoolCapacity, "Out of memory pool slot");
        Allocator->AllocCount++;
        Allocator->PoolSize++;
        Allocator->MemSizeAlloced += TotalSizeBytes;
    }
    pthread_mutex_unlock(&Allocator->Mutex);

    return AlignedPtr;
}

void Platform_FreeMemory(void *UserData, void *Buffer)
{
    if (NULL == Buffer)
        return;

    platform__allocator_data *Allocator = UserData;
    i32 PoolCapacity = STATIC_ARRAY_SIZE(Allocator->Pool);

    pthread_mutex_lock(&Allocator->Mutex);
    {
        /* look for the pointer */
        u32 Index = ((uintptr_t)Buffer >> Allocator->Log2OfPageSize) & (PoolCapacity - 1);
        i32 i = 0;
        for (; i < PoolCapacity; i++)
        {
            platform__memory *Slot = Allocator->Pool + Index;
            if (Slot->AlignedPtr == (u8 *)Buffer)
            {
                /* found the ptr, deallocate it */
                int ErrorCode = munmap(Slot->MMapPtr, Slot->SizeBytes);
                UNREACHABLE_IF(ErrorCode != 0, "munmap()");
                *Slot = (platform__memory) { 0 };
                break;
            }
            else
            {
                /* not it, move on */
                Index += 1;
                Index &= PoolCapacity - 1;
            }
        }
        UNREACHABLE_IF(i == PoolCapacity, "Unable to find pointer %p in pool", Buffer);
        Allocator->FreeCount++;
        Allocator->PoolSize--;
    }
    pthread_mutex_unlock(&Allocator->Mutex);
}

platform_request_union *Platform_Request(platform_request_tag Tag, platform_request_union *Data)
{
    switch (Tag)
    {
    case PLATFORM_GET_MemoryPageSize:
    {
        Data->MemoryPageSize = sysconf(_SC_PAGESIZE);
    } break;
    case PLATFORM_GET_MonitorDimensions:
    {
        GLFWmonitor *Monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *VideoMode = glfwGetVideoMode(Monitor);
        if (VideoMode)
        {
            Data->MonitorDimensions = (platform_window_dimensions) {
                .Width = VideoMode->width,
                .Height = VideoMode->height,
            };
        }
        else
        {
            (void)eprintfln("[NOTE]: glfwGetVideoMode() failed, resorting to glfwGetWindowSize()");
            glfwGetWindowSize(g_Window, &Data->MonitorDimensions.Width, &Data->MonitorDimensions.Height);
        }
    } break;
    case PLATFORM_GET_MouseShape:
    {
        Data->MouseShape = g_MouseShape;
    } break;
    case PLATFORM_SET_MouseShape:
    {
        platform_mouse_shape Shape = Data->MouseShape;
        GLFWcursor *CursorToSet = g_CursorShapes[Shape];
        if (CursorToSet)
        {
            glfwSetCursor(g_Window, CursorToSet);
            g_MouseShape = Shape;
        }
        else
        {
            (void)eprintfln("Unable to set mouse shape.");
        }
    } break;
    case PLATFORM_GET_ThreadCount:
    {
        Data->ThreadCount = sysconf(_SC_NPROCESSORS_CONF);
    } break;
    case PLATFORM_SET_MouseVisible:
    {
        glfwSetInputMode(g_Window, GLFW_CURSOR, (Data->MouseVisible? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED));
    } break;
    case PLATFORM_SET_MousePosition:
    {
        glfwSetCursorPos(g_Window, Data->MousePosition.X, Data->MousePosition.Y);
    } break;
    case PLATFORM_GET_ElapsedTime:
    {
        Data->ElapsedTime = GetElapsedTime();
    } break;
    case PLATFORM_GET_WindowDimensions:
    {
        platform_window_dimensions *Window = &Data->WindowDimensions;
        glfwGetWindowSize(g_Window, &Window->Width, &Window->Height);
    } break;
    case PLATFORM_SET_WindowDimensions:
    {
        platform_window_dimensions *Window = &Data->WindowDimensions;
        glfwSetWindowSize(g_Window, Window->Width, Window->Height);
    } break;
    case PLATFORM_SET_VSyncEnable:
    {
        glfwSwapInterval(Data->VSyncEnable != false);
    } break;
    case PLATFORM_SET_TargetFPS:
    {
        g_TargetFrameTime = 1.0 / Data->TargetFPS;
    } break;
    case PLATFORM_SET_WindowTitle:
    {
        glfwSetWindowTitle(g_Window, Data->WindowTitle);
    } break;
    case PLATFORM_GET_FrameTime:
    {
        Data->FrameTime = g_FrameTime;
    } break;
    case PLATFORM_GET_Allocator:
    {
        Data->Allocator = (platform_allocator) { 
            .Data = g_AllocatorData,
            .Allocate = Platform_AllocateMemory,
            .Free = Platform_FreeMemory,
        };
    } break;
    case PLATFORM_GET_FramebufferDimensions:
    {
        int Width, Height;
        glfwGetFramebufferSize(g_Window, &Width, &Height);
        Data->FramebufferDimensions = (platform_framebuffer_dimensions) {
            .Width = Width,
            .Height = Height,
        };
    } break;
    }
    return Data;
}

vulkan_platform_instance_extensions Vulkan_Platform_GetInstanceExtensions(void)
{
    vulkan_platform_instance_extensions Extensions = { 0 };
    Extensions.StringPtrArray = glfwGetRequiredInstanceExtensions(&Extensions.Count);
    return Extensions;
}

void Platform_FatalErrorNoReturn(const char *ErrorMessage)
{
    (void)eprintfln("FATAL ERROR: %s", ErrorMessage);
    UNREACHABLE();
}

platform_read_file_result Platform_ReadEntireFile(
    const char *FileName, arena_alloc *Arena, platform_file_type FileType, int Padding
) {
    const char *ErrMsg;
    FILE *f = fopen(FileName, "rb");
    if (!f)
    {
        ErrMsg = "Unable to open";
        goto Error;
    }

    fseek(f, 0, SEEK_END);
    usize FileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    isize Capacity = FileSize + (usize)FileType + Padding;
    u8 *Buffer = Arena_Alloc(Arena, Capacity);
    if (fread(Buffer, 1, FileSize, f) != FileSize)
    {
        ErrMsg = "Unable to fully read";
        fclose(f);
        goto Error;
    }
    if (FileType == PLATFORM_FILE_TYPE_TEXT)
    {
        /* null terminate */
        Buffer[FileSize] = 0;
    }
    platform_read_file_result Result = {
        .Buffer = {
            .Capacity = Capacity,
            .Count = Capacity,
            .Data = Buffer,
        },
        .ErrorMessage = NULL,
    };
    return Result;

Error:
    {
        const char *ErrnoMsg = strerror(errno);

        /* wow the cstdlib is as shitty as Vulkan's */
        int Length = snprintf(NULL, 0, "%s '%s': %s", ErrMsg, FileName, ErrnoMsg) + 1;
        char *ErrorMessageBuffer = Arena_Alloc(Arena, Length);
        snprintf(ErrorMessageBuffer, Length, "%s '%s': %s", ErrMsg, FileName, ErrnoMsg);

        return (platform_read_file_result) {
            .ErrorMessage = ErrorMessageBuffer,
        };
    }
}

VkResult Vulkan_Platform_CreateWindowSurface(VkInstance Instance, VkAllocationCallbacks *AllocCallback, VkSurfaceKHR *OutWindowSurface)
{
    return glfwCreateWindowSurface(Instance, g_Window, AllocCallback, OutWindowSurface);
}


platform_thread_handle Platform_ThreadCreate(void *UserData, platform_thread_routine Routine)
{
    pthread_t ThreadHandle;
    pthread_create(&ThreadHandle, NULL, (void *)Routine, UserData);
    platform_thread_handle Result = { ThreadHandle };
    return Result;
}

void Platform_ThreadJoin(platform_thread_handle Handle)
{
    pthread_join(Handle.Value, NULL);
}

void Platform_Allocator_SetFlags(platform_allocator *Allocator, platform_allocator_flags Flags)
{
    platform__allocator_data *AllocatorData = Allocator->Data;
    AllocatorData->Flags |= Flags;
}

platform_allocator_flags Platform_Allocator_GetFlags(const platform_allocator *Allocator)
{
    const platform__allocator_data *AllocatorData = Allocator->Data;
    return AllocatorData->Flags;
}

void Platform_Allocator_RemoveFlags(platform_allocator *Allocator, platform_allocator_flags Flags)
{
    platform__allocator_data *AllocatorData = Allocator->Data;
    AllocatorData->Flags &= ~Flags;
}

