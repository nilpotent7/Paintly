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

    /* Stamp a floater down before its layer goes dark, or it would be stranded:
     * a hidden layer accepts no edits, so it could never be dropped. */
    if (!gtk_check_button_get_active(check) && idx == a->doc->active)
        document_commit_floating(a->doc);

    l->visible = gtk_check_button_get_active(check);
    app_sync_editable(a);
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

    app_sync_editable(a);
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

static void delete_active_layer(App *a)
{
    document_deselect(a->doc);   /* don't leave floating pixels orphaned */
    document_remove_layer(a->doc, a->doc->active);
    layers_panel_refresh(a);
    canvas_repaint(a);
}

static void on_delete(GtkButton *b, gpointer user_data)
{
    delete_active_layer(user_data);
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

/* ---- drag a row to reorder ----------------------------------------------- */

/* Rebuilding the rows destroys the one this drop is running on, so leave the
 * stack first - the same reason the unsaved-changes continuations do. */
static gboolean refresh_idle(gpointer user_data)
{
    App *a = user_data;
    layers_panel_refresh(a);
    canvas_repaint(a);
    return G_SOURCE_REMOVE;
}

static int row_layer_index(GtkEventController *ctl)
{
    GtkWidget *row = gtk_event_controller_get_widget(ctl);
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "layer-index"));
}

static GdkContentProvider *drag_prepare(GtkDragSource *src, double x, double y,
                                        gpointer user_data)
{
    return gdk_content_provider_new_typed(
        G_TYPE_INT, row_layer_index(GTK_EVENT_CONTROLLER(src)));
}

static void drag_begin(GtkDragSource *src, GdkDrag *drag, gpointer user_data)
{
    App *a = user_data;
    GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(src));
    Layer *l = g_ptr_array_index(a->doc->layers,
                                 row_layer_index(GTK_EVENT_CONTROLLER(src)));

    /* A styled label rather than a snapshot of the row: rows paint no
     * background of their own, so a snapshot drags around as a bare rectangle
     * of whatever is behind the drag surface. */
    GtkWidget *icon = gtk_label_new(l->name);
    gtk_widget_add_css_class(icon, "layer-drag-icon");
    gtk_drag_icon_set_child(GTK_DRAG_ICON(gtk_drag_icon_get_for_drag(drag)), icon);

    gtk_widget_add_css_class(row, "layer-dragging");
}

static void drag_end(GtkDragSource *src, GdkDrag *drag, gboolean del,
                     gpointer user_data)
{
    gtk_widget_remove_css_class(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(src)),
        "layer-dragging");
}

static GdkDragAction drop_enter(GtkDropTarget *tgt, double x, double y,
                                gpointer user_data)
{
    gtk_widget_add_css_class(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(tgt)), "drop-into");
    return GDK_ACTION_MOVE;
}

static void drop_leave(GtkDropTarget *tgt, gpointer user_data)
{
    gtk_widget_remove_css_class(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(tgt)), "drop-into");
}

static gboolean on_drop(GtkDropTarget *tgt, const GValue *value,
                        double x, double y, gpointer user_data)
{
    App *a = user_data;
    drop_leave(tgt, a);
    if (!G_VALUE_HOLDS_INT(value))
        return FALSE;

    int from = g_value_get_int(value);
    int to   = row_layer_index(GTK_EVENT_CONTROLLER(tgt));
    if (from == to)
        return FALSE;

    document_reorder_layer(a->doc, from, to);
    g_idle_add(refresh_idle, a);
    return TRUE;
}

/* ---- keyboard ------------------------------------------------------------ */

static void focus_active_row(App *a)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(a->layers_list));
    if (row)
        gtk_widget_grab_focus(GTK_WIDGET(row));
}

/* Flip the row's eye rather than the layer, so the checkbox and the model stay
 * in step through the one handler that already owns both. */
static void toggle_active_visibility(App *a)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(a->layers_list));
    if (!row)
        return;
    GtkCheckButton *eye = g_object_get_data(G_OBJECT(row), "eye");
    gtk_check_button_set_active(eye, !gtk_check_button_get_active(eye));
}

static gboolean list_has_focus(App *a)
{
    GtkWidget *f = gtk_window_get_focus(a->window);
    return f && (f == a->layers_list || gtk_widget_is_ancestor(f, a->layers_list));
}

/* Del and Backspace are window accelerators for the *selection*, and those are
 * tried before the event ever reaches the list.  So the panel claims them at
 * the window too - in the capture phase, ahead of the accelerators, and only
 * while the list actually has the focus. */
