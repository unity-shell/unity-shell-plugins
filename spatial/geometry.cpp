#include "geometry.hpp"

#include <algorithm>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>

namespace spatial
{
namespace
{
/* Overview-only inset reserved for shell surfaces (see set_inset). */
int g_left = 0, g_right = 0, g_top = 0, g_bottom = 0;
}

void set_inset(int left, int right, int top, int bottom)
{
    g_left   = std::max(0, left);
    g_right  = std::max(0, right);
    g_top    = std::max(0, top);
    g_bottom = std::max(0, bottom);
}

world make_world(wf::output_t *o)
{
    world c;
    c.cur_ws   = o->wset()->get_current_workspace();
    c.grid     = o->wset()->get_workspace_grid_size();
    c.workarea = o->workarea->get_workarea();

    /* Reserve the shell inset within the spread's layout rect only. This never
     * touches the compositor work area, so maximized windows keep their size. */
    const int w = c.workarea.width  - g_left - g_right;
    const int h = c.workarea.height - g_top  - g_bottom;
    c.workarea.x     += g_left;
    c.workarea.y     += g_top;
    c.workarea.width  = w > 1 ? w : 1;
    c.workarea.height = h > 1 ? h : 1;

    c.output = wf::dimensions(o->get_relative_geometry());

    auto gc = wf::get_core().get_cursor_position();
    auto lg = o->get_layout_geometry();
    c.cursor = {gc.x - lg.x, gc.y - lg.y};
    return c;
}
}
