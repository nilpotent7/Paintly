#include "../tools/tool.h"

GtkWidget *ribbon_group_new(const char *label, GtkWidget *content)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), content);

    GtkWidget *caption = gtk_label_new(label);
    gtk_widget_add_css_class(caption, "ribbon-group-label");
    gtk_box_append(GTK_BOX(box), caption);
    return box;
}

static void hand_state_changed(GtkWidget *w, GtkStateFlags old, gpointer data)
{
    gboolean held = gtk_widget_get_state_flags(w) & GTK_STATE_FLAG_ACTIVE;
    const char *name = held ? "grabbing" : "pointer";
    gtk_widget_set_cursor_from_name(w, name);
    for (GtkWidget *c = gtk_widget_get_first_child(w); c;
         c = gtk_widget_get_next_sibling(c))
        gtk_widget_set_cursor_from_name(c, name);
}

/* Pointing hand over a control, closing while it is held. */
void ribbon_hand_cursor(GtkWidget *w)
{
    gtk_widget_set_cursor_from_name(w, "pointer");
    g_signal_connect(w, "state-flags-changed", G_CALLBACK(hand_state_changed), NULL);
}

static void tool_toggled(GtkToggleButton *btn, gpointer user_data)
{
    if (!gtk_toggle_button_get_active(btn))
        return;
    app_set_tool(user_data, g_object_get_data(G_OBJECT(btn), "tool-id"));
}

/* Selecting a tool from code - paste, select-all - must move the radio too;
 * app_set_tool() short-circuits the resulting "toggled", so this can't loop. */
void ribbon_sync_tool(App *a)
{
    if (!a->tool_btns || !a->tool)
        return;
    for (guint i = 0; i < a->tool_btns->len; i++) {
        GtkWidget *b = g_ptr_array_index(a->tool_btns, i);
        if (g_strcmp0(g_object_get_data(G_OBJECT(b), "tool-id"), a->tool->id) == 0)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
    }
}

static GtkWidget *tool_button(App *a, const char *tool_id, gboolean large)
{
    Tool *t = tools_find(tool_id);
    g_assert(t != NULL);

    GtkWidget *btn = gtk_toggle_button_new();
    GtkWidget *img = gtk_image_new_from_icon_name(t->icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), large ? 26 : 20);
    gtk_button_set_child(GTK_BUTTON(btn), img);
    gtk_widget_add_css_class(btn, "tool-btn");
    if (large)
        gtk_widget_add_css_class(btn, "tool-btn-large");
    gtk_widget_set_tooltip_text(btn, t->label);
    g_object_set_data(G_OBJECT(btn), "tool-id", (gpointer) t->id);

    /* Chain all tool buttons into a single radio group. */
    if (a->tool_group_leader)
        gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(btn), a->tool_group_leader);
    else
        a->tool_group_leader = GTK_TOGGLE_BUTTON(btn);

    g_signal_connect(btn, "toggled", G_CALLBACK(tool_toggled), a);
    ribbon_hand_cursor(btn);
    g_ptr_array_add(a->tool_btns, btn);
    return btn;
}

/* A large ribbon button that fires a window action rather than picking a tool. */
static GtkWidget *action_button(const char *action, const char *icon,
                                const char *tip)
{
    GtkWidget *btn = gtk_button_new();
    GtkWidget *img = gtk_image_new_from_icon_name(icon);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 26);
    gtk_button_set_child(GTK_BUTTON(btn), img);
    gtk_widget_add_css_class(btn, "tool-btn");
    gtk_widget_add_css_class(btn, "tool-btn-large");
    gtk_widget_set_tooltip_text(btn, tip);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), action);
    ribbon_hand_cursor(btn);
    return btn;
}

static void size_changed(GtkRange *range, gpointer user_data)
{
    App *a = user_data;
    a->brush_size = gtk_range_get_value(range);
    app_restyle_floating(a);   /* a just-drawn shape follows the new width */
}

static const char *const DASH_LABELS[DASH_COUNT] = {
    "Solid", "Dash", "Dot", "Dash-dot",
};

static void dash_toggled(GtkToggleButton *btn, gpointer user_data)
{
    App *a = user_data;
    if (!gtk_toggle_button_get_active(btn))
        return;
    a->dash = (DashStyle) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "dash"));
    app_restyle_floating(a);   /* a just-drawn shape follows the new pattern */
}

/* A sample of the pattern its button selects, drawn with the real thing. */
static void dash_sample_draw(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data)
{
    cairo_set_source_rgb(cr, 0.23, 0.23, 0.23);
    cairo_set_line_width(cr, 2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    tool_set_dash(cr, (DashStyle) GPOINTER_TO_INT(user_data), 2, 0);
    cairo_move_to(cr, 3, height / 2.0);
    cairo_line_to(cr, width - 3, height / 2.0);
    cairo_stroke(cr);
}

/* Radio buttons rather than a GtkDropDown.  A dropdown opens a second popover
 * inside this one, and once it had been used the Stroke popover stopped
 * answering outside clicks - only Escape would close it.  Buttons keep the
 * popover un-nested, and show the pattern instead of naming it. */
static GtkWidget *dash_row(App *a)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
    GtkToggleButton *leader = NULL;

    for (int d = 0; d < DASH_COUNT; d++) {
        GtkWidget *btn = gtk_toggle_button_new();
        GtkWidget *sample = gtk_drawing_area_new();
        gtk_widget_set_size_request(sample, 34, 16);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sample), dash_sample_draw,
                                       GINT_TO_POINTER(d), NULL);
        gtk_button_set_child(GTK_BUTTON(btn), sample);
        gtk_widget_add_css_class(btn, "tool-btn");
        gtk_widget_set_tooltip_text(btn, DASH_LABELS[d]);
        g_object_set_data(G_OBJECT(btn), "dash", GINT_TO_POINTER(d));

        if (leader)
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(btn), leader);
        else
            leader = GTK_TOGGLE_BUTTON(btn);
        if ((DashStyle) d == a->dash)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);

        g_signal_connect(btn, "toggled", G_CALLBACK(dash_toggled), a);
        ribbon_hand_cursor(btn);
        gtk_box_append(GTK_BOX(row), btn);
    }
    return row;
}

