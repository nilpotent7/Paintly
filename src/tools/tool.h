#pragma once

#include "../app.h"

typedef struct ToolContext {
    App      *app;
    Document *doc;
    Layer    *layer;       /* the active layer - draw on layer->surface   */
    GdkRGBA   color;       /* primary for left button, secondary for right */
    double    size;        /* current stroke width                         */
    double    x, y;        /* pointer position, canvas pixels              */
    double    last_x, last_y;   /* position at the previous motion event   */
    double    start_x, start_y; /* position where the button went down     */
    guint     button;      /* 1 = left, 3 = right; 0 in overlay() while idle */
    double    zoom;        /* current zoom (for 1px-on-screen overlay lines) */
} ToolContext;

struct Tool {
    const char *id;        /* stable string id, e.g. "pencil"   */
    const char *label;     /* tooltip text                      */
    const char *icon;      /* bundled icon name (data/icons/)   */
    const char *cursor;    /* cursor over the canvas, NULL = crosshair */
    gpointer    data;      /* per-tool parameters (optional)    */

    /* All callbacks are optional (NULL = no-op). */
    void (*begin) (Tool *t, ToolContext *c);            /* button pressed  */
    void (*motion)(Tool *t, ToolContext *c);            /* pointer dragged */
    void (*end)   (Tool *t, ToolContext *c);            /* button released */
    /* Draw transient feedback (shape previews…) above the composited
     * canvas.  The cairo context is already scaled to canvas coordinates. */
    void (*overlay)(Tool *t, ToolContext *c, cairo_t *cr);
    void (*restyle)(Tool *t, App *app);
    /* Called when the user switches to another tool. */
    void (*deactivate)(Tool *t, App *app);
};

extern Tool tool_pencil;
extern Tool tool_brush;
extern Tool tool_eraser;
extern Tool tool_fill;
extern Tool tool_shape_rect;
extern Tool tool_shape_ellipse;
extern Tool tool_shape_triangle;
extern Tool tool_shape_line;
extern Tool tool_select;

Tool **tools_all (int *count);
Tool  *tools_find(const char *id);

/* Set `style` on `cr`, with the dash lengths scaled to the stroke width.
 * `offset` carries the pattern across the segments of one freehand stroke. */
void tool_set_dash(cairo_t *cr, DashStyle style, double width, double offset);
