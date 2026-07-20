#include "controller.hpp"

#include <algorithm>
#include <cmath>

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

namespace spatial
{
/* Cross-output inhibit reference count used by IPC hooks. */
static int s_inhibit = 0;
bool controller::inhibited() { return s_inhibit > 0; }
void controller::inhibit() { s_inhibit++; }
void controller::uninhibit() { if (s_inhibit > 0) { s_inhibit--; } }

void controller::init()
{
    spread = std::make_unique<spread_t>(output);
    drag = std::make_unique<window_drag_t>(output, spread.get(),
        [this] (wayfire_toplevel_view v, wf::point_t ws) { activate_window(v, ws); },
        [this] { relayout(); });
    grab = std::make_unique<wf::input_grab_t>(PLUGIN_NAME, output, this, this, nullptr);

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
    output->connect(&on_view_geometry_changed);

    swipe.on_begin  = [this] (int f) { gesture_begin(f); };
    swipe.on_update = [this] (double dx, double dy) { gesture_update(dx, dy); };
    swipe.on_end    = [this] (double vx, double vy) { gesture_end(vx, vy); };
    swipe.on_pinch  = [this] (int f, double s) { gesture_pinch(f, s); };
}

void controller::fini()
{
    end_to_desktop();
    on_view_unmapped.disconnect();
    on_view_mapped.disconnect();
    on_focus_request.disconnect();
    on_workarea_changed.disconnect();
    on_view_geometry_changed.disconnect();
}

void controller::apply_resources()
{
    auto w = resources_for(cur);
    t_activate->ensure(w.activated);
    t_top->ensure(w.top);
    t_grab->ensure(w.grabbed);
    if (w.overlay) { spread->ensure_layout(make_frame_ctx(output), filter); }
    else { spread->clear(); }
}

void controller::publish_mode()
{
    const bool a = (cur == stage::apps_spread);
    const bool w = (cur == stage::workspaces_spread);
    if (a != pub_apps)
    {
        a ? output->activate_plugin(&state_apps) : output->deactivate_plugin(&state_apps);
        pub_apps = a;
    }

    if (w != pub_workspaces)
    {
        w ? output->activate_plugin(&state_workspaces) : output->deactivate_plugin(&state_workspaces);
        pub_workspaces = w;
    }
}

void controller::reconcile()
{
    stage want = stage_at(g_axis.value(), cur);
    if (want != cur)
    {
        cur = want;
        publish_mode();
    }

    apply_resources();
}

void controller::render_frame()
{
    auto ctx = make_frame_ctx(output);
    rs.g = g_axis.value();
    if (slide_active)
    {
        rs.pan_dir    = {slide_to.x - slide_from.x, slide_to.y - slide_from.y};
        rs.pan_amount = pan.value();
    } else
    {
        rs.pan_dir    = {0, 0};
        rs.pan_amount = 0;
    }

    spread->render(ctx, rs);
}

void controller::advance()
{
    if (slide_active)
    {
        if (pan.active()) { output->render->schedule_redraw(); return; }
        finish_slide();
        return;
    }

    if (g_axis.interacting_now()) { output->render->schedule_redraw(); return; }
    if (g_axis.animating())
    {
        reconcile();
        output->render->schedule_redraw();
        return;
    }

    reconcile();
    if (spread->animating()) { output->render->schedule_redraw(); return; }
    unhook();
}

void controller::set_hook() { t_hooks->ensure(true); }
void controller::unhook()   { t_hooks->ensure(false); }

void controller::settle_to(double target)
{
    /* Enter the spread up front when opening from the desktop, so there is a
     * live spread to animate into (reconcile re-drives the mode as g rises). */
    if ((cur == stage::desktop) && (target > 0.0))
    {
        cur = stage::apps_spread;
        publish_mode();
        apply_resources();
    }

    double start = g_axis.value();
    /* Nudge an opening animation off an exact stage boundary: the first frame
     * can fire with ~0ms elapsed and g sitting on the boundary maps to the lower
     * stage, which would bounce us straight back. One frame covers the nudge. */
    if (target > start) { start = std::min(target, start + 1e-3); }
    g_axis.animate_to(start, target);
    set_hook();
}

void controller::relayout_if_idle()
{
    /* Reflow when a window resizes or the workarea changes while the spread sits
     * settled. Skip while a gesture, slide, stage animation, our own window
     * activation, or a thumbnail drag is driving geometry, so we never fight
     * motion that is already in flight. */
    if ((cur == stage::desktop) || gesturing || slide_active || self_activating ||
        g_axis.active() || (drag && drag->active()))
    {
        return;
    }

    relayout();
}

void controller::relayout()
{
    spread->relayout(make_frame_ctx(output), filter);

    /* The slots ease toward their new targets on their own; keep the frame loop
     * running so that easing is drawn when the spread is otherwise settled. */
    if (!g_axis.active() && !slide_active && spread->animating()) { set_hook(); }

    render_frame();
    output->render->schedule_redraw();
}

void controller::activate_window(wayfire_toplevel_view v, wf::point_t ws)
{
    /* Our own focus_request would otherwise trip the external-activation
     * dismissal below; settle_to(0.0) already closes the spread here. */
    self_activating = true;
    if (v->minimized) { wf::get_core().default_wm->minimize_request(v, false); }
    wf::get_core().default_wm->focus_request(v);
    self_activating = false;

    output->wset()->set_workspace(ws);
    settle_to(0.0);
}

void controller::end_to_desktop()
{
    slide_active = false;
    gesturing = false;
    if (drag) { drag->cancel(); }
    filter.clear();
    g_axis.pin(0.0);
    cur = stage::desktop;
    publish_mode();
    apply_resources();
    unhook();

    if (deferred_armed)
    {
        output->render->rem_effect(&deferred_hook);
        deferred_armed = false;
        deferred_fn = nullptr;
    }
}

void controller::recenter_apps_spread()
{
    slide_active = false;
    relayout();
    g_axis.pin(1.0);
    cur = stage::apps_spread;
    set_hook();
}

void controller::close_spread()
{
    if (cur != stage::desktop) { settle_to(0.0); }
}

void controller::toggle_apps_spread()
{
    if (inhibited()) { return; }
    if (cur == stage::apps_spread) { settle_to(0.0); return; }
    filter.clear();
    settle_to(1.0);
}

void controller::toggle_workspaces_spread()
{
    if (inhibited()) { return; }
    if (cur == stage::workspaces_spread) { settle_to(0.0); return; }
    settle_to(2.0);
}

void controller::spread_app(const std::vector<std::string>& ids)
{
    if (inhibited() || ids.empty() || (cur == stage::workspaces_spread)) { return; }
    if ((cur == stage::apps_spread) && (filter == ids)) { settle_to(0.0); return; }

    filter = ids;
    if (cur == stage::desktop) { settle_to(1.0); }
    else { relayout(); }
}

bool controller::cursor_here() const
{
    return output->get_relative_geometry() & output->get_cursor_position();
}

wf::point_t controller::neighbor(wf::point_t cur_ws, double dx, double dy) const
{
    auto dims = output->wset()->get_workspace_grid_size();
    wf::point_t nb = cur_ws;
    if (std::abs(dx) > std::abs(dy))
    {
        if ((dx < 0) && (cur_ws.x < dims.width - 1)) { nb.x = cur_ws.x + 1; }
        else if ((dx > 0) && (cur_ws.x > 0)) { nb.x = cur_ws.x - 1; }
    } else
    {
        if ((dy < 0) && (cur_ws.y < dims.height - 1)) { nb.y = cur_ws.y + 1; }
        else if ((dy > 0) && (cur_ws.y > 0)) { nb.y = cur_ws.y - 1; }
    }

    return nb;
}

void controller::begin_slide()
{
    slide_from = output->wset()->get_current_workspace();
    slide_to = slide_from;
    slide_accum_x = slide_accum_y = 0;
    pan.begin(0.0, 0.0, 1.0);

    t_activate->ensure(true);
    t_top->ensure(true);
    t_grab->ensure(true);
    spread->ensure_layout(make_frame_ctx(output), filter);

    slide_active = true;
    set_hook();
}

void controller::slide_update(double dx, double dy)
{
    slide_accum_x += dx;
    slide_accum_y += dy;
    slide_to = neighbor(slide_from, slide_accum_x, slide_accum_y);

    const bool horiz = std::abs(slide_accum_x) > std::abs(slide_accum_y);
    const double mag = horiz ? std::abs(slide_accum_x) : std::abs(slide_accum_y);
    pan.hold(same_ws(slide_to, slide_from) ? 0.0 : std::clamp(mag / SWIPE_DISTANCE, 0.0, 1.0));

    output->render->schedule_redraw();
}

void controller::slide_end()
{
    const bool commit = (pan.value() > 0.5) && !same_ws(slide_to, slide_from);
    pan.animate_to(pan.value(), commit ? 1.0 : 0.0);
    set_hook();
}

void controller::finish_slide()
{
    const bool commit = (pan.value() > 0.5) && !same_ws(slide_to, slide_from);
    slide_active = false;
    if (commit) { output->wset()->set_workspace(slide_to); }
    /* The stage that started the slide cleans up (desktop tears down, apps
     * spread re-centres); cur is unchanged across a slide. */
    stage_slide_settle();
}

void controller::gesture_begin(int fingers)
{
    gesturing = false;
    if (inhibited() || !cursor_here()) { return; }

    if (fingers == 4)
    {
        if (stage_slides(cur)) { begin_slide(); }
        return;
    }

    if (fingers != 3) { return; }

    /* Bound the swipe to one stage either side of the current stage (a discrete
     * anchor), not the live g, so a single swipe cannot skip desktop -> wall. */
    const double anchor = (cur == stage::workspaces_spread) ? 2.0
        : (cur == stage::desktop) ? 0.0 : 1.0;
    const double from = g_axis.value();

    /* Lay the spread out up front so the first motion frame does not hitch. A
     * fresh swipe from the desktop is an unfiltered apps spread: drop any app-id
     * filter left over from an earlier spread-app (as toggle_apps_spread does),
     * otherwise the swipe would only show that one app. */
    if (cur == stage::desktop)
    {
        filter.clear();
        cur = stage::apps_spread;
        publish_mode();
        apply_resources();
    }

    gp_lo = std::max(0.0, anchor - 1.0);
    gp_hi = std::min(2.0, anchor + 1.0);
    g_axis.begin(from, gp_lo, gp_hi);
    gesturing = true;
    set_hook();
}

void controller::gesture_update(double dx, double dy)
{
    if (slide_active) { slide_update(dx, dy); return; }
    if (!gesturing) { return; }

    g_axis.drive(-dy / SWIPE_DISTANCE);

    /* Follow the stage across the spread <-> wall boundary while dragging, but
     * never drop to desktop mid-gesture (that teardown would drop the grab). */
    stage want = stage_at(g_axis.value(), cur);
    if ((want != cur) && (want != stage::desktop))
    {
        cur = want;
        publish_mode();
    }

    output->render->schedule_redraw();
}

void controller::gesture_end(double, double vy)
{
    if (slide_active) { slide_end(); return; }
    if (!gesturing) { return; }

    gesturing = false;
    g_axis.settle(vy);
    set_hook();
}

void controller::gesture_pinch(int fingers, double scale)
{
    if (inhibited() || (fingers < 3) || !cursor_here()) { return; }
    if (std::abs(scale - 1.0) >= PINCH_THRESHOLD) { toggle_workspaces_spread(); }
}

void controller::stage_on_button(const wlr_pointer_button_event& ev)
{
    if (ev.state != WL_POINTER_BUTTON_STATE_PRESSED)
    {
        /* The wall commits a thumbnail drag, or picks the clicked workspace. */
        if ((cur == stage::workspaces_spread) &&
            (drag->release() == window_drag_t::result::empty_click))
        {
            auto ctx = make_frame_ctx(output);
            output->wset()->set_workspace(coords::cell_at(ctx, ctx.cursor, WALL_GAP));
            settle_to(0.0);
        }

        return;
    }

    if (cur == stage::apps_spread)
    {
        auto ctx = make_frame_ctx(output);
        if (auto v = spread->view_at(ctx.cursor)) { activate_window(v, ctx.cur_ws); }
        else { close_spread(); }   /* a click on empty space dismisses the spread */
    } else if (cur == stage::workspaces_spread)
    {
        drag->press();
    }
}

void controller::stage_on_motion()
{
    if (cur != stage::workspaces_spread) { return; }

    drag->motion();
    auto ctx = make_frame_ctx(output);
    rs.hover = coords::cell_at(ctx, ctx.cursor, WALL_GAP);
}

void controller::stage_slide_settle()
{
    if (cur == stage::desktop) { end_to_desktop(); }
    else if (cur == stage::apps_spread) { recenter_apps_spread(); }
    /* the wall does not re-centre after a slide */
}

void controller::handle_pointer_button(const wlr_pointer_button_event& ev)
{
    if (ev.button != BTN_LEFT) { return; }
    stage_on_button(ev);
    update_cursor();
}

void controller::handle_pointer_motion(wf::pointf_t, uint32_t)
{
    stage_on_motion();
    update_cursor();
    output->render->schedule_redraw();
}

void controller::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev)
{
    if ((ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) || (cur == stage::desktop)) { return; }

    if (ev.keycode == KEY_ESC) { settle_to(0.0); return; }

    /* The grab owns the keyboard, so the compositor's workspace-switch binds
     * can't reach it; arrow keys move to the neighbour and stay in the spread
     * (the backdrop is kept across the relayout, so the desktop never blinks). */
    auto cur_ws = output->wset()->get_current_workspace();
    auto dims   = output->wset()->get_workspace_grid_size();
    wf::point_t to = cur_ws;
    switch (ev.keycode)
    {
      case KEY_LEFT:  to.x -= 1; break;
      case KEY_RIGHT: to.x += 1; break;
      case KEY_UP:    to.y -= 1; break;
      case KEY_DOWN:  to.y += 1; break;
      default: return;
    }

    to.x = std::clamp(to.x, 0, dims.width - 1);
    to.y = std::clamp(to.y, 0, dims.height - 1);
    if (same_ws(to, cur_ws)) { return; }

    output->wset()->set_workspace(to);
    run_next_frame([this] { relayout(); });
}

