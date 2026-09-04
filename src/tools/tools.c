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
    &tool_shape_line,
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

/* Dash lengths are multiples of the stroke width, so a pattern reads the same
 * at any size.  A zero-length "on" segment is how cairo draws a dot, which is
 * why every dashed stroke also needs a round line cap. */
void tool_set_dash(cairo_t *cr, DashStyle style, double width, double offset)
{
    double w = MAX(1.0, width);

    switch (style) {
    case DASH_DASH:
        cairo_set_dash(cr, (double[]){ 3 * w, 2.5 * w }, 2, offset);
        break;
    case DASH_DOT:
        cairo_set_dash(cr, (double[]){ 0, 2 * w }, 2, offset);
        break;
    case DASH_DASH_DOT:
        cairo_set_dash(cr, (double[]){ 3 * w, 2 * w, 0, 2 * w }, 4, offset);
        break;
    default:
        cairo_set_dash(cr, NULL, 0, 0);
        break;
    }
}
