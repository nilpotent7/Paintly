#include "../app.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

/* Overridable at build time (make DONATE_URL=...) and at run time
 * (PAINTLY_DONATE_URL=...), so a local dev server can stand in. */
#ifndef PAINTLY_DONATE_URL
#define PAINTLY_DONATE_URL "https://paintly.example.com/api/donate"
#endif

/* Ask once total use reaches FIRST, then each time another REPEAT has
 * passed since the last ask.  Seconds of open-window time, across runs. */
#define ASK_FIRST_S       3600
#define ASK_REPEAT_S      900
#define ASK_FIRST_DEV_S   15           /* PAINTLY_DONATE_DEV=1 */
#define ASK_REPEAT_DEV_S  5
#define FETCH_MAX_BYTES   (1 << 20)    /* JSON or QR image; anything bigger is wrong */
#define QR_PX             200

/* ---- usage accounting ---------------------------------------------------- */

/* Read only at close and shutdown - never at startup. */
static char *usage_path(void)
{
    return g_build_filename(g_get_user_state_dir(), "paintly", "usage.ini", NULL);
}

static GKeyFile *usage_load(void)
{
    GKeyFile *kf = g_key_file_new();
    char *path = usage_path();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);  /* absent = zero */
    g_free(path);
    return kf;
}

static void usage_save(GKeyFile *kf)
{
    char *path = usage_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_key_file_save_to_file(kf, path, NULL);
    g_free(dir);
    g_free(path);
}

static gint64 session_seconds(App *a)
{
    return (g_get_monotonic_time() - a->started_at) / G_USEC_PER_SEC;
}

static gboolean dev_mode(void)
{
    return g_getenv("PAINTLY_DONATE_DEV") != NULL;
}

/* Dev runs keep their own tally, so testing never moves the real one. */
static const char *usage_group(void)
{
    return dev_mode() ? "dev" : "usage";
}

static gint64 usage_total(GKeyFile *kf, App *a)
{
    return g_key_file_get_int64(kf, usage_group(), "seconds", NULL) + session_seconds(a);
}

gboolean donate_should_ask(App *a)
{
    GKeyFile *kf = usage_load();
    gint64 total = usage_total(kf, a);
    gboolean asked = g_key_file_has_key(kf, usage_group(), "asked_at", NULL);
    gint64 since = total - g_key_file_get_int64(kf, usage_group(), "asked_at", NULL);
    g_key_file_unref(kf);

    if (!asked)
        return total >= (dev_mode() ? ASK_FIRST_DEV_S : ASK_FIRST_S);
    return since >= (dev_mode() ? ASK_REPEAT_DEV_S : ASK_REPEAT_S);
}

void donate_record_usage(App *a)
{
    gint64 s = session_seconds(a);
    if (s <= 0)
        return;
    GKeyFile *kf = usage_load();
    g_key_file_set_int64(kf, usage_group(), "seconds",
                         g_key_file_get_int64(kf, usage_group(), "seconds", NULL) + s);
    usage_save(kf);
    g_key_file_unref(kf);
}

/* The repeat interval counts from here, whatever the answer. */
static void usage_mark_asked(App *a)
{
    GKeyFile *kf = usage_load();
    g_key_file_set_int64(kf, usage_group(), "asked_at", usage_total(kf, a));
    usage_save(kf);
    g_key_file_unref(kf);
}

/* ---- the dialog ---------------------------------------------------------- */

/* Dialog-local state: lives exactly as long as the window. */
typedef struct {
    App          *app;
    GtkWindow    *win;
    GtkStack     *stack;          /* "loading" / "error" / "info" */
    GtkWidget    *error_detail;
    GtkWidget    *qr, *network, *coins, *address, *copy_btn;
    SoupSession  *session;
    GCancellable *cancel;         /* cancelled on close, before rd is freed */
} DonateDialog;

static void donate_dialog_free(gpointer data)
{
    DonateDialog *rd = data;
    g_object_unref(rd->cancel);
    g_object_unref(rd->session);
    g_free(rd);
}

static void on_destroy(GtkWidget *win, gpointer user_data)
{
    DonateDialog *rd = user_data;
    g_cancellable_cancel(rd->cancel);   /* pending callbacks must not touch rd */
    app_run_pending(rd->app);           /* closing from the quit prompt quits */
}

static void show_error(DonateDialog *rd, const char *detail)
{
    gtk_label_set_text(GTK_LABEL(rd->error_detail), detail);
    gtk_stack_set_visible_child_name(rd->stack, "error");
}

