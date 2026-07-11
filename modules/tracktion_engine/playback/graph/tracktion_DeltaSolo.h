/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once

namespace tracktion { inline namespace engine
{

namespace plugin_node_detail
{
template<typename OutputView, typename DryView>
void applyDeltaSolo (OutputView output, const DryView& dry, bool enabled)
{
    if (! enabled)
        return;

    const auto size = dry.getSize();
    CHOC_ASSERT (size == output.getSize());

    for (decltype (size.numChannels) channel = 0; channel < size.numChannels; ++channel)
    {
        auto outputSample = output.getIterator (channel);
        auto drySample = dry.getIterator (channel);

        for (decltype (size.numFrames) frame = 0; frame < size.numFrames; ++frame)
        {
            *outputSample -= drySample.get();
            ++outputSample;
            ++drySample;
        }
    }
}
}

}} // namespace tracktion { inline namespace engine