void controller::update_cursor()
{
    wf::get_core().set_cursor(drag->active() ? "grabbing" : "default");
}

void controller::run_next_frame(std::function<void ()> fn)
{
    deferred_fn = std::move(fn);
    if (!deferred_armed)
    {
        deferred_armed = true;
        output->render->add_effect(&deferred_hook, wf::OUTPUT_EFFECT_PRE);
    }

    output->render->schedule_redraw();
}

void controller::handle_mapped(wf::view_mapped_signal *ev)
{
    if ((cur == stage::desktop) && !slide_active) { return; }
    if (!wf::toplevel_cast(ev->view)) { return; }
    if (!gesturing && !slide_active && !g_axis.animating()) { relayout(); }
}

void controller::handle_unmapped(wf::view_unmapped_signal *ev)
{
    if ((cur == stage::desktop) && !slide_active) { return; }
    auto v = wf::toplevel_cast(ev->view);
    if (!v) { return; }

    drag->forget(v);
    spread->forget(v);
    if (!gesturing && !slide_active && !g_axis.animating()) { relayout(); }
}

void controller::handle_focus_request(wf::view_focus_request_signal *ev)
{
    /* An app was activated from outside the spread (e.g. the launcher): close
     * the spread so the activation is revealed. Our own window selection is
     * guarded by self_activating. */
    if (self_activating || (cur == stage::desktop)) { return; }

    auto v = wf::toplevel_cast(ev->view);
    if (!v || (v->get_output() != output)) { return; }

    close_spread();
}
}
