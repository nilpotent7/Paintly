#include "tool.h"
#include "../core/history.h"

typedef struct {
    cairo_operator_t  op;
    cairo_antialias_t antialias;
} StrokeKind;

static const StrokeKind KIND_PENCIL = { CAIRO_OPERATOR_OVER,  CAIRO_ANTIALIAS_NONE    };
static const StrokeKind KIND_BRUSH  = { CAIRO_OPERATOR_OVER,  CAIRO_ANTIALIAS_DEFAULT };
static const StrokeKind KIND_ERASER = { CAIRO_OPERATOR_CLEAR, CAIRO_ANTIALIAS_NONE    };

/* Create a cairo context on the active layer, configured for this stroke. */
static cairo_t *stroke_cr(Tool *t, ToolContext *c)
{
    const StrokeKind *k = t->data;
    cairo_t *cr = cairo_create(c->layer->surface);
    cairo_set_operator(cr, k->op);
    cairo_set_antialias(cr, k->antialias);
    gdk_cairo_set_source_rgba(cr, &c->color);
    cairo_set_line_width(cr, MAX(1.0, c->size));
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    return cr;
}

static void stroke_begin(Tool *t, ToolContext *c)
{
    /* Snapshot the layer before the first pixel changes. */
    history_push(c->doc);

    /* A zero-length line draws nothing in cairo, so a plain click needs an
     * explicit dot. */
    cairo_t *cr = stroke_cr(t, c);
    cairo_arc(cr, c->x, c->y, MAX(1.0, c->size) / 2.0, 0, 2 * G_PI);
    cairo_fill(cr);
    cairo_destroy(cr);
    canvas_repaint(c->app);
}

static void stroke_motion(Tool *t, ToolContext *c)
{
    cairo_t *cr = stroke_cr(t, c);
    cairo_move_to(cr, c->last_x, c->last_y);
    cairo_line_to(cr, c->x, c->y);
    cairo_stroke(cr);
    cairo_destroy(cr);
    canvas_repaint(c->app);
}

Tool tool_pencil = {
    .id     = "pencil",
    .label  = "Pencil - hard-edged strokes (left: color 1, right: color 2)",
    .icon   = "paintly-pencil",
    .data   = (gpointer) &KIND_PENCIL,
    .begin  = stroke_begin,
    .motion = stroke_motion,
};

Tool tool_brush = {
    .id     = "brush",
    .label  = "Brush - smooth antialiased strokes",
    .icon   = "paintly-brush",
    .data   = (gpointer) &KIND_BRUSH,
    .begin  = stroke_begin,
    .motion = stroke_motion,
};

Tool tool_eraser = {
    .id     = "eraser",
    .label  = "Eraser - erases to transparency",
    .icon   = "paintly-eraser",
    .data   = (gpointer) &KIND_ERASER,
    .begin  = stroke_begin,
    .motion = stroke_motion,
};
