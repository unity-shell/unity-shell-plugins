#pragma once

namespace wfgs
{
/**
 * Computes an output scale seed from output pixel/physical dimensions.
 */
double compute_scale(int width_px, int height_px, int width_mm, int height_mm);
}
