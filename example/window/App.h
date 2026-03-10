#ifndef APP_H
#define APP_H

#include "Common.h"
#include "Renderer-Core.h"

struct app
{
    renderer_handle Renderer;
    renderer_mesh_handle FullScreenMesh;
    renderer_graphics_pipeline_handle GraphicsPipeline;
};

#endif /* APP_H */

