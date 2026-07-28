#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "coords.hpp"
#include "config.hpp"
#include "tracker.hpp"
#include "resources.hpp"
#include "renderer.hpp"
#include "stage.hpp"
#include "slide.hpp"
#include "gesture.hpp"
#include "drag.hpp"

namespace spatial
{
/**
 * Runs a callback once on the next PRE-render frame, then disarms itself.
 * Used to defer a relayout until after a workspace-switch transaction has
 * committed (the next frame), so the spread re-centres against settled
 * geometry. Re-arming before it fires just replaces the pending callback.
 */
class next_frame_call
{
  public:
    void arm(wf::output_t *on, std::function<void ()> fn)
    {
        output  = on;
        pending = std::move(fn);
        if (!armed)
        {
            armed = true;
            output->render->add_effect(&hook, wf::OUTPUT_EFFECT_PRE);
        }
        output->render->schedule_redraw();
    }

    void cancel() { if (armed) { disarm(); } }

  private:
    void disarm()
    {
        output->render->rem_effect(&hook);
        armed   = false;
        pending = nullptr;
    }

    wf::output_t *output = nullptr;
    std::function<void ()> pending;
    bool armed = false;
    wf::effect_hook_t hook = [this] {
        auto fn = std::move(pending);   /* grab before disarm clears pending */
        disarm();
        if (fn) { fn(); }
    };
};

/**
 * Per-output spread controller (the State-pattern context).
 *
 * One continuous axis g in [0, 2] is the single source of truth: g==0 desktop,
 * g in (0, 1] the apps spread, g in (1, 2] the workspaces wall. A gesture drives
 * g 1:1; on release a tracker animates it to the nearest of {0, 1, 2}. The mode
 * follows g (mode::classify), and compositor resources are reconciled to the
 * current mode idempotently through RAII toggles, so there is a single teardown
 * path and nothing to hand-balance.
 */
class controller : public wf::per_output_plugin_instance_t,
    public wf::pointer_interaction_t,
    public wf::keyboard_interaction_t
{
  public:
    void init() override;
    void fini() override;

    void toggle_apps_spread();
    void toggle_workspaces_spread();
    void close_spread();
    void spread_app(const std::vector<std::string>& ids);

    static bool inhibited();
    static void inhibit();
    static void uninhibit();

  private:
    void gesture_begin(int fingers);
    void gesture_update(double dx, double dy);
    void gesture_end(double vx, double vy);
    void gesture_pinch(int fingers, double scale);

    void settle_to(double target);
    void relayout(bool animate = true);
    void activate_window(wayfire_toplevel_view view, wf::point_t ws);
    void end_to_desktop();
    void recenter_apps_spread();

    /* Per-stage input behaviour: click and pointer-motion semantics differ by
     * stage, everything else is shared (see stage.hpp). */
    void stage_on_button(const wlr_pointer_button_event& ev);
    void stage_on_motion();
    void stage_slide_settle();

    void set_stage(stage s);   /* the one writer of `cur`: publishes + clears filter on desktop */
    void reconcile();
    void apply_resources();
    void publish_mode();
    void render_frame();
    void advance();
    void set_hook();
    void unhook();

    void begin_slide();
    void slide_update(double dx, double dy);
    void slide_end();
    void finish_slide();

    void handle_pointer_button(const wlr_pointer_button_event& ev) override;
    void handle_pointer_motion(wf::pointf_t position, uint32_t time_ms) override;
    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override;
    void update_cursor();

    void handle_mapped(wf::view_mapped_signal *ev);
    void handle_unmapped(wf::view_unmapped_signal *ev);
    void handle_focus_request(wf::view_focus_request_signal *ev);
    void relayout_if_idle();
    bool cursor_here() const;

    std::unique_ptr<spread_t> spread;
    std::unique_ptr<window_drag_t> drag;
    std::unique_ptr<wf::input_grab_t> grab;
    std::unique_ptr<slide_t> slide;
    swipe_gesture_t swipe;

    tracker g_axis{"spatial/duration"};
    render_state rs;

    stage cur = stage::desktop;

    std::optional<toggled> t_activate, t_top, t_grab, t_hooks;

    std::vector<std::string> filter;
    bool   gesturing = false;
    bool   self_activating = false;

    wf::effect_hook_t pre_hook  = [this] { render_frame(); };
    wf::effect_hook_t post_hook = [this] { advance(); };

    next_frame_call deferred;

    wf::plugin_activation_data_t grab_interface{
        .name = PLUGIN_NAME,
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [this] { end_to_desktop(); },
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev) { handle_unmapped(ev); };
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev) { handle_mapped(ev); };
    wf::signal::connection_t<wf::view_focus_request_signal> on_focus_request =
        [this] (wf::view_focus_request_signal *ev) { handle_focus_request(ev); };
    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed =
        [this] (wf::workarea_changed_signal *) { relayout_if_idle(); };
};
}
