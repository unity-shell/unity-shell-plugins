#include "interaction.hpp"
#include "controller.hpp"
#include "present.hpp"
#include "drag.hpp"

#include <algorithm>

#include <linux/input-event-codes.h>

namespace spatial
{
namespace
{
/* One arrow step from @from, clamped to the grid. */
wf::point_t arrow_neighbor(wf::point_t from, uint32_t keycode, wf::dimensions_t grid)
{
    wf::point_t to = from;
    switch (keycode)
    {
      case KEY_LEFT:  to.x -= 1; break;
      case KEY_RIGHT: to.x += 1; break;
      case KEY_UP:    to.y -= 1; break;
      case KEY_DOWN:  to.y += 1; break;
      default: return from;
    }
    to.x = std::clamp(to.x, 0, grid.width - 1);
    to.y = std::clamp(to.y, 0, grid.height - 1);
    return to;
}
}

void interaction::on_button(const wlr_pointer_button_event& ev, const world& ctx, phase stage)
{
    if (ev.state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        /* Only the wall drags, so it waits for release. The app spread acts now. */
        if (stage == phase::wall) { drag->press(); return; }

        if (auto view = present->view_at(ctx.cursor)) { owner->activate_window(view, ctx.cur_ws); }
        else { owner->close_spread(); }
        return;
    }

    /* drag->release activates a clicked window or drops a dragged one. It returns
     * true only for an empty click, which picks the nearest workspace. */
    if (stage == phase::wall)
    {
        if (drag->release()) { owner->enter_workspace(geom::cell_at(ctx, ctx.cursor)); }
    }
}

void interaction::on_motion(phase stage)
{
    if (stage == phase::wall) { drag->motion(); }
}

void interaction::on_key(const wlr_keyboard_key_event& ev, const world& ctx, phase stage)
{
    if ((ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) || (stage == phase::desktop)) { return; }

    /* Wall arrows move the selection, never the workspace, so Esc lands back
     * where the spread opened. */
    if (ev.keycode == KEY_ESC) { owner->close_spread(); return; }

    if (stage == phase::wall)
    {
        if ((ev.keycode == KEY_ENTER) || (ev.keycode == KEY_KPENTER) || (ev.keycode == KEY_SPACE))
        {
            owner->enter_workspace(selected);
            return;
        }

        auto to = arrow_neighbor(selected, ev.keycode, ctx.grid);
        if (to == selected) { return; }
        selected = to;
        owner->repaint();
        return;
    }

    /* App spread shows one workspace, so arrows switch it live. */
    auto to = arrow_neighbor(ctx.cur_ws, ev.keycode, ctx.grid);
    if (to == ctx.cur_ws) { return; }
    owner->switch_workspace(to);
}
}
