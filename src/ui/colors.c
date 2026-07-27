#include "../app.h"
#include <math.h>

static const char *PALETTE[20] = {
    /* row 1 - darks */
    "#000000", "#7f7f7f", "#880015", "#ed1c24", "#ff7f27",
    "#fff200", "#22b14c", "#00a2e8", "#3f48cc", "#a349a4",
    /* row 2 - lights */
    "#ffffff", "#c3c3c3", "#b97a57", "#ffaec9", "#ffc90e",
    "#efe4b0", "#b5e61d", "#99d9ea", "#7092be", "#c8bfe7",
};

void colors_refresh(App *a)
{
    if (a->color_indicator)
        gtk_widget_queue_draw(a->color_indicator);
    app_restyle_floating(a);   /* a just-drawn shape follows the new color */
}

/* ---- indicator (two overlapping circles) -------------------------------- */

static void indicator_draw(GtkDrawingArea *area, cairo_t *cr,
                           int w, int h, gpointer user_data)
{
    App *a = user_data;

    /* rear circle: secondary color (top-right) */
    cairo_arc(cr, w - 16, 15, 11, 0, 2 * G_PI);
    gdk_cairo_set_source_rgba(cr, &a->secondary);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    /* front circle: primary color (bottom-left) */
    cairo_arc(cr, 16, h - 16, 13, 0, 2 * G_PI);
    gdk_cairo_set_source_rgba(cr, &a->primary);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_stroke(cr);
}

/* ---- system color chooser ------------------------------------------------ */

typedef struct {
    App     *app;
    gboolean primary;
} ChooseCtx;

static void on_color_chosen(GObject *source, GAsyncResult *res, gpointer data)
{
    ChooseCtx *ctx = data;
    GdkRGBA *c = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(source),
                                                     res, NULL);
    if (c) {   /* NULL = user cancelled */
        if (ctx->primary)
            ctx->app->primary = *c;
        else
            ctx->app->secondary = *c;
        g_free(c);
        colors_refresh(ctx->app);
    }
    g_free(ctx);
}

static void open_chooser(App *a, gboolean primary)
{
    ChooseCtx *ctx = g_new0(ChooseCtx, 1);
    ctx->app = a;
    ctx->primary = primary;

    GtkColorDialog *dlg = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dlg, TRUE);
    gtk_color_dialog_choose_rgba(dlg, a->window,
                                 primary ? &a->primary : &a->secondary,
                                 NULL, on_color_chosen, ctx);
    g_object_unref(dlg);
}

static void indicator_clicked(GtkGestureClick *g, int n, double x, double y,
                              gpointer user_data)
{
    guint btn = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    open_chooser(user_data, btn != GDK_BUTTON_SECONDARY);
}

/* ---- palette swatches ---------------------------------------------------- */

static void swatch_set(App *a, int index, gboolean primary)
{
    GdkRGBA c;
    gdk_rgba_parse(&c, PALETTE[index]);
    if (primary)
        a->primary = c;
    else
        a->secondary = c;
    colors_refresh(a);
}

static void swatch_clicked(GtkButton *btn, gpointer user_data)
{
    swatch_set(user_data,
               GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "index")), TRUE);
}

static void swatch_right_clicked(GtkGestureClick *g, int n, double x, double y,
                                 gpointer user_data)
{
    GtkWidget *btn = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    swatch_set(user_data,
               GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "index")), FALSE);
}

/* Generate one CSS rule per swatch and load them all in a single provider. */
static void swatch_load_css(void)
{
    static gboolean done = FALSE;
    if (done)
        return;
    done = TRUE;

    GString *css = g_string_new(NULL);
    for (int i = 0; i < 20; i++)
        g_string_append_printf(css, "#swatch-%d { background-color: %s; }\n",
                               i, PALETTE[i]);
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css->str);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
    g_string_free(css, TRUE);
}

/* ---- color wheel button -------------------------------------------------- */

static void hue_to_rgb(double hue, double *r, double *g, double *b)
{
    double h = hue * 6.0;
    double x = 1.0 - fabs(fmod(h, 2.0) - 1.0);
    switch ((int) h % 6) {
    case 0: *r = 1; *g = x; *b = 0; break;
    case 1: *r = x; *g = 1; *b = 0; break;
    case 2: *r = 0; *g = 1; *b = x; break;
    case 3: *r = 0; *g = x; *b = 1; break;
    case 4: *r = x; *g = 0; *b = 1; break;
    default: *r = 1; *g = 0; *b = x; break;
    }
}

static void wheel_draw(GtkDrawingArea *area, cairo_t *cr,
                       int w, int h, gpointer user_data)
{
    const int N = 24;
    double cx = w / 2.0, cy = h / 2.0, r, g, b;
    for (int i = 0; i < N; i++) {
        hue_to_rgb((double) i / N, &r, &g, &b);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, 6);
        cairo_arc(cr, cx, cy, 8,
                  2 * G_PI * i / N, 2 * G_PI * (i + 1.15) / N);
        cairo_stroke(cr);
    }
}

static void wheel_clicked(GtkButton *btn, gpointer user_data)
{
    open_chooser(user_data, TRUE);
}

/* ---- group assembly ------------------------------------------------------ */

GtkWidget *colors_group_new(App *a)
{
    swatch_load_css();

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    /* indicator */
    GtkWidget *ind = gtk_drawing_area_new();
    a->color_indicator = ind;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ind), 46);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ind), 44);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ind), indicator_draw, a, NULL);
    gtk_widget_set_tooltip_text(ind,
        "Color 1 (front) and Color 2 (back) - click to edit");
    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 0);
    g_signal_connect(g, "pressed", G_CALLBACK(indicator_clicked), a);
    gtk_widget_add_controller(ind, GTK_EVENT_CONTROLLER(g));
    ribbon_hand_cursor(ind);
    gtk_box_append(GTK_BOX(row), ind);

    /* palette */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    for (int i = 0; i < 20; i++) {
        GtkWidget *btn = gtk_button_new();
        char name[24];
        g_snprintf(name, sizeof name, "swatch-%d", i);
        gtk_widget_set_name(btn, name);
        gtk_widget_add_css_class(btn, "swatch");
        gtk_widget_set_tooltip_text(btn, PALETTE[i]);
        g_object_set_data(G_OBJECT(btn), "index", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(swatch_clicked), a);

        GtkGesture *right = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right),
                                      GDK_BUTTON_SECONDARY);
        g_signal_connect(right, "pressed", G_CALLBACK(swatch_right_clicked), a);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(right));

        ribbon_hand_cursor(btn);
        gtk_grid_attach(GTK_GRID(grid), btn, i % 10, i / 10, 1, 1);
    }
    gtk_box_append(GTK_BOX(row), grid);

    /* wheel -> system chooser */
    GtkWidget *wheel = gtk_button_new();
    gtk_widget_add_css_class(wheel, "tool-btn");
    gtk_widget_set_tooltip_text(wheel, "Edit colors…");
    GtkWidget *wheel_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(wheel_area), 24);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(wheel_area), 24);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(wheel_area), wheel_draw,
                                   NULL, NULL);
    gtk_button_set_child(GTK_BUTTON(wheel), wheel_area);
    g_signal_connect(wheel, "clicked", G_CALLBACK(wheel_clicked), a);
    ribbon_hand_cursor(wheel);
    gtk_box_append(GTK_BOX(row), wheel);

    return ribbon_group_new("Colors", row);
}
