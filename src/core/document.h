#pragma once

#include "layer.h"

typedef struct { int x, y, w, h; } Rect;

typedef struct History History;

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

/* Selection operations.  Functions that destroy pixels push undo history
 * themselves; see each implementation for the exact policy. */
void document_select_all     (Document *doc);
void document_select_rect    (Document *doc, Rect r);
void document_deselect       (Document *doc);   /* commits floating pixels */
void document_lift_selection (Document *doc);   /* layer -> floating       */
void document_commit_floating(Document *doc);   /* floating -> layer       */
void document_delete_selection(Document *doc);
