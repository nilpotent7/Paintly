#include "document.h"
#include "history.h"

Document *document_new(int width, int height)
{
    Document *d = g_new0(Document, 1);
    d->width  = width;
    d->height = height;
    d->layers = g_ptr_array_new_with_free_func((GDestroyNotify) layer_free);
    d->active = 0;
    d->layer_counter = 1;
    d->history = history_new();

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
    if (doc->floating)
        cairo_surface_destroy(doc->floating);
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

/* ---- selection ---------------------------------------------------------- */

void document_select_all(Document *doc)
{
    document_deselect(doc);              /* commit any floating move first */
    doc->selection = (Rect){ 0, 0, doc->width, doc->height };
    doc->has_selection = TRUE;
}

void document_select_rect(Document *doc, Rect r)
{
    /* Clamp to the canvas; an empty result clears the selection. */
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
    doc->selection = r;
    doc->has_selection = TRUE;
}

void document_create_floating(Document *doc, Rect region)
{
    doc->floating = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, region.w, region.h);
    doc->float_x = region.x;
    doc->float_y = region.y;
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
    cairo_surface_destroy(doc->floating);
    doc->floating = NULL;
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
        cairo_surface_destroy(doc->floating);
        doc->floating = NULL;
        doc->has_selection = FALSE;
    } else {
        history_push(doc);
        Layer *l = document_active_layer(doc);
        cairo_t *cr = cairo_create(l->surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, doc->selection.x, doc->selection.y, doc->selection.w, doc->selection.h);
        cairo_fill(cr);
        cairo_destroy(cr);
    }
    doc->modified = TRUE;
}
