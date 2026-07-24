#pragma once

#include <cairo.h>
#include <glib.h>

typedef struct Layer {
    cairo_surface_t *surface;  /* CAIRO_FORMAT_ARGB32, premultiplied alpha  */
    char             name[64];
    double           opacity;  /* 0.0 (invisible) … 1.0 (opaque)            */
    gboolean         visible;
} Layer;

Layer *layer_new  (int width, int height, const char *name);
Layer *layer_copy (const Layer *src);
void   layer_free (Layer *layer);

/* Flood the whole layer with one color (used for the white background). */
void   layer_fill (Layer *layer, double r, double g, double b, double a);

/* Utility: deep-copy any image surface (used by layers and undo history). */
cairo_surface_t *surface_copy(cairo_surface_t *src);
