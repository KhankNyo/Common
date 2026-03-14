

#include "Platform-Core.h"
#include "Renderer-Core.h"
#include "App.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"


typedef_struct(uniform_buffer);
packed(struct uniform_buffer
{
    u32 TextureIndex;
});

typedef_struct(vertex_buffer);
packed(struct vertex_buffer
{
    v3f Pos;
    uv TexCoord;
});

internal void InitRenderer(app *App, const char *AppName);


void App_OnInit(app *App)
{
    const char *AppName = "Hello";
    Platform_Set(TargetFPS, 60);
    Platform_Set(WindowTitle, AppName);
    Platform_Set(VSyncEnable, true);

    *App = (app) { 0 };
    Arena_Create(&App->Arena, Platform_Get(Allocator), 16*MB, 16);
    InitRenderer(App, AppName);
}

void App_OnDeinit(app *App)
{
    Renderer_Destroy(App->Renderer);
    Arena_Destroy(&App->Arena);
}

void App_OnLoop(app *App)
{
    /* title */
    {
        double FrameTime = Platform_Get(FrameTime);
        char Title[256];
        snprintf(Title, sizeof Title, "tframe: %fs, fps: %f -- selected texture: %d (%d)", 
            FrameTime, 
            1 / FrameTime, 
            App->SelectedTexture, 
            App->Textures[App->SelectedTexture].Value
        );
        Platform_Set(WindowTitle, Title);
    }

    {
        uniform_buffer UniformBuffer = {
            .TextureIndex = App->Textures[App->SelectedTexture].Value,
        };
        Renderer_UpdateUniformBuffer(App->Renderer, &UniformBuffer, sizeof UniformBuffer);
    }
}

void App_OnRender(app *App)
{
    renderer_draw_pipeline_group Groups[] = {
        [0] = { .MeshHandle = App->FullScreenMesh }
    };
    renderer_draw_pipeline DrawPipelines[] = {
        [0] = {
            .GroupCount = STATIC_ARRAY_SIZE(Groups),
            .Groups = Groups,
            .GraphicsPipelineHandle = App->GraphicsPipeline,
        }
    };
    Renderer_Draw(App->Renderer, DrawPipelines, STATIC_ARRAY_SIZE(DrawPipelines));
}

void App_OnEvent(app *App, platform_event Event)
{
    switch (Event.Type)
    {
    case PLATFORM_EVENT_KEY:
    {
        platform_key_event Key = Event.As.Key;
        if (IN_RANGE('0', Key.Type, '3'))
        {
            App->SelectedTexture = Key.Type - '0';
        }
    } break;
    case PLATFORM_EVENT_MOUSE:
    case PLATFORM_EVENT_WINDOW_RESIZE:
    {
        /* handle events */
    } break;
    case PLATFORM_EVENT_FRAMEBUFFER_RESIZE:
    {
        platform_framebuffer_dimensions NewDimensions = Event.As.FramebufferResize;
        Renderer_OnFramebufferResize(App->Renderer, NewDimensions.Width, NewDimensions.Height);
    } break;
    }
}