static GtkWidget *size_group(App *a)
{
    GtkWidget *mb = gtk_menu_button_new();
    gtk_widget_add_css_class(mb, "tool-btn");
    gtk_widget_set_tooltip_text(mb, "Stroke width and dash pattern");
    GtkWidget *img = gtk_image_new_from_icon_name("paintly-size");
    gtk_image_set_pixel_size(GTK_IMAGE(img), 20);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(mb), img);

    GtkWidget *pop = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_box_append(GTK_BOX(box), gtk_label_new("Stroke width"));

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 64, 1);
    gtk_range_set_value(GTK_RANGE(scale), a->brush_size);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_scale_set_digits(GTK_SCALE(scale), 0);
    gtk_widget_set_size_request(scale, 180, -1);
    g_signal_connect(scale, "value-changed", G_CALLBACK(size_changed), a);
    gtk_box_append(GTK_BOX(box), scale);

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), gtk_label_new("Dash"));
    gtk_box_append(GTK_BOX(box), dash_row(a));

    gtk_popover_set_child(GTK_POPOVER(pop), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    ribbon_hand_cursor(mb);
    return mb;
}

static void layers_toggled(GtkToggleButton *btn, gpointer user_data)
{
    App *a = user_data;
    if (a->layers_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(a->layers_revealer),
                                      gtk_toggle_button_get_active(btn));
}

GtkWidget *ribbon_new(App *a)
{
    GtkWidget *ribbon = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 22);
    gtk_widget_add_css_class(ribbon, "ribbon");
    /* Group contents use vexpand to center icons above the caption; without
     * this explicit override that flag would propagate up and make the
     * ribbon steal height from the canvas workspace. */
    gtk_widget_set_vexpand_set(ribbon, TRUE);
    gtk_widget_set_vexpand(ribbon, FALSE);

    /* -- Selection ------------------------------------------------------- */
    GtkWidget *sel_btn = tool_button(a, "select", TRUE);
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Selection", sel_btn));

    /* -- Image ----------------------------------------------------------- */
    GtkWidget *resize_btn = action_button("win.resize", "paintly-resize",
                                          "Resize the selection, or the whole "
                                          "image when nothing is selected");
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Image", resize_btn));

    /* -- Tools (2-column grid) ---------- */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    GtkWidget *pencil = tool_button(a, "pencil", FALSE);
    gtk_grid_attach(GTK_GRID(grid), pencil,                          0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tool_button(a, "fill",   FALSE), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tool_button(a, "eraser", FALSE), 0, 1, 1, 1);
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Tools", grid));

    /* -- Brushes --------------------------------------------------------- */
    GtkWidget *brush = tool_button(a, "brush", TRUE);
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Brushes", brush));

    /* -- Shapes (2-column grid, matching Tools) --------------------------- */
    GtkWidget *shapes = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(shapes), 2);
    gtk_grid_set_column_spacing(GTK_GRID(shapes), 2);
    gtk_grid_attach(GTK_GRID(shapes), tool_button(a, "shape-rect",     FALSE), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(shapes), tool_button(a, "shape-ellipse",  FALSE), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(shapes), tool_button(a, "shape-triangle", FALSE), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(shapes), tool_button(a, "shape-line",     FALSE), 1, 1, 1, 1);
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Shapes", shapes));

    /* -- Stroke ---------------------------------------------------------- */
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Stroke", size_group(a)));

    /* -- Colors (built in colors.c) -------------------------------------- */
    gtk_box_append(GTK_BOX(ribbon), colors_group_new(a));

    /* -- Layers (far right, toggles the side panel) ----------------------- */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(ribbon), spacer);

    a->layers_btn = gtk_toggle_button_new();
    GtkWidget *img = gtk_image_new_from_icon_name("paintly-layers");
    gtk_image_set_pixel_size(GTK_IMAGE(img), 26);
    gtk_button_set_child(GTK_BUTTON(a->layers_btn), img);
    gtk_widget_add_css_class(a->layers_btn, "tool-btn");
    gtk_widget_add_css_class(a->layers_btn, "tool-btn-large");
    gtk_widget_set_tooltip_text(a->layers_btn, "Show or hide the Layers panel");
    g_signal_connect(a->layers_btn, "toggled", G_CALLBACK(layers_toggled), a);
    ribbon_hand_cursor(a->layers_btn);
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Layers", a->layers_btn));

    /* Pencil starts active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pencil), TRUE);

    return ribbon;
}
