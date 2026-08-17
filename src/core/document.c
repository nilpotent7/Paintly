#include "document.h"
#include "history.h"
#include <math.h>

static const gboolean CLAMP_SELECTION_TO_CANVAS = FALSE;

Document *document_new(int width, int height)
{
    Document *d = g_new0(Document, 1);
    d->width  = width;
    d->height = height;
    d->layers = g_ptr_array_new_with_free_func((GDestroyNotify) layer_free);
    d->active = 0;
    d->layer_counter = 1;
    d->history = history_new();
    d->resize_handle = HANDLE_NONE;

    /* Start with an opaque white canvas. */
    Layer *bg = layer_new(width, height, "Background");
    layer_fill(bg, 1, 1, 1, 1);
    g_ptr_array_add(d->layers, bg);
    return d;
}

void document_free(Document *doc)
{
    if (!doc)
        return;
    history_free(doc->history);
    g_ptr_array_unref(doc->layers);
    document_drop_floating(doc);
    g_free(doc->filepath);
    g_free(doc);
}

Layer *document_active_layer(Document *doc)
{
    return g_ptr_array_index(doc->layers, doc->active);
}

Layer *document_add_layer(Document *doc)
{
    char name[64];
    doc->layer_counter++;
    g_snprintf(name, sizeof name, "Layer %d", doc->layer_counter);

    /* New layers are fully transparent and sit directly above the active
     * one, which then becomes the new active layer. */
    Layer *l = layer_new(doc->width, doc->height, name);
    g_ptr_array_insert(doc->layers, doc->active + 1, l);
    doc->active += 1;
    doc->modified = TRUE;
    return l;
}

void document_remove_layer(Document *doc, int index)
{
    if (doc->layers->len <= 1)
        return;                          /* always keep at least one layer */
    if (index < 0 || index >= (int) doc->layers->len)
        return;
    g_ptr_array_remove_index(doc->layers, index);  /* frees the layer */
    doc->active = MAX(0, index - 1);
    doc->modified = TRUE;
}

void document_move_layer(Document *doc, int index, int dir)
{
    int other = index + dir;
    if (index < 0 || index >= (int) doc->layers->len ||
        other < 0 || other >= (int) doc->layers->len)
        return;
    Layer *a = g_ptr_array_index(doc->layers, index);
    Layer *b = g_ptr_array_index(doc->layers, other);
    doc->layers->pdata[index] = b;
    doc->layers->pdata[other] = a;
    if (doc->active == index)
        doc->active = other;
    else if (doc->active == other)
        doc->active = index;
    doc->modified = TRUE;
}

void document_render(Document *doc, cairo_t *cr, cairo_filter_t filter)
{
    for (guint i = 0; i < doc->layers->len; i++) {
        Layer *l = g_ptr_array_index(doc->layers, i);
        if (!l->visible)
            continue;
        cairo_save(cr);
        cairo_set_source_surface(cr, l->surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), filter);
        cairo_paint_with_alpha(cr, l->opacity);
        cairo_restore(cr);

        /* Floating (moved) selection pixels hover above the active layer
         * but below everything stacked on top of it. */
        if ((int) i == doc->active && doc->floating) {
            cairo_save(cr);
            cairo_set_source_surface(cr, doc->floating,
                                     doc->float_x, doc->float_y);
            cairo_pattern_set_filter(cairo_get_source(cr), filter);
            cairo_paint_with_alpha(cr, l->opacity);
            cairo_restore(cr);
        }
    }
}

cairo_surface_t *document_flatten(Document *doc)
{
    cairo_surface_t *out =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, doc->width, doc->height);
    cairo_t *cr = cairo_create(out);
    document_render(doc, cr, CAIRO_FILTER_GOOD);
    cairo_destroy(cr);
    return out;
}

