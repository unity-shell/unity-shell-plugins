#pragma once

#include "config.hpp"

namespace spatial
{
/** The three discrete points of the spatial axis g in [0, 2]. */
enum class stage { desktop, apps_spread, workspaces_spread };

/**
 * The single g -> stage transfer function. Hysteresis on the up-crossing keeps
 * jitter around g == 1 from flapping the input semantics; the current stage is
 * the only state it needs. The renderer stays continuous across the boundary,
 * so only click/drag semantics change with the stage.
 */
inline stage stage_at(double g, stage current)
{
    if (g <= 0.0) { return stage::desktop; }
    if (current == stage::workspaces_spread) { return (g <= 1.0) ? stage::apps_spread : stage::workspaces_spread; }
    return (g >= 1.0 + STAGE_HYSTERESIS) ? stage::workspaces_spread : stage::apps_spread;
}

/** Compositor resources a stage needs held while it is active. */
struct stage_resources
{
    bool activated = false;
    bool top       = false;
    bool grabbed   = false;
    bool overlay   = false;
};

inline stage_resources resources_for(stage s)
{
    return (s == stage::desktop) ? stage_resources{} : stage_resources{true, true, true, true};
}

/** Whether a four-finger slide pans workspaces from this stage. */
inline bool stage_slides(stage s) { return s != stage::workspaces_spread; }
}
