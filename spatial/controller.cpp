#include "controller.hpp"

#include <utility>

#include <linux/input-event-codes.h>

#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/plugins/ipc/ipc-rules-common.hpp>

namespace spatial
{
namespace
{
/* Scope guard for self_activating: blocks the re-entrant focus_request dismiss. */
struct scoped_flag
{
    bool& flag;
    explicit scoped_flag(bool& f) : flag(f) { flag = true; }
    ~scoped_flag() { flag = false; }
};
}

/* Cross-output inhibit reference count used by IPC hooks. */
static int s_inhibit = 0;
bool controller::inhibited() { return s_inhibit > 0; }
void controller::inhibit() { s_inhibit++; }
void controller::uninhibit() { if (s_inhibit > 0) { s_inhibit--; } }

void controller::reflow_for_inset() { relayout_if_idle(); }

void controller::init()
{
    present = std::make_unique<present_t>(output);
    drag = std::make_unique<window_drag_t>(output, present.get(),
        [this] (wayfire_toplevel_view view, wf::point_t ws) { activate_window(view, ws); },
        /* Snap after a drop: the thumbnail is already at the drop point, so
         * animating its slot would fling it back from the source cell. */
        [this] { relayout(false); });
    grab = std::make_unique<wf::input_grab_t>(PLUGIN_NAME, output, this, this, nullptr);
    slide = std::make_unique<slide_t>(output);

    input = std::make_unique<interaction>(this, present.get(), drag.get());

    t_activate.emplace(
        [this] { output->activate_plugin(&grab_interface); },
        [this] { output->deactivate_plugin(&grab_interface); });
    t_top.emplace(
        [this] { wf::scene::set_node_enabled(
            output->node_for_layer(wf::scene::layer::TOP), true); },
        [this] { wf::scene::set_node_enabled(
            output->node_for_layer(wf::scene::layer::TOP), false); });
    t_grab.emplace(
        [this] { grab->set_wants_raw_input(false); grab->grab_input(wf::scene::layer::WORKSPACE); },
        [this] { grab->ungrab_input(); });
    t_hooks.emplace(
        [this]
        {
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
            output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
            output->render->schedule_redraw();
        },
        [this]
        {
            output->render->rem_effect(&pre_hook);
            output->render->rem_effect(&post_hook);
        });

    wf::get_core().connect(&on_view_unmapped);
    wf::get_core().connect(&on_focus_request);
    output->connect(&on_view_mapped);
    output->connect(&on_workarea_changed);

    swipe.on_begin  = [this] (int fingers) { gesture_begin(fingers); };
    swipe.on_update = [this] (double dx, double dy) { gesture_update(dx, dy); };
    swipe.on_end    = [this] (double vx, double vy) { gesture_end(vx, vy); };
    swipe.on_pinch  = [this] (int fingers, double scale) { gesture_pinch(fingers, scale); };
}

void controller::fini()
{
    end_to_desktop();
    on_view_unmapped.disconnect();
    on_view_mapped.disconnect();
    on_focus_request.disconnect();
    on_workarea_changed.disconnect();
}

void controller::apply_resources()
{
    /* Held whenever visible: any non-desktop phase, or an active slide. */
    const bool overview = (current != phase::desktop) || (slide && slide->active());
    t_activate->ensure(overview);
    t_top->ensure(overview);
    t_grab->ensure(overview);
    if (overview) { present->ensure_layout(make_world(output), filter); }
    else { present->clear(); }
}

void controller::publish_phase(phase stage)
{
    /* Driven by the visible g, so the panel tracks the animation, not the
     * latched phase. */
    if (stage == published) { return; }
    published = stage;

    wf::json_t data;
    data["stage"] = phase_name(stage);
    wf::ipc_rules::send_event_to_subscribes(data, "spatial/stage#");
}

void controller::set_phase(phase stage)
{
    if (stage == current) { return; }
    /* Each spread opens unfiltered. Only spread_app sets the filter. */
    if (stage == phase::desktop) { filter.clear(); }
    if (stage == phase::wall) { input->seed(output->wset()->get_current_workspace()); }
    current = stage;
}

void controller::reconcile()
{
    set_phase(phase_of(g_axis.value(), current));
    apply_resources();
}

void controller::render_frame()
{
    auto ctx = make_world(output);
    const double g = g_axis.value();
    publish_phase(phase_of(g, published));

    wf::point_t pan_dir{0, 0};
    double pan_amount = 0.0;
    if (slide->active())
    {
        pan_dir    = slide->pan_dir();
        pan_amount = slide->pan_amount();
    }

    present->render(ctx, g, pan_dir, pan_amount, input->selection());
}

void controller::repaint()
{
    render_frame();
    output->render->schedule_redraw();
}

void controller::advance()
{
    if (slide->active())
    {
        if (slide->advancing()) { output->render->schedule_redraw(); return; }
        finish_slide();
        return;
    }

    if (g_axis.interacting_now()) { output->render->schedule_redraw(); return; }
    if (g_axis.animating())
    {
        /* A settle holds its destination. Re-deriving from the in-flight g would
         * bounce an opening spread back to desktop near g == 0. */
        apply_resources();
        output->render->schedule_redraw();
        return;
    }

    /* Settle done: latch the phase from where g landed. */
    reconcile();
    if (present->animating()) { output->render->schedule_redraw(); return; }
    unhook();
}

void controller::set_hook() { t_hooks->ensure(true); }
void controller::unhook()   { t_hooks->ensure(false); }

void controller::settle_to(double target)
{
    /* Opening: enter the destination phase now so there's a spread to animate
     * into. Closing keeps the phase until g reaches desktop, so it stays visible. */
    if (target > 0.0)
    {
        set_phase(target >= 2.0 ? phase::wall : phase::apps);
        apply_resources();
    }

    g_axis.animate_to(g_axis.value(), target);
    set_hook();
}

bool controller::can_relayout()
{
    /* Settled and idle. Reflowing mid-motion would fight it. */
    return (current != phase::desktop) && !g_axis.active() && !slide->active() &&
        !self_activating && !(drag && drag->active());
}

void controller::relayout_if_idle()
{
    if (can_relayout()) { relayout(); }
}

void controller::relayout(bool animate)
{
    present->layout(make_world(output), filter, animate);

    /* Keep the loop running so the slots' easing is drawn when otherwise settled. */
    if (!g_axis.active() && !slide->active() && present->animating()) { set_hook(); }

    repaint();
}

void controller::activate_window(wayfire_toplevel_view view, wf::point_t ws)
{
    scoped_flag guard{self_activating};   /* our own focus_request must not dismiss us */
    if (view->minimized) { wf::get_core().default_wm->minimize_request(view, false); }
    wf::get_core().default_wm->focus_request(view);
    enter_workspace(ws);
}

void controller::enter_workspace(wf::point_t ws)
{
    output->wset()->set_workspace(ws);
    settle_to(0.0);
}

void controller::switch_workspace(wf::point_t ws)
{
    /* Slide to the neighbour like a 4-finger slide; finish_slide commits it. */
    slide->start_to(ws);
    apply_resources();
    set_hook();
}

void controller::end_to_desktop()
{
    if (slide) { slide->cancel(); }
    if (drag) { drag->cancel(); }
    g_axis.pin(0.0);
    set_phase(phase::desktop);
    publish_phase(phase::desktop);  /* this path unhooks, so no frame will publish it */
    apply_resources();
    unhook();
}

void controller::recenter_apps_spread()
{
    slide->cancel();
    /* Cell-local slots don't move on the switch, so the re-centre eases, no snap. */
    present->layout(make_world(output), filter, /*animate=*/true);
    repaint();
    g_axis.pin(1.0);
    set_phase(phase::apps);
    set_hook();
}

void controller::close_spread()
{
    if (current != phase::desktop) { settle_to(0.0); }
}

void controller::toggle_apps_spread()
{
    if (inhibited()) { return; }
    if (current == phase::apps) { settle_to(0.0); return; }
    filter.clear();
    settle_to(1.0);
}

void controller::toggle_workspaces_spread()
{
    if (inhibited()) { return; }
    if (current == phase::wall) { settle_to(0.0); return; }
    settle_to(2.0);
}

void controller::spread_app(const std::vector<std::string>& ids)
{
    if (inhibited() || ids.empty() || (current == phase::wall)) { return; }
    if ((current == phase::apps) && (filter == ids)) { settle_to(0.0); return; }

    filter = ids;
    if (current == phase::desktop) { settle_to(1.0); }
    else { relayout(); }
}

bool controller::cursor_here() const
{
    return output->get_relative_geometry() & output->get_cursor_position();
}

void controller::begin_slide()
{
    /* Mark active first. apply_resources then grants the overview set even from
     * the desktop. */
    slide->begin();
    apply_resources();
    set_hook();
}

void controller::slide_update(double dx, double dy)
{
    slide->update(dx, dy);
    output->render->schedule_redraw();
}

void controller::slide_end()
{
    slide->release();
    set_hook();
}

void controller::finish_slide()
{
    if (auto ws = slide->finish()) { output->wset()->set_workspace(*ws); }
    /* current is unchanged across a slide. Each phase cleans up its own way. */
    if (current == phase::desktop) { end_to_desktop(); }
    else if (current == phase::apps) { recenter_apps_spread(); }
    /* the wall does not re-centre */
}

void controller::gesture_begin(int fingers)
{
    if (inhibited() || !cursor_here()) { return; }

    if (fingers == 4)
    {
        if (slides(current)) { begin_slide(); }
        return;
    }

    if (fingers != 3) { return; }

    /* Bound to one phase either side, so a swipe can't skip desktop to wall. */
    const double anchor = (current == phase::wall) ? 2.0
        : (current == phase::desktop) ? 0.0 : 1.0;
    const double from = g_axis.value();

    /* Lay out up front so the first motion frame does not hitch. */
    if (current == phase::desktop)
    {
        set_phase(phase::apps);
        apply_resources();
    }

    const double lo = std::max(0.0, anchor - 1.0);
    const double hi = std::min(2.0, anchor + 1.0);
    g_axis.begin(from, lo, hi);
    set_hook();
}

void controller::gesture_update(double dx, double dy)
{
    if (slide->active()) { slide_update(dx, dy); return; }
    if (!g_axis.interacting_now()) { return; }

    g_axis.drive(-dy / SWIPE_DISTANCE);

    /* Follow the phase while dragging, but never to desktop (would drop the grab). */
    phase want = phase_of(g_axis.value(), current);
    if (want != phase::desktop) { set_phase(want); }

    output->render->schedule_redraw();
}

void controller::gesture_end(double, double vy)
{
    if (slide->active()) { slide_end(); return; }
    if (!g_axis.interacting_now()) { return; }

    g_axis.settle(vy);
    set_hook();
}

void controller::gesture_pinch(int fingers, double scale)
{
    if (inhibited() || (fingers < 3) || !cursor_here()) { return; }
    if (std::abs(scale - 1.0) >= PINCH_THRESHOLD) { toggle_workspaces_spread(); }
}

void controller::handle_pointer_button(const wlr_pointer_button_event& ev)
{
    if (ev.button != BTN_LEFT) { return; }
    input->on_button(ev, make_world(output), current);
    update_cursor();
}

void controller::handle_pointer_motion(wf::pointf_t, uint32_t)
{
    input->on_motion(current);
    update_cursor();
    output->render->schedule_redraw();
}

void controller::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev)
{
    input->on_key(ev, make_world(output), current);
}

void controller::update_cursor()
{
    wf::get_core().set_cursor(drag->active() ? "grabbing" : "default");
}

void controller::handle_mapped(wf::view_mapped_signal *ev)
{
    if ((current == phase::desktop) && !slide->active()) { return; }
    if (!wf::toplevel_cast(ev->view)) { return; }
    relayout_if_idle();
}

void controller::handle_unmapped(wf::view_unmapped_signal *ev)
{
    if ((current == phase::desktop) && !slide->active()) { return; }
    auto view = wf::toplevel_cast(ev->view);
    if (!view) { return; }

    drag->forget(view);
    present->forget(view);
    relayout_if_idle();
}

void controller::handle_focus_request(wf::view_focus_request_signal *ev)
{
    /* External activation (e.g. the launcher): close so it is revealed. Ours is
     * guarded by self_activating. */
    if (self_activating || (current == phase::desktop)) { return; }

    auto view = wf::toplevel_cast(ev->view);
    if (!view || (view->get_output() != output)) { return; }

    close_spread();
}
}