void document_resize_canvas(Document *doc, Rect r,
                            double red, double green, double blue, double alpha)
{
    r.w = MAX(1, r.w);
    r.h = MAX(1, r.h);
    if (r.x == 0 && r.y == 0 && r.w == doc->width && r.h == doc->height)
        return;

    cairo_surface_t *floater = doc->floating
        ? cairo_surface_reference(doc->floating) : NULL;
    double fx = doc->float_x, fy = doc->float_y;
    document_deselect(doc);

    /* Push after the stamp, so undo brings the pixels back with the size. */
    history_push_canvas(doc);

    int old_w = doc->width, old_h = doc->height;
    for (guint i = 0; i < doc->layers->len; i++) {
        Layer *l = g_ptr_array_index(doc->layers, i);
        cairo_surface_t *s =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
        cairo_t *cr = cairo_create(s);

        if (i == 0) {
            cairo_set_source_rgba(cr, red, green, blue, alpha);
            cairo_paint(cr);
        }
        cairo_rectangle(cr, -r.x, -r.y, old_w, old_h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, l->surface, -r.x, -r.y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);

        cairo_destroy(cr);
        cairo_surface_destroy(l->surface);
        l->surface = s;
    }
    doc->width  = r.w;
    doc->height = r.h;

    if (floater) {
        Layer *l = document_active_layer(doc);
        cairo_t *cr = cairo_create(l->surface);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, 0, 0, r.w, r.h);
        cairo_rectangle(cr, -r.x, -r.y, old_w, old_h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, floater, fx - r.x, fy - r.y);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(floater);
    }
    doc->modified = TRUE;
}

/* ---- selection ---------------------------------------------------------- */

void document_select_all(Document *doc)
{
    document_deselect(doc);              /* commit any floating move first */
    doc->selection = (Rect){ 0, 0, doc->width, doc->height };
    doc->has_selection = TRUE;
}

void document_select_rect(Document *doc, Rect r)
{
    int x2 = MIN(r.x + r.w, doc->width);
    int y2 = MIN(r.y + r.h, doc->height);
    r.x = MAX(0, r.x);
    r.y = MAX(0, r.y);
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    if (r.w <= 0 || r.h <= 0) {
        doc->has_selection = FALSE;
        return;
    }
    document_set_selection(doc, r);
}

void document_set_selection(Document *doc, Rect r)
{
    doc->selection = (Rect){ r.x, r.y, MAX(1, r.w), MAX(1, r.h) };
    doc->has_selection = TRUE;
}

/* The grip snapshot is scratch tied to whichever pixels are floating now. */
static void clear_resize_src(Document *doc)
{
    if (doc->resize_src)
        cairo_surface_destroy(doc->resize_src);
    doc->resize_src = NULL;
}

void document_create_floating(Document *doc, Rect region)
{
    document_drop_floating(doc);
    doc->floating = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, region.w, region.h);
    doc->float_x = region.x;
    doc->float_y = region.y;
}

void document_drop_floating(Document *doc)
{
    clear_resize_src(doc);           /* the snapshot dies with the pixels */
    if (doc->floating) {
        cairo_surface_destroy(doc->floating);
        doc->floating = NULL;
    }
}

void document_move_floating(Document *doc, double x, double y)
{
    if (!doc->floating)
        return;
    doc->float_x = round(x);
    doc->float_y = round(y);
    if (CLAMP_SELECTION_TO_CANVAS) {
        doc->float_x = CLAMP(doc->float_x, 0, doc->width  - doc->selection.w);
        doc->float_y = CLAMP(doc->float_y, 0, doc->height - doc->selection.h);
    }
    doc->selection.x = (int) doc->float_x;
    doc->selection.y = (int) doc->float_y;
}

