#pragma once

#include <optional>

#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>

#include "tracker.hpp"

namespace spatial
{
/**
 * A four-finger workspace pan. It accumulates the drag, animates the pan
 * fraction, and reports the workspace to commit on release. It owns only the
 * pan mechanics; the controller wires it to compositor resources, the frame
 * loop, and the workspace set.
 */
class slide_t
{
  public:
    explicit slide_t(wf::output_t *output) : output(output) {}

    void begin();
    void update(double dx, double dy);
    void release();

    /** End the pan: clears active state and returns the workspace to commit. */
    std::optional<wf::point_t> finish();
    void cancel() { active_ = false; }

    bool active() const { return active_; }
    bool advancing() { return pan.active(); }
    wf::point_t pan_dir() const { return {target.x - from.x, target.y - from.y}; }
    double pan_amount() { return pan.value(); }

  private:
    wf::point_t neighbor(wf::point_t from, double dx, double dy) const;
    static bool same(wf::point_t a, wf::point_t b) { return (a.x == b.x) && (a.y == b.y); }

    wf::output_t *output;
    tracker pan{"spatial/duration"};
    wf::point_t from{0, 0}, target{0, 0};
    double accum_x = 0, accum_y = 0;
    bool active_ = false;
};
}
