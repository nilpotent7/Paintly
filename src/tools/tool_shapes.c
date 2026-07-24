#include "tool.h"
#include "../core/history.h"
#include <math.h>

typedef enum { SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE } ShapeKind;

static void shape_path(cairo_t *cr, ShapeKind kind,
                       double x0, double y0, double x1, double y1)
{
    double l = MIN(x0, x1), t = MIN(y0, y1);
    double w = MAX(1, fabs(x1 - x0)), h = MAX(1, fabs(y1 - y0));

    switch (kind) {
    case SHAPE_RECT:
        cairo_rectangle(cr, l, t, w, h);
        break;
    case SHAPE_ELLIPSE:
        /* Build a unit circle in a temporarily squashed coordinate space;
         * restoring before the stroke keeps the line width uniform. */
        cairo_save(cr);
        cairo_translate(cr, l + w / 2, t + h / 2);
        cairo_scale(cr, w / 2, h / 2);
        cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
        cairo_restore(cr);
        break;
    case SHAPE_TRIANGLE:  /* isosceles: apex top-center */
        cairo_move_to(cr, l + w / 2, t);
        cairo_line_to(cr, l + w, t + h);
        cairo_line_to(cr, l, t + h);
        cairo_close_path(cr);
        break;
    }
}

static void shape_stroke(cairo_t *cr, ToolContext *c, ShapeKind kind)
{
    gdk_cairo_set_source_rgba(cr, &c->color);
    cairo_set_line_width(cr, MAX(1.0, c->size));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    shape_path(cr, kind, c->start_x, c->start_y, c->x, c->y);
    cairo_stroke(cr);
}

static gboolean too_small(ToolContext *c)
{
    return fabs(c->x - c->start_x) < 1 && fabs(c->y - c->start_y) < 1;
}

static void shapes_motion(Tool *t, ToolContext *c)
{
    canvas_repaint(c->app);   /* the preview lives in overlay() */
}

static void shapes_end(Tool *t, ToolContext *c)
{
    if (too_small(c))         /* a plain click stamps nothing */
        return;
    history_push(c->doc);
    cairo_t *cr = cairo_create(c->layer->surface);
    shape_stroke(cr, c, (ShapeKind) GPOINTER_TO_INT(t->data));
    cairo_destroy(cr);
    canvas_repaint(c->app);
}

static void shapes_overlay(Tool *t, ToolContext *c, cairo_t *cr)
{
    if (!c->button || too_small(c))   /* only while dragging */
        return;
    shape_stroke(cr, c, (ShapeKind) GPOINTER_TO_INT(t->data));
}

Tool tool_shape_rect = {
    .id      = "shape-rect",
    .label   = "Rectangle",
    .icon    = "paintly-shape-rect",
    .data    = GINT_TO_POINTER(SHAPE_RECT),
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
};

Tool tool_shape_ellipse = {
    .id      = "shape-ellipse",
    .label   = "Ellipse",
    .icon    = "paintly-shape-ellipse",
    .data    = GINT_TO_POINTER(SHAPE_ELLIPSE),
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
};

Tool tool_shape_triangle = {
    .id      = "shape-triangle",
    .label   = "Triangle",
    .icon    = "paintly-shape-triangle",
    .data    = GINT_TO_POINTER(SHAPE_TRIANGLE),
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
};
