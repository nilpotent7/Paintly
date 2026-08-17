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
    ribbon_sync_tool(a);
    canvas_repaint(a);
}

void app_restyle_floating(App *a)
{
    if (a->tool && a->tool->restyle)
        a->tool->restyle(a->tool, a);
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

/* Read an image into a fresh document; reports failure to the user. */
static void load_image_path(App *a, const char *path)
{
    GError *err = NULL;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, &err);
    if (!pb) {
        show_error(a, err->message);
        g_error_free(err);
        return;
    }

    /* The image becomes the Background layer of a fresh document */
    Document *doc = document_new(gdk_pixbuf_get_width(pb),
                                 gdk_pixbuf_get_height(pb));
    cairo_t *cr = cairo_create(document_active_layer(doc)->surface);
    gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
    /* SOURCE copies the image's alpha verbatim instead of blending it over
     * the layer's opaque white, which would flatten away any transparency. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    g_object_unref(pb);

    app_load_document(a, doc, path);
}

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
    load_image_path(a, path);
    g_free(path);
}

static void open_dialog(App *a)
{
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

static gboolean run_pending_idle(gpointer data)
{
    App *a = data;
    AppContinue then = a->pending;
    a->pending = NULL;
    if (then)
        then(a);
    return G_SOURCE_REMOVE;
}

static void run_pending(App *a)
{
    if (a->pending)
        g_idle_add(run_pending_idle, a);
}

static gboolean do_save(App *a, const char *path)
{
    document_deselect(a->doc);    /* commit floating pixels before export */
    cairo_surface_t *flat = document_flatten(a->doc);
    cairo_status_t st = cairo_surface_write_to_png(flat, path);
    cairo_surface_destroy(flat);
    if (st != CAIRO_STATUS_SUCCESS) {
        show_error(a, cairo_status_to_string(st));
        return FALSE;
    }
    if (a->doc->filepath != path) {
        g_free(a->doc->filepath);
        a->doc->filepath = g_strdup(path);
    }
    a->doc->modified = FALSE;
    app_update_title(a);
    canvas_repaint(a);
    return TRUE;
}

static void on_save_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (!f) {
        a->pending = NULL;        /* cancelling the save cancels the rest */
        return;
    }
    char *path = g_file_get_path(f);
    g_object_unref(f);
    if (!path) {
        a->pending = NULL;
        show_error(a, "Only local destinations are supported.");
        return;
    }
    /* Everything is saved as PNG - append the suffix if it's missing. */
    if (!g_str_has_suffix(path, ".png")) {
        char *fixed = g_strconcat(path, ".png", NULL);
        g_free(path);
        path = fixed;
    }
    if (do_save(a, path))
        run_pending(a);
    else
        a->pending = NULL;
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

static void save_now(App *a)
{
    if (a->doc->filepath) {
        if (do_save(a, a->doc->filepath))
            run_pending(a);
        else
            a->pending = NULL;
    } else {
        save_with_dialog(a);
    }
}

static void act_save(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    save_now(user_data);
}

static void act_save_as(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    save_with_dialog(user_data);
}

/* ---- unsaved changes ----------------------------------------------------- */

/* Button indices, in the order they are handed to the alert dialog. */
typedef enum { CONFIRM_CANCEL, CONFIRM_DISCARD, CONFIRM_SAVE } ConfirmChoice;

static gboolean save_now_idle(gpointer data)
{
    save_now(data);
    return G_SOURCE_REMOVE;
}

static void on_confirm_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    /* A dismissed dialog reports the cancel button, so -1 never reaches here. */
    switch (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL)) {
    case CONFIRM_DISCARD: run_pending(a);               break;
    case CONFIRM_SAVE:    g_idle_add(save_now_idle, a); break;
    default:              a->pending = NULL;
    }
}

static void confirm_unsaved(App *a, AppContinue then)
{
    if (!a->doc->modified) {
        then(a);
        return;
    }
    a->pending = then;

    char *base = a->doc->filepath ? g_path_get_basename(a->doc->filepath)
                                  : g_strdup("Untitled");
    GtkAlertDialog *dlg = gtk_alert_dialog_new("Save changes to “%s”?", base);
    gtk_alert_dialog_set_detail(dlg,
        "Your changes will be lost if you don't save them.");
    gtk_alert_dialog_set_buttons(dlg,
        (const char *[]){ "Cancel", "Discard", "Save", NULL });
    gtk_alert_dialog_set_cancel_button (dlg, CONFIRM_CANCEL);
    gtk_alert_dialog_set_default_button(dlg, CONFIRM_SAVE);
    gtk_alert_dialog_choose(dlg, a->window, NULL, on_confirm_done, a);

    g_object_unref(dlg);
    g_free(base);
}

/* ---- other actions ------------------------------------------------------- */

static void load_blank(App *a)
{
    app_load_document(a, document_new(DEFAULT_W, DEFAULT_H), NULL);
}

static void close_window(App *a)
{
    gtk_window_destroy(a->window);   /* does not re-emit ::close-request */
}

static void quit_app(App *a)
{
    g_application_quit(G_APPLICATION(a->gapp));
}

static void act_new(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    confirm_unsaved(user_data, load_blank);
}

static void act_open(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    confirm_unsaved(user_data, open_dialog);
}

/* The window manager's close button; TRUE blocks the close while we ask. */
static gboolean on_close_request(GtkWindow *win, gpointer user_data)
{
    App *a = user_data;
    if (!a->doc->modified)
        return FALSE;
    confirm_unsaved(a, close_window);
    return TRUE;
}

static void after_pixels_changed(App *a)
{
    canvas_repaint(a);
    layers_panel_queue_thumbs(a);
    statusbar_update(a);
    app_update_title(a);
}

