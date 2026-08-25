#include "schedule.hpp"

#include <libnova/julian_day.h>
#include <libnova/solar.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace night_light
{
namespace
{
/* The only names the schedule option accepts. A new mode goes here and in
 * night-light.xml, nowhere else. */
constexpr std::pair<std::string_view, schedule_mode> mode_names[] = {
    {"sunset-to-sunrise", schedule_mode::sunset_to_sunrise},
    {"manual", schedule_mode::manual},
};

double wrap24(double hours)
{
    hours = std::fmod(hours, 24.0);
    return (hours < 0.0) ? hours + 24.0 : hours;
}

/* Signed distance from @reference to @hour around a 24 hour clock, in (-12, 12]. */
double clock_diff(double hour, double reference)
{
    const double gap = wrap24(hour - reference);
    return (gap > 12.0) ? gap - 24.0 : gap;
}

/* [start, end), which may wrap midnight. */
bool inside(double now, double start, double end)
{
    if (start == end)
    {
        return false;
    }

    if (start < end)
    {
        return (now >= start) && (now < end);
    }

    return (now >= start) || (now < end);
}

/* @half_ramp is the ramp on each side of both ends of the warm interval. */
double interval_fraction(double now, double start, double end, double half_ramp)
{
    if (half_ramp > 0.0)
    {
        const double to_start = clock_diff(now, start);
        const double to_end   = clock_diff(now, end);
        const bool near_start = std::abs(to_start) < half_ramp;
        const bool near_end   = std::abs(to_end) < half_ramp;

        /* A transition longer than the night makes the two ramps overlap.
         * Following the nearer end keeps the curve continuous. */
        if (near_start && (!near_end || (std::abs(to_start) <= std::abs(to_end))))
        {
            return std::clamp(0.5 + to_start / (2.0 * half_ramp), 0.0, 1.0);
        }

        if (near_end)
        {
            return std::clamp(0.5 - to_end / (2.0 * half_ramp), 0.0, 1.0);
        }
    }

    return inside(now, start, end) ? 1.0 : 0.0;
}
}

std::optional<schedule_mode> parse_mode(std::string_view name)
{
    for (const auto& [text, mode] : mode_names)
    {
        if (name == text)
        {
            return mode;
        }
    }

    return {};
}

bool location_unset(const settings& cfg)
{
    return (cfg.latitude == 0.0) && (cfg.longitude == 0.0);
}

sun_times sun_times_for(double latitude, double longitude, const local_time& at)
{
    /* libnova answers in Julian days and UT, so local midnight in UT is the
     * reference the times fold back onto. */
    ln_date date{at.year, at.month, at.day, 0, 0, 0.0};
    const double midnight = ln_get_julian_day(&date) - at.utc_offset / 24.0;

    ln_lnlat_posn observer{longitude, latitude};
    ln_rst_time rst{};
    const int circumpolar = ln_get_solar_rst(midnight, &observer, &rst);

    sun_times sun;
    if (circumpolar != 0)
    {
        /* Positive means the sun never sets, negative that it never rises.
         * libnova documents neither, so this was checked against the sun's
         * altitude at local noon over 64 cases from latitude 66.6 to 89, both
         * hemispheres, every month. */
        sun.state = (circumpolar > 0) ? sun_state::polar_day : sun_state::polar_night;
        return sun;
    }

    sun.state   = sun_state::normal;
    sun.sunrise = wrap24((rst.rise - midnight) * 24.0);
    sun.sunset  = wrap24((rst.set - midnight) * 24.0);
    return sun;
}

double night_fraction(const settings& cfg, const local_time& at, const sun_times& sun)
{
    /* transition_seconds spans the whole ramp and is centred on the transition,
     * so half of it falls on each side. */
    const double half_ramp = std::max(0, cfg.transition_seconds) / 7200.0;
    const double now = wrap24(at.hours);

    if (cfg.mode == schedule_mode::manual)
    {
        return interval_fraction(now, wrap24(cfg.from), wrap24(cfg.to), half_ramp);
    }

    if (location_unset(cfg))
    {
        /* Never warm the screen from a point in the Gulf of Guinea. */
        return 0.0;
    }

    switch (sun.state)
    {
      case sun_state::polar_day:
        return 0.0;

      case sun_state::polar_night:
        return 1.0;

      case sun_state::normal:
        return interval_fraction(now, sun.sunset, sun.sunrise, half_ramp);
    }

    return 0.0;
}

double target_kelvin(const settings& cfg, const local_time& at, const sun_times& sun)
{
    if (!cfg.enabled)
    {
        return neutral_kelvin;
    }

    return cfg.day_kelvin + night_fraction(cfg, at, sun) * (cfg.night_kelvin - cfg.day_kelvin);
}
}
