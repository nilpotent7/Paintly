#include "../tools/tool.h"
#include "../core/history.h"
#include <math.h>

#define ZOOM_MIN 0.10
#define ZOOM_MAX 8.0

#define GRIP_OUT   (HANDLE_PX / 2)      /* grip centre, outside the edge it marks */
#define GRIP_ROOM  ((int) HANDLE_PX + 1)         /* room a grip needs beside it */
#define CANVAS_MIN 8                            /* smallest canvas, in px */

/* Build the context passed to tool callbacks from current app state. */
static ToolContext ctx_make(App *a, guint button)
{
    return (ToolContext) {
        .app     = a,
        .doc     = a->doc,
        .layer   = document_active_layer(a->doc),
        .color   = (button == GDK_BUTTON_SECONDARY) ? a->secondary : a->primary,
        .size    = a->brush_size,
        .x       = a->cur_x,   .y       = a->cur_y,
        .last_x  = a->last_x,  .last_y  = a->last_y,
        .start_x = a->press_x, .start_y = a->press_y,
        .button  = button,
        .zoom    = a->zoom,
    };
}

/* 16×16 repeating light/dark pattern shown behind transparent pixels. */
static cairo_pattern_t *checker_pattern(void)
{
    static cairo_pattern_t *pat = NULL;
    if (!pat) {
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 16, 16);
        cairo_t *cr = cairo_create(s);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
        cairo_rectangle(cr, 0, 0, 8, 8);
        cairo_rectangle(cr, 8, 8, 8, 8);
        cairo_fill(cr);
        cairo_destroy(cr);
        pat = cairo_pattern_create_for_surface(s);
        cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
        cairo_surface_destroy(s);   /* pattern keeps its own reference */
    }
    return pat;
}

static void draw_ants(App *a, cairo_t *cr)
{
    Document *d = a->doc;
    double sx = d->floating ? d->float_x : d->selection.x;
    double sy = d->floating ? d->float_y : d->selection.y;
    double lw = 1.0 / a->zoom;              /* one screen pixel */

    cairo_save(cr);
    cairo_set_line_width(cr, lw);
    /* White base line, then black dashes on top: visible on any color. */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, sx, sy, d->selection.w, d->selection.h);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_dash(cr, (double[]){ 4 * lw, 4 * lw }, 2, 0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* HANDLE_PX is the outer size, so the path is inset by half the outline. */
static void draw_grip(cairo_t *cr, double cx, double cy, double zoom)
{
    double lw = 1.0 / zoom;
    double s  = HANDLE_PX / zoom - lw;
    cairo_rectangle(cr, cx - s / 2, cy - s / 2, s, s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.13, 0.13, 0.13);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
}

static int canvas_px(int image_px, double zoom)
{
    return MAX(1, (int) (image_px * zoom));
}

static Rect canvas_rect(App *a)
{
    return a->canvas_grip == HANDLE_NONE
         ? (Rect){ 0, 0, a->doc->width, a->doc->height }
         : a->grip_rect;
}

static void draw_cb(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    App *a = user_data;
    Rect g = canvas_rect(a);

    cairo_translate(cr, -g.x * a->zoom, -g.y * a->zoom);

    /* Checkerboard in screen space, so its squares stay a constant size. */
    cairo_set_source(cr, checker_pattern());
    cairo_paint(cr);

    cairo_scale(cr, a->zoom, a->zoom);

    Layer *bottom = g_ptr_array_index(a->doc->layers, 0);
    if (a->canvas_grip != HANDLE_NONE && bottom->visible) {
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, g.x, g.y, g.w, g.h);
        cairo_rectangle(cr, 0, 0, a->doc->width, a->doc->height);
        cairo_set_source_rgba(cr, a->secondary.red, a->secondary.green,
                              a->secondary.blue,
                              a->secondary.alpha * bottom->opacity);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    /* NEAREST keeps pixels crisp when zoomed in; GOOD smooths zoom-out. */
    document_render(a->doc, cr,
                    a->zoom >= 1.0 ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);

    if (a->doc->has_selection) {
        draw_ants(a, cr);
        for (Handle h = HANDLE_NW; h < HANDLE_COUNT; h++) {
            double hx, hy;
            document_handle_pos(a->doc, h, &hx, &hy);
            draw_grip(cr, hx, hy, a->zoom);
        }
    }

    if (a->tool && a->tool->overlay) {
        ToolContext c = ctx_make(a, a->pressed ? a->pressed_button : 0);
        a->tool->overlay(a->tool, &c, cr);
    }
}

/* ---- canvas resize grips ------------------------------------------------- */

static const char *const HANDLE_CURSOR[HANDLE_COUNT] = {
    "nw-resize", "n-resize", "ne-resize", "e-resize",
    "se-resize", "s-resize", "sw-resize", "w-resize",
};

/* The framed canvas, in grip-layer coordinates.  It is the frame rather than
 * the drawing area so the grips clear the frame's own 1px border. */
static gboolean grip_canvas_box(App *a, Rect *box)
{
    GtkWidget *frame = a->canvas ? gtk_widget_get_parent(a->canvas) : NULL;
    graphene_point_t p;
    if (!frame || !a->grip_layer ||
        !gtk_widget_compute_point(frame, a->grip_layer,
                                  &GRAPHENE_POINT_INIT(0, 0), &p))
        return FALSE;
    *box = (Rect){ (int) p.x, (int) p.y,
                   gtk_widget_get_width(frame),
                   gtk_widget_get_height(frame) };
    return box->w > 0 && box->h > 0;
}

/* Grip centre for handle `h`, pushed out by exactly half a grip so the grip
 * sits outside the canvas with its inner corner on the corner it marks.
 * Rounded because these are screen pixels: draw_grip wants whole ones. */
static void grip_pos(Rect box, Handle h, double *x, double *y)
{
    rect_handle_pos(box, h, x, y);
    if      (*x <= box.x)         *x -= GRIP_OUT;
    else if (*x >= box.x + box.w) *x += GRIP_OUT;
    if      (*y <= box.y)         *y -= GRIP_OUT;
    else if (*y >= box.y + box.h) *y += GRIP_OUT;
    *x = round(*x);
    *y = round(*y);
}

static Handle grip_hit(App *a, double x, double y)
{
    Rect box;
    if (!grip_canvas_box(a, &box))
        return HANDLE_NONE;
    double reach = HANDLE_PX / 2 + 2;
    Handle best = HANDLE_NONE;
    double best_d2 = reach * reach;

    for (Handle h = HANDLE_NW; h < HANDLE_COUNT; h++) {
        double gx, gy;
        grip_pos(box, h, &gx, &gy);
        double d2 = (x - gx) * (x - gx) + (y - gy) * (y - gy);
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = h;
        }
    }
    return best;
}

