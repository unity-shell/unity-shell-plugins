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
#include "mode.hpp"
#include "gesture.hpp"
#include "drag.hpp"

namespace spatial
{
/**
 * Per-output overview controller (the State-pattern context).
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

    void gesture_begin(int fingers);
    void gesture_update(double dx, double dy);
    void gesture_end(double vx, double vy);
    void gesture_pinch(int fingers, double scale);

    void toggle_apps_spread();
    void toggle_workspaces_spread();
    void close_overview();
    void spread_app(const std::vector<std::string>& ids);

    wf::output_t *out() const { return output; }
    spread_t& renderer() { return *spread; }
    window_drag_t& dragger() { return *drag; }
    frame_ctx frame() const { return make_frame_ctx(output); }
    render_state& rstate() { return rs; }
    void settle_to(double target);
    void relayout();
    void activate_window(wayfire_toplevel_view view, wf::point_t ws);
    void end_to_desktop();
    void recenter_apps_spread();

    static bool inhibited();
    static void inhibit();
    static void uninhibit();

  private:
    using mode_t = mode_id;

    mode& current() { return *mode_ptr(cur); }
    mode *mode_ptr(mode_id m);

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
    wf::point_t neighbor(wf::point_t cur, double dx, double dy) const;
    static bool same_ws(wf::point_t a, wf::point_t b) { return a.x == b.x && a.y == b.y; }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override;
    void handle_pointer_motion(wf::pointf_t position, uint32_t time_ms) override;
    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override;
    void update_cursor();

    void run_next_frame(std::function<void ()> fn);
    void handle_mapped(wf::view_mapped_signal *ev);
    void handle_unmapped(wf::view_unmapped_signal *ev);
    void handle_focus_request(wf::view_focus_request_signal *ev);
    bool cursor_here() const;

    std::unique_ptr<spread_t> spread;
    std::unique_ptr<window_drag_t> drag;
    std::unique_ptr<wf::input_grab_t> grab;
    swipe_gesture_t swipe;

    tracker g_axis{"spatial/duration"};
    tracker pan{"spatial/duration"};
    render_state rs;

    mode_id cur = mode_id::desktop;
    desktop_mode            m_desktop;
    apps_spread_mode        m_apps;
    workspaces_spread_mode  m_workspaces;

    std::optional<toggled> t_activate, t_top, t_grab, t_hooks;

    std::vector<std::string> filter;
    bool   gesturing = false;
    bool   self_activating = false;
    double gp_lo = 0, gp_hi = 2;

    bool slide_active = false;
    wf::point_t slide_from{0, 0}, slide_to{0, 0};
    double slide_accum_x = 0, slide_accum_y = 0;

    wf::effect_hook_t pre_hook  = [this] { render_frame(); };
    wf::effect_hook_t post_hook = [this] { advance(); };

    std::function<void ()> deferred_fn;
    bool deferred_armed = false;
    wf::effect_hook_t deferred_hook = [this] {
        output->render->rem_effect(&deferred_hook);
        deferred_armed = false;
        auto fn = std::move(deferred_fn);
        deferred_fn = nullptr;
        if (fn) { fn(); }
    };

    wf::plugin_activation_data_t grab_interface{
        .name = PLUGIN_NAME,
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [this] { end_to_desktop(); },
    };

    wf::plugin_activation_data_t state_apps{.name = "spatial-spread", .capabilities = 0};
    wf::plugin_activation_data_t state_workspaces{.name = "spatial-wall", .capabilities = 0};
    bool pub_apps = false, pub_workspaces = false;

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev) { handle_unmapped(ev); };
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev) { handle_mapped(ev); };
    wf::signal::connection_t<wf::view_focus_request_signal> on_focus_request =
        [this] (wf::view_focus_request_signal *ev) { handle_focus_request(ev); };
};
}
