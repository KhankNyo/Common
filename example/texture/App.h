#ifndef APP_H
#define APP_H

#include "Common.h"
#include "Renderer-Core.h"
#include "Memory.h"
#include "Arena.h"


#define TEXTURE_COUNT 3

struct app
{
    renderer_handle Renderer;
    renderer_mesh_handle FullScreenMesh;
    renderer_resource_binding ResourceGroupBinding;
    renderer_graphics_pipeline_handle GraphicsPipeline;

    uint SelectedTexture;
    renderer_texture_handle Textures[TEXTURE_COUNT];

    arena_alloc Arena;
};

#endif /* APP_H */

