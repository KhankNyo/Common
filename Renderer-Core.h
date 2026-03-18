
#ifdef __cplusplus
extern "C" {
#endif


#ifndef RENDERER_CORE_H
#define RENDERER_CORE_H

#include "Common.h"
#include "Profiler.h"


typedef_struct(renderer_vertex_attributes);
typedef_struct(renderer_vertex_description);
typedef_struct(renderer_graphics_pipeline_config);
typedef_struct(renderer);
typedef renderer *renderer_handle;
typedef_struct(renderer_draw_pipeline);
typedef_struct(renderer_draw_pipeline_group);
typedef_struct(renderer_scissor);

#define NEW_API

#if !defined(NEW_API)
typedef handle(u32) renderer_mesh_handle; 
typedef handle(u32) renderer_texture_handle;
typedef handle(u32) renderer_graphics_pipeline_handle;
#else
typedef handle(u64) renderer_graphics_pipeline_handle;
typedef handle(u64) renderer_resource_group_handle;
typedef handle(u64) renderer_sampler_handle;
typedef handle(u64) renderer_mesh_handle;
typedef handle(u64) renderer_texture_handle;
typedef handle(u64) renderer_uniform_buffer_handle;

typedef_struct(renderer_resource_group_config);
typedef_struct(renderer_texture_config);
typedef_struct(renderer_mesh_config);
typedef_struct(renderer_sampler_config);
typedef_struct(renderer_uniform_buffer_config);
#endif


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
    RENDERER_GFXFT_NONE = 0,
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

struct renderer_draw_pipeline
{
    renderer_graphics_pipeline_handle GraphicsPipelineHandle;
    i32 GroupCount;
    struct renderer_draw_pipeline_group {
        renderer_mesh_handle MeshHandle;
        u32 Mesh_IndexBuffer_FirstElement;      /* index of the first element in the index buffer to start drawing from */
        u32 Mesh_IndexBuffer_ElementCount;      /* the number of indices starting from the first element in the index buffer, 
                                                    can be 0 to use all elements from the first element specified. */
        /* if Width == 0, Width will be the screen width
         * if Height == 0, Height will be the screen height */
        struct renderer_scissor {
            i32 OffsetX, OffsetY;               /* offsets are relative to the top left of the screen */
            u32 Width, Height;                  /* screen width and height */
        } Scissor;
    } *Groups;
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
#if !defined(NEW_API)
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
 * can be called any time after Renderer_Init() and before Renderer_Destroy(), 
 * and inside the lifetime of the mesh handle */
renderer_result Renderer_UpdateMesh(
    renderer_handle Renderer, 
    renderer_mesh_handle Handle,
    const void *VertexBuffer, isize VertexCount, isize VertexElemSizeBytes,
    const u32 *IndexBuffer, isize IndexCount
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

/* call this whenever a uniform needs to be updated
 * after Renderer_CreateGraphicsPipelines()
 * */
void Renderer_UpdateUniformBuffer(
    renderer_handle Renderer, 
    const void *Data, isize SizeBytes
);



#else

#define RENDERER_GLOBAL_RESOURCE_GROUP (renderer_resource_group_handle) { 0 }
#define RENDERER_DEFAULT_SAMPLER (renderer_sampler_handle) { 0 }

/*
 Op order: 
    Renderer_CreateResourceGroup()
        Renderer_CreateSampler()
            Renderer_CreateStaticTexture()
            Renderer_CreateMutableTexture()
        Renderer_CreateStaticMesh()
        Renderer_CreateMutableMesh()
    Renderer_BindResourceGroup()
    Renderer_CreateGraphicsPipeline()

    Renderer_UpdateMutableMesh();
    Renderer_UpdateMutableTexture();
    Renderer_UpdateUniformBuffer();
    Renderer_Draw();

*/

typedef enum 
{
    RENDERER_FILTER_NEAREST = 0,
    RENDERER_FILTER_LINEAR = 1,
} renderer_filter_type;


struct renderer_resource_group_config 
{
    isize ImagePoolSizeBytes;
    isize GpuBufferPoolSizeBytes;
    isize CpuBufferPoolSizeBytes;
    isize UniformBufferSizeBytes;

    u32 UniformBufferBinding;   /* must match in shader */
    u32 TextureArrayBinding;    /* must match in shader */
};

struct renderer_sampler_config
{
    renderer_filter_type MagFilter;
    renderer_filter_type MinFilter;
    bool8 EnableAnisotropyFiltering;
};

struct renderer_texture_config
{
    renderer_sampler_handle SamplerHandle;
    renderer_image_format Format;
    u32 Width, Height;
    int MipLevels;
};

struct renderer_mesh_config
{
    isize VertexBufferElementSizeBytes;
    isize VertexCount;
    isize IndexCount;
};



renderer_resource_group_handle Renderer_CreateResourceGroup(
    renderer_handle Renderer,
    const renderer_resource_group_config *ResourceGroupConfig
);
void Renderer_DestroyResourceGroup(
    renderer_handle Renderer, 
    renderer_resource_group_handle ResourceGroup
);
void Renderer_BindResourceGroup(
    renderer_handle Renderer,
    renderer_resource_group_handle ResourceGroup
);

renderer_sampler_handle Renderer_CreateSampler(
    renderer_handle Renderer, 
    renderer_resource_group_handle ResourceGroup,
    const renderer_sampler_config *TextureSamplerConfig
);
/* returns a texture handle, or the default texture (handle 0) if unable to create texture
 * also returns a pointer to a staging buffer in OutTextureBuffer. 
 * The staging buffer will have a capacity of Width*Height*4
 * NOTE: only RENDERER_IMAGE_FORMAT_RGBA and RENDERER_IMAGE_FORMAT_BGRA are supported for mutable textures
 * */
renderer_texture_handle Renderer_CreateMutableTexture(
    renderer_handle Renderer,
    renderer_resource_group_handle ResourceGroup,
    const renderer_texture_config *TextureConfig
);
renderer_mesh_handle Renderer_CreateMutableMesh(
    renderer_handle Renderer, 
    renderer_resource_group_handle ResourceGroup,
    const renderer_mesh_config *MeshConfig
);
renderer_texture_handle Renderer_CreateStaticTexture(
    renderer_handle Renderer,
    renderer_resource_group_handle ResourceGroup,
    const renderer_texture_config *TextureConfig,
    const void *Image
);
renderer_mesh_handle Renderer_CreateStaticMesh(
    renderer_handle Renderer,
    renderer_resource_group_handle ResourceGroup,
    const renderer_mesh_config *MeshConfig,
    const void *VertexBuffer,
    const u32 *IndexBuffer
);

renderer_graphics_pipeline_handle Renderer_CreateGraphicsPipeline(
    renderer_handle Renderer,
    renderer_resource_group_handle ResourceGroupHandle,
    const renderer_graphics_pipeline_config *Config
);



/* NOTE: Width and Height must be less than or equal to the width and height passed to Renderer_CreateMutableTexture() */
void Renderer_UpdateMutableTexture(
    renderer_handle Renderer, 
    renderer_texture_handle MutableTexture,
    const void *Buffer, u32 Width, u32 Height
);

/* updatese a mutable mesh */
void Renderer_UpdateMutableMesh(
    renderer_handle Renderer, 
    renderer_mesh_handle MutableMesh,
    const void *VertexBuffer, isize VertexCount, 
    const u32 *IndexBuffer, isize IndexCount
);

void Renderer_UpdateUniformBuffer(
    renderer_handle Renderer,
    renderer_uniform_buffer_handle UniformBuffer,
    const void *Data, isize SizeBytes
);




force_inline void Renderer__InitDefaultResources(renderer_handle Renderer)
{
    /* default sampler */
    {
        renderer_sampler_config SamplerConfig = {
            .MagFilter = RENDERER_FILTER_LINEAR,
            .MinFilter = RENDERER_FILTER_LINEAR,
            .EnableAnisotropyFiltering = false,
        };
        renderer_sampler_handle Sampler = Renderer_CreateSampler(Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &SamplerConfig);
        (void)Sampler;
    }

    /* default texture */
    {
        renderer_texture_config TextureConfig = {
            .SamplerHandle = { 0 },
            .Format = RENDERER_IMAGE_FORMAT_RGBA, 
            .Width = 1,
            .Height = 1,
        };
        u32 Pink = 0xFFFF00FF;
        renderer_texture_handle Texture = Renderer_CreateStaticTexture(Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &TextureConfig, &Pink);
        (void)Texture;
    }

    /* default mesh: full screen rectangle */
    {
        float VertexBuffer[] = {
            -1, 1, 0, 
            1, 1, 0,
            1, -1, 0, 
            -1, -1, 0
        };
        u32 IndexBuffer[] = {
            0, 1, 2, 
            2, 3, 0
        };
        renderer_mesh_config MeshConfig = {
            .VertexBufferElementSizeBytes = sizeof(float) * 3,
            .VertexCount = 4,
            .IndexCount = 6,
        };
        renderer_mesh_handle Mesh = Renderer_CreateStaticMesh(Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &MeshConfig, VertexBuffer, IndexBuffer);
        (void)Mesh;
    }

    /* default graphics pipeline */
    {
        renderer_vertex_attributes Attrib = {
            .Binding = 0, .Location = 0, .Offset = 0, .Type = RENDERER_TYPE_F32x3,
        };
        renderer_vertex_description VertexDesc = {
            .Binding = 0, 
            .Stride = sizeof(float)*3, 
            .AttribCount = 1,
            .Attribs = &Attrib,
        };
        renderer_graphics_pipeline_config Config = {
            .EnabledGraphicsFeatures = RENDERER_GFXFT_NONE,
            .FragShaderCode = NULL,
            .FragShaderCodeSizeBytes = 0,
            .VertShaderCode = NULL,
            .VertShaderCodeSizeBytes = 0,
            .VertexDescription = &VertexDesc,
        };
        renderer_graphics_pipeline_handle GraphicsPipeline = Renderer_CreateGraphicsPipeline(Renderer, RENDERER_GLOBAL_RESOURCE_GROUP, &Config);
        (void)GraphicsPipeline;
    }
}

#endif

/* call on a specific event */
void Renderer_OnFramebufferResize(renderer_handle Renderer, int Width, int Height);


/* One actual draw command will be issued for each group in a pipeline */
void Renderer_Draw(
    renderer_handle Renderer, 
    const renderer_draw_pipeline *DrawPipelines, i32 DrawPipelineCount
);


#endif /* RENDERER_CORE_H */


#ifdef __cplusplus
}
#endif
