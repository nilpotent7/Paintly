#include "tool.h"
#include <math.h>

typedef enum { SEL_IDLE, SEL_DRAG, SEL_MOVE } SelPhase;

static SelPhase phase = SEL_IDLE;
static double grab_dx, grab_dy;   /* pointer offset inside the floater */

static gboolean point_in_selection(Document *d, double x, double y)
{
    if (!d->has_selection)
        return FALSE;
    double sx = d->floating ? d->float_x : d->selection.x;
    double sy = d->floating ? d->float_y : d->selection.y;
    return x >= sx && y >= sy && x < sx + d->selection.w && y < sy + d->selection.h;
}

static void select_begin(Tool *t, ToolContext *c)
{
    Document *d = c->doc;
    if (point_in_selection(d, c->x, c->y)) {
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
        /* Snap to whole pixels so the drop is always crisp. */
        d->float_x = round(c->x - grab_dx);
        d->float_y = round(c->y - grab_dy);
        d->selection.x = (int) d->float_x;
        d->selection.y = (int) d->float_y;
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
    phase = SEL_IDLE;
    canvas_repaint(c->app);
    statusbar_update(c->app);
}

static void select_deactivate(Tool *t, App *a)
{
    document_deselect(a->doc);
    phase = SEL_IDLE;
    canvas_repaint(a);
    statusbar_update(a);
    layers_panel_queue_thumbs(a);   /* committing may have changed pixels */
}

Tool tool_select = {
    .id         = "select",
    .label      = "Select - drag a rectangle, then drag inside it to move",
    .icon       = "paintly-select",
    .begin      = select_begin,
    .motion     = select_motion,
    .end        = select_end,
    .deactivate = select_deactivate,
};
