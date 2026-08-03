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
#include <wayfire/plugins/ipc/ipc-rules-common.hpp>

namespace spatial
{
namespace
{
/* Sets a flag for the duration of a scope (exception-safe, always cleared). */
struct scoped_flag
{
    bool& flag;
    explicit scoped_flag(bool& f) : flag(f) { flag = true; }
    ~scoped_flag() { flag = false; }
};

/* The workspace one arrow-key step from `from`, clamped to the grid. Returns
 * `from` unchanged for any non-arrow key or a step off the edge. */
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
        /* Snap after a drop: the thumbnail is already at the drop point, so
         * animating its slot would fling it back from the source cell. */
        [this] { relayout(false); });
    grab = std::make_unique<wf::input_grab_t>(PLUGIN_NAME, output, this, this, nullptr);
    slide = std::make_unique<slide_t>(output);

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
}

void controller::apply_resources()
{
    /* The overview needs its resources whenever it is visible: any non-desktop
     * stage, or an active slide (which pans the spread even from the desktop).
     * This keeps a single resource path (no hand-forcing in begin_slide). */
    const bool apps_spread = (cur != stage::desktop) || (slide && slide->active());
    auto w = resources_for(apps_spread ? stage::apps_spread : stage::desktop);
    t_activate->ensure(w.activated);
    t_top->ensure(w.top);
    t_grab->ensure(w.grabbed);
    if (w.overlay) { spread->ensure_layout(make_frame_ctx(output), filter); }
    else { spread->clear(); }
}

void controller::publish_stage(stage s)
{
    /* Broadcast the visible stage as an ipc-rules event for the panel clients.
     * Driven by the on-screen g (see render_frame), so the panel tracks an
     * opening/closing animation live rather than the latched input stage. */
    if (s == published) { return; }
    published = s;

    const char *name =
        (s == stage::apps_spread)       ? "apps_spread" :
        (s == stage::workspaces_spread) ? "workspaces_spread" : "desktop";

    wf::json_t data;
    data["stage"] = name;
    wf::ipc_rules::send_event_to_subscribes(data, "spatial/stage#");
}

void controller::set_stage(stage s)
{
    if (s == cur) { return; }
    /* Filter clears on the transition back to desktop, so each spread opens
     * unfiltered; only spread_app sets it. */
    if (s == stage::desktop) { filter.clear(); }
    /* Entering the wall seeds the selection on the current workspace; the ring
     * starts there and the arrows move it from a known-valid cell. */
    if (s == stage::workspaces_spread) { sel = output->wset()->get_current_workspace(); }
    cur = s;
}

void controller::reconcile()
{
    set_stage(stage_at(g_axis.value(), cur));
    apply_resources();
}

void controller::render_frame()
{
    render_state rs;
    auto ctx = make_frame_ctx(output);
    rs.g   = g_axis.value();
    rs.sel = sel;
    /* Feed the panel from the visible axis, so an opening/closing animation is
     * reflected live (the input stage `cur` holds its destination and would lag). */
    publish_stage(stage_at(rs.g, published));
    if (slide->active())
    {
        rs.pan_dir    = slide->pan_dir();
        rs.pan_amount = slide->pan_amount();
    } else
    {
        rs.pan_dir    = {0, 0};
        rs.pan_amount = 0;
    }

    spread->render(ctx, rs);
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
        /* A settle holds the destination stage it started with; don't re-derive
         * from the in-flight g (that would bounce an opening spread to desktop at
         * g~0). Just keep resources live and frames coming. */
        apply_resources();
        output->render->schedule_redraw();
        return;
    }

    /* Settle finished: latch the final stage from where g landed. */
    reconcile();
    if (spread->animating()) { output->render->schedule_redraw(); return; }
    unhook();
}

void controller::set_hook() { t_hooks->ensure(true); }
void controller::unhook()   { t_hooks->ensure(false); }

void controller::settle_to(double target)
{
    /* Open/switch (target > 0): enter the destination stage now so there's a live
     * spread to animate into; advance holds it for the animation. Close (target 0)
     * keeps the current stage until g reaches desktop, so it stays visible. */
    if (target > 0.0)
    {
        set_stage(target >= 2.0 ? stage::workspaces_spread : stage::apps_spread);
        apply_resources();
    }

    g_axis.animate_to(g_axis.value(), target);
    set_hook();
}

bool controller::can_relayout()
{
    /* The spread sits settled and idle: no gesture or settle animation on the
     * axis, no slide, no self-activation close, and no thumbnail drag. Reflowing
     * outside this window would fight motion that is already in flight. */
    return (cur != stage::desktop) && !g_axis.active() && !slide->active() &&
        !self_activating && !(drag && drag->active());
}

void controller::relayout_if_idle()
{
    /* Reflow when a window resizes or the workarea changes while settled. */
    if (can_relayout()) { relayout(); }
}

void controller::relayout(bool animate)
{
    spread->layout(make_frame_ctx(output), filter, animate);

    /* The slots ease toward their new targets on their own; keep the frame loop
     * running so that easing is drawn when the spread is otherwise settled. */
    if (!g_axis.active() && !slide->active() && spread->animating()) { set_hook(); }

    repaint();
}

