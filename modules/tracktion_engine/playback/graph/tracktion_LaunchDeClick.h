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
#include <vector>

#if __has_include(<choc/audio/choc_SampleBuffers.h>)
 #include <choc/audio/choc_SampleBuffers.h>
#else
 #include "../../../3rd_party/choc/audio/choc_SampleBuffers.h"
#endif

namespace tracktion { inline namespace engine
{

class AudioStartDeClick
{
public:
    void prepare (size_t numChannels)
    {
        discontinuities.resize (numChannels);
        reset();
    }

    void reset() noexcept
    {
        fadeSamples = 0;
        samplesProcessed = 0;
    }

    void begin (choc::buffer::ChannelArrayView<float> audio, int newFadeSamples)
    {
        reset();

        if (newFadeSamples <= 0 || audio.getNumFrames() == 0)
            return;

        if (discontinuities.size() < audio.getNumChannels())
            return;

        fadeSamples = newFadeSamples;

        for (choc::buffer::ChannelCount channel = 0; channel < audio.getNumChannels(); ++channel)
            discontinuities[channel] = audio.getIterator (channel).sample[0];

        process (audio);
    }

    void process (choc::buffer::ChannelArrayView<float> audio)
    {
        if (fadeSamples <= 0 || samplesProcessed >= fadeSamples)
            return;

        if (discontinuities.size() < audio.getNumChannels())
        {
            reset();
            return;
        }

        const auto fadeLength = std::min (audio.getNumFrames(),
                                          static_cast<choc::buffer::FrameCount> (fadeSamples - samplesProcessed));

        for (choc::buffer::ChannelCount channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            auto dest = audio.getIterator (channel).sample;
            const auto discontinuity = discontinuities[channel];

            if (discontinuity == 0.0f)
                continue;

            for (choc::buffer::FrameCount i = 0; i < fadeLength; ++i)
            {
                const auto sample = samplesProcessed + static_cast<int> (i);
                const auto phase = fadeSamples == 1 ? 0.0f
                                                    : static_cast<float> (sample) / static_cast<float> (fadeSamples - 1);
                constexpr auto pi = 3.14159265358979323846f;
                const auto correction = 0.5f * (1.0f + std::cos (pi * phase));
                dest[i] -= discontinuity * correction;
            }
        }

        samplesProcessed += static_cast<int> (fadeLength);

        if (samplesProcessed >= fadeSamples)
            reset();
    }

    size_t getNumChannels() const noexcept
    {
        return discontinuities.size();
    }

private:
    std::vector<float> discontinuities;
    int fadeSamples = 0;
    int samplesProcessed = 0;
};

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

        for (choc::buffer::FrameCount i = 0; i < fadeLength; ++i)
        {
            const auto phase = fadeLength == 1 ? 0.0f
                                               : static_cast<float> (i) / static_cast<float> (fadeLength - 1);
            constexpr auto pi = 3.14159265358979323846f;
            const auto correction = 0.5f * (1.0f + std::cos (pi * phase));
            dest[i] -= discontinuity * correction;
        }
    }
}

}} // namespace tracktion::engine