#ifdef NEW_API
internal renderer_texture_handle LoadTexture(renderer_handle Renderer, renderer_sampler_handle Sampler, const char *FilePath, arena_alloc *Scratchpad)
{
    renderer_texture_handle Texture = { 0 };
    arena_snapshot Snapshot = Arena_SaveSnapshot(Scratchpad);
    u8 *Image = NULL;
    const char *ErrorMessage = "";

    platform_read_file_result ReadResult = Platform_ReadEntireFile(FilePath, Scratchpad, PLATFORM_FILE_TYPE_BINARY, 0);
    if (ReadResult.ErrorMessage)
    {
        ErrorMessage = ReadResult.ErrorMessage;
        goto Error;
    }

    int Width = 0, Height = 0, Channels = 0;
    stbi_set_flip_vertically_on_load(true);
    Image = stbi_load_from_memory(ReadResult.Buffer.Data, ReadResult.Buffer.Count, &Width, &Height, &Channels, 4);
    if (!Image)
    {
        ErrorMessage = "stbi_load_from_memory()";
        goto Error;
    }

    renderer_texture_config Config = {
        .Format = RENDERER_IMAGE_FORMAT_RGBA,
        .Width = Width,
        .Height = Height,
        .Sampler = Sampler,
    };
    Texture = Renderer_CreateStaticTexture(Renderer,
        RENDERER_GLOBAL_RESOURCE_GROUP,
        &Config, 
        Image
    );
    if (!RENDERER_IS_HANDLE_VALID(Texture))
    {
        ErrorMessage = "Renderer_UploadTexture()";
        goto Error;
    }

    Arena_RestoreSnapshot(Scratchpad, Snapshot);
    ASSERT(Texture.Value != 0);
    free(Image);
    return Texture;

Error:
    (void)eprintfln("[ERROR]: Unable to load image, defaulting to texture 0 (should be bright pink): %s", ErrorMessage);
    free(Image);
    Arena_RestoreSnapshot(Scratchpad, Snapshot);
    return (renderer_texture_handle) { 0 };
}
#else
internal renderer_texture_handle LoadTexture(renderer_handle Renderer, const char *FilePath, arena_alloc *Scratchpad)
{
    renderer_texture_handle Texture = { 0 };
    arena_snapshot Snapshot = Arena_SaveSnapshot(Scratchpad);
    u8 *Image = NULL;
    const char *ErrorMessage = "";

    platform_read_file_result ReadResult = Platform_ReadEntireFile(FilePath, Scratchpad, PLATFORM_FILE_TYPE_BINARY, 0);
    if (ReadResult.ErrorMessage)
    {
        ErrorMessage = ReadResult.ErrorMessage;
        goto Error;
    }

    int Width = 0, Height = 0, Channels = 0;
    stbi_set_flip_vertically_on_load(true);
    Image = stbi_load_from_memory(ReadResult.Buffer.Data, ReadResult.Buffer.Count, &Width, &Height, &Channels, 4);
    if (!Image)
    {
        ErrorMessage = "stbi_load_from_memory()";
        goto Error;
    }

    Texture = Renderer_UploadTexture(Renderer, Image, Width, Height, 1, RENDERER_IMAGE_FORMAT_RGBA);
    if (!RENDERER_IS_HANDLE_VALID(Texture))
    {
        ErrorMessage = "Renderer_UploadTexture()";
        goto Error;
    }

    Arena_RestoreSnapshot(Scratchpad, Snapshot);
    ASSERT(Texture.Value != 0);
    free(Image);
    return Texture;

Error:
    (void)eprintfln("[ERROR]: Unable to load image, defaulting to texture 0 (should be bright pink): %s", ErrorMessage);
    free(Image);
    Arena_RestoreSnapshot(Scratchpad, Snapshot);
    return (renderer_texture_handle) { 0 };
}
#endif