/* Body of a finished request, or NULL and *err (CANCELLED once rd is gone). */
static GBytes *finish_fetch(GObject *source, GAsyncResult *res, GError **err)
{
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), res, err);
    if (!body)
        return NULL;
    SoupMessage *msg = soup_session_get_async_result_message(SOUP_SESSION(source), res);
    guint status = soup_message_get_status(msg);
    if (!SOUP_STATUS_IS_SUCCESSFUL(status)) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "The server answered %u %s.",
                    status, soup_message_get_reason_phrase(msg));
    } else if (g_bytes_get_size(body) > FETCH_MAX_BYTES) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "The server's reply is too large.");
    } else {
        return body;
    }
    g_bytes_unref(body);
    return NULL;
}

static gboolean cancelled(GError *err)
{
    return g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

static void set_qr(DonateDialog *rd, GBytes *png)
{
    GError *err = NULL;
    GdkTexture *tex = gdk_texture_new_from_bytes(png, &err);
    if (!tex) {
        g_warning("donate: QR image: %s", err->message);
        g_error_free(err);
        return;                   /* the address below still works */
    }
    gtk_image_set_from_paintable(GTK_IMAGE(rd->qr), GDK_PAINTABLE(tex));
    gtk_widget_set_visible(rd->qr, TRUE);
    g_object_unref(tex);
}

static void on_qr_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
    GError *err = NULL;
    GBytes *png = finish_fetch(source, res, &err);
    if (!png) {
        if (!cancelled(err))
            g_warning("donate: QR image: %s", err->message);
        g_error_free(err);
        return;                   /* the address alone still works */
    }
    set_qr(user_data, png);
    g_bytes_unref(png);
}

static void fetch(DonateDialog *rd, const char *url, GAsyncReadyCallback done)
{
    SoupMessage *msg = soup_message_new(SOUP_METHOD_GET, url);
    if (!msg) {
        show_error(rd, "The donation address is not a valid URL.");
        return;
    }
    soup_session_send_and_read_async(rd->session, msg, G_PRIORITY_DEFAULT,
                                     rd->cancel, done, rd);
    g_object_unref(msg);
}

/* A data: URI is decoded in place; an http(s) URL is fetched separately. */
static void load_qr(DonateDialog *rd, const char *qr)
{
    if (g_str_has_prefix(qr, "data:")) {
        GBytes *png = soup_uri_decode_data_uri(qr, NULL);
        if (png) {
            set_qr(rd, png);
            g_bytes_unref(png);
        }
    } else if (g_str_has_prefix(qr, "https://") || g_str_has_prefix(qr, "http://")) {
        fetch(rd, qr, on_qr_fetched);
    }
}

static const char *member_string(JsonObject *o, const char *name)
{
    JsonNode *n = json_object_get_member(o, name);
    return (n && JSON_NODE_HOLDS_VALUE(n) &&
            json_node_get_value_type(n) == G_TYPE_STRING) ? json_node_get_string(n) : NULL;
}

/* "coins": ["USDT", "USDC"] -> "USDT, USDC" */
static char *coins_text(JsonObject *o)
{
    JsonNode *n = json_object_get_member(o, "coins");
    if (!n || !JSON_NODE_HOLDS_ARRAY(n))
        return NULL;
    JsonArray *arr = json_node_get_array(n);
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < json_array_get_length(arr); i++) {
        JsonNode *e = json_array_get_element(arr, i);
        if (!JSON_NODE_HOLDS_VALUE(e) || json_node_get_value_type(e) != G_TYPE_STRING)
            continue;
        if (s->len)
            g_string_append(s, ", ");
        g_string_append(s, json_node_get_string(e));
    }
    return g_string_free(s, s->len == 0);
}

