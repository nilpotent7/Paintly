#include "../tools/tool.h"
#include "../core/history.h"

#define DEFAULT_W 1220
#define DEFAULT_H 644

/* ---- small helpers ------------------------------------------------------- */

static void show_error(App *a, const char *msg)
{
    GtkAlertDialog *d = gtk_alert_dialog_new("%s", msg);
    gtk_alert_dialog_show(d, a->window);
    g_object_unref(d);
}

void app_update_title(App *a)
{
    char *base = a->doc->filepath ? g_path_get_basename(a->doc->filepath)
                                  : g_strdup("Untitled");
    char *title = g_strdup_printf("%s%s - Paintly",
                                  a->doc->modified ? "• " : "", base);
    gtk_window_set_title(a->window, title);
    g_free(base);
    g_free(title);
}

void app_set_tool(App *a, const char *tool_id)
{
    Tool *t = tools_find(tool_id);
    if (!t || t == a->tool)
        return;
    if (a->tool && a->tool->deactivate)
        a->tool->deactivate(a->tool, a);
    a->tool = t;
    canvas_repaint(a);
}

void app_load_document(App *a, Document *doc, const char *path)
{
    if (a->doc)
        document_free(a->doc);
    a->doc = doc;
    if (path)
        doc->filepath = g_strdup(path);
    a->zoom = 1.0;
    canvas_update_size(a);
    statusbar_sync_zoom(a);
    layers_panel_refresh(a);
    statusbar_update(a);
    app_update_title(a);
}

/* ---- file open ----------------------------------------------------------- */

static void on_open_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (!f)
        return;                                   /* cancelled */
    char *path = g_file_get_path(f);
    g_object_unref(f);
    if (!path) {
        show_error(a, "Only local files can be opened.");
        return;
    }

    GError *err = NULL;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, &err);
    if (!pb) {
        show_error(a, err->message);
        g_error_free(err);
        g_free(path);
        return;
    }

    /* The image becomes the Background layer of a fresh document */
    Document *doc = document_new(gdk_pixbuf_get_width(pb),
                                 gdk_pixbuf_get_height(pb));
    cairo_t *cr = cairo_create(document_active_layer(doc)->surface);
    gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    g_object_unref(pb);

    app_load_document(a, doc, path);
    g_free(path);
}

static void act_open(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Open Image");

    GtkFileFilter *ff = gtk_file_filter_new();
    gtk_file_filter_set_name(ff, "Images");
    gtk_file_filter_add_pattern(ff, "*.png");
    gtk_file_filter_add_pattern(ff, "*.jpg");
    gtk_file_filter_add_pattern(ff, "*.jpeg");
    gtk_file_filter_add_pattern(ff, "*.bmp");
    gtk_file_filter_add_pattern(ff, "*.gif");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, ff);
    g_object_unref(ff);
    gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filters));
    g_object_unref(filters);

    gtk_file_dialog_open(fd, a->window, NULL, on_open_done, a);
    g_object_unref(fd);
}

/* ---- file save ----------------------------------------------------------- */

static void do_save(App *a, const char *path)
{
    document_deselect(a->doc);    /* commit floating pixels before export */
    cairo_surface_t *flat = document_flatten(a->doc);
    cairo_status_t st = cairo_surface_write_to_png(flat, path);
    cairo_surface_destroy(flat);
    if (st != CAIRO_STATUS_SUCCESS) {
        show_error(a, cairo_status_to_string(st));
        return;
    }
    if (a->doc->filepath != path) {
        g_free(a->doc->filepath);
        a->doc->filepath = g_strdup(path);
    }
    a->doc->modified = FALSE;
    app_update_title(a);
    canvas_repaint(a);
}

static void on_save_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (!f)
        return;
    char *path = g_file_get_path(f);
    g_object_unref(f);
    if (!path) {
        show_error(a, "Only local destinations are supported.");
        return;
    }
    /* Everything is saved as PNG - append the suffix if it's missing. */
    if (!g_str_has_suffix(path, ".png")) {
        char *fixed = g_strconcat(path, ".png", NULL);
        g_free(path);
        path = fixed;
    }
    do_save(a, path);
    g_free(path);
}

static void save_with_dialog(App *a)
{
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_title(fd, "Save Image");
    if (a->doc->filepath) {
        char *base = g_path_get_basename(a->doc->filepath);
        gtk_file_dialog_set_initial_name(fd, base);
        g_free(base);
    } else {
        gtk_file_dialog_set_initial_name(fd, "untitled.png");
    }
    gtk_file_dialog_save(fd, a->window, NULL, on_save_done, a);
    g_object_unref(fd);
}

