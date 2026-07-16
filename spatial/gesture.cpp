#include "gesture.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace spatial
{
static constexpr uint32_t HISTORY_MS = 150;

/* Gesture signal bridge with short history-based swipe velocity estimation. */
struct swipe_gesture_t::impl
{
    swipe_gesture_t& owner;

    template<class E> using event = wf::input_event_signal<E>;

    struct sample_t { uint32_t time; double dx, dy; };
    std::vector<sample_t> history;
    int    pinch_fingers = 0;
    double pinch_scale   = 1.0;

    void velocity(double& vx, double& vy) const
    {
        vx = vy = 0;
        if (history.size() < 2) { return; }

        const uint32_t period = history.back().time - history.front().time;
        if (period == 0) { return; }

        double sx = 0, sy = 0;
        for (size_t i = 1; i < history.size(); i++) { sx += history[i].dx; sy += history[i].dy; }
        vx = sx / period;
        vy = sy / period;
    }

    wf::signal::connection_t<event<wlr_pointer_swipe_begin_event>> begin_cb =
        [this] (event<wlr_pointer_swipe_begin_event> *ev)
    {
        history.clear();
        if (owner.on_begin) { owner.on_begin((int) ev->event->fingers); }
    };

    wf::signal::connection_t<event<wlr_pointer_swipe_update_event>> update_cb =
        [this] (event<wlr_pointer_swipe_update_event> *ev)
    {
        const uint32_t t = ev->event->time_msec;
        history.push_back({t, ev->event->dx, ev->event->dy});
        while ((history.size() > 1) && (t - history.front().time > HISTORY_MS))
        {
            history.erase(history.begin());
        }

        if (owner.on_update) { owner.on_update(ev->event->dx, ev->event->dy); }
    };

    wf::signal::connection_t<event<wlr_pointer_swipe_end_event>> end_cb =
        [this] (event<wlr_pointer_swipe_end_event> *)
    {
        double vx, vy;
        velocity(vx, vy);
        if (owner.on_end) { owner.on_end(vx, vy); }
    };

    wf::signal::connection_t<event<wlr_pointer_pinch_begin_event>> pinch_begin_cb =
        [this] (event<wlr_pointer_pinch_begin_event> *ev)
    {
        pinch_fingers = (int) ev->event->fingers;
        pinch_scale   = 1.0;
    };

    wf::signal::connection_t<event<wlr_pointer_pinch_update_event>> pinch_update_cb =
        [this] (event<wlr_pointer_pinch_update_event> *ev) { pinch_scale = ev->event->scale; };

    wf::signal::connection_t<event<wlr_pointer_pinch_end_event>> pinch_end_cb =
        [this] (event<wlr_pointer_pinch_end_event> *ev)
    {
        if (!ev->event->cancelled && owner.on_pinch) { owner.on_pinch(pinch_fingers, pinch_scale); }
    };

    explicit impl(swipe_gesture_t& o) : owner(o)
    {
        wf::get_core().connect(&begin_cb);
        wf::get_core().connect(&update_cb);
        wf::get_core().connect(&end_cb);
        wf::get_core().connect(&pinch_begin_cb);
        wf::get_core().connect(&pinch_update_cb);
        wf::get_core().connect(&pinch_end_cb);
    }
};

swipe_gesture_t::swipe_gesture_t() : priv(std::make_unique<impl>(*this)) {}
swipe_gesture_t::~swipe_gesture_t() = default;
}
