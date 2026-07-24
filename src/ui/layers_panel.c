#include "../app.h"

static int row_to_layer(App *a, int row_index)
{
    return (int) a->doc->layers->len - 1 - row_index;
}

static void thumb_draw(GtkDrawingArea *area, cairo_t *cr,
                       int w, int h, gpointer user_data)
{
    App *a = user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(area), "layer-index"));
    if (idx < 0 || idx >= (int) a->doc->layers->len)
        return;
    Layer *l = g_ptr_array_index(a->doc->layers, idx);

    /* small transparency checker */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.88, 0.88, 0.88);
    for (int y = 0; y < h; y += 6)
        for (int x = ((y / 6) % 2) * 6; x < w; x += 12)
            cairo_rectangle(cr, x, y, 6, 6);
    cairo_fill(cr);

    /* layer content, scaled to fit */
    double s = MIN((double) w / a->doc->width, (double) h / a->doc->height);
    cairo_translate(cr, (w - a->doc->width * s) / 2, (h - a->doc->height * s) / 2);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, l->surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint_with_alpha(cr, l->opacity);
}

static void eye_toggled(GtkCheckButton *check, gpointer user_data)
{
    App *a = user_data;
    if (a->layers_guard)
        return;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(check), "layer-index"));
    if (idx < 0 || idx >= (int) a->doc->layers->len)
        return;
    Layer *l = g_ptr_array_index(a->doc->layers, idx);
    l->visible = gtk_check_button_get_active(check);
    canvas_repaint(a);
}

static void on_row_selected(GtkListBox *list, GtkListBoxRow *row,
                            gpointer user_data)
{
    App *a = user_data;
    if (a->layers_guard || !row)
        return;
    int idx = row_to_layer(a, gtk_list_box_row_get_index(row));
    if (idx == a->doc->active)
        return;

    /* Floating selection pixels belong to the old layer - stamp them down
     * before switching. */
    document_commit_floating(a->doc);
    a->doc->active = idx;

    a->layers_guard = TRUE;
    gtk_range_set_value(GTK_RANGE(a->opacity_scale),
                        document_active_layer(a->doc)->opacity * 100);
    a->layers_guard = FALSE;

    canvas_repaint(a);
    layers_panel_queue_thumbs(a);
}

static void opacity_changed(GtkRange *range, gpointer user_data)
{
    App *a = user_data;
    if (a->layers_guard)
        return;
    document_active_layer(a->doc)->opacity = gtk_range_get_value(range) / 100.0;
    canvas_repaint(a);
    layers_panel_queue_thumbs(a);
}

/* ---- structural buttons -------------------------------------------------- */

static void on_add(GtkButton *b, gpointer user_data)
{
    App *a = user_data;
    document_add_layer(a->doc);
    layers_panel_refresh(a);
    canvas_repaint(a);
}

static void on_delete(GtkButton *b, gpointer user_data)
{
    App *a = user_data;
    document_deselect(a->doc);   /* don't leave floating pixels orphaned */
    document_remove_layer(a->doc, a->doc->active);
    layers_panel_refresh(a);
    canvas_repaint(a);
}

static void on_raise(GtkButton *b, gpointer user_data)
{
    App *a = user_data;
    document_move_layer(a->doc, a->doc->active, +1);
    layers_panel_refresh(a);
    canvas_repaint(a);
}

static void on_lower(GtkButton *b, gpointer user_data)
{
    App *a = user_data;
    document_move_layer(a->doc, a->doc->active, -1);
    layers_panel_refresh(a);
    canvas_repaint(a);
}

/* ---- public -------------------------------------------------------------- */

void layers_panel_queue_thumbs(App *a)
{
    if (!a->layer_thumbs)
        return;
    for (guint i = 0; i < a->layer_thumbs->len; i++)
        gtk_widget_queue_draw(g_ptr_array_index(a->layer_thumbs, i));
}

