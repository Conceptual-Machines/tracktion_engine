/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once

namespace tracktion { inline namespace engine
{

/** Owns an arrangement clip's audio-reader subgraph and only processes it while
    the arrangement owns the track. The wrapper remains in the main graph so
    clip-level parallelism and the arranger/launcher switching topology are
    preserved while inaudible readers do no real-time work. */
class ArrangerClipControlNode final : public tracktion::graph::Node
{
public:
    ArrangerClipControlNode (AudioTrack&, std::unique_ptr<Node>);

    tracktion::graph::NodeProperties getNodeProperties() override;
    std::vector<Node*> getDirectInputNodes() override;
    std::vector<Node*> getInternalNodes() override;
    void prepareToPlay (const PlaybackInitialisationInfo&) override;
    bool isReadyToProcess() override;
    void prefetchBlock (juce::Range<int64_t>) override;
    void process (ProcessContext&) override;

private:
    AudioTrack& track;
    std::unique_ptr<Node> input;
    std::vector<Node*> orderedNodes, leafNodes;
    bool processInputForBlock = false;
};

}} // namespace tracktion { inline namespace engine
