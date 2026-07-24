#include "../tools/tool.h"
#include <math.h>

#define ZOOM_MIN 0.10
#define ZOOM_MAX 8.0

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

static void draw_cb(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer user_data)
{
    App *a = user_data;

    /* Transparency checkerboard, in screen space (constant square size). */
    cairo_set_source(cr, checker_pattern());
    cairo_paint(cr);

    /* Everything below happens in canvas coordinates. */
    cairo_scale(cr, a->zoom, a->zoom);

    /* NEAREST keeps pixels crisp when zoomed in; GOOD smooths zoom-out. */
    document_render(a->doc, cr,
                    a->zoom >= 1.0 ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);

    if (a->doc->has_selection)
        draw_ants(a, cr);

    if (a->tool && a->tool->overlay) {
        ToolContext c = ctx_make(a, a->pressed ? a->pressed_button : 0);
        a->tool->overlay(a->tool, &c, cr);
    }
}

/* ---- input --------------------------------------------------------------- */

static void on_press(GtkGestureClick *gesture, int n_press,
                     double wx, double wy, gpointer user_data)
{
    App *a = user_data;
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (a->pressed || (btn != GDK_BUTTON_PRIMARY && btn != GDK_BUTTON_SECONDARY))
        return;

    a->pressed = TRUE;
    a->pressed_button = btn;
    a->press_x = a->last_x = a->cur_x = wx / a->zoom;
    a->press_y = a->last_y = a->cur_y = wy / a->zoom;

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
    a->cur_x = wx / a->zoom;
    a->cur_y = wy / a->zoom;

    if (a->tool && a->tool->end) {
        ToolContext c = ctx_make(a, btn);
        a->tool->end(a->tool, &c);
    }
    a->pressed = FALSE;
    statusbar_update(a);
    layers_panel_queue_thumbs(a);   /* the stroke likely changed pixels */
}

static void on_motion(GtkEventControllerMotion *ctrl,
                      double wx, double wy, gpointer user_data)
{
    App *a = user_data;
    a->last_x = a->cur_x;
    a->last_y = a->cur_y;
    a->cur_x = wx / a->zoom;
    a->cur_y = wy / a->zoom;
    a->hover_valid = TRUE;

    if (a->pressed && a->tool && a->tool->motion) {
        ToolContext c = ctx_make(a, a->pressed_button);
        a->tool->motion(a->tool, &c);
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
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, a, NULL);
    gtk_widget_set_cursor(area, gdk_cursor_new_from_name("crosshair", NULL));

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0); /* any button */
    g_signal_connect(click, "pressed",  G_CALLBACK(on_press),   a);
    g_signal_connect(click, "released", G_CALLBACK(on_release), a);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), a);
    g_signal_connect(motion, "leave",  G_CALLBACK(on_leave),  a);
    gtk_widget_add_controller(area, motion);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), a);
    gtk_widget_add_controller(area, scroll);

    canvas_update_size(a);
    return area;
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
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a->canvas),
                                       (int) (a->doc->width * a->zoom));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->canvas),
                                        (int) (a->doc->height * a->zoom));
    canvas_repaint(a);
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