/* Only the canvas moving or resizing moves a grip - not a pixel change. */
static void grips_repaint(App *a)
{
    if (a->grip_layer)
        gtk_widget_queue_draw(a->grip_layer);
}

static void grips_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                          int width, int height, gpointer user_data)
{
    App *a = user_data;
    Rect box;
    if (!grip_canvas_box(a, &box))
        return;
    for (Handle h = HANDLE_NW; h < HANDLE_COUNT; h++) {
        double gx, gy;
        grip_pos(box, h, &gx, &gy);
        draw_grip(cr, gx, gy, 1.0);   /* layer pixels are screen pixels */
    }
}

#define TO_CANVAS_X(a, wx) ((wx) / (a)->zoom + canvas_rect(a).x)
#define TO_CANVAS_Y(a, wy) ((wy) / (a)->zoom + canvas_rect(a).y)

static gboolean event_surface_pos(GtkEventController *ctrl, double *sx, double *sy)
{
    GdkEvent *ev = gtk_event_controller_get_current_event(ctrl);
    return ev && gdk_event_get_position(ev, sx, sy);
}

static void grip_pin_frame(App *a, gboolean pin)
{
    GtkWidget *frame = gtk_widget_get_parent(a->canvas);
    if (!frame)
        return;
    if (pin) {
        double fx = 0, fy = 0;
        graphene_point_t out;
        if (gtk_widget_compute_point(frame, gtk_widget_get_parent(frame),
                                     &GRAPHENE_POINT_INIT(0, 0), &out)) {
            fx = out.x;
            fy = out.y;
        }
        a->grip_margin_x = (int) fx;
        a->grip_margin_y = (int) fy;
        gtk_widget_set_halign(frame, GTK_ALIGN_START);
        gtk_widget_set_valign(frame, GTK_ALIGN_START);
    } else {
        gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);
    }
    gtk_widget_set_margin_start(frame, pin ? a->grip_margin_x : CANVAS_MARGIN);
    gtk_widget_set_margin_top  (frame, pin ? a->grip_margin_y : CANVAS_MARGIN);
}