static void on_info_fetched(GObject *source, GAsyncResult *res, gpointer user_data)
{
    GError *err = NULL;
    GBytes *body = finish_fetch(source, res, &err);
    if (!body) {
        if (!cancelled(err))
            show_error(user_data, err->message);
        g_error_free(err);
        return;
    }
    DonateDialog *rd = user_data;   /* safe: not cancelled, so still open */

    JsonParser *parser = json_parser_new();
    gsize len;
    const char *data = g_bytes_get_data(body, &len);
    if (!json_parser_load_from_data(parser, data, (gssize) len, &err)) {
        show_error(rd, err->message);
        g_error_free(err);
        goto out;
    }
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *o = (root && JSON_NODE_HOLDS_OBJECT(root)) ? json_node_get_object(root) : NULL;
    const char *address = o ? member_string(o, "walletAddress") : NULL;
    if (!address) {
        show_error(rd, "The server's reply has no wallet address.");
        goto out;
    }

    const char *network = member_string(o, "network");
    char *coins = coins_text(o);
    gtk_label_set_text(GTK_LABEL(rd->address), address);
    gtk_label_set_text(GTK_LABEL(rd->network), network ? network : "—");
    gtk_label_set_text(GTK_LABEL(rd->coins), coins ? coins : "—");
    g_free(coins);

    const char *qr = member_string(o, "qrCode");
    if (qr)
        load_qr(rd, qr);
    gtk_stack_set_visible_child_name(rd->stack, "info");

out:
    g_object_unref(parser);
    g_bytes_unref(body);
}

static void start_fetch(DonateDialog *rd)
{
    const char *url = g_getenv("PAINTLY_DONATE_URL");
    gtk_stack_set_visible_child_name(rd->stack, "loading");
    fetch(rd, url ? url : PAINTLY_DONATE_URL, on_info_fetched);
}

static void on_retry(GtkButton *btn, gpointer user_data)
{
    start_fetch(user_data);
}

static void on_copy(GtkButton *btn, gpointer user_data)
{
    DonateDialog *rd = user_data;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(btn)),
                           gtk_label_get_text(GTK_LABEL(rd->address)));
    gtk_button_set_label(btn, "Copied");
}

static void on_close(GtkButton *btn, gpointer user_data)
{
    gtk_window_destroy(((DonateDialog *) user_data)->win);
}

static GtkWidget *key_label(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "donate-key");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    return l;
}

static GtkWidget *value_label(void)
{
    GtkWidget *l = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    return l;
}

static GtkWidget *loading_page(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    GtkWidget *spin = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spin));
    gtk_widget_set_size_request(spin, 24, 24);
    gtk_box_append(GTK_BOX(box), spin);
    gtk_box_append(GTK_BOX(box), key_label("Loading donation details…"));
    gtk_widget_set_halign(gtk_widget_get_last_child(box), GTK_ALIGN_CENTER);
    return box;
}

static GtkWidget *error_page(DonateDialog *rd)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    GtkWidget *title = gtk_label_new("Couldn't load donation details.");
    gtk_box_append(GTK_BOX(box), title);

    rd->error_detail = gtk_label_new(NULL);
    gtk_widget_add_css_class(rd->error_detail, "donate-key");
    gtk_label_set_wrap(GTK_LABEL(rd->error_detail), TRUE);
    gtk_label_set_justify(GTK_LABEL(rd->error_detail), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(box), rd->error_detail);

    GtkWidget *retry = gtk_button_new_with_label("Try again");
    gtk_widget_set_halign(retry, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(retry, 6);
    g_signal_connect(retry, "clicked", G_CALLBACK(on_retry), rd);
    gtk_box_append(GTK_BOX(box), retry);
    return box;
}

static GtkWidget *info_page(DonateDialog *rd)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    /* Hidden until a QR actually decodes, so a bad image leaves no hole.  An
     * image, not a picture: its pixel size holds whatever the server sends. */
    rd->qr = gtk_image_new();
    gtk_widget_add_css_class(rd->qr, "donate-qr");
    gtk_image_set_pixel_size(GTK_IMAGE(rd->qr), QR_PX);
    gtk_widget_set_halign(rd->qr, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(rd->qr, FALSE);
    gtk_box_append(GTK_BOX(box), rd->qr);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    rd->network = value_label();
    rd->coins   = value_label();
    gtk_grid_attach(GTK_GRID(grid), key_label("Network"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), rd->network,          1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), key_label("Coins"),   0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), rd->coins,            1, 1, 1, 1);
    gtk_box_append(GTK_BOX(box), grid);

    /* Copy sits on the caption row so the address gets the full width. */
    GtkWidget *addr_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *caption = key_label("Wallet address");
    gtk_widget_set_hexpand(caption, TRUE);
    gtk_box_append(GTK_BOX(head), caption);
    rd->copy_btn = gtk_button_new_with_label("Copy");
    gtk_widget_add_css_class(rd->copy_btn, "donate-copy");
    g_signal_connect(rd->copy_btn, "clicked", G_CALLBACK(on_copy), rd);
    gtk_box_append(GTK_BOX(head), rd->copy_btn);
    gtk_box_append(GTK_BOX(addr_box), head);

    rd->address = gtk_label_new(NULL);
    gtk_widget_add_css_class(rd->address, "donate-address");
    gtk_label_set_selectable(GTK_LABEL(rd->address), TRUE);
    gtk_label_set_wrap(GTK_LABEL(rd->address), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(rd->address), PANGO_WRAP_CHAR);
    /* A hyphen inside a wallet address reads as part of it. */
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_insert_hyphens_new(FALSE));
    gtk_label_set_attributes(GTK_LABEL(rd->address), attrs);
    pango_attr_list_unref(attrs);
    gtk_label_set_xalign(GTK_LABEL(rd->address), 0.0);
    gtk_box_append(GTK_BOX(addr_box), rd->address);
    gtk_box_append(GTK_BOX(box), addr_box);
    return box;
}

