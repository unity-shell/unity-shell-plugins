#include "ramp.hpp"

#include <colord.h>

#include <algorithm>
#include <cmath>

namespace night_light
{
whitepoint whitepoint_for(double kelvin)
{
    CdColorRGB rgb{};
    if (!cd_color_get_blackbody_rgb_full(kelvin, &rgb, CD_COLOR_BLACKBODY_FLAG_NONE))
    {
        /* Outside colord's table. The schema keeps temperatures inside it, so
         * this only guards against a value that should not exist. */
        return {};
    }

    return {rgb.R, rgb.G, rgb.B};
}

bool is_neutral(const whitepoint& point)
{
    /* One step of a 16-bit ramp is about 1.5e-5, so a smaller error cannot move
     * a single entry. */
    constexpr double tolerance = 1e-5;
    return (std::abs(point.r - 1.0) < tolerance) && (std::abs(point.g - 1.0) < tolerance) &&
           (std::abs(point.b - 1.0) < tolerance);
}

std::vector<uint16_t> make_ramp(size_t size, const whitepoint& point)
{
    std::vector<uint16_t> ramp(3 * size);
    if (size == 0)
    {
        return ramp;
    }

    const double top_index = (size > 1) ? double(size - 1) : 1.0;
    const double channel_gain[3] = {point.r, point.g, point.b};

    for (size_t channel = 0; channel < 3; channel++)
    {
        for (size_t entry = 0; entry < size; entry++)
        {
            const double level =
                std::clamp(double(entry) / top_index * channel_gain[channel], 0.0, 1.0);
            ramp[channel * size + entry] = uint16_t(std::lround(level * UINT16_MAX));
        }
    }

    return ramp;
}
}
