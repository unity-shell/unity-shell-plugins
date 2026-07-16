#include "coords.hpp"

#include <algorithm>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/workarea.hpp>

namespace spatial
{
/**
 * Collect current output geometry and pointer data used for spread layout.
 */
frame_ctx make_frame_ctx(wf::output_t *o)
{
    frame_ctx c;
    c.cur_ws   = o->wset()->get_current_workspace();
    c.grid     = o->wset()->get_workspace_grid_size();
    c.workarea = o->workarea->get_workarea();

    auto og = o->get_relative_geometry();
    c.output = {std::max(1, og.width), std::max(1, og.height)};

    auto gc = wf::get_core().get_cursor_position();
    auto lg = o->get_layout_geometry();
    c.cursor = {gc.x - lg.x, gc.y - lg.y};
    return c;
}
}
