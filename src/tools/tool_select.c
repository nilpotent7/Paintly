#include "tool.h"
#include <math.h>

typedef enum { SEL_IDLE, SEL_DRAG, SEL_MOVE, SEL_RESIZE } SelPhase;

static SelPhase phase = SEL_IDLE;
static double grab_dx, grab_dy;   /* pointer offset inside the floater */

static void select_begin(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    Handle h = document_hit_handle(d, c->x, c->y, c->zoom);

    if (h != HANDLE_NONE) {
        document_resize_begin(d, h);       /* lifts first if still on the layer */
        phase = SEL_RESIZE;
    } else if (document_point_in_selection(d, c->x, c->y)) {
        document_lift_selection(d);        /* no-op if already floating */
        phase = SEL_MOVE;
        grab_dx = c->x - d->float_x;
        grab_dy = c->y - d->float_y;
    } else {
        document_deselect(d);              /* commits any floating pixels */
        phase = SEL_DRAG;
    }
    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void select_motion(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    if (phase == SEL_DRAG) {
        Rect r = {
            (int) floor(MIN(c->start_x, c->x)),
            (int) floor(MIN(c->start_y, c->y)),
            (int) ceil(fabs(c->x - c->start_x)),
            (int) ceil(fabs(c->y - c->start_y)),
        };
        document_select_rect(d, r);
    } else if (phase == SEL_MOVE) {
        /* Snaps to whole pixels and stops at the canvas edge. */
        document_move_floating(d, c->x - grab_dx, c->y - grab_dy);
    } else if (phase == SEL_RESIZE) {
        document_resize_to(d, document_resize_rect(d, c->x, c->y));
    }
    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void select_end(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    /* A stray click (nothing dragged) clears the selection */
    if (phase == SEL_DRAG && d->has_selection && d->selection.w < 2 && d->selection.h < 2)
        d->has_selection = FALSE;
    else if (phase == SEL_RESIZE)
        document_resize_end(d);
    phase = SEL_IDLE;
    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void select_deactivate(Tool *t, App *a)
{
    document_resize_end(a->doc);
    document_deselect(a->doc);
    phase = SEL_IDLE;
    canvas_repaint(a);
    statusbar_update(a);
    layers_panel_queue_thumbs(a);   /* committing may have changed pixels */
}

Tool tool_select = {
    .id         = "select",
    .label      = "Select - drag a rectangle, then drag inside it to move "
                  "or grab a handle to resize",
    .icon       = "paintly-select",
    .begin      = select_begin,
    .motion     = select_motion,
    .end        = select_end,
    .deactivate = select_deactivate,
};