void document_lift_selection(Document *doc)
{
    if (!doc->has_selection || doc->floating)
        return;

    /* One undo entry covers the whole lift-move-commit gesture: undoing
     * after a move restores the layer exactly as it was before the lift. */
    history_push(doc);

    document_create_floating(doc, doc->selection);
    
    Layer *l = document_active_layer(doc);
    cairo_t *cr = cairo_create(doc->floating);
    cairo_set_source_surface(cr, l->surface, -doc->selection.x, -doc->selection.y);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Punch a transparent hole where the pixels used to be. */
    cr = cairo_create(l->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_rectangle(cr, doc->selection.x, doc->selection.y, doc->selection.w, doc->selection.h);
    cairo_fill(cr);
    cairo_destroy(cr);
}

void document_commit_floating(Document *doc)
{
    if (!doc->floating)
        return;
    Layer *l = document_active_layer(doc);
    cairo_t *cr = cairo_create(l->surface);
    cairo_set_source_surface(cr, doc->floating, doc->float_x, doc->float_y);
    cairo_paint(cr);
    cairo_destroy(cr);

    doc->selection.x = (int) doc->float_x;
    doc->selection.y = (int) doc->float_y;
    document_drop_floating(doc);
    doc->modified = TRUE;
}

void document_deselect(Document *doc)
{
    document_commit_floating(doc);
    doc->has_selection = FALSE;
}

void document_delete_selection(Document *doc)
{
    if (!doc->has_selection)
        return;

    if (doc->floating) {
        /* The lift already pushed history and cleared the region, so simply
         * dropping the floating pixels deletes them. */
        document_drop_floating(doc);
    } else {
        history_push(doc);
        Layer *l = document_active_layer(doc);
        cairo_t *cr = cairo_create(l->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, doc->selection.x, doc->selection.y, doc->selection.w, doc->selection.h);
        cairo_fill(cr);
        cairo_destroy(cr);
    }
    /* Either way the region is gone, so the selection goes with it. */
    doc->has_selection = FALSE;
    doc->modified = TRUE;
}

cairo_surface_t *document_copy_selection(Document *doc)
{
    if (!doc->has_selection)
        return NULL;
    Rect r = document_selection_rect(doc);

    cairo_surface_t *out =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
    cairo_t *cr = cairo_create(out);
    /* SOURCE copies alpha verbatim; whatever hangs off the canvas or off the
     * floater stays transparent, exactly as it looks on screen. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    if (doc->floating)
        cairo_set_source_surface(cr, doc->floating, 0, 0);
    else
        cairo_set_source_surface(cr, document_active_layer(doc)->surface,
                                 -r.x, -r.y);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}

void document_paste(Document *doc, cairo_surface_t *src, int x, int y,
                    double red, double green, double blue, double alpha)
{
    Rect r = { x, y,
               cairo_image_surface_get_width (src),
               cairo_image_surface_get_height(src) };
    if (r.w < 1 || r.h < 1)
        return;

    Rect fit = { 0, 0, MAX(doc->width,  r.x + r.w),
                       MAX(doc->height, r.y + r.h) };
    if (fit.w > doc->width || fit.h > doc->height) {
        document_resize_canvas(doc, fit, red, green, blue, alpha);
    } else {
        /* Stamp first, snapshot second: one undo then removes only the paste. */
        document_deselect(doc);
        history_push(doc);
    }

    document_set_selection(doc, r);      /* verbatim - a paste may hang off */
    document_create_floating(doc, doc->selection);
    cairo_t *cr = cairo_create(doc->floating);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    doc->modified = TRUE;
}

/* ---- selection geometry -------------------------------------------------- */

Rect document_selection_rect(Document *doc)
{
    Rect r = doc->selection;
    if (doc->floating) {
        r.x = (int) doc->float_x;
        r.y = (int) doc->float_y;
    }
    return r;
}

