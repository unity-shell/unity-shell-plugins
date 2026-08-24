#pragma once

#include "config.hpp"

namespace spatial
{
/* Derived from g for the panel and a couple of gates, never stored as behavior. */
enum class phase { desktop, apps, wall };

/* Hysteresis on the up-crossing so jitter near g==1 does not flap the phase. */
inline phase phase_of(double g, phase current)
{
    if (g <= 0.0) { return phase::desktop; }
    if (current == phase::wall) { return (g <= 1.0) ? phase::apps : phase::wall; }
    return (g >= 1.0 + STAGE_HYSTERESIS) ? phase::wall : phase::apps;
}

/* Not on the wall: it already shows every workspace. */
inline bool slides(phase p) { return p != phase::wall; }

/* Panel event names. Keep stable for the shell. */
inline const char *phase_name(phase p)
{
    return (p == phase::apps) ? "apps_spread" :
           (p == phase::wall) ? "workspaces_spread" : "desktop";
}
}
