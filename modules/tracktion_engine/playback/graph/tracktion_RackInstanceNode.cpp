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

RackInstanceNode::RackInstanceNode (RackInstance::Ptr ri, std::unique_ptr<Node> inputNode, ChannelMap channelMapToUse,
                                    ProcessState& ps, SampleRateAndBlockSize info)
    : TracktionEngineNode (ps),
      plugin (std::move (ri)), input (std::move (inputNode)), channelMap (std::move (channelMapToUse))
{
    assert (plugin);
    assert (input);

    for (auto& chan : channelMap)
    {
        assert (std::get<2> (chan) != nullptr);
        maxNumChannels = std::max (maxNumChannels,
                                   std::get<1> (chan) + 1);
    }

    for (size_t chan = 0; chan < 2; ++chan)
        lastGain[chan] = dbToGain (std::get<2> (channelMap[chan])->getCurrentValue());

    plugin->baseClassInitialise ({ TimePosition(), info.sampleRate, info.blockSize });
    isInitialised = true;
}

RackInstanceNode::~RackInstanceNode()
{
    if (isInitialised && ! plugin->baseClassNeedsInitialising())
        plugin->baseClassDeinitialise();
}

std::vector<tracktion::graph::Node*> RackInstanceNode::getDirectInputNodes()
{
    return { input.get() };
}

tracktion::graph::NodeProperties RackInstanceNode::getNodeProperties()
{
    auto props = input->getNodeProperties();
    props.numberOfChannels = (int) maxNumChannels;
    props.hasMidi = true;
    props.hasAudio = true;

    if (props.nodeID != 0)
        hash_combine (props.nodeID, static_cast<size_t> (3615177560026405684)); // "RackInstanceNode"

    return props;
}

void RackInstanceNode::prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo& info)
{
    if (input->numOutputNodes > 1)
        return;

    auto props = getNodeProperties();

    if (props.latencyNumSamples > 0)
        automationAdjustmentTime = TimeDuration::fromSamples (-props.latencyNumSamples, info.sampleRate);

    const auto inputNumChannels = input->getNodeProperties().numberOfChannels;
    const auto desiredNumChannels = props.numberOfChannels;

    if (info.enableNodeMemorySharing && inputNumChannels >= desiredNumChannels)
    {
        canUseSourceBuffers = true;
        setOptimisations ({ tracktion::graph::ClearBuffers::no,
                            tracktion::graph::AllocateAudioBuffer::no });
    }
}

bool RackInstanceNode::isReadyToProcess()
{
    return input->hasProcessed();
}

void RackInstanceNode::preProcess (choc::buffer::FrameCount, juce::Range<int64_t>)
{
    if (canUseSourceBuffers)
        setBufferViewToUse (input.get(), input->getProcessedOutput().audio);
}

void RackInstanceNode::prefetchBlock (juce::Range<int64_t>)
{
    // This updates automation for the RackInstance gains etc.
    plugin->prepareForNextBlock (getEditTimeRange().getStart() + automationAdjustmentTime);
}

void RackInstanceNode::process (ProcessContext& pc)
{
    assert ((int) pc.buffers.audio.getNumChannels() == maxNumChannels);
    auto inputBuffers = input->getProcessedOutput();

    // Always copy MIDI
    pc.buffers.midi.copyFrom (inputBuffers.midi);

    // Copy audio applying gain
    int channel = 0;

    for (auto& chan : channelMap)
    {
        auto srcChan = std::get<0> (chan);
        auto destChan = std::get<1> (chan);

        if (srcChan < 0)
            continue;

        if (destChan < 0)
            continue;

        if ((choc::buffer::ChannelCount) srcChan >= inputBuffers.audio.getNumChannels())
            continue;

        auto src = inputBuffers.audio.getChannel ((choc::buffer::ChannelCount) srcChan);
        auto dest = pc.buffers.audio.getChannel ((choc::buffer::ChannelCount) destChan);
        auto gain = dbToGain (std::get<2> (chan)->getCurrentValue());

        copyIfNotAliased (dest, src);

        if (gain == lastGain[channel])
        {
            if (gain != 1.0f)
                applyGain (dest, gain);
        }
        else
        {
            juce::SmoothedValue<float> smoother (lastGain[channel]);
            smoother.setTargetValue (gain);
            smoother.reset ((int) dest.getNumFrames());
            applyGainPerFrame (dest, [&] { return smoother.getNextValue(); });

            lastGain[channel] = gain;
        }

        ++channel;
    }
}


//==============================================================================
//==============================================================================
int getRackInputBusID (EditItemID rackID)
{
    constexpr size_t rackInputMagicNum = 0x7261636b496e;
    return static_cast<int> (hash (rackInputMagicNum, rackID.getRawID()));
}

int getRackOutputBusID (EditItemID rackID)
{
    constexpr size_t rackOutputMagicNum = 0x7261636b4f7574;
    return static_cast<int> (hash (rackOutputMagicNum, rackID.getRawID()));
}

std::unique_ptr<tracktion::graph::Node> createNodeForRackInstance (RackInstance& rackInstance,
                                                                   std::unique_ptr<tracktion::graph::Node> node,
                                                                   ProcessState& processState,
                                                                   SampleRateAndBlockSize sampleRateAndBlockSize)
{
    using namespace tracktion::graph;

    jassert (node != nullptr);

    if (! rackInstance.isEnabled())
        return node;

    const auto rackInputID = getRackInputBusID (rackInstance.rackTypeID);
    const auto rackOutputID = getRackOutputBusID (rackInstance.rackTypeID);

    // The input to the instance is referenced by the dry signal path
    auto* inputNode = node.get();

    // Send
    // N.B. the channel indicies from the RackInstance start a 1 so we need to subtract this to get a 0-indexed channel
    RackInstanceNode::ChannelMap sendChannelMap;
    sendChannelMap[0] = { 0, rackInstance.leftInputGoesTo - 1, rackInstance.leftInDb };
    sendChannelMap[1] = { 1, rackInstance.rightInputGoesTo - 1, rackInstance.rightInDb };
    node = makeNode<RackInstanceNode> (rackInstance, std::move (node), std::move (sendChannelMap), processState, sampleRateAndBlockSize);
    node = makeNode<SendNode> (std::move (node), rackInputID);
    node = makeNode<ReturnNode> (makeNode<SinkNode> (std::move (node)), rackOutputID);

    // Return
    RackInstanceNode::ChannelMap returnChannelMap;
    returnChannelMap[0] = { rackInstance.leftOutputComesFrom - 1, 0, rackInstance.leftOutDb };
    returnChannelMap[1] = { rackInstance.rightOutputComesFrom - 1, 1, rackInstance.rightOutDb };
    node = makeNode<RackInstanceNode> (rackInstance, std::move (node), std::move (returnChannelMap), processState, sampleRateAndBlockSize);

    RackInstance::Ptr rack (&rackInstance);
    return makeNode<RackReturnNode> (std::move (node),
                                     [rack, wetGain = rackInstance.wetGain]
                                     {
                                         return rack->isDeltaSoloEnabled() ? 1.0f : wetGain->getCurrentValue();
                                     },
                                     inputNode,
                                     [rack, dryGain = rackInstance.dryGain]
                                     {
                                         return rack->isDeltaSoloEnabled() ? -1.0f : dryGain->getCurrentValue();
                                     });
}

}} // namespace tracktion { inline namespace engine
