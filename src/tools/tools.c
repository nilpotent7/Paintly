#include "tool.h"
#include <string.h>

static Tool *ALL[] = {
    &tool_select,
    &tool_pencil,
    &tool_fill,
    &tool_eraser,
    &tool_brush,
    &tool_shape_rect,
    &tool_shape_ellipse,
    &tool_shape_triangle,
};

Tool **tools_all(int *count)
{
    *count = G_N_ELEMENTS(ALL);
    return ALL;
}

Tool *tools_find(const char *id)
{
    for (guint i = 0; i < G_N_ELEMENTS(ALL); i++)
        if (strcmp(ALL[i]->id, id) == 0)
            return ALL[i];
    return NULL;
}
