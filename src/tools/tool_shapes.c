#include "tool.h"
#include "../core/history.h"
#include <math.h>

typedef enum { SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE } ShapeKind;
typedef enum { SHAPE_IDLE, SHAPE_DRAG, SHAPE_MOVE, SHAPE_RESIZE } ShapePhase;

static ShapePhase phase = SHAPE_IDLE;
static double grab_dx, grab_dy;   // pointer offset inside the floater

static struct {
    gboolean  valid;
    ShapeKind kind;
    Rect      geom;        // shape bounds in canvas px, before stroke padding
    gboolean  secondary;   // drawn with the right button -> follows color 2
} live;

static gboolean live_shape(Document *d)
{
    return live.valid && d->floating && d->has_selection;
}

/* Room for the stroke, which straddles the path by half its width. */
static int stroke_pad(double width)
{
    return (int) ceil(MAX(1.0, width) / 2.0) + 1;
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

static void shape_style(cairo_t *cr, const GdkRGBA *color, double width)
{
    gdk_cairo_set_source_rgba(cr, color);
    cairo_set_line_width(cr, MAX(1.0, width));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

static void shape_render(App *a)
{
    Document *d = a->doc;
    int pad = stroke_pad(a->brush_size);
    Rect box = { live.geom.x - pad,     live.geom.y - pad,
                 live.geom.w + 2 * pad, live.geom.h + 2 * pad };

    document_set_selection(d, box);
    document_create_floating(d, d->selection);

    cairo_t *cr = cairo_create(d->floating);
    cairo_translate(cr, -d->selection.x, -d->selection.y);
    shape_style(cr, live.secondary ? &a->secondary : &a->primary, a->brush_size);
    shape_path(cr, live.kind, live.geom.x, live.geom.y,
               live.geom.x + live.geom.w, live.geom.y + live.geom.h);
    cairo_stroke(cr);
    cairo_destroy(cr);
}

static gboolean too_small(ToolContext *c)
{
    return fabs(c->x - c->start_x) < 1 && fabs(c->y - c->start_y) < 1;
}

static void shapes_begin(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    Handle h = document_hit_handle(d, c->x, c->y, c->zoom);

    if (h != HANDLE_NONE) {
        document_resize_begin(d, h);
        phase = SHAPE_RESIZE;
    } else if (document_point_in_selection(d, c->x, c->y)) {
        document_lift_selection(d);        // no-op if already floating
        phase = SHAPE_MOVE;
        grab_dx = c->x - d->float_x;
        grab_dy = c->y - d->float_y;
    } else {
        live.valid = FALSE;
        document_deselect(d);              // commits any floating pixels
        phase = SHAPE_DRAG;
    }

    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void shapes_motion(Tool *t, ToolContext *c)
{
    Document *d = c->doc;

    if (phase == SHAPE_MOVE) {
        // Snaps to whole pixels and stops at the canvas edge.
        int px = d->selection.x, py = d->selection.y;
        document_move_floating(d, c->x - grab_dx, c->y - grab_dy);
        if (live_shape(d)) {              // keep the description in step
            live.geom.x += d->selection.x - px;
            live.geom.y += d->selection.y - py;
        }
    } else if (phase == SHAPE_RESIZE) {
        Rect box = document_resize_rect(d, c->x, c->y);
        if (live_shape(d)) {
            int pad = stroke_pad(c->app->brush_size);
            live.geom = (Rect){ box.x + pad, box.y + pad,
                                MAX(1, box.w - 2 * pad), MAX(1, box.h - 2 * pad) };
            shape_render(c->app);
        } else {
            document_resize_to(d, box);
        }
    }

    canvas_repaint(c->app);   // the preview lives in overlay()
    statusbar_update(c->app);
}

static void shapes_end(Tool *t, ToolContext *c)
{
    Document *d = c->doc;

    if (phase == SHAPE_RESIZE) {
        document_resize_end(d);
        phase = SHAPE_MOVE;
        return;
    }
    if (phase == SHAPE_MOVE)
        return;
    if (too_small(c)) {                    // a plain click stamps nothing
        phase = SHAPE_IDLE;
        return;
    }

    /* One entry covers the whole draw-restyle-move-commit gesture. */
    history_push(d);

    live.valid     = TRUE;
    live.kind      = (ShapeKind) GPOINTER_TO_INT(t->data);
    live.secondary = (c->button == GDK_BUTTON_SECONDARY);
    live.geom = (Rect){ (int) floor(MIN(c->start_x, c->x)),
                        (int) floor(MIN(c->start_y, c->y)),
                        (int) MAX(1, fabs(c->x - c->start_x)),
                        (int) MAX(1, fabs(c->y - c->start_y)) };
    shape_render(c->app);

    phase = SHAPE_MOVE;      // the fresh shape is immediately draggable
    canvas_repaint(c->app);
}

static void shapes_overlay(Tool *t, ToolContext *c, cairo_t *cr)
{
    if (!c->button || too_small(c) || phase != SHAPE_DRAG)   // only while dragging
        return;

    shape_style(cr, &c->color, c->size);
    shape_path(cr, (ShapeKind) GPOINTER_TO_INT(t->data),
               c->start_x, c->start_y, c->x, c->y);
    cairo_stroke(cr);
}

static void shapes_restyle(Tool *t, App *a)
{
    if (live_shape(a->doc))
        shape_render(a);
}

static void shapes_deactivate(Tool *t, App *a)
{
    phase = SHAPE_IDLE;
    live.valid = FALSE;
    document_resize_end(a->doc);
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
    .restyle = shapes_restyle,
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
    .restyle = shapes_restyle,
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
    .restyle = shapes_restyle,
    .deactivate = shapes_deactivate,
};