void donate_dialog_show(App *a)
{
    DonateDialog *rd = g_new0(DonateDialog, 1);
    rd->app = a;
    rd->cancel = g_cancellable_new();
    rd->session = soup_session_new_with_options("timeout", 15,
                                                "user-agent", "Paintly/" PAINTLY_VERSION,
                                                NULL);

    GtkWidget *win = gtk_window_new();
    rd->win = GTK_WINDOW(win);
    gtk_window_set_title(rd->win, "Support Paintly");
    gtk_window_set_transient_for(rd->win, a->window);
    gtk_window_set_modal(rd->win, TRUE);
    gtk_window_set_resizable(rd->win, FALSE);
    gtk_window_set_default_size(rd->win, 380, -1);
    g_object_set_data_full(G_OBJECT(win), "donate-dialog", rd, donate_dialog_free);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), rd);

    GtkEventController *keys = gtk_shortcut_controller_new();
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(keys),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string("Escape"),
                         gtk_named_action_new("window.close")));
    gtk_widget_add_controller(win, keys);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top   (box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start (box, 18);
    gtk_widget_set_margin_end   (box, 18);

    GtkWidget *blurb = gtk_label_new("Paintly is free. If it has been useful to you, "
                                     "a donation helps keep it that way.");
    gtk_widget_add_css_class(blurb, "donate-blurb");
    gtk_label_set_wrap(GTK_LABEL(blurb), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(blurb), 44);
    gtk_label_set_justify(GTK_LABEL(blurb), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(box), blurb);

    GtkWidget *stack = gtk_stack_new();
    rd->stack = GTK_STACK(stack);
    gtk_stack_set_vhomogeneous(rd->stack, FALSE);
    gtk_widget_set_size_request(stack, -1, 140);
    gtk_stack_add_named(rd->stack, loading_page(),  "loading");
    gtk_stack_add_named(rd->stack, error_page(rd),  "error");
    gtk_stack_add_named(rd->stack, info_page(rd),   "info");
    gtk_box_append(GTK_BOX(box), stack);

    GtkWidget *bar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), FALSE);
    GtkWidget *close = gtk_button_new_with_label("Close");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close), rd);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), close);
    gtk_window_set_titlebar(rd->win, bar);

    gtk_window_set_child(rd->win, box);
    start_fetch(rd);
    gtk_window_present(rd->win);
}

/* ---- the prompt on close ------------------------------------------------- */

/* Button indices, in the order they are handed to the alert dialog. */
typedef enum { ASK_NO, ASK_DONATE } AskChoice;

static void on_ask_done(GObject *source, GAsyncResult *res, gpointer data)
{
    App *a = data;
    /* Escape and the window manager count as No: the user was leaving. */
    if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL) == ASK_DONATE)
        donate_dialog_show(a);    /* closing it runs a->pending */
    else
        app_run_pending(a);
}

void donate_ask(App *a, AppContinue then)
{
    a->pending = then;
    usage_mark_asked(a);

    GtkAlertDialog *dlg = gtk_alert_dialog_new("Has Paintly been useful?");
    gtk_alert_dialog_set_detail(dlg,
        "Paintly is free. A small donation helps keep it that way.");
    gtk_alert_dialog_set_buttons(dlg, (const char *[]){ "No", "Donate", NULL });
    gtk_alert_dialog_set_cancel_button (dlg, ASK_NO);
    gtk_alert_dialog_set_default_button(dlg, ASK_DONATE);
    gtk_alert_dialog_choose(dlg, a->window, NULL, on_ask_done, a);
    g_object_unref(dlg);
}
