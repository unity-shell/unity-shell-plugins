#pragma once

#include <functional>
#include <optional>
#include <string>

struct wlr_output;

namespace wfgs
{
/**
 * Per-key seed provider: given a schema key, returns a computed value to write
 * on attach, or nullopt to keep the key's applied default.
 */
using seeder_fn = std::function<std::optional<std::string>(const std::string& key)>;

/**
 * Builds the seed policy for an output section: supplies the auto-detected
 * scale and leaves every other key to its default.
 */
seeder_fn output_seeder(wlr_output *output);

/**
 * Computes an output scale seed from output pixel/physical dimensions.
 */
double compute_scale(int width_px, int height_px, int width_mm, int height_mm);
}