static void act_save(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    if (a->doc->filepath)
        do_save(a, a->doc->filepath);
    else
        save_with_dialog(a);
}

static void act_save_as(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    save_with_dialog(user_data);
}

/* ---- other actions ------------------------------------------------------- */

static void act_new(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    app_load_document(a, document_new(DEFAULT_W, DEFAULT_H), NULL);
}

static void after_pixels_changed(App *a)
{
    canvas_repaint(a);
    layers_panel_queue_thumbs(a);
    statusbar_update(a);
    app_update_title(a);
}

static void act_undo(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    history_undo(a->doc);
    after_pixels_changed(a);
}

static void act_redo(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    history_redo(a->doc);
    after_pixels_changed(a);
}

static void act_select_all(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    document_select_all(a->doc);
    app_set_tool(a, "select");
    after_pixels_changed(a);
}

static void act_deselect(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    document_deselect(a->doc);
    after_pixels_changed(a);
}

static void act_delete_selection(GSimpleAction *action, GVariant *param,
                                 gpointer user_data)
{
    App *a = user_data;
    document_delete_selection(a->doc);
    after_pixels_changed(a);
}

static void act_zoom_in(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    statusbar_zoom_step(user_data, +1);
}

static void act_zoom_out(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    statusbar_zoom_step(user_data, -1);
}

static void act_zoom_reset(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    canvas_set_zoom(user_data, 1.0);
}

static void act_toggle_layers(GSimpleAction *action, GVariant *param,
                              gpointer user_data)
{
    App *a = user_data;
    GtkToggleButton *b = GTK_TOGGLE_BUTTON(a->layers_btn);
    gtk_toggle_button_set_active(b, !gtk_toggle_button_get_active(b));
}

static void act_quit(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    g_application_quit(G_APPLICATION(a->gapp));
}

static const GActionEntry WIN_ACTIONS[] = {
    { "new",              act_new },
    { "open",             act_open },
    { "save",             act_save },
    { "save-as",          act_save_as },
    { "undo",             act_undo },
    { "redo",             act_redo },
    { "select-all",       act_select_all },
    { "deselect",         act_deselect },
    { "delete-selection", act_delete_selection },
    { "zoom-in",          act_zoom_in },
    { "zoom-out",         act_zoom_out },
    { "zoom-reset",       act_zoom_reset },
    { "toggle-layers",    act_toggle_layers },
};

/* ---- menu bar ------------------------------------------------------------ */

/* Append a bunch of (label, action) pairs as one separated section. */
static void menu_section(GMenu *menu, const char *const items[][2], int n)
{
    GMenu *sec = g_menu_new();
    for (int i = 0; i < n; i++)
        g_menu_append(sec, items[i][0], items[i][1]);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(sec));
    g_object_unref(sec);
}

static GMenuModel *build_menubar(void)
{
    GMenu *bar = g_menu_new();

    GMenu *file = g_menu_new();
    menu_section(file, (const char *const[][2]) {
        { "_New",   "win.new" },
        { "_Open…", "win.open" } }, 2);
    menu_section(file, (const char *const[][2]) {
        { "_Save",     "win.save" },
        { "Save _As…", "win.save-as" } }, 2);
    menu_section(file, (const char *const[][2]) {
        { "_Quit", "app.quit" } }, 1);
    g_menu_append_submenu(bar, "_File", G_MENU_MODEL(file));
    g_object_unref(file);

    GMenu *edit = g_menu_new();
    menu_section(edit, (const char *const[][2]) {
        { "_Undo", "win.undo" },
        { "_Redo", "win.redo" } }, 2);
    menu_section(edit, (const char *const[][2]) {
        { "Select _All",      "win.select-all" },
        { "D_eselect",        "win.deselect" },
        { "_Delete Selection", "win.delete-selection" } }, 3);
    g_menu_append_submenu(bar, "_Edit", G_MENU_MODEL(edit));
    g_object_unref(edit);

    GMenu *view = g_menu_new();
    menu_section(view, (const char *const[][2]) {
        { "Zoom _In",     "win.zoom-in" },
        { "Zoom _Out",    "win.zoom-out" },
        { "_Actual Size", "win.zoom-reset" } }, 3);
    menu_section(view, (const char *const[][2]) {
        { "_Layers Panel", "win.toggle-layers" } }, 1);
    g_menu_append_submenu(bar, "_View", G_MENU_MODEL(view));
    g_object_unref(view);

    return G_MENU_MODEL(bar);
}

