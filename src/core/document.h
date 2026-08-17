#pragma once

#include "layer.h"

typedef struct { int x, y, w, h; } Rect;

typedef struct History History;

/* The eight resize grips around a selection, clockwise from the top-left. */
typedef enum {
    HANDLE_NONE = -1,
    HANDLE_NW, HANDLE_N, HANDLE_NE, HANDLE_E,
    HANDLE_SE, HANDLE_S, HANDLE_SW, HANDLE_W,
    HANDLE_COUNT
} Handle;

#define HANDLE_PX 8.0   /* on-screen grip size, in pixels at any zoom */

typedef struct Document {
    int        width, height;
    GPtrArray *layers;          /* Layer*, index 0 = bottom               */
    int        active;          /* index of the layer tools draw on       */
    int        layer_counter;   /* for auto-naming: "Layer 2", "Layer 3"… */
    History   *history;

    /* selection */
    gboolean         has_selection;
    Rect             selection;       /* canvas pixels, normalized (w,h > 0)    */
    cairo_surface_t *floating;  /* lifted pixels being moved, or NULL     */
    double           float_x, float_y;

    cairo_surface_t *resize_src;
    Rect             resize_from;
    Handle           resize_handle;

    char     *filepath;         /* last save/open location, or NULL       */
    gboolean  modified;
} Document;

Document *document_new (int width, int height);
void      document_free(Document *doc);

Layer *document_active_layer(Document *doc);
Layer *document_add_layer   (Document *doc);              /* above active  */
void   document_remove_layer(Document *doc, int index);   /* keeps >= 1    */
void   document_move_layer  (Document *doc, int index, int dir); /* ±1     */

/* Paint all visible layers (and the floating selection) into `cr`.
 * `filter` controls scaling interpolation: NEAREST gives crisp pixels when
 * the canvas is drawn zoomed-in, GOOD gives smooth downscaling. */
void document_render(Document *doc, cairo_t *cr, cairo_filter_t filter);

/* Composite everything into a single new surface (for saving to PNG). */
cairo_surface_t *document_flatten(Document *doc);

void document_resize_canvas(Document *doc, Rect r,
                            double red, double green, double blue, double alpha);

/* Selection operations.  Functions that destroy pixels push undo history
 * themselves; see each implementation for the exact policy. */
void document_select_all     (Document *doc);
void document_select_rect    (Document *doc, Rect r);   // clamped to canvas
void document_set_selection  (Document *doc, Rect r);   // verbatim, may hang off
void document_deselect       (Document *doc);   // commits floating pixels
void document_lift_selection (Document *doc);   // layer -> floating
void document_commit_floating(Document *doc);   // floating -> layer
void document_create_floating(Document *doc, Rect region);   // new floating
void document_drop_floating  (Document *doc);   // discard, don't stamp down
void document_delete_selection(Document *doc);
/* Move the floating pixels to (x, y), clamped so they stay on the canvas. */
void document_move_floating  (Document *doc, double x, double y);
/* A fresh copy of the selected pixels - the floating ones if there are any,
 * otherwise straight off the active layer.  NULL when nothing is selected;
 * the caller owns the surface. */
cairo_surface_t *document_copy_selection(Document *doc);
/* Drop `src` in at (x, y) as a floating selection, ready to be moved. */
void document_paste(Document *doc, cairo_surface_t *src, int x, int y,
                    double red, double green, double blue, double alpha);

/* ---- selection geometry ------------------------------------------------- */

/* The selection rect, following the floating pixels while they are moved. */
Rect     document_selection_rect    (Document *doc);
gboolean document_point_in_selection(Document *doc, double x, double y);
/* Center of grip `h` on an arbitrary rect, and on the selection. */
void     rect_handle_pos    (Rect r, Handle h, double *x, double *y);
void     document_handle_pos(Document *doc, Handle h, double *x, double *y);
Handle   document_hit_handle(Document *doc, double x, double y, double zoom);

Rect rect_resize(Rect r, Handle h, double x, double y);

void document_resize_begin(Document *doc, Handle h);
Rect document_resize_rect (Document *doc, double x, double y);
void document_resize_to   (Document *doc, Rect target);
void document_resize_end  (Document *doc);
