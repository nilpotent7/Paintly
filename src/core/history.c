#include "history.h"

typedef struct {
    int              layer_index;
    cairo_surface_t *pixels;     /* the layer's surface as it was */
} HistEntry;

struct History {
    GPtrArray *undo;   /* HistEntry*, last element = most recent */
    GPtrArray *redo;
};

static void entry_free(gpointer p)
{
    HistEntry *e = p;
    cairo_surface_destroy(e->pixels);
    g_free(e);
}

History *history_new(void)
{
    History *h = g_new0(History, 1);
    h->undo = g_ptr_array_new_with_free_func(entry_free);
    h->redo = g_ptr_array_new_with_free_func(entry_free);
    return h;
}

void history_free(History *h)
{
    if (!h)
        return;
    g_ptr_array_unref(h->undo);
    g_ptr_array_unref(h->redo);
    g_free(h);
}

void history_push(Document *doc)
{
    History *h = doc->history;

    /* A new action invalidates everything that was undone. */
    g_ptr_array_set_size(h->redo, 0);

    if (h->undo->len >= HISTORY_MAX)
        g_ptr_array_remove_index(h->undo, 0);

    HistEntry *e = g_new0(HistEntry, 1);
    e->layer_index = doc->active;
    e->pixels = surface_copy(document_active_layer(doc)->surface);
    g_ptr_array_add(h->undo, e);

    doc->modified = TRUE;
}

/* Undo and redo are symmetrical: pop an entry from one stack, swap its
 * surface with the layer's live surface, push it onto the other stack. */
static void restore(Document *doc, GPtrArray *from, GPtrArray *to)
{
    if (from->len == 0)
        return;
    HistEntry *e = g_ptr_array_steal_index(from, from->len - 1);

    /* If layers were removed since the snapshot the index may be stale
     * clamp so we never read out of bounds (see history.h limitations). */
    int idx = MIN(e->layer_index, (int) doc->layers->len - 1);
    Layer *l = g_ptr_array_index(doc->layers, idx);

    cairo_surface_t *now = l->surface;
    l->surface = e->pixels;
    e->pixels  = now;
    g_ptr_array_add(to, e);

    doc->modified = TRUE;
}

void history_undo(Document *doc)
{
    restore(doc, doc->history->undo, doc->history->redo);
}

void history_redo(Document *doc)
{
    restore(doc, doc->history->redo, doc->history->undo);
}
