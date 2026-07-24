#include "tool.h"
#include "../core/history.h"
#include <math.h>

typedef enum { SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE } ShapeKind;
typedef enum { SHAPE_IDLE, SHAPE_DRAG, SHAPE_MOVE } ShapePhase;

static ShapePhase phase = SHAPE_IDLE;
static double grab_dx, grab_dy;   // pointer offset inside the floater

static gboolean point_in_selection(Document *d, double x, double y)
{
    if (!d->has_selection)
        return FALSE;
    double sx = d->floating ? d->float_x : d->selection.x;
    double sy = d->floating ? d->float_y : d->selection.y;
    return x >= sx && y >= sy && x < sx + d->selection.w && y < sy + d->selection.h;
}

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

static void shape_stroke(cairo_t *cr, ToolContext *c, int startOffset, int xOff, int yOff, ShapeKind kind)
{
    gdk_cairo_set_source_rgba(cr, &c->color);
    cairo_set_line_width(cr, MAX(1.0, c->size));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    shape_path(cr, kind, c->start_x - xOff + startOffset, c->start_y - yOff + startOffset, c->x - xOff + startOffset, c->y - yOff + startOffset);
    cairo_stroke(cr);
}

static gboolean too_small(ToolContext *c)
{
    return fabs(c->x - c->start_x) < 1 && fabs(c->y - c->start_y) < 1;
}

static void shapes_begin(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    if (point_in_selection(d, c->x, c->y)) {
        phase = SHAPE_MOVE;
        grab_dx = c->x - d->float_x;
        grab_dy = c->y - d->float_y;
    } else {
        document_deselect(d);    // commits any floating pixels
        phase = SHAPE_DRAG;
    }
    
    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void shapes_motion(Tool *t, ToolContext *c)
{
    if (phase == SHAPE_MOVE) {
        // Snap to whole pixels so the drop is always crisp.
        c->doc->float_x = round(c->x - grab_dx);
        c->doc->float_y = round(c->y - grab_dy);
        c->doc->selection.x = (int) c->doc->float_x;
        c->doc->selection.y = (int) c->doc->float_y;
    }

    canvas_repaint(c->app);   // the preview lives in overlay()
}

static void shapes_end(Tool *t, ToolContext *c)
{
    if (phase == SHAPE_MOVE || too_small(c))         // a plain click stamps nothing
        return;

    phase = SHAPE_MOVE;

    history_push(c->doc);

    document_commit_floating(c->doc); // Commit any current floating surface

    int x = floor(MIN(c->start_x, c->x));
    int y = floor(MIN(c->start_y, c->y));
    int width = (int) MAX(1, fabs(c->last_x - c->start_x));
    int height = (int) MAX(1, fabs(c->last_y - c->start_y));

    // Stroke Width Padding
    x -= c->app->brush_size;
    y -= c->app->brush_size;
    width += 2*c->app->brush_size;
    height += 2*c->app->brush_size;
    
    Rect r = { x, y, width, height };

    document_select_rect(c->doc, r);
    document_create_floating(c->doc, c->doc->selection); // Create a new floating surface

    cairo_t *cr = cairo_create(c->doc->floating); // paint to floating layer
    
    int XStartOffset = c->doc->selection.x + c->app->brush_size;
    int YStartOffset = c->doc->selection.y + c->app->brush_size;
    shape_stroke(cr, c, c->app->brush_size, XStartOffset, YStartOffset, (ShapeKind) GPOINTER_TO_INT(t->data));
    
    cairo_destroy(cr);
    canvas_repaint(c->app);
}

static void shapes_overlay(Tool *t, ToolContext *c, cairo_t *cr)
{
    if (!c->button || too_small(c) || phase != SHAPE_DRAG)   // only while dragging
        return;
    
    shape_stroke(cr, c, 0, 0, 0, (ShapeKind) GPOINTER_TO_INT(t->data));
}

static void shapes_deactivate(Tool *t, App *a)
{
    phase = SHAPE_IDLE;
    document_deselect(a->doc);
    canvas_repaint(a);
    statusbar_update(a);
    layers_panel_queue_thumbs(a);   // committing may have changed pixels
}

Tool tool_shape_rect = {
    .id      = "shape-rect",
    .label   = "Rectangle",
    .icon    = "paintly-shape-rect",
    .data    = GINT_TO_POINTER(SHAPE_RECT),
    .begin   = shapes_begin,
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
    .deactivate = shapes_deactivate,
};

Tool tool_shape_ellipse = {
    .id      = "shape-ellipse",
    .label   = "Ellipse",
    .icon    = "paintly-shape-ellipse",
    .data    = GINT_TO_POINTER(SHAPE_ELLIPSE),
    .begin   = shapes_begin,
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
    .deactivate = shapes_deactivate,
};

Tool tool_shape_triangle = {
    .id      = "shape-triangle",
    .label   = "Triangle",
    .icon    = "paintly-shape-triangle",
    .data    = GINT_TO_POINTER(SHAPE_TRIANGLE),
    .begin   = shapes_begin,
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
    .deactivate = shapes_deactivate,
};
