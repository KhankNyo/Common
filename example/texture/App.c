

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
internal void UpdateUniformBuffer(app *App)
{
    uniform_buffer UniformBuffer = {
        .TextureIndex = Renderer_GetTextureIndex(App->Renderer, App->Textures[App->SelectedTexture]),
    };
    Renderer_UpdateUniformBuffer(App->Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &UniformBuffer, sizeof UniformBuffer);
}


void App_OnInit(app *App)
{
    const char *AppName = "Hello";
    Platform_Set(TargetFPS, 2000);
    Platform_Set(WindowTitle, AppName);
    Platform_Set(VSyncEnable, true);

    *App = (app) { 0 };
    Arena_Create(&App->Arena, Platform_Get(Allocator), 16*MB, 16);
    InitRenderer(App, AppName);
    UpdateUniformBuffer(App);
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
        snprintf(Title, sizeof Title, "tframe: %fs, fps: %f -- selected texture: %d", 
            FrameTime, 
            1 / FrameTime, 
            App->SelectedTexture
        );
        Platform_Set(WindowTitle, Title);
    }

    if (App->SelectedTexture == TEXTURE_COUNT - 1)
    {
        u32 *Ptr = App->CustomTexture;
        ASSERT(Ptr);
        float Pi = 3.14159;
        float ElapsedTime = Platform_Get(ElapsedTime);
        u32 Color = (sinf(ElapsedTime * 2*Pi) + 1.0f) * 0.5f * 255.0f;
        for (int y = 0; y < App->CustomTextureHeight; y++)
        {
            for (int x = 0; x < App->CustomTextureWidth; x++)
            {
                *Ptr++ = 0xFF000000
                    | (Color) << 16
                    | (Color) << 8 
                    | Color << 0;
            }
        }
        Renderer_UpdateMutableTexture(App->Renderer, 
            App->Textures[TEXTURE_COUNT - 1], 
            &(renderer_update_texture_config) { 
                .GenerateMipmap = true, .MipLevels = 4 
            }
        );
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
        if (IN_RANGE('0', Key.Type, '0' + TEXTURE_COUNT - 1))
        {
            App->SelectedTexture = Key.Type - '0';
            UpdateUniformBuffer(App);
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



internal renderer_texture_handle LoadTexture(
    renderer_handle Renderer, 
    renderer_sampler_handle Sampler, 
    const char *FilePath, 
    arena_alloc *Scratchpad
) {
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

    renderer_static_texture_config Config = {
        .Format = RENDERER_IMAGE_FORMAT_RGBA,
        .Width = Width,
        .Height = Height,
        .SamplerHandle = Sampler,
    };
    Texture = Renderer_CreateStaticTexture(Renderer,
        RENDERER_GLOBAL_RESOURCE_GROUP,
        &Config, 
        Image
    );

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

internal void InitRenderer(app *App, const char *AppName)
{
    /* create the renderer */
    {
        renderer_config Config = {
            .AppName = AppName,
            .FramesInFlight = 3,
        };
        App->Renderer = Renderer_Create(&Config);

        /* configure */
        {
            renderer_msaa_flags Samples = Renderer_GetAvailableMSAAFlags(App->Renderer);
            if ((Samples & RENDERER_MSAA_4X))
            {
                Renderer_SetScreenMSAA(App->Renderer, RENDERER_MSAA_4X);
            }
        }
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
    }

    /* textures */
    {
        const char *TextureNames[] = {
            "assets/gradient.bmp",
        };

        STATIC_ASSERT(STATIC_ARRAY_SIZE(TextureNames) <= TEXTURE_COUNT - 1, "Too many textures");
        renderer_sampler_config SamplerConfig = {
            .MagFilter = RENDERER_FILTER_LINEAR,
            .MinFilter = RENDERER_FILTER_LINEAR,
            .EnableAnisotropyFiltering = false,
        };
        renderer_sampler_handle Sampler = Renderer_CreateSampler(App->Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &SamplerConfig);
        for (int i = 0; i < (int)STATIC_ARRAY_SIZE(TextureNames); i++)
        {
            App->Textures[i] = LoadTexture(App->Renderer, Sampler, TextureNames[i], &App->Arena);
        }

        App->CustomTextureWidth = 256;
        App->CustomTextureHeight = 256;
        App->Textures[TEXTURE_COUNT - 1] = Renderer_CreateMutableTexture(
            App->Renderer, 
            RENDERER_GLOBAL_RESOURCE_GROUP, 
            &(renderer_mutable_texture_config) {
                .Format = RENDERER_IMAGE_FORMAT_RGBA,
                .Width = App->CustomTextureWidth,
                .Height = App->CustomTextureHeight,
                .SamplerHandle = Sampler,
            }, 
            (void **)&App->CustomTexture, 
            &App->CustomTextureSizeBytes
        );
    }

    /* resource binding (in shader) */
    {
        renderer_resource_binding_config Config = {
            .UniformBufferBinding = 0,
            .TextureArrayBinding = 1,
        };
        App->ResourceGroupBinding = Renderer_BindResourceGroup(App->Renderer, &Config);
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
        App->GraphicsPipeline = Renderer_CreateGraphicsPipeline(
            App->Renderer, 
            App->ResourceGroupBinding, 
            &GraphicsPipelineConfig
        );
    }
}
