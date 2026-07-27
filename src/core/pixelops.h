#pragma once

#include <cairo.h>
#include <glib.h>

/* Convert non-premultiplied 0..1 RGBA into a premultiplied ARGB32 pixel. */
guint32 pixel_from_rgba(double r, double g, double b, double a);

void flood_fill(cairo_surface_t *surface, int x, int y, guint32 color);