internal void InitRenderer(app *App, const char *AppName)
{
    /* create the renderer */
    {
        int FramesInFlight = 3;
        bool32 ForceTripleBuffering = false;
        profiler *Profiler = NULL;
        App->Renderer = Renderer_Init(AppName, FramesInFlight, ForceTripleBuffering, Profiler);
    }

    /* create a full screen mesh */
    {
        vertex_buffer Vertices[4] = {
            [0] = {
                .Pos.At = {-1, 1, 0},
                .TexCoord.At = {0, 1},
            },
            [1] = {
                .Pos.At = {1, 1, 0},
                .TexCoord.At = {1, 1},
            },
            [2] = {
                .Pos.At = {1, -1, 0},
                .TexCoord.At = {1, 0},
            },
            [3] = {
                .Pos.At = {-1, -1, 0},
                .TexCoord.At = {0, 0},
            }
        };
        u32 Indices[6] = {
            0, 1, 2,
            2, 3, 0
        };
#ifdef NEW_API
        renderer_mesh_config MeshConfig = {
            .IndexCount = 6,
            .VertexCount = 4,
            .VertexBufferElementSizeBytes = sizeof(vertex_buffer),
        };
        App->FullScreenMesh = Renderer_CreateStaticMesh(App->Renderer, 
            RENDERER_GLOBAL_RESOURCE_GROUP, 
            &MeshConfig, 
            Vertices, 
            Indices
        );
#else
        App->FullScreenMesh = Renderer_CreateMesh(App->Renderer, sizeof Vertices, sizeof Indices);
        renderer_result Result = Renderer_UpdateMesh(App->Renderer, 
            App->FullScreenMesh, 
            Vertices, STATIC_ARRAY_SIZE(Vertices), sizeof Vertices[0], 
            Indices, STATIC_ARRAY_SIZE(Indices)
        );
        UNREACHABLE_IF(Result != RENDERER_SUCCESS, "Renderer_UpdateMesh()");
#endif
    }

    /* textures */
    {
        const char *TextureNames[] = {
            "assets/gradient.bmp",
        };
#ifndef NEW_API
        for (uint i = 0; i < STATIC_ARRAY_SIZE(TextureNames); i++)
        {
            App->Textures[i] = LoadTexture(App->Renderer, TextureNames[i], &App->Arena);
        }
#else
        renderer_sampler_config SamplerConfig = {
            .MagFilter = RENDERER_FILTER_LINEAR,
            .MinFilter = RENDERER_FILTER_LINEAR,
            .EnableAnisotrophyFiltering = true,
        };
        renderer_sampler_handle Sampler = Renderer_CreateSampler(App->Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &SamplerConfig);
        for (uint i = 0; i < STATIC_ARRAY_SIZE(TextureNames); i++)
        {
            App->Textures[i] = LoadTexture(App->Renderer, Sampler, TextureNames[i], &App->Arena);
        }
#endif
    }

    /* upload shader and create graphics pipeline */
    {
        const u8 *VertexShaderCode = NULL;
        isize VertexShaderCodeSizeBytes = 0;
        {
            platform_read_file_result Result = Platform_ReadEntireFile("bin/texture.vert.spv", &App->Arena, PLATFORM_FILE_TYPE_BINARY, 0);
            ASSERT(!Result.ErrorMessage, "%s", Result.ErrorMessage);

            VertexShaderCode = Result.Buffer.Data;
            VertexShaderCodeSizeBytes = Result.Buffer.Count;
        }

        const u8 *FragmentShaderCode = NULL;
        isize FragmentShaderCodeSizeBytes = 0;
        {
            platform_read_file_result Result = Platform_ReadEntireFile("bin/texture.frag.spv", &App->Arena, PLATFORM_FILE_TYPE_BINARY, 0);
            ASSERT(!Result.ErrorMessage, "%s", Result.ErrorMessage);

            FragmentShaderCode = Result.Buffer.Data;
            FragmentShaderCodeSizeBytes = Result.Buffer.Count;
        }

        renderer_vertex_attributes VertexAttribs[] = {
            [0] = { /* NOTE: location must match with shader */
                .Location = 0, .Offset = offsetof(vertex_buffer, Pos), .Type = RENDERER_TYPE_F32x3,
            },
            [1] = {
                .Location = 1, .Offset = offsetof(vertex_buffer, TexCoord), .Type = RENDERER_TYPE_F32x2,
            },
        };
        renderer_vertex_description VertexDesc = {
            .Binding = 0,
            .AttribCount = STATIC_ARRAY_SIZE(VertexAttribs),
            .Attribs = VertexAttribs,
            .Stride = sizeof(vertex_buffer),
        };
        renderer_graphics_pipeline_config GraphicsPipelineConfig = {
            .EnabledGraphicsFeatures = RENDERER_GFXFT_ALL & ~RENDERER_GFXFT_Z_BUFFER,
            .CullingDirection = RENDERER_CULLING_CLOCKWISE,
            .VertShaderCode = VertexShaderCode,
            .VertShaderCodeSizeBytes = VertexShaderCodeSizeBytes,
            .FragShaderCode = FragmentShaderCode,
            .FragShaderCodeSizeBytes = FragmentShaderCodeSizeBytes,
            .VertexDescription = &VertexDesc,
        };
#ifdef NEW_API
        App->GraphicsPipeline = Renderer_CreateGraphicsPipeline(App->Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &GraphicsPipelineConfig);
#else
        int MSAASampleCount = Renderer_IsMSAASampleCountSupported(App->Renderer, 4)? 4 : 1;
        int UniformBufferSizeBytes = sizeof(uniform_buffer);
        int GraphicsPipelineCount = 1;
        Renderer_CreateGraphicsPipelines(App->Renderer, 
            UniformBufferSizeBytes, 
            MSAASampleCount, 
            GraphicsPipelineCount, &GraphicsPipelineConfig, 
            &App->GraphicsPipeline
        );
#endif
    }
}
