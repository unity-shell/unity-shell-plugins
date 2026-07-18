#include "gsettings-seed.hpp"

#include <wayfire/nonstd/wlroots.hpp>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int MIN_INTEGER_SCALE = 1;
constexpr int MAX_INTEGER_SCALE = 4;
constexpr int MAX_DENOMINATOR = 4;
constexpr long MINIMUM_LOGICAL_AREA = 800 * 480;
constexpr int MOBILE_TARGET_DPI = 135;
constexpr int LARGE_TARGET_DPI = 110;
constexpr double LARGE_MIN_INCHES = 20.0;

bool has_aspect_as_size(int w, int h)
{
    return (w == 1600 && h == 900) || (w == 1600 && h == 1000) ||
           (w == 160 && h == 90) || (w == 160 && h == 100) ||
           (w == 16 && h == 9) || (w == 16 && h == 10);
}

int gcd_int(int a, int b)
{
    while (a > 0 && b > 0)
    {
        if (b > a)
        {
            b %= a;
        } else
        {
            a %= b;
        }
    }

    return std::max(a, b);
}

double closest_supported_scale(int px_w, int px_h, double target)
{
    double best = 1.0;
    double best_error = 1e30;
    for (int den = 1; den <= MAX_DENOMINATOR; den++)
    {
        for (int num = MIN_INTEGER_SCALE * den; num <= MAX_INTEGER_SCALE * den; num++)
        {
            if (((px_w * den) % num) != 0 || ((px_h * den) % num) != 0)
            {
                continue;
            }

            if (gcd_int(num, den) > 1)
            {
                continue;
            }

            const double scale = static_cast<double>(num) / den;
            const long lw = static_cast<long>(std::floor(px_w / scale));
            const long lh = static_cast<long>(std::floor(px_h / scale));
            if (lw * lh < MINIMUM_LOGICAL_AREA)
            {
                continue;
            }

            const double error = std::fabs(scale - target);
            if (error < best_error)
            {
                best = scale;
                best_error = error;
            }
        }
    }

    return best;
}

double output_scale(wlr_output *output)
{
    int rw = output->width;
    int rh = output->height;
    if (rw <= 0 || rh <= 0)
    {
        if (wlr_output_mode *mode = wlr_output_preferred_mode(output))
        {
            rw = mode->width;
            rh = mode->height;
        }
    }

    return wfgs::compute_scale(rw, rh, output->phys_width, output->phys_height);
}
}

namespace wfgs
{
seeder_fn output_seeder(wlr_output *output)
{
    return [output](const std::string& key) -> std::optional<std::string>
    {
        if (key == "scale")
        {
            /* Computed override: write the auto-detected scale instead of the
             * schema default, so HiDPI outputs come up scaled on first connect. */
            return std::to_string(output_scale(output));
        }

        return std::nullopt;
    };
}

/* Port of the output scale seeding heuristic used for initial defaults. */
double compute_scale(int width_px, int height_px, int width_mm, int height_mm)
{
    if (width_px <= 0 || height_px <= 0)
    {
        return 1.0;
    }

    if (width_mm <= 0 || height_mm <= 0 || has_aspect_as_size(width_mm, height_mm))
    {
        return 1.0;
    }

    const double diag_inches = std::hypot(double(width_mm), double(height_mm)) / 25.4;
    const int target_dpi = (diag_inches < LARGE_MIN_INCHES) ? MOBILE_TARGET_DPI : LARGE_TARGET_DPI;
    const double native_dpi = std::hypot(double(width_px), double(height_px)) / diag_inches;
    const double perfect_scale = native_dpi / target_dpi;
    return closest_supported_scale(width_px, height_px, perfect_scale);
}
}