static void after_restore(App *a, gboolean structural)
{
    if (structural) {
        canvas_update_size(a);
        layers_panel_refresh(a);
    }
    after_pixels_changed(a);
}

static void act_undo(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    after_restore(a, history_undo(a->doc));
}

static void act_redo(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    after_restore(a, history_redo(a->doc));
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

/* ---- clipboard ----------------------------------------------------------- */

/* GDK_MEMORY_DEFAULT is laid out exactly like CAIRO_FORMAT_ARGB32, so both
 * conversions are a straight copy of the pixels. */
static GdkTexture *texture_from_surface(cairo_surface_t *s)
{
    cairo_surface_flush(s);
    int stride = cairo_image_surface_get_stride(s);
    int height = cairo_image_surface_get_height(s);
    GBytes *bytes = g_bytes_new(cairo_image_surface_get_data(s),
                                (gsize) stride * height);
    GdkTexture *t = gdk_memory_texture_new(cairo_image_surface_get_width(s),
                                           height, GDK_MEMORY_DEFAULT,
                                           bytes, stride);
    g_bytes_unref(bytes);
    return t;
}

static cairo_surface_t *surface_from_texture(GdkTexture *t)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    gdk_texture_get_width(t),
                                                    gdk_texture_get_height(t));
    cairo_surface_flush(s);
    gdk_texture_download(t, cairo_image_surface_get_data(s),
                         cairo_image_surface_get_stride(s));
    cairo_surface_mark_dirty(s);
    return s;
}

/* The system clipboard, so a copy is usable outside Paintly too. */
static gboolean copy_selection(App *a)
{
    cairo_surface_t *s = document_copy_selection(a->doc);
    if (!s)
        return FALSE;                       /* nothing selected */
    GdkTexture *t = texture_from_surface(s);
    gdk_clipboard_set_texture(gtk_widget_get_clipboard(GTK_WIDGET(a->window)), t);
    g_object_unref(t);
    cairo_surface_destroy(s);
    return TRUE;
}

static void act_copy(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    copy_selection(user_data);
}

static void act_cut(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    if (!copy_selection(a))
        return;
    document_delete_selection(a->doc);
    after_pixels_changed(a);
}

static void on_paste_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    GdkTexture *t = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source),
                                                      res, NULL);
    if (!t)
        return;                             /* nothing image-shaped to paste */
    cairo_surface_t *s = surface_from_texture(t);
    g_object_unref(t);

    app_set_tool(a, "select");    /* the pasted pixels are draggable at once */
    document_paste(a->doc, s, 0, 0);
    cairo_surface_destroy(s);
    after_pixels_changed(a);
}

static void act_paste(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    App *a = user_data;
    gdk_clipboard_read_texture_async(
        gtk_widget_get_clipboard(GTK_WIDGET(a->window)), NULL, on_paste_done, a);
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
    confirm_unsaved(user_data, quit_app);
}

static const GActionEntry WIN_ACTIONS[] = {
    { "new",              act_new },
    { "open",             act_open },
    { "save",             act_save },
    { "save-as",          act_save_as },
    { "undo",             act_undo },
    { "redo",             act_redo },
    { "cut",              act_cut },
    { "copy",             act_copy },
    { "paste",            act_paste },
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
        { "Cu_t",   "win.cut" },
        { "_Copy",  "win.copy" },
        { "_Paste", "win.paste" } }, 3);
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
        { "win.cut",              { "<Control>x", NULL } },
        { "win.copy",             { "<Control>c", NULL } },
        { "win.paste",            { "<Control>v", NULL } },
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
    a->canvas_grip = HANDLE_NONE;   /* zero would read as HANDLE_NW */
    gdk_rgba_parse(&a->primary, "#000000");
    gdk_rgba_parse(&a->secondary, "#ffffff");
    a->tool = tools_find("pencil");
    a->layer_thumbs = g_ptr_array_new();
    a->tool_btns    = g_ptr_array_new();

    GtkWidget *win = gtk_application_window_new(gapp);
    a->window = GTK_WINDOW(win);
    gtk_window_set_default_size(a->window, 1280, 800);
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), TRUE);
    g_action_map_add_action_entries(G_ACTION_MAP(win), WIN_ACTIONS,
                                    G_N_ELEMENTS(WIN_ACTIONS), a);
    g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), a);

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
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(a->scroller), canvas_new(a));
    /* Ctrl+wheel zooms anywhere in the window, not just over the canvas. */
    canvas_attach_zoom(a, win);

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

/* ---- opening from the command line --------------------------------------- */

/* Hand a file to a window of its own: one document per process, so extra
 * arguments become extra Paintlys rather than fighting over this one. */
static void spawn_instance(GFile *file)
{
    char *path = g_file_get_path(file);
    if (!path)
        return;                     /* non-local: nothing to hand over */
    char *self = g_file_read_link("/proc/self/exe", NULL);
    if (!self)
        self = g_find_program_in_path("paintly");
    if (self) {
        char *argv[] = { self, path, NULL };
        g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL);
    }
    g_free(self);
    g_free(path);
}

/* `paintly image.png`, or a file manager's Open With. GApplication sends the
 * arguments here instead of emitting ::activate, so the window is built here.
 * The document it lands on is always the untouched default one - this process
 * was started by this command line - so there is nothing to save first. */
void app_open(GtkApplication *gapp, GFile **files, int n_files,
              const char *hint, gpointer user_data)
{
    App *a = user_data;
    app_activate(gapp, a);          /* the window has to exist to load into */

    for (int i = 1; i < n_files; i++)
        spawn_instance(files[i]);

    char *path = g_file_get_path(files[0]);
    if (!path) {
        show_error(a, "Only local files can be opened.");
        return;
    }
    load_image_path(a, path);
    g_free(path);
}
