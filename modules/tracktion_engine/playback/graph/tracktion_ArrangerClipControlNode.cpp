/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion { inline namespace engine
{

ArrangerClipControlNode::ArrangerClipControlNode (AudioTrack& trackToUse,
                                                  std::unique_ptr<Node> inputNode)
    : track (trackToUse), input (std::move (inputNode))
{
    assert (input != nullptr);
    setOptimisations ({ tracktion::graph::ClearBuffers::yes,
                        tracktion::graph::AllocateAudioBuffer::yes });

    orderedNodes = transformNodes (*input, false);

    for (auto* node : orderedNodes)
        if (node->getDirectInputNodes().empty())
            leafNodes.push_back (node);
}

tracktion::graph::NodeProperties ArrangerClipControlNode::getNodeProperties()
{
    auto props = input->getNodeProperties();
    constexpr size_t seed = 10270521611998855823ULL;
    props.nodeID = hash (seed, props.nodeID);
    return props;
}

std::vector<tracktion::graph::Node*> ArrangerClipControlNode::getDirectInputNodes()
{
    return {};
}

std::vector<tracktion::graph::Node*> ArrangerClipControlNode::getInternalNodes()
{
    return orderedNodes;
}

void ArrangerClipControlNode::prepareToPlay (const PlaybackInitialisationInfo& info)
{
    auto internalInfo = info;
    internalInfo.allocateAudioBuffer = {};
    internalInfo.deallocateAudioBuffer = {};

    for (auto* node : orderedNodes)
        node->initialise (internalInfo);
}

bool ArrangerClipControlNode::isReadyToProcess()
{
    if (processInputForBlock)
        for (auto* leafNode : leafNodes)
            if (! leafNode->isReadyToProcess())
                return false;

    return true;
}

void ArrangerClipControlNode::prefetchBlock (juce::Range<int64_t> referenceSampleRange)
{
    processInputForBlock = ! track.playSlotClips.get();

    if (processInputForBlock)
        for (auto* node : orderedNodes)
            node->prepareForNextBlock (referenceSampleRange);
}

void ArrangerClipControlNode::process (ProcessContext& pc)
{
    if (! processInputForBlock)
        return;

    for (auto* node : orderedNodes)
        node->process (pc.numSamples, pc.referenceSampleRange);

    auto sourceBuffers = input->getProcessedOutput();
    copyIfNotAliased (pc.buffers.audio, sourceBuffers.audio);
    pc.buffers.midi.copyFrom (sourceBuffers.midi);
}

}} // namespace tracktion { inline namespace engine
