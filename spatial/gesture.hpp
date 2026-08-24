#pragma once

#include <functional>
#include <memory>

namespace spatial
{
/* Turns raw swipe and pinch events into gesture callbacks. */
class swipe_gesture_t
{
  public:
    swipe_gesture_t();
    ~swipe_gesture_t();
    swipe_gesture_t(const swipe_gesture_t&) = delete;
    swipe_gesture_t& operator =(const swipe_gesture_t&) = delete;

    std::function<void (int)>            on_begin;
    std::function<void (double, double)> on_update;
    std::function<void (double, double)> on_end;
    std::function<void (int, double)>    on_pinch;

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
