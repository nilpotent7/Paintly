#pragma once

#include <gtk/gtk.h>
#include "core/document.h"

typedef struct Tool Tool;   /* full definition in tools/tool.h */
typedef struct App  App;

#define CANVAS_MARGIN 28    /* workspace gap around the canvas frame */

typedef void (*AppContinue)(App *a);

/* Stroke dash patterns, in the order the ribbon lists them. */
typedef enum {
    DASH_SOLID, DASH_DASH, DASH_DOT, DASH_DASH_DOT, DASH_COUNT
} DashStyle;

struct App {
    GtkApplication *gapp;
    GtkWindow      *window;
    Document       *doc;

    /* --- active tool state ---------------------------------------------- */
    Tool    *tool;
    GdkRGBA  primary;      /* left mouse button color  */
    GdkRGBA  secondary;    /* right mouse button color */
    double   brush_size;   /* stroke width in canvas pixels */
    DashStyle dash;        /* dash pattern every stroked line follows */
    double   dash_phase;   /* distance drawn so far in the current stroke */

    /* --- canvas --------------------------------------------------------- */
    GtkWidget *canvas;     /* GtkDrawingArea, exactly the size of the image */
    GtkWidget *grip_layer; /* paints the canvas grips outside that edge      */
    GtkWidget *scroller;
    double     zoom;       /* 1.0 = 100% */
    gboolean   pressed;    /* is a stroke in progress?  */
    guint      pressed_button;
    double     press_x, press_y;   /* canvas coords at press   */
    double     last_x,  last_y;    /* previous motion position */
    double     cur_x,   cur_y;     /* latest position          */
    gboolean   hover_valid;        /* pointer is over canvas   */
    const char *cursor_name;       /* cursor currently set on the canvas */
    Handle     canvas_grip;        /* canvas grip being dragged, or HANDLE_NONE */
    Rect       grip_rect;          /* previewed canvas, in current image coords */
    double     grip_ox, grip_oy;   /* image origin in surface coords, at press  */
    int        grip_margin_x, grip_margin_y;  /* frame offset pinned for the drag */

    /* --- widgets other modules poke ------------------------------------- */
    GtkToggleButton *tool_group_leader;  /* radio-group anchor for tools */
    GPtrArray *tool_btns;                /* one ribbon toggle per tool   */
    GtkWidget *layers_btn;               /* ribbon toggle for the panel  */
    GtkWidget *color_indicator;
    GtkWidget *status_dims, *status_cursor, *status_sel;
    GtkWidget *zoom_scale, *zoom_label;
    gboolean   zoom_guard;               /* break slider feedback loops  */
    GtkWidget *layers_revealer, *layers_list, *opacity_scale;
    gboolean   layers_guard;
    GPtrArray *layer_thumbs;             /* thumbnail widgets, top-first */

    AppContinue pending;   /* action waiting on a prompt (unsaved, donate) */
    gint64      started_at;  /* monotonic µs at window creation, for usage time */
};

/* ui/app.c */
void app_startup (GtkApplication *gapp, gpointer user_data);
void app_activate(GtkApplication *gapp, gpointer user_data);
/* ::open - file arguments from the command line or a file manager. */
void app_open    (GtkApplication *gapp, GFile **files, int n_files,
                  const char *hint, gpointer user_data);
void app_set_tool(App *a, const char *tool_id);
void app_update_title(App *a);
void app_restyle_floating(App *a);
/* FALSE while the active layer is hidden - nothing may touch its pixels. */
gboolean app_can_edit(App *a);
/* Re-gate the editing actions and the canvas cursor on that answer. */
void app_sync_editable(App *a);
/* Replace the current document (takes ownership; path may be NULL). */
void app_load_document(App *a, Document *doc, const char *path);
/* Run App.pending from an idle, then clear it; a no-op when nothing waits. */
void app_run_pending(App *a);

/* ui/canvas.c */
/* The canvas, its frame and the grip layer: drop the result in the scroller. */
GtkWidget *canvas_new(App *a);
/* Make Ctrl+wheel zoom anywhere inside `widget`, not just over the canvas. */
void canvas_attach_zoom(App *a, GtkWidget *widget);
void canvas_repaint(App *a);            /* queue a redraw               */
void canvas_sync_cursor(App *a);        /* after the edit lock changed  */
/* Top-left of the visible workspace, in canvas pixels, clamped to the image. */
void canvas_view_origin(App *a, double *x, double *y);
void canvas_update_size(App *a);        /* after zoom/document change   */
void canvas_set_zoom(App *a, double zoom);

/* ui/resize_dialog.c */
/* Ask for a new size - in pixels or as a percentage - and apply it to the
 * selection if there is one, otherwise to the whole image. */
void resize_dialog_show(App *a);

/* ui/donate.c */
/* Wallet address, network, coins and QR, fetched from the website on open.
 * Closing it runs App.pending, which is how the quit prompt's Donate quits. */
void donate_dialog_show(App *a);
/* TRUE when the close-time prompt is due: an hour of use in total, then every
 * further 15 minutes (15 s, then 5 s under PAINTLY_DONATE_DEV). */
gboolean donate_should_ask(App *a);
/* "Has Paintly been useful?" - No runs `then`, Donate runs it after the dialog. */
void donate_ask(App *a, AppContinue then);
/* Add this session's time to the stored total; once, at shutdown. */
void donate_record_usage(App *a);

/* ui/ribbon.c */
GtkWidget *ribbon_new(App *a);
GtkWidget *ribbon_group_new(const char *label, GtkWidget *content);
/* Check the button for App.tool, after a tool switch that wasn't a click. */
void ribbon_sync_tool(App *a);
/* Pointing hand over a control, closing while it is held. */
void ribbon_hand_cursor(GtkWidget *w);

/* ui/colors.c */
GtkWidget *colors_group_new(App *a);
void colors_refresh(App *a);

/* ui/layers_panel.c */
GtkWidget *layers_panel_new(App *a);
void layers_panel_refresh(App *a);      /* rebuild rows (structure changed) */
void layers_panel_queue_thumbs(App *a); /* redraw thumbnails (pixels changed) */

/* ui/statusbar.c */
GtkWidget *statusbar_new(App *a);
void statusbar_update(App *a);
void statusbar_sync_zoom(App *a);
void statusbar_zoom_step(App *a, int dir);
