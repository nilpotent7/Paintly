#include "../app.h"
#include <math.h>

static const int ZOOM_STEPS[] = { 10, 25, 50, 75, 100, 150, 200, 300, 400, 600, 800 };
#define N_STEPS ((int) G_N_ELEMENTS(ZOOM_STEPS))

void statusbar_update(App *a)
{
    if (!a->status_dims)
        return;
    char buf[128];

    if (a->hover_valid) {
        g_snprintf(buf, sizeof buf, "✛ %d, %d px",
                   (int) CLAMP(a->cur_x, 0, a->doc->width),
                   (int) CLAMP(a->cur_y, 0, a->doc->height));
        gtk_label_set_text(GTK_LABEL(a->status_cursor), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(a->status_cursor), "");
    }

    /* While a canvas grip is dragged the readout shows where it would land. */
    gboolean sizing = a->canvas_grip != HANDLE_NONE;
    g_snprintf(buf, sizeof buf, "%d × %d px",
               sizing ? a->grip_rect.w : a->doc->width,
               sizing ? a->grip_rect.h : a->doc->height);
    gtk_label_set_text(GTK_LABEL(a->status_dims), buf);

    if (a->doc->has_selection) {
        g_snprintf(buf, sizeof buf, "Selection: %d × %d px",
                   a->doc->selection.w, a->doc->selection.h);
        gtk_label_set_text(GTK_LABEL(a->status_sel), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(a->status_sel), "");
    }
}

void statusbar_sync_zoom(App *a)
{
    if (!a->zoom_label)
        return;
    a->zoom_guard = TRUE;
    gtk_range_set_value(GTK_RANGE(a->zoom_scale), a->zoom * 100);
    a->zoom_guard = FALSE;

    char buf[16];
    g_snprintf(buf, sizeof buf, "%d%%", (int) round(a->zoom * 100));
    gtk_label_set_text(GTK_LABEL(a->zoom_label), buf);
}

void statusbar_zoom_step(App *a, int dir)
{
    int cur = (int) round(a->zoom * 100);
    if (dir > 0) {
        for (int i = 0; i < N_STEPS; i++)
            if (ZOOM_STEPS[i] > cur) {
                canvas_set_zoom(a, ZOOM_STEPS[i] / 100.0);
                return;
            }
    } else {
        for (int i = N_STEPS - 1; i >= 0; i--)
            if (ZOOM_STEPS[i] < cur) {
                canvas_set_zoom(a, ZOOM_STEPS[i] / 100.0);
                return;
            }
    }
}

static void zoom_scale_changed(GtkRange *range, gpointer user_data)
{
    App *a = user_data;
    if (a->zoom_guard)
        return;
    canvas_set_zoom(a, gtk_range_get_value(range) / 100.0);
}

static void zoom_minus(GtkButton *b, gpointer user_data)
{
    statusbar_zoom_step(user_data, -1);
}

static void zoom_plus(GtkButton *b, gpointer user_data)
{
    statusbar_zoom_step(user_data, +1);
}

GtkWidget *statusbar_new(App *a)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(bar, "statusbar");

    a->status_cursor = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(a->status_cursor), 14);
    gtk_widget_set_halign(a->status_cursor, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bar), a->status_cursor);

    a->status_dims = gtk_label_new("");
    gtk_box_append(GTK_BOX(bar), a->status_dims);

    a->status_sel = gtk_label_new("");
    gtk_box_append(GTK_BOX(bar), a->status_sel);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    GtkWidget *minus = gtk_button_new_with_label("−");
    gtk_widget_set_tooltip_text(minus, "Zoom out");
    g_signal_connect(minus, "clicked", G_CALLBACK(zoom_minus), a);
    gtk_box_append(GTK_BOX(bar), minus);

    a->zoom_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                             10, 800, 1);
    gtk_scale_set_draw_value(GTK_SCALE(a->zoom_scale), FALSE);
    gtk_widget_set_size_request(a->zoom_scale, 150, -1);
    gtk_range_set_value(GTK_RANGE(a->zoom_scale), 100);
    g_signal_connect(a->zoom_scale, "value-changed",
                     G_CALLBACK(zoom_scale_changed), a);
    gtk_box_append(GTK_BOX(bar), a->zoom_scale);

    GtkWidget *plus = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(plus, "Zoom in");
    g_signal_connect(plus, "clicked", G_CALLBACK(zoom_plus), a);
    gtk_box_append(GTK_BOX(bar), plus);

    a->zoom_label = gtk_label_new("100%");
    gtk_widget_add_css_class(a->zoom_label, "zoom-label");
    gtk_label_set_xalign(GTK_LABEL(a->zoom_label), 1.0);
    gtk_box_append(GTK_BOX(bar), a->zoom_label);

    statusbar_update(a);
    return bar;
}
