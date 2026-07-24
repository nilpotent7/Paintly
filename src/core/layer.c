#include "layer.h"

Layer *layer_new(int width, int height, const char *name)
{
    Layer *l = g_new0(Layer, 1);
    l->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    g_strlcpy(l->name, name, sizeof l->name);
    l->opacity = 1.0;
    l->visible = TRUE;
    return l;
}

cairo_surface_t *surface_copy(cairo_surface_t *src)
{
    cairo_surface_t *dst = cairo_image_surface_create(
        cairo_image_surface_get_format(src),
        cairo_image_surface_get_width(src),
        cairo_image_surface_get_height(src));
    cairo_t *cr = cairo_create(dst);
    cairo_set_source_surface(cr, src, 0, 0);
    /* SOURCE copies pixels verbatim, including their alpha values. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    return dst;
}

Layer *layer_copy(const Layer *src)
{
    Layer *l = g_new0(Layer, 1);
    *l = *src;
    l->surface = surface_copy(src->surface);
    return l;
}

void layer_free(Layer *layer)
{
    if (!layer)
        return;
    cairo_surface_destroy(layer->surface);
    g_free(layer);
}

void layer_fill(Layer *layer, double r, double g, double b, double a)
{
    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
}