static void canvas_grip_drag(App *a, double x, double y)
{
    Rect full = { 0, 0, a->doc->width, a->doc->height };
    Handle h = a->canvas_grip;
    Rect g = rect_resize(full, h, x, y);

    if (g.w < CANVAS_MIN) {
        if (h == HANDLE_NW || h == HANDLE_W || h == HANDLE_SW)
            g.x = g.x + g.w - CANVAS_MIN;
        g.w = CANVAS_MIN;
    }
    if (g.h < CANVAS_MIN) {
        if (h == HANDLE_NW || h == HANDLE_N || h == HANDLE_NE)
            g.y = g.y + g.h - CANVAS_MIN;
        g.h = CANVAS_MIN;
    }
    a->grip_rect = g;

    gtk_drawing_area_set_content_width (GTK_DRAWING_AREA(a->canvas),
                                        canvas_px(g.w, a->zoom));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->canvas),
                                        canvas_px(g.h, a->zoom));

    GtkWidget *frame = gtk_widget_get_parent(a->canvas);
    if (frame) {
        gtk_widget_set_margin_start(frame,
            MAX(GRIP_ROOM, a->grip_margin_x + (int) (g.x * a->zoom)));
        gtk_widget_set_margin_top(frame,
            MAX(GRIP_ROOM, a->grip_margin_y + (int) (g.y * a->zoom)));
    }
    canvas_repaint(a);
    grips_repaint(a);
    statusbar_update(a);      /* the readout follows the previewed size */
}

static void canvas_grip_drop(App *a)
{
    Rect g = a->grip_rect;
    a->canvas_grip = HANDLE_NONE;
    grip_pin_frame(a, FALSE);
    if (g.x || g.y || g.w != a->doc->width || g.h != a->doc->height)
        document_resize_canvas(a->doc, g, a->secondary.red, a->secondary.green,
                               a->secondary.blue, a->secondary.alpha);
    canvas_update_size(a);
    layers_panel_queue_thumbs(a);
    statusbar_update(a);
    app_update_title(a);
}

static void grip_set_cursor(App *a, const char *name)
{
    GtkWidget *overlay = a->grip_layer ? gtk_widget_get_parent(a->grip_layer)
                                       : NULL;
    if (!overlay)
        return;
    GdkCursor *cur = gtk_widget_get_cursor(overlay);
    if (g_strcmp0(cur ? gdk_cursor_get_name(cur) : NULL, name) == 0)
        return;
    gtk_widget_set_cursor_from_name(overlay, name);
}

static void on_grip_press(GtkGestureClick *gesture, int n_press,
                          double x, double y, gpointer user_data)
{
    App *a = user_data;
    double sx, sy;
    Handle h = grip_hit(a, x, y);
    /* One gesture at a time: a tool may already own another button. */
    if (h == HANDLE_NONE || a->pressed ||
        !event_surface_pos(GTK_EVENT_CONTROLLER(gesture), &sx, &sy))
        return;

    a->canvas_grip = h;
    a->grip_rect = (Rect){ 0, 0, a->doc->width, a->doc->height };

    double ex, ey;
    rect_handle_pos(a->grip_rect, h, &ex, &ey);
    a->grip_ox = sx - ex * a->zoom;
    a->grip_oy = sy - ey * a->zoom;
    grip_pin_frame(a, TRUE);
}

static void on_grip_release(GtkGestureClick *gesture, int n_press,
                            double x, double y, gpointer user_data)
{
    App *a = user_data;
    if (a->canvas_grip != HANDLE_NONE)
        canvas_grip_drop(a);
}

static void on_grip_cancel(GtkGesture *gesture, GdkEventSequence *seq,
                           gpointer user_data)
{
    App *a = user_data;
    if (a->canvas_grip != HANDLE_NONE)
        canvas_update_size(a);      /* aborts the drag, then re-fits */
}

static void on_grip_motion(GtkEventControllerMotion *ctrl,
                           double x, double y, gpointer user_data)
{
    App *a = user_data;
    double sx, sy;
    if (a->canvas_grip != HANDLE_NONE) {
        if (event_surface_pos(GTK_EVENT_CONTROLLER(ctrl), &sx, &sy))
            canvas_grip_drag(a, (sx - a->grip_ox) / a->zoom,
                                (sy - a->grip_oy) / a->zoom);
        return;
    }
    Handle h = grip_hit(a, x, y);
    grip_set_cursor(a, h == HANDLE_NONE ? NULL : HANDLE_CURSOR[h]);
}

