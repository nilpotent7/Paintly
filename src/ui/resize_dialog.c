#include "../app.h"
#include <math.h>

#define RESIZE_MAX_PX  10000   /* per side; keeps one layer under ~400 MB */
#define RESIZE_MAX_PCT 1000

/* Dialog-local state: it lives exactly as long as the window, so it hangs off
 * the window rather than off App. */
typedef struct {
    App       *app;
    GtkWindow *win;
    GtkWidget *px_radio;         /* the other radio is percentage */
    GtkWidget *w_spin, *h_spin;
    GtkWidget *ratio;
    int        base_w, base_h;   /* the size 100% refers to */
    gboolean   selection;        /* resizing the selection, not the image */
    gboolean   pixels;           /* current mode, tracked so a toggle can convert */
    gboolean   guard;            /* break the aspect-ratio feedback loop */
} ResizeDialog;

/* The pixel size the fields currently ask for. */
static void target_size(ResizeDialog *rd, int *w, int *h)
{
    double vw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rd->w_spin));
    double vh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rd->h_spin));

    if (rd->pixels) {
        *w = (int) round(vw);
        *h = (int) round(vh);
    } else {
        *w = (int) round(rd->base_w * vw / 100.0);
        *h = (int) round(rd->base_h * vh / 100.0);
    }
    *w = CLAMP(*w, 1, RESIZE_MAX_PX);
    *h = CLAMP(*h, 1, RESIZE_MAX_PX);
}

/* Mirror the edited field into the other one, keeping the proportions.  In
 * percentage mode that just means the two percentages stay equal. */
static void follow_ratio(ResizeDialog *rd, GtkWidget *edited)
{
    if (rd->guard || !gtk_check_button_get_active(GTK_CHECK_BUTTON(rd->ratio)))
        return;

    GtkWidget *other = (edited == rd->w_spin) ? rd->h_spin : rd->w_spin;
    double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(edited)), scale = 1.0;
    if (rd->pixels)
        scale = (edited == rd->w_spin) ? (double) rd->base_h / rd->base_w
                                       : (double) rd->base_w / rd->base_h;

    rd->guard = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(other), MAX(1, round(v * scale)));
    rd->guard = FALSE;
}

static void on_spin(GtkSpinButton *spin, gpointer user_data)
{
    follow_ratio(user_data, GTK_WIDGET(spin));
}

static void on_ratio(GtkCheckButton *btn, gpointer user_data)
{
    ResizeDialog *rd = user_data;
    if (gtk_check_button_get_active(btn))
        follow_ratio(rd, rd->w_spin);   /* height catches up with width */
}

static void on_mode(GtkCheckButton *btn, gpointer user_data)
{
    ResizeDialog *rd = user_data;
    gboolean px = gtk_check_button_get_active(GTK_CHECK_BUTTON(rd->px_radio));
    /* Both radios emit on every switch; only the real change matters. */
    if (rd->guard || px == rd->pixels)
        return;

    double vw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rd->w_spin));
    double vh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rd->h_spin));
    double nw, nh;
    if (px) {
        nw = round(rd->base_w * vw / 100.0);
        nh = round(rd->base_h * vh / 100.0);
    } else {
        nw = round(vw * 100.0 / rd->base_w);
        nh = round(vh * 100.0 / rd->base_h);
    }
    rd->pixels = px;

    /* Range first, then value: a value outside the old range would be clamped. */
    double max = px ? RESIZE_MAX_PX : RESIZE_MAX_PCT;
    rd->guard = TRUE;
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(rd->w_spin), 1, max);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(rd->h_spin), 1, max);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rd->w_spin), CLAMP(nw, 1, max));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rd->h_spin), CLAMP(nh, 1, max));
    rd->guard = FALSE;
}

static void on_cancel(GtkButton *btn, gpointer user_data)
{
    ResizeDialog *rd = user_data;
    gtk_window_destroy(rd->win);
}

static void on_apply(GtkButton *btn, gpointer user_data)
{
    ResizeDialog *rd = user_data;
    App *a = rd->app;
    int w, h;
    target_size(rd, &w, &h);

    /* The selection is only still there if nothing dismissed it meanwhile. */
    if (rd->selection && a->doc->has_selection) {
        document_scale_selection(a->doc, w, h);
    } else {
        document_scale_canvas(a->doc, w, h);
        canvas_update_size(a);
    }
    canvas_repaint(a);
    layers_panel_queue_thumbs(a);
    statusbar_update(a);
    app_update_title(a);
    gtk_window_destroy(rd->win);
}

