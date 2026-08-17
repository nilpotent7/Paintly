#pragma once

#include <gtk/gtk.h>
#include "core/document.h"

typedef struct Tool Tool;   /* full definition in tools/tool.h */
typedef struct App  App;

#define CANVAS_MARGIN 28    /* workspace gap around the canvas frame */

typedef void (*AppContinue)(App *a);

struct App {
    GtkApplication *gapp;
    GtkWindow      *window;
    Document       *doc;

    /* --- active tool state ---------------------------------------------- */
    Tool    *tool;
    GdkRGBA  primary;      /* left mouse button color  */
    GdkRGBA  secondary;    /* right mouse button color */
    double   brush_size;   /* stroke width in canvas pixels */

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

    AppContinue pending;   /* action waiting on the unsaved-changes prompt */
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
/* Replace the current document (takes ownership; path may be NULL). */
void app_load_document(App *a, Document *doc, const char *path);

/* ui/canvas.c */
/* The canvas, its frame and the grip layer: drop the result in the scroller. */
GtkWidget *canvas_new(App *a);
/* Make Ctrl+wheel zoom anywhere inside `widget`, not just over the canvas. */
void canvas_attach_zoom(App *a, GtkWidget *widget);
void canvas_repaint(App *a);            /* queue a redraw               */
void canvas_update_size(App *a);        /* after zoom/document change   */
void canvas_set_zoom(App *a, double zoom);

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