void layers_panel_refresh(App *a)
{
    if (!a->layers_list)
        return;
    a->layers_guard = TRUE;

    g_ptr_array_set_size(a->layer_thumbs, 0);
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(a->layers_list)))
        gtk_list_box_remove(GTK_LIST_BOX(a->layers_list), child);

    int n = (int) a->doc->layers->len;
    for (int i = n - 1; i >= 0; i--) {          /* top-most layer first */
        Layer *l = g_ptr_array_index(a->doc->layers, i);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *eye = gtk_check_button_new();
        gtk_check_button_set_active(GTK_CHECK_BUTTON(eye), l->visible);
        gtk_widget_set_tooltip_text(eye, "Show or hide this layer");
        g_object_set_data(G_OBJECT(eye), "layer-index", GINT_TO_POINTER(i));
        g_signal_connect(eye, "toggled", G_CALLBACK(eye_toggled), a);
        gtk_box_append(GTK_BOX(row), eye);

        GtkWidget *thumb = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(thumb), 44);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(thumb), 34);
        gtk_widget_add_css_class(thumb, "layer-thumb");
        g_object_set_data(G_OBJECT(thumb), "layer-index", GINT_TO_POINTER(i));
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(thumb), thumb_draw, a, NULL);
        g_ptr_array_add(a->layer_thumbs, thumb);
        gtk_box_append(GTK_BOX(row), thumb);

        GtkWidget *name = gtk_label_new(l->name);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(row), name);

        gtk_list_box_append(GTK_LIST_BOX(a->layers_list), row);
    }

    GtkListBoxRow *active_row = gtk_list_box_get_row_at_index(
        GTK_LIST_BOX(a->layers_list), n - 1 - a->doc->active);
    gtk_list_box_select_row(GTK_LIST_BOX(a->layers_list), active_row);

    gtk_range_set_value(GTK_RANGE(a->opacity_scale),
                        document_active_layer(a->doc)->opacity * 100);
    a->layers_guard = FALSE;
}

static GtkWidget *small_button(const char *label, const char *tip,
                               GCallback cb, App *a)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, a);
    return b;
}

GtkWidget *layers_panel_new(App *a)
{
    GtkWidget *rev = gtk_revealer_new();
    a->layers_revealer = rev;
    gtk_revealer_set_transition_type(GTK_REVEALER(rev),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_reveal_child(GTK_REVEALER(rev), FALSE);

    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(panel, "layers-panel");
    gtk_widget_set_size_request(panel, 250, -1);

    GtkWidget *title = gtk_label_new("Layers");
    gtk_widget_add_css_class(title, "panel-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel), title);

    a->layers_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(a->layers_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(a->layers_list, "row-selected",
                     G_CALLBACK(on_row_selected), a);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), a->layers_list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(panel), scroll);

    GtkWidget *oplabel = gtk_label_new("Opacity");
    gtk_widget_set_halign(oplabel, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(panel), oplabel);

    a->opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                0, 100, 1);
    gtk_scale_set_digits(GTK_SCALE(a->opacity_scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(a->opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(a->opacity_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(a->opacity_scale), 100);
    g_signal_connect(a->opacity_scale, "value-changed",
                     G_CALLBACK(opacity_changed), a);
    gtk_box_append(GTK_BOX(panel), a->opacity_scale);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(btns),
        small_button("+", "Add layer above the active one", G_CALLBACK(on_add), a));
    gtk_box_append(GTK_BOX(btns),
        small_button("−", "Delete the active layer", G_CALLBACK(on_delete), a));
    gtk_box_append(GTK_BOX(btns),
        small_button("↑", "Raise the active layer", G_CALLBACK(on_raise), a));
    gtk_box_append(GTK_BOX(btns),
        small_button("↓", "Lower the active layer", G_CALLBACK(on_lower), a));
    gtk_box_append(GTK_BOX(panel), btns);

    gtk_revealer_set_child(GTK_REVEALER(rev), panel);
    return rev;
}