/* A label above a spin button, as one column of the two-field row. */
static GtkWidget *field(const char *caption, GtkWidget *spin)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *label = gtk_label_new(caption);
    gtk_widget_add_css_class(label, "ribbon-group-label");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), spin);
    return box;
}

void resize_dialog_show(App *a)
{
    Document *d = a->doc;
    ResizeDialog *rd = g_new0(ResizeDialog, 1);
    rd->app       = a;
    rd->selection = d->has_selection;
    rd->pixels    = TRUE;

    Rect sel = document_selection_rect(d);
    rd->base_w = rd->selection ? sel.w : d->width;
    rd->base_h = rd->selection ? sel.h : d->height;

    GtkWidget *win = gtk_window_new();
    rd->win = GTK_WINDOW(win);
    gtk_window_set_title(rd->win, rd->selection ? "Resize Selection" : "Resize Image");
    gtk_window_set_transient_for(rd->win, a->window);
    gtk_window_set_modal(rd->win, TRUE);
    gtk_window_set_resizable(rd->win, FALSE);
    g_object_set_data_full(G_OBJECT(win), "resize-dialog", rd, g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top   (box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start (box, 18);
    gtk_widget_set_margin_end   (box, 18);

    char buf[96];
    g_snprintf(buf, sizeof buf, "%s is %d × %d px",
               rd->selection ? "Selection" : "Image", rd->base_w, rd->base_h);
    GtkWidget *current = gtk_label_new(buf);
    gtk_label_set_xalign(GTK_LABEL(current), 0);
    gtk_box_append(GTK_BOX(box), current);

    /* -- pixels / percentage --------------------------------------------- */
    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    rd->px_radio = gtk_check_button_new_with_label("Pixels");
    GtkWidget *pct_radio = gtk_check_button_new_with_label("Percentage");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(pct_radio),
                               GTK_CHECK_BUTTON(rd->px_radio));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rd->px_radio), TRUE);
    gtk_box_append(GTK_BOX(modes), rd->px_radio);
    gtk_box_append(GTK_BOX(modes), pct_radio);
    gtk_box_append(GTK_BOX(box), modes);

    /* -- the two fields --------------------------------------------------- */
    rd->w_spin = gtk_spin_button_new_with_range(1, RESIZE_MAX_PX, 1);
    rd->h_spin = gtk_spin_button_new_with_range(1, RESIZE_MAX_PX, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rd->w_spin), rd->base_w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rd->h_spin), rd->base_h);
    gtk_editable_set_width_chars(GTK_EDITABLE(rd->w_spin), 7);
    gtk_editable_set_width_chars(GTK_EDITABLE(rd->h_spin), 7);

    GtkWidget *fields = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(fields), field("Width",  rd->w_spin));
    gtk_box_append(GTK_BOX(fields), field("Height", rd->h_spin));
    gtk_box_append(GTK_BOX(box), fields);

    rd->ratio = gtk_check_button_new_with_label("Maintain aspect ratio");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rd->ratio), TRUE);
    gtk_box_append(GTK_BOX(box), rd->ratio);

    /* Connected last, so building the fields above fires nothing. */
    g_signal_connect(rd->px_radio, "toggled", G_CALLBACK(on_mode),  rd);
    g_signal_connect(pct_radio,    "toggled", G_CALLBACK(on_mode),  rd);
    g_signal_connect(rd->w_spin, "value-changed", G_CALLBACK(on_spin), rd);
    g_signal_connect(rd->h_spin, "value-changed", G_CALLBACK(on_spin), rd);
    g_signal_connect(rd->ratio,  "toggled", G_CALLBACK(on_ratio), rd);

    /* -- header bar buttons ----------------------------------------------- */
    GtkWidget *bar = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), FALSE);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *apply  = gtk_button_new_with_label("Resize");
    gtk_widget_add_css_class(apply, "suggested-action");
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), rd);
    g_signal_connect(apply,  "clicked", G_CALLBACK(on_apply),  rd);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), cancel);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(bar), apply);
    gtk_window_set_titlebar(rd->win, bar);

    gtk_window_set_child(rd->win, box);
    gtk_widget_grab_focus(rd->w_spin);
    gtk_window_present(rd->win);
}
