#pragma once

#include "ramp.hpp"

#include <optional>
#include <string_view>

namespace night_light
{
enum class schedule_mode
{
    sunset_to_sunrise,
    manual,
};

struct settings
{
    bool enabled = false;
    double night_kelvin = 4000.0;
    double day_kelvin   = neutral_kelvin;
    schedule_mode mode  = schedule_mode::sunset_to_sunrise;
    double from = 20.0;
    double to   = 6.0;
    double latitude  = 0.0;
    double longitude = 0.0;
    int transition_seconds = 3600;
};

struct local_time
{
    int year  = 1970;
    int month = 1;
    int day   = 1;
    /* Hours since local midnight. */
    double hours = 0.0;
    /* Local time minus UTC, DST included. */
    double utc_offset = 0.0;
};

enum class sun_state
{
    normal,
    polar_day,
    polar_night,
};

struct sun_times
{
    sun_state state = sun_state::normal;
    /* Hours since local midnight. */
    double sunrise = 0.0;
    double sunset  = 0.0;
};

/* Empty when @name is not one of the names the metadata lists, so the caller can
 * name the bad value the way core does for a bad output transform. */
std::optional<schedule_mode> parse_mode(std::string_view name);

bool location_unset(const settings& cfg);

/* Sunrise and sunset from libnova, for the local calendar date of @at. Longitude
 * is positive east. Only the date and the UTC offset of @at are read, so one call
 * covers the whole local day. */
sun_times sun_times_for(double latitude, double longitude, const local_time& at);

/* 0 is full day, 1 is full night. */
double night_fraction(const settings& cfg, const local_time& at, const sun_times& sun);

double target_kelvin(const settings& cfg, const local_time& at, const sun_times& sun);
}
