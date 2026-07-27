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
    return btn;
}

static void size_changed(GtkRange *range, gpointer user_data)
{
    App *a = user_data;
    a->brush_size = gtk_range_get_value(range);
    app_restyle_floating(a);   /* a just-drawn shape follows the new width */
}

static GtkWidget *size_group(App *a)
{
    GtkWidget *mb = gtk_menu_button_new();
    gtk_widget_add_css_class(mb, "tool-btn");
    gtk_widget_set_tooltip_text(mb, "Stroke width");
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

    /* -- Shapes ---------------------------------------------------------- */
    GtkWidget *shapes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_append(GTK_BOX(shapes), tool_button(a, "shape-rect",     FALSE));
    gtk_box_append(GTK_BOX(shapes), tool_button(a, "shape-ellipse",  FALSE));
    gtk_box_append(GTK_BOX(shapes), tool_button(a, "shape-triangle", FALSE));
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Shapes", shapes));

    /* -- Size ------------------------------------------------------------ */
    gtk_box_append(GTK_BOX(ribbon), ribbon_group_new("Size", size_group(a)));

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
