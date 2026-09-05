/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion { inline namespace engine
{

RackReturnNode::RackReturnNode (std::unique_ptr<Node> wetNode,
                                std::function<float()> wetGainFunc,
                                Node* dryNode,
                                std::function<float()> dryGainFunc)
    : wetInput (std::move (wetNode)),
      uncompensatedDryInput (dryNode),
      dryInput (dryNode),
      wetGainFunction (std::move (wetGainFunc)),
      dryGainFunction (std::move (dryGainFunc))
{
    assert (wetInput);
    assert (wetGainFunction);
    assert (dryInput);
    assert (dryGainFunction);

    lastWetGain = wetGainFunction();
    lastDryGain = dryGainFunction();
}

std::vector<tracktion::graph::Node*> RackReturnNode::getDirectInputNodes()
{
    return { wetInput.get(), dryInput };
}

tracktion::graph::NodeProperties RackReturnNode::getNodeProperties()
{
    auto wetProps = wetInput->getNodeProperties();
    auto dryProps = dryInput->getNodeProperties();

    auto props = wetProps;
    props.hasAudio = true;
    props.numberOfChannels = std::max (wetProps.numberOfChannels, dryProps.numberOfChannels);
    props.latencyNumSamples = std::max (wetProps.latencyNumSamples, dryProps.latencyNumSamples);
    hash_combine (props.nodeID, dryProps.nodeID);

    constexpr size_t rackReturnNodeMagicHash = size_t (0x726b52657475726e);

    if (props.nodeID != 0)
        hash_combine (props.nodeID, rackReturnNodeMagicHash);

    return props;
}

TransformResult RackReturnNode::transform (TransformOptions& options)
{
    if (options.disableLatencyCompensation)
        return TransformResult::none;

    // Sized on every pass rather than latched on the first. The wet path runs
    // through the Rack's send/return buses, which connect during the transform
    // too, so a Rack instance can be visited while its wet latency is still
    // short by whatever precedes it. That happens to a nested instance, whose
    // Rack's own input return is in a sibling graph that may be visited later.
    const auto wetLatency = wetInput->getNodeProperties().latencyNumSamples;
    const auto dryLatency = uncompensatedDryInput->getNodeProperties().latencyNumSamples;
    const auto required = std::max (0, wetLatency - dryLatency);

    if (required == compensationNumSamples)
        return TransformResult::none;

    const bool replacing = dryLatencyNode != nullptr;
    compensationNumSamples = required;

    if (required == 0)
    {
        dryInput = uncompensatedDryInput;
        dryLatencyNode.reset();
    }
    else
    {
        auto node = tracktion::graph::makeNode<tracktion::graph::LatencyNode> (uncompensatedDryInput, required);
        dryInput = node.get();
        dryLatencyNode = std::move (node);
    }

    // The node that was standing here is gone, and the caller's ordering still
    // holds a pointer to it.
    return replacing ? TransformResult::nodesDeleted
                     : TransformResult::connectionsMade;
}

void RackReturnNode::prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo&)
{
    if (wetInput->numOutputNodes > 1)
        return;

    const auto inputNumChannels = wetInput->getNodeProperties().numberOfChannels;
    const auto desiredNumChannels = getNodeProperties().numberOfChannels;

    if (inputNumChannels >= desiredNumChannels)
    {
        canUseWetSourceBuffers = true;
        setOptimisations ({ tracktion::graph::ClearBuffers::no,
                            tracktion::graph::AllocateAudioBuffer::no });
    }
}

bool RackReturnNode::isReadyToProcess()
{
    return wetInput->hasProcessed() && dryInput->hasProcessed();
}

void RackReturnNode::preProcess (choc::buffer::FrameCount, juce::Range<int64_t>)
{
    if (canUseWetSourceBuffers)
        setBufferViewToUse (wetInput.get(), wetInput->getProcessedOutput().audio);
}

void RackReturnNode::process (ProcessContext& pc)
{
    auto destAudio = pc.buffers.audio;

    auto wetSource = wetInput->getProcessedOutput();
    auto drySource = dryInput->getProcessedOutput();

    assert (destAudio.getNumFrames() == wetSource.audio.getNumFrames());
    assert (wetSource.audio.getNumFrames() == drySource.audio.getNumFrames());

    const float wetGain = wetGainFunction();
    const float dryGain = dryGainFunction();


    // Always copy MIDI (N.B. MIDI is always the wet signal with no gain applied)
    pc.buffers.midi.copyFrom (wetSource.midi);


    // Copy wet audio applying gain
    copyIfNotAliased (destAudio.getFirstChannels (wetSource.audio.getNumChannels()),
                      wetSource.audio);

    if (wetGain == lastWetGain)
    {
        if (wetGain != 1.0f)
            applyGain (destAudio, wetGain);
    }
    else
    {
        const auto step = (wetGain - lastWetGain) / destAudio.getNumFrames();
        applyGainPerFrame (destAudio, [start = lastWetGain, step]() mutable { return start += step; });

        lastWetGain = wetGain;
    }


    // Add dry audio applying gain
    auto dryDestView = destAudio.getFirstChannels (drySource.audio.getNumChannels());

    if (dryGain == lastDryGain)
    {
        if (dryGain == 1.0f)
            add (dryDestView, drySource.audio);
        else
            tracktion::graph::add (dryDestView, drySource.audio, dryGain);
    }
    else
    {
        tracktion::graph::addApplyingGainRamp (dryDestView, drySource.audio, lastDryGain, dryGain);
        lastDryGain = dryGain;
    }
}

}} // namespace tracktion { inline namespace engine
