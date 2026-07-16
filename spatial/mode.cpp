#include "mode.hpp"
#include "controller.hpp"
#include "coords.hpp"
#include "config.hpp"

#include <wayfire/workspace-set.hpp>

namespace spatial
{
/* Desktop -> apps-spread threshold classifier. */
mode_id desktop_mode::classify(double g) const
{
    return (g > 0.0) ? mode_id::apps_spread : mode_id::desktop;
}

void desktop_mode::slide_settle(controller& c)
{
    c.end_to_desktop();
}

mode_id apps_spread_mode::classify(double g) const
{
    if (g <= 0.0) { return mode_id::desktop; }
    return (g >= 1.0 + STAGE_HYSTERESIS) ? mode_id::workspaces_spread : mode_id::apps_spread;
}

void apps_spread_mode::on_button(controller& c, const wlr_pointer_button_event& ev)
{
    if (ev.state != WL_POINTER_BUTTON_STATE_PRESSED) { return; }

    auto ctx = c.frame();
    if (auto v = c.renderer().view_at(ctx.cursor))
    {
        c.activate_window(v, ctx.cur_ws);
    }
}

void apps_spread_mode::slide_settle(controller& c)
{
    c.recenter_apps_spread();
}

mode_id workspaces_spread_mode::classify(double g) const
{
    if (g <= 0.0) { return mode_id::desktop; }
    return (g <= 1.0) ? mode_id::apps_spread : mode_id::workspaces_spread;
}

void workspaces_spread_mode::on_button(controller& c, const wlr_pointer_button_event& ev)
{
    if (ev.state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        c.dragger().press();
        return;
    }

    if (c.dragger().release() == window_drag_t::result::empty_click)
    {
        auto ctx = c.frame();
        c.out()->wset()->set_workspace(coords::cell_at(ctx, ctx.cursor, WALL_GAP));
        c.settle_to(0.0);
    }
}

void workspaces_spread_mode::on_motion(controller& c)
{
    c.dragger().motion();
    auto ctx = c.frame();
    c.rstate().hover = coords::cell_at(ctx, ctx.cursor, WALL_GAP);
}
}
