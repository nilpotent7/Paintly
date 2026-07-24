#pragma once

#include <gtk/gtk.h>
#include "core/document.h"

typedef struct Tool Tool;   /* full definition in tools/tool.h */

typedef struct App {
    GtkApplication *gapp;
    GtkWindow      *window;
    Document       *doc;

    /* --- active tool state ---------------------------------------------- */
    Tool    *tool;
    GdkRGBA  primary;      /* left mouse button color  */
    GdkRGBA  secondary;    /* right mouse button color */
    double   brush_size;   /* stroke width in canvas pixels */

    /* --- canvas --------------------------------------------------------- */
    GtkWidget *canvas;     /* GtkDrawingArea */
    GtkWidget *scroller;
    double     zoom;       /* 1.0 = 100% */
    gboolean   pressed;    /* is a stroke in progress?  */
    guint      pressed_button;
    double     press_x, press_y;   /* canvas coords at press   */
    double     last_x,  last_y;    /* previous motion position */
    double     cur_x,   cur_y;     /* latest position          */
    gboolean   hover_valid;        /* pointer is over canvas   */

    /* --- widgets other modules poke ------------------------------------- */
    GtkToggleButton *tool_group_leader;  /* radio-group anchor for tools */
    GtkWidget *layers_btn;               /* ribbon toggle for the panel  */
    GtkWidget *color_indicator;
    GtkWidget *status_dims, *status_cursor, *status_sel;
    GtkWidget *zoom_scale, *zoom_label;
    gboolean   zoom_guard;               /* break slider feedback loops  */
    GtkWidget *layers_revealer, *layers_list, *opacity_scale;
    gboolean   layers_guard;
    GPtrArray *layer_thumbs;             /* thumbnail widgets, top-first */
} App;

/* ui/app.c */
void app_startup (GtkApplication *gapp, gpointer user_data);
void app_activate(GtkApplication *gapp, gpointer user_data);
void app_set_tool(App *a, const char *tool_id);
void app_update_title(App *a);
/* Replace the current document (takes ownership; path may be NULL). */
void app_load_document(App *a, Document *doc, const char *path);

/* ui/canvas.c */
GtkWidget *canvas_new(App *a);
void canvas_repaint(App *a);            /* queue a redraw               */
void canvas_update_size(App *a);        /* after zoom/document change   */
void canvas_set_zoom(App *a, double zoom);

/* ui/ribbon.c */
GtkWidget *ribbon_new(App *a);
GtkWidget *ribbon_group_new(const char *label, GtkWidget *content);

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
