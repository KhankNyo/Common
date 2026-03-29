#ifndef PLATFORM_CORE_H
#define PLATFORM_CORE_H

#include "Common.h"
#include "Memory.h"
#include "Arena.h"
#include "Containers.h"


typedef struct 
{
    i32 Width, Height;
} platform_window_dimensions;
typedef platform_window_dimensions platform_framebuffer_dimensions;

typedef enum 
{
    PLATFORM_EVENT_KEY = 1,
    PLATFORM_EVENT_MOUSE = 2,
    PLATFORM_EVENT_WINDOW_RESIZE = 3,
    PLATFORM_EVENT_FRAMEBUFFER_RESIZE = 4,
    PLATFORM_EVENT__LAST = PLATFORM_EVENT_FRAMEBUFFER_RESIZE,
#define PLATFORM_EVENT_COUNT (PLATFORM_EVENT__LAST + 1)
} platform_event_type;
typedef enum 
{
    /* printable chars are represented by their char literal, except alphabetical chars. Capitalized char literals are used */ 
    PLATFORM_KEY_LSHIFT = 0x0100,
    PLATFORM_KEY_RSHIFT = 0x0101,
    PLATFORM_KEY_TAB    = 0x0102,
    PLATFORM_KEY_ENTER  = 0x0103,
    PLATFORM_KEY_SPACE  = 0x0104,
    PLATFORM_KEY_LCONTROL = 0x0105,
    PLATFORM_KEY_RCONTROL = 0x0106,
    PLATFORM_KEY_BACKSPACE = 0x0107,
    PLATFORM_KEY_CAPSLOCK = 0x0108,
    PLATFORM_KEY_ESCAPE = 0x0109,
    PLATFORM_KEY_LEFT_ARROW = 0x010A,
    PLATFORM_KEY_RIGHT_ARROW = 0x010B,
    PLATFORM_KEY_DOWN_ARROW = 0x010C,
    PLATFORM_KEY_UP_ARROW = 0x010D,
    PLATFORM_KEY__LAST = PLATFORM_KEY_UP_ARROW,
#define PLATFORM_KEY_COUNT (PLATFORM_KEY__LAST + 1)
} platform_key_type;
typedef enum
{
    PLATFORM_MOUSE_BUTTON_LEFT = 1 << 0, 
    PLATFORM_MOUSE_BUTTON_RIGHT = 1 << 1,
    PLATFORM_MOUSE_BUTTON_MIDDLE = 1 << 2,
#define PLATFORM_MOUSE_BUTTON_COUNT 3
} platform_mouse_button;
#define GetButtonIndex(mouse_button_flag) (CountTrailingZeros32(mouse_button) + 1)

typedef_struct(platform_event);
typedef_union(platform_event_union);
typedef_struct(platform_key_event);
typedef_struct(platform_mouse_event);
struct platform_event 
{
    platform_event_type Type;
    union platform_event_union {
        struct platform_key_event {
            platform_key_type Type;
            bool8 IsDown;
        } Key;
        struct platform_mouse_event {
            platform_mouse_button ButtonDownMask;
            double X, Y, WheelScrollX, WheelScrollY;
        } Mouse;
        platform_window_dimensions WindowResize;
        platform_window_dimensions FramebufferResize;
    } As;
};

typedef enum 
{
    PLATFORM_MOUSE_SHAPE_ARROW = 0,
    PLATFORM_MOUSE_SHAPE_RESIZE_VERTICAL,
    PLATFORM_MOUSE_SHAPE_RESIZE_HORIZONTAL,
    PLATFORM_MOUSE_SHAPE_RESIZE_TOP_RIGHT_AND_BOTTOM_LEFT,
    PLATFORM_MOUSE_SHAPE_RESIZE_TOP_LEFT_AND_BOTTOM_RIGHT,
    PLATFORM_MOUSE_SHAPE__LAST = PLATFORM_MOUSE_SHAPE_RESIZE_TOP_LEFT_AND_BOTTOM_RIGHT,
#define PLATFORM_MOUSE_SHAPE_COUNT (PLATFORM_MOUSE_SHAPE__LAST + 1)
} platform_mouse_shape;

typedef enum 
{
    PLATFORM_SET_VSyncEnable,
    PLATFORM_SET_TargetFPS,
    PLATFORM_SET_WindowDimensions, 
    PLATFORM_SET_WindowTitle,
    PLATFORM_SET_MousePosition,
    PLATFORM_SET_MouseVisible,
    PLATFORM_SET_MouseShape,
    PLATFORM_GET_WindowDimensions,
    PLATFORM_GET_ElapsedTime,
    PLATFORM_GET_FrameTime,
    PLATFORM_GET_Allocator,
    PLATFORM_GET_FramebufferDimensions,
    PLATFORM_GET_ThreadCount,
    PLATFORM_GET_MouseShape,
    PLATFORM_GET_MonitorDimensions,
    PLATFORM_GET_MemoryPageSize,
} platform_request_tag;
typedef_union(platform_request_union);
union platform_request_union {
    bool32 VSyncEnable;
    double TargetFPS;
    double ElapsedTime;
    double FrameTime;
    const char *WindowTitle;
    platform_window_dimensions WindowDimensions;
    platform_framebuffer_dimensions FramebufferDimensions;
    void *RendererFunctionLoader;
    memory_alloc_interface Allocator;
    struct platform_mouse_position {
        double X, Y;
    } MousePosition;
    bool8 MouseVisible;
    uint ThreadCount;
    platform_mouse_shape MouseShape;
    platform_window_dimensions MonitorDimensions;
    isize MemoryPageSize;
};

typedef_struct(platform_read_file_result);
typedef dynamic_array(u8) u8_array;
struct platform_read_file_result 
{
    u8_array Buffer; /* Data is owned by the arena */
    const char *ErrorMessage; /* NULL if read successfully, otherwise the pointer is owned by the arena */
};
typedef enum 
{
    PLATFORM_FILE_TYPE_BINARY = 0,
    PLATFORM_FILE_TYPE_TEXT = 1, /* will null terminate */
} platform_file_type;

typedef handle(u64) platform_thread_handle;
typedef void (*platform_thread_routine)(void *UserData);



#define Platform_Get(target) \
    (Platform_Request(PLATFORM_GET_ ## target, &(platform_request_union) { 0 })-> target)
#define Platform_Set(target, ...) \
    ((void)Platform_Request(PLATFORM_SET_ ## target, &(platform_request_union) { .target = __VA_ARGS__ }))
platform_request_union *Platform_Request(platform_request_tag Tag, platform_request_union *Data);

platform_read_file_result Platform_ReadEntireFile(
    const char *FileName, arena_alloc *Arena, platform_file_type FileType, int Padding
);
void Platform_FatalErrorNoReturn(const char *ErrorMessage);

platform_thread_handle Platform_ThreadCreate(void *UserData, platform_thread_routine Routine);
void Platform_ThreadJoin(platform_thread_handle Handle);



/* NOTE: application must define these in App.h: */
typedef_struct(app);
void App_OnInit(app *);
void App_OnDeinit(app *);
void App_OnLoop(app *);
void App_OnRender(app *);
void App_OnEvent(app *, platform_event Event);



#endif /* PLATFORM_CORE_H */