void controller::activate_window(wayfire_toplevel_view v, wf::point_t ws)
{
    /* Suppress the external-activation dismissal for our own focus_request and
     * workspace switch; settle_to(0.0) already closes the spread here. */
    scoped_flag guard{self_activating};
    if (v->minimized) { wf::get_core().default_wm->minimize_request(v, false); }
    wf::get_core().default_wm->focus_request(v);
    output->wset()->set_workspace(ws);
    settle_to(0.0);
}

void controller::end_to_desktop()
{
    if (slide) { slide->cancel(); }
    if (drag) { drag->cancel(); }
    g_axis.pin(0.0);   /* clears the axis interacting/animating state */
    set_stage(stage::desktop);
    publish_stage(stage::desktop);  /* this path unhooks, so no render frame will publish */
    apply_resources();
    unhook();

    deferred.cancel();
}

void controller::recenter_apps_spread()
{
    slide->cancel();
    /* Slots are cell-local (see renderer place()), so the workspace switch does
     * not move them -- relayout re-computes for the new current workspace and
     * the slots ease from where they already are (a no-op), with no snap. */
    spread->layout(make_frame_ctx(output), filter, /*animate=*/true);
    repaint();
    g_axis.pin(1.0);
    set_stage(stage::apps_spread);
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

void controller::begin_slide()
{
    /* Mark the slide active first, then reconcile resources through the one path:
     * apply_resources sees the active slide and grants the overview set (and lays
     * out the spread), even from the desktop stage. */
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
    /* The stage that started the slide cleans up (desktop tears down, apps
     * spread re-centres); cur is unchanged across a slide. */
    stage_slide_settle();
}

void controller::gesture_begin(int fingers)
{
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

    /* Lay the spread out up front so the first motion frame does not hitch.
     * Filter is already empty here (it clears on the desktop boundary), so the
     * swipe opens an unfiltered spread. */
    if (cur == stage::desktop)
    {
        set_stage(stage::apps_spread);
        apply_resources();
    }

    const double lo = std::max(0.0, anchor - 1.0);
    const double hi = std::min(2.0, anchor + 1.0);
    g_axis.begin(from, lo, hi);   /* marks the axis interacting until settle */
    set_hook();
}

void controller::gesture_update(double dx, double dy)
{
    if (slide->active()) { slide_update(dx, dy); return; }
    if (!g_axis.interacting_now()) { return; }

    g_axis.drive(-dy / SWIPE_DISTANCE);

    /* Follow the stage across the spread <-> wall boundary while dragging, but
     * never drop to desktop mid-gesture (that teardown would drop the grab). */
    stage want = stage_at(g_axis.value(), cur);
    if (want != stage::desktop) { set_stage(want); }

    output->render->schedule_redraw();
}

void controller::gesture_end(double, double vy)
{
    if (slide->active()) { slide_end(); return; }
    if (!g_axis.interacting_now()) { return; }

    g_axis.settle(vy);   /* ends the interacting state and animates to a stage */
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
        if ((cur == stage::workspaces_spread) && drag->release())
        {
            auto ctx = make_frame_ctx(output);
            output->wset()->set_workspace(coords::cell_at(ctx, ctx.cursor));
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

    /* Esc dismisses either spread. On the wall the arrows only move `sel`, never
     * the workspace, so this settles back onto the workspace we opened from. */
    if (ev.keycode == KEY_ESC) { settle_to(0.0); return; }

    auto grid = output->wset()->get_workspace_grid_size();

    if (cur == stage::workspaces_spread)
    {
        /* Wall: select then commit. Arrows move the ring over the static wall;
         * Enter/Space enter the selected cell (mirrors clicking it). */
        if ((ev.keycode == KEY_ENTER) || (ev.keycode == KEY_KPENTER) || (ev.keycode == KEY_SPACE))
        {
            output->wset()->set_workspace(sel);
            settle_to(0.0);
            return;
        }

        auto to = arrow_neighbor(sel, ev.keycode, grid);
        if (to == sel) { return; }
        sel = to;
        repaint();
        return;
    }

    /* App spread shows one workspace, so arrows switch it live and re-lay-out in
     * place (the backdrop is kept across the relayout, so the desktop never blinks). */
    auto cur_ws = output->wset()->get_current_workspace();
    auto to = arrow_neighbor(cur_ws, ev.keycode, grid);
    if (to == cur_ws) { return; }
    output->wset()->set_workspace(to);
    deferred.arm(output, [this] { relayout(); });
}

void controller::update_cursor()
{
    wf::get_core().set_cursor(drag->active() ? "grabbing" : "default");
}

void controller::handle_mapped(wf::view_mapped_signal *ev)
{
    if ((cur == stage::desktop) && !slide->active()) { return; }
    if (!wf::toplevel_cast(ev->view)) { return; }
    relayout_if_idle();
}

void controller::handle_unmapped(wf::view_unmapped_signal *ev)
{
    if ((cur == stage::desktop) && !slide->active()) { return; }
    auto v = wf::toplevel_cast(ev->view);
    if (!v) { return; }

    drag->forget(v);
    spread->forget(v);
    relayout_if_idle();
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
