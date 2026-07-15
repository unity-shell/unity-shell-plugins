#include "drag.hpp"
#include "renderer.hpp"
#include "coords.hpp"
#include "config.hpp"

#include <algorithm>
#include <utility>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/util.hpp>

namespace spatial
{
struct window_drag_t::impl
{
    wf::output_t *output;
    spread_t *spread;
    std::function<void (wayfire_toplevel_view, wf::point_t)> on_click;
    std::function<void ()> on_moved;
    bool pressed = false;
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> drag;

    impl(wf::output_t *o, spread_t *s,
        std::function<void (wayfire_toplevel_view, wf::point_t)> click,
        std::function<void ()> moved) :
        output(o), spread(s), on_click(std::move(click)), on_moved(std::move(moved))
    {}

    wayfire_toplevel_view pick(wf::pointf_t local) { return spread->view_at(local); }

    void drop(wayfire_toplevel_view v)
    {
        auto ctx    = make_frame_ctx(output);
        auto target = coords::cell_at(ctx, ctx.cursor, WALL_GAP);
        auto src    = output->wset()->get_view_main_workspace(v);
        if ((target.x != src.x) || (target.y != src.y))
        {
            auto g = v->get_geometry();
            v->move(g.x + (target.x - src.x) * ctx.output.width,
                g.y + (target.y - src.y) * ctx.output.height);
        }

        on_moved();
    }

    void start(wayfire_toplevel_view view, wf::pointf_t local)
    {
        auto thumb = spread->thumb_of(view);
        if ((thumb.width <= 0) || (thumb.height <= 0)) { return; }

        wf::pointf_t rel = {(local.x - thumb.x) / (double) thumb.width,
            (local.y - thumb.y) / (double) thumb.height};

        spread->release_for_drag(view);

        auto bbox = wf::view_bounding_box_up_to(view, "wobbly");
        wf::move_drag::drag_options_t opts;
        opts.initial_scale = (double) bbox.width / std::max(1, thumb.width);
        drag->start_drag(view, rel, opts);
    }

    void press()
    {
        if (drag->view) { drag->handle_input_released(); }
        pressed = true;
        drag->set_pending_drag(wf::get_core().get_cursor_position());
    }

    void motion()
    {
        if (!pressed) { return; }

        auto cp = wf::get_core().get_cursor_position();
        wf::point_t to{(int) cp.x, (int) cp.y};

        if (drag->view) { drag->handle_motion(to); return; }
        if (!drag->should_start_pending_drag(to)) { return; }

        auto lg = output->get_layout_geometry();
        wf::pointf_t local{(double) (to.x - lg.x), (double) (to.y - lg.y)};
        if (auto v = pick(local)) { start(v, local); drag->handle_motion(to); }
    }

    window_drag_t::result release()
    {
        pressed = false;

        if (drag->view)
        {
            auto v = drag->view;
            drag->handle_input_released();
            drop(v);
            return window_drag_t::result::dragged;
        }

        auto ctx = make_frame_ctx(output);
        if (auto v = pick(ctx.cursor))
        {
            on_click(v, coords::cell_at(ctx, ctx.cursor, WALL_GAP));
            return window_drag_t::result::window_click;
        }

        return window_drag_t::result::empty_click;
    }

    void cancel()
    {
        pressed = false;
        if (drag->view) { drag->handle_input_released(); }
    }

    void forget(wayfire_toplevel_view view)
    {
        if (drag->view == view) { cancel(); }
    }

    bool active() { return drag->view != nullptr; }
};

window_drag_t::window_drag_t(wf::output_t *output, spread_t *spread,
    std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
    std::function<void ()> on_moved) :
    priv(std::make_unique<impl>(output, spread, std::move(on_click), std::move(on_moved)))
{}

window_drag_t::~window_drag_t() = default;

void window_drag_t::press() { priv->press(); }
void window_drag_t::motion() { priv->motion(); }
window_drag_t::result window_drag_t::release() { return priv->release(); }
void window_drag_t::cancel() { priv->cancel(); }
void window_drag_t::forget(wayfire_toplevel_view view) { priv->forget(view); }
bool window_drag_t::active() const { return priv->active(); }
}
