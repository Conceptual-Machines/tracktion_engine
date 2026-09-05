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

/**
    Sends an input Node to a Rack bus handling the channel mapping and channel gain levels.
*/
class RackInstanceNode final    : public tracktion::graph::Node,
                                  public TracktionEngineNode
{
public:
    using ChannelMap = std::array<std::tuple<int, int, AutomatableParameter::Ptr>, 2>;

    /** Creates a RackInstanceNode that maps an input node channel to an output channel
        and applies a gain parameter to each mapped channel.
    */
    RackInstanceNode (RackInstance::Ptr, std::unique_ptr<Node>, ChannelMap channelMap,
                      ProcessState&, SampleRateAndBlockSize);
    ~RackInstanceNode() override;

    std::vector<Node*> getDirectInputNodes() override;
    tracktion::graph::NodeProperties getNodeProperties() override;
    void prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo&) override;
    bool isReadyToProcess() override;
    void prefetchBlock (juce::Range<int64_t>) override;
    void preProcess (choc::buffer::FrameCount, juce::Range<int64_t>) override;
    void process (ProcessContext&) override;

private:
    //==============================================================================
    RackInstance::Ptr plugin;
    std::unique_ptr<Node> input;
    ChannelMap channelMap;
    TimeDuration automationAdjustmentTime;
    int maxNumChannels = 0;
    float lastGain[2];
    bool canUseSourceBuffers = false, isInitialised = false;
};


//==============================================================================
/** Bus IDs a Rack's own graph returns its input from and sends its output to. */
int getRackInputBusID (EditItemID rackID);
int getRackOutputBusID (EditItemID rackID);

/** Routes a Node through a Rack's graph via that pair of buses, applying the
    instance's channel maps, gains and delta solo.

    Used both for an instance in a track's plugin list and for one nested in
    another Rack's chain, which is otherwise built as a plain PluginNode over a
    plugin whose applyToBuffer does nothing.
*/
std::unique_ptr<tracktion::graph::Node> createNodeForRackInstance (RackInstance&,
                                                                   std::unique_ptr<tracktion::graph::Node>,
                                                                   ProcessState&,
                                                                   SampleRateAndBlockSize);

}} // namespace tracktion { inline namespace engine
