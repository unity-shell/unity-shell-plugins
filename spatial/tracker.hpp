#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <wayfire/util/duration.hpp>
#include <wayfire/option-wrapper.hpp>

#include "config.hpp"

namespace spatial
{
inline double snap_target(double live, double velocity, double lo, double hi)
{
    double t;
    if (velocity < -FLING_VELOCITY)     { t = std::floor(live) + 1.0; }
    else if (velocity > FLING_VELOCITY) { t = std::ceil(live) - 1.0; }
    else                                { t = std::round(live); }
    return std::clamp(t, lo, hi);
}

class tracker
{
  public:
    explicit tracker(const std::string& duration_option) :
        anim{wf::option_wrapper_t<wf::animation_description_t>{duration_option}}
    {}

    double value() { return interacting ? live : (double) anim; }
    bool interacting_now() const { return interacting; }
    bool animating() { return anim.running(); }
    bool active() { return interacting || anim.running(); }

    void begin(double from, double low, double high)
    {
        interacting = true;
        min = low;
        max = high;
        base = live = std::clamp(from, low, high);
        accum = 0;
    }

    void drive(double delta)
    {
        accum += delta;
        live = std::clamp(base + accum, min, max);
    }

    void hold(double v) { interacting = true; live = std::clamp(v, min, max); }

    void settle(double velocity)
    {
        interacting = false;
        anim.animate(live, snap_target(live, velocity, min, max));
    }

    void animate_to(double from, double to) { interacting = false; anim.animate(from, to); }
    void pin(double v) { interacting = false; anim.animate(v, v); }

  private:
    wf::animation::simple_animation_t anim;
    bool interacting = false;
    double live = 0, base = 0, accum = 0, min = 0, max = 2;
};
}
