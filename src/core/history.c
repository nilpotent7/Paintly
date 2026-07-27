#include "history.h"

typedef struct {
    int              layer_index;
    cairo_surface_t *pixels;     /* the layer's surface as it was */
    GPtrArray       *layers;     /* every layer, deep-copied, or NULL */
    int              width, height;
} HistEntry;

struct History {
    GPtrArray *undo;   /* HistEntry*, last element = most recent */
    GPtrArray *redo;
};

static void entry_free(gpointer p)
{
    HistEntry *e = p;
    if (e->pixels)
        cairo_surface_destroy(e->pixels);
    if (e->layers)
        g_ptr_array_unref(e->layers);
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

/* Reserve the next undo slot; the caller fills in what it snapshots. */
static HistEntry *push_entry(Document *doc)
{
    History *h = doc->history;

    /* A new action invalidates everything that was undone. */
    g_ptr_array_set_size(h->redo, 0);

    if (h->undo->len >= HISTORY_MAX)
        g_ptr_array_remove_index(h->undo, 0);

    HistEntry *e = g_new0(HistEntry, 1);
    g_ptr_array_add(h->undo, e);

    doc->modified = TRUE;
    return e;
}

void history_push(Document *doc)
{
    HistEntry *e = push_entry(doc);
    e->layer_index = doc->active;
    e->pixels = surface_copy(document_active_layer(doc)->surface);
}

void history_push_canvas(Document *doc)
{
    HistEntry *e = push_entry(doc);
    e->layer_index = -1;
    e->layers = g_ptr_array_new_with_free_func((GDestroyNotify) layer_free);
    for (guint i = 0; i < doc->layers->len; i++)
        g_ptr_array_add(e->layers, layer_copy(g_ptr_array_index(doc->layers, i)));
    e->width  = doc->width;
    e->height = doc->height;
}

static gboolean restore(Document *doc, GPtrArray *from, GPtrArray *to)
{
    if (from->len == 0)
        return FALSE;
    HistEntry *e = g_ptr_array_steal_index(from, from->len - 1);

    document_drop_floating(doc);
    doc->has_selection = FALSE;

    gboolean structural = e->layers != NULL;
    if (structural) {
        GPtrArray *live = doc->layers;
        int w = doc->width, h = doc->height;
        doc->layers = e->layers;  doc->width = e->width;  doc->height = e->height;
        e->layers   = live;       e->width   = w;         e->height   = h;
        doc->active = CLAMP(doc->active, 0, (int) doc->layers->len - 1);
    } else {
        /* If layers were removed since the snapshot the index may be stale
         * clamp so we never read out of bounds (see history.h limitations). */
        int idx = MIN(e->layer_index, (int) doc->layers->len - 1);
        Layer *l = g_ptr_array_index(doc->layers, idx);

        cairo_surface_t *now = l->surface;
        l->surface = e->pixels;
        e->pixels  = now;
    }
    g_ptr_array_add(to, e);

    doc->modified = TRUE;
    return structural;
}

gboolean history_undo(Document *doc)
{
    return restore(doc, doc->history->undo, doc->history->redo);
}

gboolean history_redo(Document *doc)
{
    return restore(doc, doc->history->redo, doc->history->undo);
}