static gboolean on_key(GtkEventControllerKey *ctl, guint keyval, guint keycode,
                       GdkModifierType state, gpointer user_data)
{
    App *a = user_data;
    if (!list_has_focus(a))
        return FALSE;
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK))
        return FALSE;

    if (keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete) {
        delete_active_layer(a);
        focus_active_row(a);        /* the refresh rebuilt the row we were on */
        return TRUE;
    }
    if (keyval == GDK_KEY_BackSpace) {
        toggle_active_visibility(a);
        return TRUE;
    }
    return FALSE;
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

        /* The row is built explicitly rather than let GtkListBox wrap the box,
         * because the drag controllers and the reorder index live on it. */
        GtkWidget *lrow = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lrow), row);
        g_object_set_data(G_OBJECT(lrow), "layer-index", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(lrow), "eye", eye);

        GtkDragSource *src = gtk_drag_source_new();
        gtk_drag_source_set_actions(src, GDK_ACTION_MOVE);
        g_signal_connect(src, "prepare",    G_CALLBACK(drag_prepare), a);
        g_signal_connect(src, "drag-begin", G_CALLBACK(drag_begin), a);
        g_signal_connect(src, "drag-end",   G_CALLBACK(drag_end), a);
        gtk_widget_add_controller(lrow, GTK_EVENT_CONTROLLER(src));

        GtkDropTarget *tgt = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_MOVE);
        g_signal_connect(tgt, "enter", G_CALLBACK(drop_enter), a);
        g_signal_connect(tgt, "leave", G_CALLBACK(drop_leave), a);
        g_signal_connect(tgt, "drop",  G_CALLBACK(on_drop), a);
        gtk_widget_add_controller(lrow, GTK_EVENT_CONTROLLER(tgt));

        gtk_list_box_append(GTK_LIST_BOX(a->layers_list), lrow);
    }

    GtkListBoxRow *active_row = gtk_list_box_get_row_at_index(
        GTK_LIST_BOX(a->layers_list), n - 1 - a->doc->active);
    gtk_list_box_select_row(GTK_LIST_BOX(a->layers_list), active_row);

    gtk_range_set_value(GTK_RANGE(a->opacity_scale),
                        document_active_layer(a->doc)->opacity * 100);
    a->layers_guard = FALSE;

    /* Add / delete / reorder / a new document can all change the active
     * layer, so every caller of this gets the lock resynced for free. */
    app_sync_editable(a);
}

static GtkWidget *icon_button(const char *icon, const char *tip,
                              GCallback cb, App *a)
{
    GtkWidget *img = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 20);   /* the icons' natural size */

    GtkWidget *b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), img);
    gtk_widget_set_tooltip_text(b, tip);
    ribbon_hand_cursor(b);
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

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), a);
    gtk_widget_add_controller(GTK_WIDGET(a->window), keys);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), a->layers_list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(panel), scroll);

    /* Label and slider read as one control, so they get their own tight box
     * instead of the panel's 8px rhythm. */
    GtkWidget *opbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *oplabel = gtk_label_new("Opacity");
    gtk_widget_add_css_class(oplabel, "opacity-label");
    gtk_widget_set_halign(oplabel, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(opbox), oplabel);

    a->opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                0, 100, 1);
    gtk_scale_set_digits(GTK_SCALE(a->opacity_scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(a->opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(a->opacity_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(a->opacity_scale), 100);
    g_signal_connect(a->opacity_scale, "value-changed",
                     G_CALLBACK(opacity_changed), a);
    gtk_box_append(GTK_BOX(opbox), a->opacity_scale);
    gtk_box_append(GTK_BOX(panel), opbox);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(btns, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(btns), icon_button("paintly-layer-add",
        "Add layer above the active one", G_CALLBACK(on_add), a));
    gtk_box_append(GTK_BOX(btns), icon_button("paintly-layer-delete",
        "Delete the active layer (Del)", G_CALLBACK(on_delete), a));
    gtk_box_append(GTK_BOX(btns), icon_button("paintly-layer-up",
        "Raise the active layer", G_CALLBACK(on_raise), a));
    gtk_box_append(GTK_BOX(btns), icon_button("paintly-layer-down",
        "Lower the active layer", G_CALLBACK(on_lower), a));
    gtk_box_append(GTK_BOX(panel), btns);

    gtk_revealer_set_child(GTK_REVEALER(rev), panel);
    return rev;
}
