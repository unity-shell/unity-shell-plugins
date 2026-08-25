#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace night_light
{
/* The temperature at which the ramp is the identity. */
inline constexpr double neutral_kelvin = 6500.0;

/* Per-channel gain in 0..1, relative to a neutral screen. */
struct whitepoint
{
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

/* colord's blackbody table, sampled at 100 K and interpolated linearly. 6500 K
 * comes back as exactly (1, 1, 1), and red never drops below 1, so the ramp
 * warms without dimming. */
whitepoint whitepoint_for(double kelvin);

bool is_neutral(const whitepoint& point);

/* Linear ramp of @size entries per channel scaled by @point, laid out red, then
 * green, then blue, which is what wlr_color_transform_init_lut_3x1d() reads. */
std::vector<uint16_t> make_ramp(size_t size, const whitepoint& point);
}
