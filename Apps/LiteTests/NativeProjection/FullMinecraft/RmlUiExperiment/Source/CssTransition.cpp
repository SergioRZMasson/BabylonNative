#include "CssTransition.h"
#include <algorithm>
#include <cmath>

namespace
{
    double Cubic(double parameter, double first, double second)
    {
        const double inverse = 1 - parameter;
        return 3 * inverse * inverse * parameter * first +
               3 * inverse * parameter * parameter * second + parameter * parameter * parameter;
    }

    double Progress(const MinecraftUi::OpacityTransition& transition, double seconds)
    {
        return transition.duration > 0
                   ? std::clamp((seconds - transition.start) / transition.duration, 0.0, 1.0)
                   : 1;
    }
}

namespace MinecraftUi
{
    double CssEase(double progress)
    {
        if (progress <= 0 || progress >= 1)
        {
            return std::clamp(progress, 0.0, 1.0);
        }
        double lower{};
        double upper{1};
        for (unsigned iteration = 0; iteration < 48; ++iteration)
        {
            const double parameter = (lower + upper) * 0.5;
            if (Cubic(parameter, 0.25, 0.25) < progress)
            {
                lower = parameter;
            }
            else
            {
                upper = parameter;
            }
        }
        return Cubic((lower + upper) * 0.5, 0.1, 1);
    }

    bool SetOpacityTarget(OpacityTransition& transition, double alpha)
    {
        alpha = std::clamp(alpha, 0.0, 1.0);
        if (!transition.initialized || transition.configuredDuration <= 0)
        {
            transition.value = alpha;
            transition.end = alpha;
            transition.reversingStart = alpha;
            transition.pending = false;
            transition.active = false;
            return true;
        }
        transition.pending = true;
        transition.pendingEnd = alpha;
        return false;
    }

    bool AdvanceOpacity(OpacityTransition& transition, double seconds)
    {
        const double previous = transition.value;
        if (!transition.initialized)
        {
            transition.initialized = true;
            return false;
        }
        const bool interrupted = transition.active;
        const double easedProgress = transition.active ? CssEase(Progress(transition, seconds)) : 1;
        if (transition.active)
        {
            transition.value = transition.from + (transition.end - transition.from) * easedProgress;
            if (Progress(transition, seconds) >= 1)
            {
                transition.active = false;
            }
        }
        if (transition.pending)
        {
            transition.pending = false;
            if (transition.pendingEnd != transition.end)
            {
                double factor{1};
                double reversingStart = transition.value;
                if (interrupted && transition.pendingEnd == transition.reversingStart)
                {
                    // CSS Transitions' reversing-adjusted start value and shortening factor.
                    factor = std::clamp(std::abs(easedProgress * transition.reversingFactor +
                                                 (1 - transition.reversingFactor)),
                                        0.0, 1.0);
                    reversingStart = transition.end;
                }
                transition.from = transition.value;
                transition.end = transition.pendingEnd;
                transition.reversingStart = reversingStart;
                transition.reversingFactor = factor;
                transition.duration = transition.configuredDuration * factor;
                transition.start = seconds;
                transition.active = transition.from != transition.end && transition.duration > 0;
                if (!transition.active)
                {
                    transition.value = transition.end;
                }
            }
        }
        return transition.value != previous;
    }
}
