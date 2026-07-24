#include "tool.h"
#include "../core/history.h"
#include "../core/pixelops.h"

static void fill_begin(Tool *t, ToolContext *c)
{
    history_push(c->doc);
    guint32 px = pixel_from_rgba(c->color.red, c->color.green,
                                 c->color.blue, c->color.alpha);
    flood_fill(c->layer->surface, (int) c->x, (int) c->y, px);
    canvas_repaint(c->app);
}

Tool tool_fill = {
    .id    = "fill",
    .label = "Fill - flood an area with color",
    .icon  = "paintly-fill",
    .begin = fill_begin,
};