/* ---- input --------------------------------------------------------------- */

/* Cursors drawn from bundled art, because no cursor theme ships a pencil and
 * none of these would land its hot point on the tool's tip if it did.  The
 * hot point is given in the art's own 24-unit coordinates. */
#define CURSOR_PX 24

static const struct { const char *name, *file; int hot_x, hot_y; } ART_CURSORS[] = {
    { "pencil", "/org/paintly/cursors/pencil.svg", 2, 22 },
    { "brush",  "/org/paintly/cursors/brush.svg",  2, 22 },
    { "eraser", "/org/paintly/cursors/eraser.svg", 2, 22 },
    { "fill",   "/org/paintly/cursors/fill.svg",   2, 22 },
};

/* Built on first use and kept: nothing here runs before the first frame. */
static GdkCursor *art_cursor(const char *name)
{
    static GdkCursor *cache[G_N_ELEMENTS(ART_CURSORS)];
    static gboolean   tried[G_N_ELEMENTS(ART_CURSORS)];

    for (guint i = 0; i < G_N_ELEMENTS(ART_CURSORS); i++) {
        if (g_strcmp0(name, ART_CURSORS[i].name) != 0)
            continue;
        if (!tried[i]) {
            tried[i] = TRUE;
            GdkPixbuf *pb = gdk_pixbuf_new_from_resource_at_scale(
                ART_CURSORS[i].file, CURSOR_PX, CURSOR_PX, TRUE, NULL);
            if (pb) {
                GdkTexture *tex = gdk_texture_new_for_pixbuf(pb);
                cache[i] = gdk_cursor_new_from_texture(tex, ART_CURSORS[i].hot_x,
                                                       ART_CURSORS[i].hot_y, NULL);
                g_object_unref(tex);
                g_object_unref(pb);
            }
        }
        return cache[i];
    }
    return NULL;
}

static void set_cursor(App *a, const char *name)
{
    if (g_strcmp0(name, a->cursor_name) == 0)
        return;
    a->cursor_name = name;
    GdkCursor *art = art_cursor(name);
    if (art)
        gtk_widget_set_cursor(a->canvas, art);
    else
        gtk_widget_set_cursor_from_name(a->canvas, name);
}

static void update_cursor(App *a)
{
    const char *name = (a->tool && a->tool->cursor) ? a->tool->cursor : "crosshair";
    Handle h = document_hit_handle(a->doc, a->cur_x, a->cur_y, a->zoom);

    if (h != HANDLE_NONE)
        name = HANDLE_CURSOR[h];
    else if (document_point_in_selection(a->doc, a->cur_x, a->cur_y))
        name = "move";

    set_cursor(a, name);
}

static void on_press(GtkGestureClick *gesture, int n_press,
                     double wx, double wy, gpointer user_data)
{
    App *a = user_data;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (a->pressed || a->canvas_grip != HANDLE_NONE ||
        (btn != GDK_BUTTON_PRIMARY && btn != GDK_BUTTON_SECONDARY))
        return;      /* one gesture at a time, tool or canvas grip */

    a->pressed = TRUE;
    a->pressed_button = btn;
    a->press_x = a->last_x = a->cur_x = TO_CANVAS_X(a, wx);
    a->press_y = a->last_y = a->cur_y = TO_CANVAS_Y(a, wy);

    if (a->tool && a->tool->begin) {
        ToolContext c = ctx_make(a, btn);
        a->tool->begin(a->tool, &c);
    }
    statusbar_update(a);
}

static void on_release(GtkGestureClick *gesture, int n_press,
                       double wx, double wy, gpointer user_data)
{
    App *a = user_data;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (!a->pressed || btn != a->pressed_button)
        return;

    a->last_x = a->cur_x;
    a->last_y = a->cur_y;
    a->cur_x = TO_CANVAS_X(a, wx);
    a->cur_y = TO_CANVAS_Y(a, wy);
    a->pressed = FALSE;

    if (a->tool && a->tool->end) {
        ToolContext c = ctx_make(a, btn);
        a->tool->end(a->tool, &c);
    }
    statusbar_update(a);
    layers_panel_queue_thumbs(a);   /* the stroke likely changed pixels */
    app_update_title(a);            /* ... so the document is now dirty */
}