gboolean document_point_in_selection(Document *doc, double x, double y)
{
    if (!doc->has_selection)
        return FALSE;
    Rect r = document_selection_rect(doc);
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void rect_handle_pos(Rect r, Handle h, double *x, double *y)
{
    double l = r.x, t = r.y, rt = r.x + r.w, b = r.y + r.h;
    double mx = r.x + r.w / 2.0, my = r.y + r.h / 2.0;
    /* clockwise from the top-left, matching the Handle enum */
    const double xs[HANDLE_COUNT] = { l, mx, rt, rt, rt, mx,  l,  l };
    const double ys[HANDLE_COUNT] = { t,  t,  t, my,  b,  b,  b, my };

    *x = xs[h];
    *y = ys[h];
}

void document_handle_pos(Document *doc, Handle h, double *x, double *y)
{
    rect_handle_pos(document_selection_rect(doc), h, x, y);
}

Handle document_hit_handle(Document *doc, double x, double y, double zoom)
{
    if (!doc->has_selection)
        return HANDLE_NONE;

    /* Grab area is generous but constant on screen; nearest grip wins so
     * corners still work when a tiny selection makes them all overlap. */
    double reach = (HANDLE_PX / 2 + 2) / MAX(zoom, 0.01);
    Handle best = HANDLE_NONE;
    double best_d2 = reach * reach;

    for (Handle h = HANDLE_NW; h < HANDLE_COUNT; h++) {
        double hx, hy;
        document_handle_pos(doc, h, &hx, &hy);
        double d2 = (x - hx) * (x - hx) + (y - hy) * (y - hy);
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = h;
        }
    }
    return best;
}

/* ---- resizing the selection ---------------------------------------------- */

void document_resize_begin(Document *doc, Handle h)
{
    if (!doc->has_selection || h == HANDLE_NONE)
        return;
    document_lift_selection(doc);        /* no-op when already floating */
    if (!doc->floating)
        return;
    doc->resize_handle = h;
    doc->resize_from   = document_selection_rect(doc);
    doc->resize_src    = cairo_surface_reference(doc->floating);
}

Rect rect_resize(Rect r, Handle h, double px, double py)
{
    int l = r.x, t = r.y, rt = r.x + r.w, b = r.y + r.h;
    int x = (int) round(px), y = (int) round(py);

    switch (h) {
    case HANDLE_NW: l  = MIN(x, rt - 1); t = MIN(y, b - 1);  break;
    case HANDLE_N:                       t = MIN(y, b - 1);  break;
    case HANDLE_NE: rt = MAX(x, l + 1);  t = MIN(y, b - 1);  break;
    case HANDLE_E:  rt = MAX(x, l + 1);                      break;
    case HANDLE_SE: rt = MAX(x, l + 1);  b = MAX(y, t + 1);  break;
    case HANDLE_S:                       b = MAX(y, t + 1);  break;
    case HANDLE_SW: l  = MIN(x, rt - 1); b = MAX(y, t + 1);  break;
    case HANDLE_W:  l  = MIN(x, rt - 1);                     break;
    default: break;
    }
    return (Rect){ l, t, rt - l, b - t };
}

Rect document_resize_rect(Document *doc, double px, double py)
{
    Rect r = rect_resize(doc->resize_from, doc->resize_handle, px, py);
    if (CLAMP_SELECTION_TO_CANVAS) {
        int l = CLAMP(r.x, 0, doc->width),  rt = CLAMP(r.x + r.w, 0, doc->width);
        int t = CLAMP(r.y, 0, doc->height), b  = CLAMP(r.y + r.h, 0, doc->height);
        r = (Rect){ l, t, rt - l, b - t };
    }
    return r;
}

void document_resize_to(Document *doc, Rect target)
{
    if (!doc->resize_src || target.w < 1 || target.h < 1)
        return;
    int sw = cairo_image_surface_get_width (doc->resize_src);
    int sh = cairo_image_surface_get_height(doc->resize_src);

    cairo_surface_t *dst =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target.w, target.h);
    cairo_t *cr = cairo_create(dst);
    cairo_scale(cr, (double) target.w / sw, (double) target.h / sh);
    cairo_set_source_surface(cr, doc->resize_src, 0, 0);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);

    if (doc->floating)
        cairo_surface_destroy(doc->floating);
    doc->floating  = dst;
    doc->float_x   = target.x;
    doc->float_y   = target.y;
    doc->selection = target;
    doc->modified  = TRUE;
}

void document_resize_end(Document *doc)
{
    clear_resize_src(doc);
    doc->resize_handle = HANDLE_NONE;
}
