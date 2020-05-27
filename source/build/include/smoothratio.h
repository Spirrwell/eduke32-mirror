// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
//
// This file has been modified from Ken Silverman's original release
// by the EDuke32 team (development@voidpoint.com)

#ifndef SMOOTHRATIO_H
#define SMOOTHRATIO_H

#include "compat.h"
#include "clockticks.hpp"

static FORCE_INLINE int32_t calc_smoothratio(ClockTicks const totalclk, ClockTicks const ototalclk, int32_t ticsPerSec, int32_t gameTicsPerSec)
{
    int const   truncrfreq = Blrintf(floorf(refreshfreq * ticsPerSec / timerGetClockRate()));
    int const   clk        = (totalclk - ototalclk).toScale16();
    float const fracTics   = clk * truncrfreq * (1.f / (65536.f * ticsPerSec));

    return clamp(tabledivide32_noinline(Blrintf(65536 * fracTics * gameTicsPerSec), truncrfreq), 0, 65536);
}

#endif
