#include "tool.h"
#include "../core/history.h"
#include <math.h>

typedef enum { SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE, SHAPE_LINE } ShapeKind;
typedef enum { SHAPE_IDLE, SHAPE_DRAG, SHAPE_MOVE, SHAPE_RESIZE } ShapePhase;

static ShapePhase phase = SHAPE_IDLE;
static double grab_dx, grab_dy;   // pointer offset inside the floater

static struct {
    gboolean  valid;
    ShapeKind kind;
    Rect      geom;        // shape bounds in canvas px, before stroke padding
    Rect      box;         // padded selection the last render produced
    gboolean  flip;        // a line running bottom-left -> top-right
    gboolean  secondary;   // drawn with the right button -> follows color 2
} live;

static gboolean live_shape(Document *d)
{
    if (!live.valid || !d->floating || !d->has_selection)
        return FALSE;
    /* Anything that resampled the floater from outside this tool - the Resize
     * dialog - leaves the description describing a shape that is no longer
     * there.  A move only shifts the rect, so only the size is compared. */
    return d->selection.w == live.box.w && d->selection.h == live.box.h;
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
    case SHAPE_LINE:   /* the one kind that cares which diagonal was dragged */
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        break;
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

static void shape_style(cairo_t *cr, const GdkRGBA *color, double width,
                        DashStyle dash)
{
    gdk_cairo_set_source_rgba(cr, color);
    cairo_set_line_width(cr, MAX(1.0, width));
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    /* Round caps finish a line neatly and make a zero-length dash a dot. */
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    tool_set_dash(cr, dash, width, 0);
}

static void shape_render(App *a)
{
    Document *d = a->doc;
    int pad = stroke_pad(a->brush_size);
    Rect box = { live.geom.x - pad,     live.geom.y - pad,
                 live.geom.w + 2 * pad, live.geom.h + 2 * pad };

    document_set_selection(d, box);
    document_create_floating(d, d->selection);
    live.box = d->selection;

    /* The bounds alone can't say which way a line runs, so `flip` picks the
     * diagonal; every other kind normalizes them anyway. */
    double y0 = live.geom.y, y1 = live.geom.y + live.geom.h;
    if (live.flip) {
        double swap = y0;
        y0 = y1;
        y1 = swap;
    }

    cairo_t *cr = cairo_create(d->floating);
    cairo_translate(cr, -d->selection.x, -d->selection.y);
    shape_style(cr, live.secondary ? &a->secondary : &a->primary,
                a->brush_size, a->dash);
    shape_path(cr, live.kind, live.geom.x, y0, live.geom.x + live.geom.w, y1);
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
    live.flip      = (c->x - c->start_x) * (c->y - c->start_y) < 0;
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

    shape_style(cr, &c->color, c->size, c->app->dash);
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

Tool tool_shape_line = {
    .id      = "shape-line",
    .label   = "Line - drag to draw, then drag it or its handles to adjust",
    .icon    = "paintly-shape-line",
    .data    = GINT_TO_POINTER(SHAPE_LINE),
    .begin   = shapes_begin,
    .motion  = shapes_motion,
    .end     = shapes_end,
    .overlay = shapes_overlay,
    .restyle = shapes_restyle,
    .deactivate = shapes_deactivate,
};
