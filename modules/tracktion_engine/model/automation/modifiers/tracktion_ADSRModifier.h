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

/** A gated ADSR envelope modulator with per-segment curves and tempo sync. */
class ADSRModifier   : public Modifier,
                       private ValueTreeAllEventListener
{
public:
    ADSRModifier (Edit&, const juce::ValueTree&);
    ~ADSRModifier() override;

    using Ptr = juce::ReferenceCountedObjectPtr<ADSRModifier>;
    using Array = juce::ReferenceCountedArray<ADSRModifier>;

    using Modifier::initialise;
    void initialise() override;
    juce::String getName() const override               { return TRANS("ADSR"); }

    //==============================================================================
    /** The current envelope stage. Audio-domain state, not a parameter. */
    enum class Stage
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    //==============================================================================
    /** Returns the current value of the modifier (envelope * depth). */
    float getCurrentValue() override;

    /** Returns the envelope value before depth has been applied. */
    float getCurrentEnvelopeValue() const noexcept;

    /** Returns the stage the envelope is currently in. */
    Stage getCurrentStage() const noexcept;

    /** Returns the progress through the current stage between 0 & 1. */
    float getCurrentStagePhase() const noexcept;

    AutomatableParameter::ModifierAssignment* createAssignment (const juce::ValueTree&) override;

    ProcessingPosition getProcessingPosition() override { return ProcessingPosition::preFX; }

    void applyToBuffer (const PluginRenderContext&) override;

    /** Programmatically open the gate and enter the attack stage immediately.
        Performs an immediate retrigger instead of deferring to the next
        updateStreamTime() block, so it must be called from the audio thread.
        @param forceZeroValue  If true, the attack starts from 0 to create a transient
                               gap (used for cross-track sidechain). Self-triggered
                               envelopes should pass false to retrigger click-free
                               from the current envelope value. */
    void triggerNoteOn (bool forceZeroValue = true);

    //==============================================================================
    struct Assignment : public AutomatableParameter::ModifierAssignment
    {
        Assignment (const juce::ValueTree&, const ADSRModifier&);

        bool isForModifierSource (const ModifierSource&) const override;
        juce::ReferenceCountedObjectPtr<ADSRModifier> getModifier() const;

        const EditItemID adsrModifierID;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Assignment)
    };

    //==============================================================================
    juce::String getSelectableDescription() override    { return getName(); }

    //==============================================================================
    juce::CachedValue<float> attack, decay, sustain, release,
                             attackCurve, decayCurve, releaseCurve,
                             depth, syncType, tempoSync,
                             attackSync, decaySync, releaseSync;

    AutomatableParameter::Ptr attackParam, decayParam, sustainParam, releaseParam,
                              attackCurveParam, decayCurveParam, releaseCurveParam,
                              depthParam, syncTypeParam, tempoSyncParam,
                              attackSyncParam, decaySyncParam, releaseSyncParam;

    /** Gate the envelope. true = no note held. Closing the gate sends the
        envelope into its release stage; opening it retriggers the attack.
        Calling triggerNoteOn() automatically clears the gate. */
    void setGated (bool g)  { gated_.store (g, std::memory_order_release); }
    bool isGated() const    { return gated_.load (std::memory_order_acquire); }

    /** When true, applyToBuffer() ignores the destination track's own MIDI.
        Used for cross-track sidechain envelopes that are gated externally
        via setGated()/triggerNoteOn(). */
    void setSkipNativeResync (bool skip)  { skipNativeResync_.store (skip, std::memory_order_release); }
    bool getSkipNativeResync() const      { return skipNativeResync_.load (std::memory_order_acquire); }

    /** Kept identical to LFOModifier so MAGDA's trigger plumbing can treat
        both modifiers uniformly. In note mode the ADSR gate is always driven
        from the MIDI stream regardless of this flag. */
    void setGateOnTriggerSource (bool g)
    {
        gateOnTriggerSource_.store (g, std::memory_order_release);
        if (! g)
            gated_.store (false, std::memory_order_release);
    }
    bool getGateOnTriggerSource() const   { return gateOnTriggerSource_.load (std::memory_order_acquire); }

private:
    struct ADSRModifierTimer;
    std::unique_ptr<ADSRModifierTimer> modifierTimer;

    LambdaTimer changedTimer;
    std::atomic<float> currentValue { 0.0f }, currentEnvelopeValue { 0.0f }, stageProgress { 0.0f };
    std::atomic<int> currentStage { (int) Stage::idle };
    std::atomic<bool> gated_ { true };  // true = no note held
    std::atomic<bool> skipNativeResync_ { false };
    std::atomic<bool> gateOnTriggerSource_ { false };
    int heldNotes_ = 0;  // audio-thread only; touched from applyToBuffer

    void valueTreeChanged() override;
};

}} // namespace tracktion { inline namespace engine
