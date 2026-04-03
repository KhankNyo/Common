#ifndef APP_H
#define APP_H

#include "Common.h"
#include "Renderer-Core.h"


typedef_struct(vertex_buffer);
struct vertex_buffer
{
    rgba Color;
    v3f Pos;
    u32 Pad0;
};

struct app
{
    renderer_handle Renderer;
    renderer_mesh_handle FullScreenMesh;
    renderer_mesh_handle MutableMesh;
    renderer_graphics_pipeline_handle GraphicsPipeline;
    renderer_resource_group_handle ResourceGroup;
    renderer_resource_binding ResourceBinding;

    isize VertexCapacity;
    isize IndexCapacity;
    vertex_buffer *VertexPtr;
    u32 *IndexPtr;
};

#endif /* APP_H */

