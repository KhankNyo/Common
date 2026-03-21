#ifndef APP_H
#define APP_H

#include "Common.h"
#include "Renderer-Core.h"

struct app
{
    renderer_handle Renderer;
    renderer_mesh_handle FullScreenMesh;
    renderer_graphics_pipeline_handle GraphicsPipeline;
#ifdef NEW_API
    renderer_resource_group_handle ResourceGroup;
#endif
};

#endif /* APP_H */

