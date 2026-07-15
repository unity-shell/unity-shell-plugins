#pragma once

#include <functional>
#include <utility>

namespace spatial
{
class toggled
{
  public:
    toggled(std::function<void ()> on, std::function<void ()> off) :
        on_(std::move(on)), off_(std::move(off))
    {}

    toggled(const toggled&) = delete;
    toggled& operator =(const toggled&) = delete;

    ~toggled() { ensure(false); }

    void ensure(bool want)
    {
        if (want == held) { return; }
        held = want;
        held ? on_() : off_();
    }

    bool active() const { return held; }

  private:
    std::function<void ()> on_, off_;
    bool held = false;
};
}
