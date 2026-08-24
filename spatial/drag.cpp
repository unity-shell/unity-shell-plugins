#include "drag.hpp"
#include "present.hpp"
#include "geometry.hpp"
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
/* Drag engine that ties spread thumbnails to the move-drag core API. */
struct window_drag_t::impl
{
    wf::output_t *output;
    present_t *present;
    std::function<void (wayfire_toplevel_view, wf::point_t)> on_click;
    std::function<void ()> on_moved;
    bool pressed = false;
    wf::shared_data::ref_ptr_t<wf::move_drag::core_drag_t> core;

    impl(wf::output_t *output, present_t *present,
        std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
        std::function<void ()> on_moved) :
        output(output), present(present),
        on_click(std::move(on_click)), on_moved(std::move(on_moved))
    {}

    wayfire_toplevel_view pick(wf::pointf_t at) { return present->view_at(at); }

    void drop(wayfire_toplevel_view view)
    {
        auto ctx    = make_world(output);
        auto target = geom::cell_at(ctx, ctx.cursor);
        auto source = output->wset()->get_view_main_workspace(view);
        if ((target.x != source.x) || (target.y != source.y))
        {
            auto view_geo = view->get_geometry();
            view->move(view_geo.x + (target.x - source.x) * ctx.output.width,
                view_geo.y + (target.y - source.y) * ctx.output.height);
        }

        on_moved();
    }

    void start(wayfire_toplevel_view view, wf::pointf_t at)
    {
        auto thumb = present->thumb_of(view);
        if ((thumb.width <= 0) || (thumb.height <= 0)) { return; }

        wf::pointf_t grab_offset = {(at.x - thumb.x) / thumb.width,
            (at.y - thumb.y) / thumb.height};

        present->release_for_drag(view);

        auto bbox = wf::view_bounding_box_up_to(view, "wobbly");
        wf::move_drag::drag_options_t opts;
        opts.initial_scale = bbox.width / thumb.width;
        core->start_drag(view, grab_offset, opts);
    }

    void press()
    {
        if (core->view) { core->handle_input_released(); }
        pressed = true;
        core->set_pending_drag(wf::get_core().get_cursor_position());
    }

    void motion()
    {
        if (!pressed) { return; }

        auto cursor = wf::get_core().get_cursor_position();
        if (core->view) { core->handle_motion(cursor); return; }
        if (!core->should_start_pending_drag(cursor)) { return; }

        auto layout_geo = output->get_layout_geometry();
        wf::pointf_t local_point{cursor.x - layout_geo.x, cursor.y - layout_geo.y};
        if (auto view = pick(local_point)) { start(view, local_point); core->handle_motion(cursor); }
    }

    bool release()
    {
        pressed = false;

        if (core->view)
        {
            auto view = core->view;
            core->handle_input_released();
            drop(view);
            return false;
        }

        auto ctx = make_world(output);
        if (auto view = pick(ctx.cursor))
        {
            on_click(view, geom::cell_at(ctx, ctx.cursor));
            return false;
        }

        return true;   /* empty click */
    }

    void cancel()
    {
        pressed = false;
        if (core->view) { core->handle_input_released(); }
    }

    void forget(wayfire_toplevel_view view)
    {
        if (core->view == view) { cancel(); }
    }

    bool active() { return core->view != nullptr; }
};

window_drag_t::window_drag_t(wf::output_t *output, present_t *present,
    std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
    std::function<void ()> on_moved) :
    priv(std::make_unique<impl>(output, present, std::move(on_click), std::move(on_moved)))
{}

window_drag_t::~window_drag_t() = default;

void window_drag_t::press() { priv->press(); }
void window_drag_t::motion() { priv->motion(); }
bool window_drag_t::release() { return priv->release(); }
void window_drag_t::cancel() { priv->cancel(); }
void window_drag_t::forget(wayfire_toplevel_view view) { priv->forget(view); }
bool window_drag_t::active() const { return priv->active(); }
}
