/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--.' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once

#include <algorithm>
#include <cmath>

#if __has_include(<choc/audio/choc_SampleBuffers.h>)
 #include <choc/audio/choc_SampleBuffers.h>
#else
 #include "../../../3rd_party/choc/audio/choc_SampleBuffers.h"
#endif

namespace tracktion { inline namespace engine
{

/** Removes a discontinuity from the start of a buffer without gain-fading the
    audio which follows it. The initial sample offset is returned smoothly to
    zero over fadeSamples, preserving any transient superimposed on that offset. */
inline void applyAudioStartDeClick (choc::buffer::ChannelArrayView<float> audio,
                                    int fadeSamples)
{
    const auto fadeLength = std::min (audio.getNumFrames(),
                                      static_cast<choc::buffer::FrameCount> (std::max (0, fadeSamples)));

    if (fadeLength == 0)
        return;

    for (choc::buffer::ChannelCount channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto dest = audio.getIterator (channel).sample;
        const auto discontinuity = dest[0];

        if (discontinuity == 0.0f)
            continue;

        if (fadeLength == 1)
        {
            dest[0] = 0.0f;
            continue;
        }

        for (choc::buffer::FrameCount i = 0; i < fadeLength; ++i)
        {
            const auto phase = static_cast<float> (i) / static_cast<float> (fadeLength - 1);
            constexpr auto pi = 3.14159265358979323846f;
            const auto correction = 0.5f * (1.0f + std::cos (pi * phase));
            dest[i] -= discontinuity * correction;
        }
    }
}

}} // namespace tracktion::engine