void app_startup(GtkApplication *gapp, gpointer user_data)
{
    App *a = user_data;

    GMenuModel *bar = build_menubar();
    gtk_application_set_menubar(gapp, bar);
    g_object_unref(bar);

    static const GActionEntry APP_ACTIONS[] = { { "quit", act_quit } };
    g_action_map_add_action_entries(G_ACTION_MAP(gapp), APP_ACTIONS,
                                    G_N_ELEMENTS(APP_ACTIONS), a);

    struct { const char *action; const char *accels[3]; } shortcuts[] = {
        { "win.new",              { "<Control>n", NULL } },
        { "win.open",             { "<Control>o", NULL } },
        { "win.save",             { "<Control>s", NULL } },
        { "win.save-as",          { "<Control><Shift>s", NULL } },
        { "win.undo",             { "<Control>z", NULL } },
        { "win.redo",             { "<Control>y", "<Control><Shift>z", NULL } },
        { "win.select-all",       { "<Control>a", NULL } },
        { "win.deselect",         { "Escape", NULL } },
        { "win.delete-selection", { "Delete", "BackSpace", NULL } },
        { "win.zoom-in",          { "<Control>plus", "<Control>equal", NULL } },
        { "win.zoom-out",         { "<Control>minus", NULL } },
        { "win.zoom-reset",       { "<Control>0", NULL } },
        { "win.toggle-layers",    { "<Control>l", NULL } },
        { "app.quit",             { "<Control>q", NULL } },
    };
    for (guint i = 0; i < G_N_ELEMENTS(shortcuts); i++)
        gtk_application_set_accels_for_action(gapp, shortcuts[i].action,
                                              shortcuts[i].accels);
}

/* ---- window assembly ----------------------------------------------------- */

/* `PAINTLY_BENCH=1 time ./build/paintly` quits right after the first frame
 * is scheduled - a cheap way to measure real startup time. */
static gboolean bench_quit(gpointer data)
{
    g_application_quit(data);
    return G_SOURCE_REMOVE;
}

void app_activate(GtkApplication *gapp, gpointer user_data)
{
    App *a = user_data;
    if (a->window) {
        gtk_window_present(a->window);
        return;
    }

    /* No dark theme at the moment. */
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", FALSE, NULL);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/org/paintly/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_icon_theme_add_resource_path(
        gtk_icon_theme_get_for_display(gdk_display_get_default()),
        "/org/paintly/icons");
    gtk_window_set_default_icon_name("paintly-app");

    /* initial state */
    a->doc = document_new(DEFAULT_W, DEFAULT_H);
    a->zoom = 1.0;
    a->brush_size = 3;
    gdk_rgba_parse(&a->primary, "#000000");
    gdk_rgba_parse(&a->secondary, "#ffffff");
    a->tool = tools_find("pencil");
    a->layer_thumbs = g_ptr_array_new();

    GtkWidget *win = gtk_application_window_new(gapp);
    a->window = GTK_WINDOW(win);
    gtk_window_set_default_size(a->window, 1280, 800);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), TRUE);
    g_action_map_add_action_entries(G_ACTION_MAP(win), WIN_ACTIONS,
                                    G_N_ELEMENTS(WIN_ACTIONS), a);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Build the layers panel first: the ribbon's Layers button needs
     * a->layers_revealer to exist. */
    GtkWidget *panel = layers_panel_new(a);
    gtk_box_append(GTK_BOX(root), ribbon_new(a));

    GtkWidget *work = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    a->scroller = gtk_scrolled_window_new();
    gtk_widget_add_css_class(a->scroller, "workspace");
    gtk_widget_set_hexpand(a->scroller, TRUE);
    gtk_widget_set_vexpand(a->scroller, TRUE);

    /* A 1-widget box provides the thin border + shadow around the canvas
     * without interfering with the drawing area's own size. */
    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(frame, "canvas-frame");
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(frame, 28);
    gtk_widget_set_margin_bottom(frame, 28);
    gtk_widget_set_margin_start(frame, 28);
    gtk_widget_set_margin_end(frame, 28);
    gtk_box_append(GTK_BOX(frame), canvas_new(a));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(a->scroller), frame);

    gtk_box_append(GTK_BOX(work), a->scroller);
    gtk_box_append(GTK_BOX(work), panel);
    gtk_box_append(GTK_BOX(root), work);

    gtk_box_append(GTK_BOX(root), statusbar_new(a));

    gtk_window_set_child(a->window, root);
    layers_panel_refresh(a);
    app_update_title(a);
    gtk_window_present(a->window);

    if (g_getenv("PAINTLY_BENCH"))
        g_idle_add(bench_quit, gapp);
}
