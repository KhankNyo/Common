#ifndef RENDERER_CORE_H
#define RENDERER_CORE_H

#include "Common.h"
#include "Memory.h"
#include "Profiler.h"

#define RENDERER_IS_HANDLE_VALID(handle) ((handle).Value != 0)
typedef handle(u32) renderer_mesh_handle; 
typedef handle(u32) renderer_texture_handle;
typedef handle(u32) renderer_resource_handle;
typedef handle(u32) renderer_graphics_pipeline_handle;
typedef_struct(renderer_vertex_attributes);
typedef_struct(renderer_vertex_description);
typedef_struct(renderer_graphics_pipeline_config);
typedef_struct(renderer);
typedef renderer *renderer_handle;
typedef_struct(renderer_draw_group);
typedef_struct(renderer_scissor);


typedef enum 
{
    RENDERER_SUCCESS = 0,
    RENDERER_ERROR_INVALID_HANDLE = 1,
    RENDERER_ERROR_OUT_OF_MEMORY = 2,
} renderer_result;

typedef enum 
{
    RENDERER_IMAGE_FORMAT_RGBA = 1,
    RENDERER_IMAGE_FORMAT_BGRA = 2,
    /* compressed data: */
    RENDERER_IMAGE_FORMAT_BC1  = 3,
    RENDERER_IMAGE_FORMAT_BC2  = 4,
    RENDERER_IMAGE_FORMAT_BC3  = 5,

    RENDERER_IMAGE_FORMAT_DXT1 = RENDERER_IMAGE_FORMAT_BC1,
    RENDERER_IMAGE_FORMAT_DXT2 = RENDERER_IMAGE_FORMAT_BC1,
    RENDERER_IMAGE_FORMAT_DXT3 = RENDERER_IMAGE_FORMAT_BC2,
    RENDERER_IMAGE_FORMAT_DXT4 = RENDERER_IMAGE_FORMAT_BC2,
    RENDERER_IMAGE_FORMAT_DXT5 = RENDERER_IMAGE_FORMAT_BC3,
} renderer_image_format;

typedef enum 
{
    RENDERER_TYPE_F32x1 = 1,
    RENDERER_TYPE_F32x2 = 2,
    RENDERER_TYPE_F32x3 = 3,
    RENDERER_TYPE_F32x4 = 4,
    RENDERER_TYPE_U32x1 = 5,
} renderer_type;

typedef enum 
{
    RENDERER_GFXFT_BACKFACE_CULLING     = 1 << 0,
    RENDERER_GFXFT_BLENDING             = 1 << 1,
    RENDERER_GFXFT_Z_BUFFER             = 1 << 2,
    RENDERER_GFXFT_MSAA                 = 1 << 3,
    RENDERER_GFXFT_SAMPLE_SHADING       = 1 << 4, /* anti aliasing within a texture */

    RENDERER_GFXFT_ALL = 0x1F,
} renderer_graphics_feature_flags;

typedef enum 
{
    RENDERER_CULLING_CLOCKWISE = 0,
    RENDERER_CULLING_COUNTER_CLOCKWISE = 1,
} renderer_culling_direction;

struct renderer_graphics_pipeline_config
{
    renderer_graphics_feature_flags EnabledGraphicsFeatures;
    renderer_culling_direction CullingDirection;
    float SampleShadingMin;

    const u8 *FragShaderCode;
    isize FragShaderCodeSizeBytes;

    const u8 *VertShaderCode;
    isize VertShaderCodeSizeBytes; 

    const struct renderer_vertex_description
    {
        int Stride;
        int Binding;
        int AttribCount;
        struct renderer_vertex_attributes {
            int Binding;
            int Location;
            int Offset;
            renderer_type Type;
        } *Attribs;
    } *VertexDescription; /* multiple pipelines can use the same vertex description */
};

struct renderer_scissor
{
    i32 OffsetX, OffsetY;
    u32 Width, Height;
    u32 MeshIndexBase;
    u32 MeshIndexCount; /* use UINT32_MAX to use the mesh's count */
    renderer_mesh_handle Mesh;
};

struct renderer_draw_group
{
    const renderer_scissor *Scissors;
    i32 ScissorCount;
    renderer_graphics_pipeline_handle GraphicsPipelineHandle;
};




/*
    Example workflow: 
        // app init code
            Renderer_Init(); 
                Renderer_CreateMesh();
                if (Renderer_IsMSAASampleCountSupported(SampleCount))
                    Renderer_UpdateMesh();
                    ... some other stuff
                Renderer_UpdateMesh();
                Renderer_UploadTexture();
            Renderer_CreateGraphicsPipelines();
        // app loop:
            while (application loop)
                ... do application stuff ...
                Renderer_UpdateMesh();
                if (frame buffer is resized)
                    Renderer_OnFramebufferResize();
                Renderer_Draw();
        // app deinit:
            Renderer_Destroy();
 */

renderer_handle Renderer_Init(const char *AppName, int FramesInFlight, bool32 ForceTripleBuffering, profiler *Profiler);
void Renderer_Destroy(renderer_handle Renderer);
bool32 Renderer_IsMSAASampleCountSupported(renderer_handle Renderer, int SampleCount);


/* 
 * Data management
 * */

/* uploads a texture on the gpu,
 * must be called between Renderer_Init() and Renderer_CreateGraphicsPipelines() */
renderer_texture_handle Renderer_UploadTexture(
    renderer_handle Renderer, 
    const void *Image, u32 Width, u32 Height, u32 MipLevels,
    renderer_image_format Format
);
/* create a mesh on the gpu, can be updated via Renderer_UpdateMesh(), 
 * must be called in between Renderer_Init() and Renderer_CreateGraphicsPipelines() */
renderer_mesh_handle Renderer_CreateMesh(
    renderer_handle Renderer, 
    isize VertexBufferCapacityBytes, 
    isize IndexBufferCapacityBytes
);
/* updates a mesh created by Renderer_CreateMesh(), 
 * can be called any time after Renderer_Init() and before Renderer_Destroy() */
renderer_result Renderer_UpdateMesh(
    renderer_handle Renderer, 
    renderer_mesh_handle Handle,
    const void *VertexBuffer, isize VertexCount, isize VertexElemSizeBytes,
    const u32 *Indices, isize IndexCount
);

/* 
 * call these after uploading/creating resources
 * */
void Renderer_CreateGraphicsPipelines(
    renderer_handle Renderer, 
    isize UniformBufferCapacity, 
#define RENDERER_NO_MSAA 1
    int MSAASampleCount,
    int GraphicsPipelineCount,
    const renderer_graphics_pipeline_config *GraphicsPipelineConfig, /* per pipeline */
    renderer_graphics_pipeline_handle *OutGraphicsPipelines
);


/* call this whenever a uniform is needed to be updated, 
 * after Renderer_CreateGraphicsPipelines()
 * */
void Renderer_UpdateUniformBuffer(
    renderer_handle Renderer, 
    const void *Data, isize SizeBytes
);

/* call on a specific event */
void Renderer_OnFramebufferResize(renderer_handle Renderer, int Width, int Height);

/* One pipeline will be used for each group
 * One draw command will be issued for each scissor within a group */
void Renderer_Draw(
    renderer_handle Renderer, 
    const renderer_draw_group *DrawGroups, isize DrawGroupCount
);


#endif /* RENDERER_CORE_H */

