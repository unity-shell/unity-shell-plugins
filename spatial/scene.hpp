#pragma once

#include <vector>

#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>

namespace spatial
{
/* On-screen preview rects for hit-testing. Previews never overlap, so any rect
 * under the cursor is the pick. Cells are a grid (geom::cell_at), not here. */
struct pickable
{
    wf::geometry_t        rect;
    wayfire_toplevel_view view;
};

using frame = std::vector<pickable>;

inline const pickable* topmost(const frame& previews, wf::pointf_t cursor)
{
    for (auto it = previews.rbegin(); it != previews.rend(); ++it)
    {
        if (it->rect & cursor) { return &*it; }
    }
    return nullptr;
}
}
