#include "slide.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>

#include <wayfire/workspace-set.hpp>

namespace spatial
{
wf::point_t slide_t::neighbor(wf::point_t from, double dx, double dy) const
{
    auto dims = output->wset()->get_workspace_grid_size();
    wf::point_t nb = from;
    if (std::abs(dx) > std::abs(dy))
    {
        if ((dx < 0) && (from.x < dims.width - 1)) { nb.x = from.x + 1; }
        else if ((dx > 0) && (from.x > 0)) { nb.x = from.x - 1; }
    } else
    {
        if ((dy < 0) && (from.y < dims.height - 1)) { nb.y = from.y + 1; }
        else if ((dy > 0) && (from.y > 0)) { nb.y = from.y - 1; }
    }

    return nb;
}

void slide_t::begin()
{
    from = output->wset()->get_current_workspace();
    target = from;
    accum_x = accum_y = 0;
    pan.begin(0.0, 0.0, 1.0);
    active_ = true;
}

void slide_t::update(double dx, double dy)
{
    accum_x += dx;
    accum_y += dy;
    target = neighbor(from, accum_x, accum_y);

    const bool horiz = std::abs(accum_x) > std::abs(accum_y);
    const double mag  = horiz ? std::abs(accum_x) : std::abs(accum_y);
    pan.hold(same(target, from) ? 0.0 : std::clamp(mag / SWIPE_DISTANCE, 0.0, 1.0));
}

void slide_t::release()
{
    const bool commit = (pan.value() > 0.5) && !same(target, from);
    pan.animate_to(pan.value(), commit ? 1.0 : 0.0);
}

std::optional<wf::point_t> slide_t::finish()
{
    const bool commit = (pan.value() > 0.5) && !same(target, from);
    active_ = false;
    return commit ? std::optional<wf::point_t>(target) : std::nullopt;
}
}
