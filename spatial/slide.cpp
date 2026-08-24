#include "slide.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>

#include <wayfire/workspace-set.hpp>

namespace spatial
{
wf::point_t slide_t::neighbor(wf::point_t from, double dx, double dy) const
{
    auto grid = output->wset()->get_workspace_grid_size();
    wf::point_t neighbour = from;
    if (std::abs(dx) > std::abs(dy))
    {
        if ((dx < 0) && (from.x < grid.width - 1)) { neighbour.x = from.x + 1; }
        else if ((dx > 0) && (from.x > 0)) { neighbour.x = from.x - 1; }
    } else
    {
        if ((dy < 0) && (from.y < grid.height - 1)) { neighbour.y = from.y + 1; }
        else if ((dy > 0) && (from.y > 0)) { neighbour.y = from.y - 1; }
    }

    return neighbour;
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

    const bool horizontal = std::abs(accum_x) > std::abs(accum_y);
    const double magnitude = horizontal ? std::abs(accum_x) : std::abs(accum_y);
    pan.hold((target == from) ? 0.0 : std::clamp(magnitude / SWIPE_DISTANCE, 0.0, 1.0));
}

void slide_t::start_to(wf::point_t neighbour)
{
    from = output->wset()->get_current_workspace();
    target = neighbour;
    accum_x = accum_y = 0;
    pan.animate_to(0.0, 1.0);
    active_ = true;
}

void slide_t::release()
{
    pan.animate_to(pan.value(), committing() ? 1.0 : 0.0);
}

std::optional<wf::point_t> slide_t::finish()
{
    const bool commit = committing();
    active_ = false;
    return commit ? std::optional<wf::point_t>(target) : std::nullopt;
}
}
