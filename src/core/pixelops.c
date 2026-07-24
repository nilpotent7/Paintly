#include "pixelops.h"

guint32 pixel_from_rgba(double r, double g, double b, double a)
{
    guint32 A = (guint32) (a * 255.0 + 0.5);
    guint32 R = (guint32) (r * a * 255.0 + 0.5);   /* premultiplied */
    guint32 G = (guint32) (g * a * 255.0 + 0.5);
    guint32 B = (guint32) (b * a * 255.0 + 0.5);
    return (A << 24) | (MIN(R, 255) << 16) | (MIN(G, 255) << 8) | MIN(B, 255);
}

void flood_fill(cairo_surface_t *surface, int x, int y, guint32 color)
{
    int w = cairo_image_surface_get_width(surface);
    int h = cairo_image_surface_get_height(surface);
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;

    /* Sync any pending cairo drawing before touching raw memory. */
    cairo_surface_flush(surface);
    guint32 *px = (guint32 *) cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;

    guint32 target = px[y * stride + x];
    if (target == color)
        return;

    int cap = 1024, top = 0;
    int (*stack)[2] = g_malloc(cap * sizeof *stack);
    stack[top][0] = x;
    stack[top][1] = y;
    top = 1;

    while (top > 0) {
        top--;
        int cx = stack[top][0], cy = stack[top][1];
        guint32 *row = px + cy * stride;
        if (row[cx] != target)
            continue;

        /* Expand to the whole horizontal run containing (cx, cy). */
        int left = cx, right = cx;
        while (left > 0 && row[left - 1] == target)
            left--;
        while (right < w - 1 && row[right + 1] == target)
            right++;
        for (int i = left; i <= right; i++)
            row[i] = color;

        /* Seed the first pixel of every matching run directly above/below. */
        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = cy + dy;
            if (ny < 0 || ny >= h)
                continue;
            guint32 *nrow = px + ny * stride;
            for (int i = left; i <= right; i++) {
                if (nrow[i] == target && (i == left || nrow[i - 1] != target)) {
                    if (top == cap) {
                        cap *= 2;
                        stack = g_realloc(stack, cap * sizeof *stack);
                    }
                    stack[top][0] = i;
                    stack[top][1] = ny;
                    top++;
                }
            }
        }
    }

    g_free(stack);
    /* Tell cairo the surface changed behind its back. */
    cairo_surface_mark_dirty(surface);
}