static void on_motion(GtkEventControllerMotion *ctrl,
                      double wx, double wy, gpointer user_data)
{
    App *a = user_data;
    a->last_x = a->cur_x;
    a->last_y = a->cur_y;
    a->cur_x = TO_CANVAS_X(a, wx);
    a->cur_y = TO_CANVAS_Y(a, wy);
    a->hover_valid = TRUE;

    if (a->pressed) {
        if (a->tool && a->tool->motion) {
            ToolContext c = ctx_make(a, a->pressed_button);
            a->tool->motion(a->tool, &c);
        }
    } else if (a->canvas_grip == HANDLE_NONE) {
        update_cursor(a);       /* a grip drag owns the cursor while it lasts */
    }
    statusbar_update(a);
}

static void on_leave(GtkEventControllerMotion *ctrl, gpointer user_data)
{
    App *a = user_data;
    a->hover_valid = FALSE;
    statusbar_update(a);
}

/* Ctrl + mouse wheel zooms, like every image editor. */
static gboolean on_scroll(GtkEventControllerScroll *ctrl,
                          double dx, double dy, gpointer user_data)
{
    App *a = user_data;
    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (!(state & GDK_CONTROL_MASK))
        return FALSE;               /* let the scrolled window scroll */
    statusbar_zoom_step(a, dy < 0 ? +1 : -1);
    return TRUE;
}

/* ---- public API ---------------------------------------------------------- */

GtkWidget *canvas_new(App *a)
{
    GtkWidget *area = gtk_drawing_area_new();
    a->canvas = area;
    a->canvas_grip = HANDLE_NONE;
    a->cursor_name = "crosshair";
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, a, NULL);
    gtk_widget_set_cursor_from_name(area, a->cursor_name);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0); /* any button */
    g_signal_connect(click, "pressed",  G_CALLBACK(on_press),   a);
    g_signal_connect(click, "released", G_CALLBACK(on_release), a);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), a);
    g_signal_connect(motion, "leave",  G_CALLBACK(on_leave),  a);
    gtk_widget_add_controller(area, motion);

    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(frame, "canvas-frame");
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top   (frame, CANVAS_MARGIN);
    gtk_widget_set_margin_bottom(frame, CANVAS_MARGIN);
    gtk_widget_set_margin_start (frame, CANVAS_MARGIN);
    gtk_widget_set_margin_end   (frame, CANVAS_MARGIN);
    gtk_box_append(GTK_BOX(frame), area);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), frame);
    a->grip_layer = gtk_drawing_area_new();
    gtk_widget_set_can_target(a->grip_layer, FALSE);   /* paints only */
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->grip_layer),
                                   grips_draw_cb, a, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), a->grip_layer);

    GtkGesture *grip_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(grip_click),
                                  GDK_BUTTON_PRIMARY);
    g_signal_connect(grip_click, "pressed",  G_CALLBACK(on_grip_press),   a);
    g_signal_connect(grip_click, "released", G_CALLBACK(on_grip_release), a);
    g_signal_connect(grip_click, "cancel",   G_CALLBACK(on_grip_cancel),  a);
    gtk_widget_add_controller(overlay, GTK_EVENT_CONTROLLER(grip_click));

    GtkEventController *grip_motion = gtk_event_controller_motion_new();
    g_signal_connect(grip_motion, "motion", G_CALLBACK(on_grip_motion), a);
    gtk_widget_add_controller(overlay, grip_motion);

    canvas_update_size(a);
    return overlay;
}

void canvas_attach_zoom(App *a, GtkWidget *widget)
{
    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), a);
    gtk_widget_add_controller(widget, scroll);
}

void canvas_repaint(App *a)
{
    if (a->canvas)
        gtk_widget_queue_draw(a->canvas);
}

void canvas_update_size(App *a)
{
    if (!a->canvas)
        return;
    if (a->canvas_grip != HANDLE_NONE) {
        a->canvas_grip = HANDLE_NONE;
        grip_pin_frame(a, FALSE);
        statusbar_update(a);        /* the readout was showing the preview */
    }
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a->canvas),
                                       canvas_px(a->doc->width, a->zoom));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->canvas),
                                        canvas_px(a->doc->height, a->zoom));
    canvas_repaint(a);
    grips_repaint(a);
}

void canvas_set_zoom(App *a, double zoom)
{
    zoom = CLAMP(zoom, ZOOM_MIN, ZOOM_MAX);
    if (zoom == a->zoom)
        return;
    a->zoom = zoom;
    canvas_update_size(a);
    statusbar_sync_zoom(a);
}
