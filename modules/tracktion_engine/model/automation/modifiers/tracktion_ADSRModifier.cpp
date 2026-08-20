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

struct ADSRModifier::ADSRModifierTimer    : public ModifierTimer
{
    ADSRModifierTimer (ADSRModifier& adsr)
        : modifier (adsr)
    {
    }

    using Stage = ADSRModifier::Stage;

    void updateStreamTime (TimePosition editTime, int numSamples) override
    {
        using namespace ModifierCommon;
        const double blockLength = numSamples / modifier.getSampleRate();
        modifier.setEditTime (editTime);
        modifier.updateParameterStreams (editTime);

        const auto syncTypeThisBlock = getTypedParamValue<SyncType> (*modifier.syncTypeParam);
        const bool tempoSyncOn = getBoolParamValue (*modifier.tempoSyncParam);

        // Recompute the bar length every block so live tempo changes track
        double secondsPerBar = 0.0;

        if (tempoSyncOn)
        {
            tempoSequence.set (editTime);
            const auto currentTempo = tempoSequence.getTempo();
            const auto currentTimeSig = tempoSequence.getTimeSignature();
            // MAGDA fix: a bar is the numerator counted in the denominator's note
            // value, and a beat is a quarter note, so 6/8 is three beats and not
            // six. Using the numerator alone made every synced modifier in an x/8
            // signature run long by 4/denominator (#2128).
            secondsPerBar = (60.0 / currentTempo) * (4.0 * currentTimeSig.numerator / currentTimeSig.denominator);
        }

        auto stageDuration = [&] (const AutomatableParameter& msParam, const AutomatableParameter& syncParam) -> double
        {
            if (tempoSyncOn)
            {
                const auto division = getTypedParamValue<RateType> (syncParam);

                // A division of hertz means "no division", fall back to the ms param
                if (division != hertz)
                    return getBarFraction (division) * secondsPerBar;
            }

            return msParam.getCurrentValue() / 1000.0;
        };

        attackSeconds   = stageDuration (*modifier.attackParam,  *modifier.attackSyncParam);
        decaySeconds    = stageDuration (*modifier.decayParam,   *modifier.decaySyncParam);
        releaseSeconds  = stageDuration (*modifier.releaseParam, *modifier.releaseSyncParam);
        sustainLevel    = juce::jlimit (0.0f, 1.0f, modifier.sustainParam->getCurrentValue());

        // Free mode is free-running: the gate is always open so the envelope
        // cycles A -> D -> R continuously. Transport mode follows playback
        // (N.B. isPlaying() reads a cached bool, the race with the message
        // thread is benign for a per-block gate). Note mode follows the
        // MIDI/external gate.
        bool gateOpen = true;

        if (syncTypeThisBlock == ModifierCommon::transport)
            gateOpen = modifier.edit.getTransport().isPlaying();
        else if (syncTypeThisBlock == ModifierCommon::note)
            gateOpen = ! modifier.gated_.load (std::memory_order_acquire);

        // Edge detection: retrigger/release from the current envelope value so
        // gate changes are click-free from any stage
        if (gateOpen && ! lastGate)
            enterStage (Stage::attack, envValue);
        else if (! gateOpen && lastGate && stage != Stage::idle)
            enterStage (Stage::release, envValue);

        lastGate = gateOpen;

        advance (blockLength, syncTypeThisBlock == ModifierCommon::free, gateOpen);
        publish();
    }

    /** Zero-delay retrigger for the external sidechain path.
        Audio thread only, like updateStreamTime(). */
    void noteOn (bool forceZeroValue)
    {
        const float startValue = forceZeroValue ? 0.0f : envValue;
        enterStage (Stage::attack, startValue);
        envValue = startValue;

        // The gate was opened by the caller; mark it as seen so the next
        // block's edge detection doesn't retrigger a second time
        lastGate = true;
        publish();
    }

    void enterStage (Stage newStage, float startValue)
    {
        stage = newStage;
        timeInStage = 0.0;
        stagePhase = 0.0f;
        stageStartValue = startValue;
    }

    static float interpolateSegment (float t, float startValue, float endValue, float curve)
    {
        t = juce::jlimit (0.0f, 1.0f, t);

        if (curve == 0.0f || startValue == endValue)
            return startValue + (endValue - startValue) * t;

        const juce::Point<float> start { 0.0f, startValue };
        const juce::Point<float> end { 1.0f, endValue };
        const auto control = BezierHelpers::getQuadraticControlPoint (start, end, curve);

        return BezierHelpers::getQuadraticYFromX (t, start, control, end);
    }

    void advance (double dt, bool freeRunning, bool gateOpen)
    {
        constexpr double minStageSeconds = 0.0001;

        // A single block can step through several stages when durations are
        // very short. The guard bounds the loop when all durations are zero.
        for (int guard = 8; --guard >= 0;)
        {
            switch (stage)
            {
                case Stage::idle:
                {
                    envValue = 0.0f;
                    stagePhase = 0.0f;

                    // Free mode loops while the gate is open
                    if (freeRunning && gateOpen)
                    {
                        enterStage (Stage::attack, 0.0f);
                        continue;
                    }

                    return;
                }

                case Stage::attack:
                {
                    if (attackSeconds <= minStageSeconds)
                    {
                        envValue = 1.0f;
                        enterStage (Stage::decay, 1.0f);
                        continue;
                    }

                    timeInStage += dt;

                    if (timeInStage >= attackSeconds)
                    {
                        dt = timeInStage - attackSeconds;
                        envValue = 1.0f;
                        enterStage (Stage::decay, 1.0f);
                        continue;
                    }

                    stagePhase = (float) (timeInStage / attackSeconds);
                    envValue = interpolateSegment (stagePhase, stageStartValue, 1.0f,
                                                   modifier.attackCurveParam->getCurrentValue());
                    return;
                }

                case Stage::decay:
                {
                    if (decaySeconds <= minStageSeconds)
                    {
                        envValue = sustainLevel;
                        enterStage (Stage::sustain, sustainLevel);
                        continue;
                    }

                    timeInStage += dt;

                    if (timeInStage >= decaySeconds)
                    {
                        dt = timeInStage - decaySeconds;
                        envValue = sustainLevel;
                        enterStage (Stage::sustain, sustainLevel);
                        continue;
                    }

                    stagePhase = (float) (timeInStage / decaySeconds);
                    envValue = interpolateSegment (stagePhase, stageStartValue, sustainLevel,
                                                   modifier.decayCurveParam->getCurrentValue());
                    return;
                }

                case Stage::sustain:
                {
                    // Track the sustain parameter live while held
                    envValue = sustainLevel;
                    stagePhase = 0.0f;

                    // Free mode skips the hold so it cycles A -> D -> R
                    if (freeRunning)
                    {
                        enterStage (Stage::release, envValue);
                        continue;
                    }

                    return;
                }

                case Stage::release:
                {
                    if (releaseSeconds <= minStageSeconds)
                    {
                        envValue = 0.0f;
                        enterStage (Stage::idle, 0.0f);
                        continue;
                    }

                    timeInStage += dt;

                    if (timeInStage >= releaseSeconds)
                    {
                        dt = timeInStage - releaseSeconds;
                        envValue = 0.0f;
                        enterStage (Stage::idle, 0.0f);
                        continue;
                    }

                    stagePhase = (float) (timeInStage / releaseSeconds);
                    envValue = interpolateSegment (stagePhase, stageStartValue, 0.0f,
                                                   modifier.releaseCurveParam->getCurrentValue());
                    return;
                }
            }
        }
    }

    void publish()
    {
        jassert (! std::isnan (envValue));
        modifier.currentEnvelopeValue.store (envValue, std::memory_order_release);
        modifier.currentValue.store (envValue * modifier.depthParam->getCurrentValue(), std::memory_order_release);
        modifier.currentStage.store ((int) stage, std::memory_order_release);
        modifier.stageProgress.store (stagePhase, std::memory_order_release);
    }

    ADSRModifier& modifier;
    tempo::Sequence::Position tempoSequence { createPosition (modifier.edit.tempoSequence) };

    // Audio-thread only state, touched from updateStreamTime() and noteOn()
    Stage stage = Stage::idle;
    double timeInStage = 0.0;
    double attackSeconds = 0.0, decaySeconds = 0.0, releaseSeconds = 0.0;
    float sustainLevel = 0.0f;
    float stageStartValue = 0.0f;
    float stagePhase = 0.0f;
    float envValue = 0.0f;
    bool lastGate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ADSRModifierTimer)
};

//==============================================================================
ADSRModifier::ADSRModifier (Edit& e, const juce::ValueTree& v)
    : Modifier (e, v)
{
    auto um = &edit.getUndoManager();

    attack.referTo (state, IDs::attack, um, 10.0f);
    decay.referTo (state, IDs::decay, um, 200.0f);
    sustain.referTo (state, IDs::sustain, um, 0.7f);
    release.referTo (state, IDs::release, um, 300.0f);
    attackCurve.referTo (state, IDs::attackCurve, um, 0.0f);
    decayCurve.referTo (state, IDs::decayCurve, um, 0.0f);
    releaseCurve.referTo (state, IDs::releaseCurve, um, 0.0f);
    depth.referTo (state, IDs::depth, um, 1.0f);
    syncType.referTo (state, IDs::syncType, um, float (ModifierCommon::free));
    tempoSync.referTo (state, IDs::tempoSync, um, 0.0f);
    attackSync.referTo (state, IDs::attackSync, um, float (ModifierCommon::quarter));
    decaySync.referTo (state, IDs::decaySync, um, float (ModifierCommon::quarter));
    releaseSync.referTo (state, IDs::releaseSync, um, float (ModifierCommon::quarter));

    auto addDiscreteParam = [this] (const juce::String& paramID, const juce::String& name,
                                    juce::Range<float> valueRange, juce::CachedValue<float>& val,
                                    const juce::StringArray& labels) -> AutomatableParameter*
    {
        auto* p = new DiscreteLabelledParameter (paramID, name, *this, valueRange, labels.size(), labels);
        addAutomatableParameter (p);
        p->attachToCurrentValue (val);

        return p;
    };

    auto addParam = [this] (const juce::String& paramID, const juce::String& name,
                            juce::NormalisableRange<float> valueRange, float centreVal,
                            juce::CachedValue<float>& val, const juce::String& suffix) -> AutomatableParameter*
    {
        valueRange.setSkewForCentre (centreVal);
        auto* p = new SuffixedParameter (paramID, name, *this, valueRange, suffix);
        addAutomatableParameter (p);
        p->attachToCurrentValue (val);

        return p;
    };

    using namespace ModifierCommon;
    attackParam         = addParam ("attack",               TRANS("Attack"),        { 0.0f, 30000.0f }, 200.0f,                             attack,         "ms");
    decayParam          = addParam ("decay",                TRANS("Decay"),         { 0.0f, 30000.0f }, 500.0f,                             decay,          "ms");
    sustainParam        = addParam ("sustain",              TRANS("Sustain"),       { 0.0f, 1.0f }, 0.5f,                                   sustain,        {});
    releaseParam        = addParam ("release",              TRANS("Release"),       { 0.0f, 30000.0f }, 500.0f,                             release,        "ms");
    attackCurveParam    = addParam ("attackCurve",          TRANS("Attack curve"),  { -0.5f, 0.5f }, 0.0f,                                  attackCurve,    {});
    decayCurveParam     = addParam ("decayCurve",           TRANS("Decay curve"),   { -0.5f, 0.5f }, 0.0f,                                  decayCurve,     {});
    releaseCurveParam   = addParam ("releaseCurve",         TRANS("Release curve"), { -0.5f, 0.5f }, 0.0f,                                  releaseCurve,   {});
    depthParam          = addParam ("depth",                TRANS("Depth"),         { 0.0f, 1.0f }, 0.5f,                                   depth,          {});
    syncTypeParam       = addDiscreteParam ("syncType",     TRANS("Sync type"),     { 0.0f, (float) getSyncTypeChoices().size() - 1 },      syncType,       getSyncTypeChoices());
    tempoSyncParam      = addDiscreteParam ("tempoSync",    TRANS("Tempo sync"),    { 0.0f, 1.0f },                                         tempoSync,      { NEEDS_TRANS("Off"), NEEDS_TRANS("On") });
    attackSyncParam     = addDiscreteParam ("attackSync",   TRANS("Attack sync"),   { 0.0f, (float) getRateTypeChoices().size() - 1 },      attackSync,     getRateTypeChoices());
    decaySyncParam      = addDiscreteParam ("decaySync",    TRANS("Decay sync"),    { 0.0f, (float) getRateTypeChoices().size() - 1 },      decaySync,      getRateTypeChoices());
    releaseSyncParam    = addDiscreteParam ("releaseSync",  TRANS("Release sync"),  { 0.0f, (float) getRateTypeChoices().size() - 1 },      releaseSync,    getRateTypeChoices());

    changedTimer.setCallback ([this]
                              {
                                  changedTimer.stopTimer();
                                  changed();
                              });

    state.addListener (this);
}

ADSRModifier::~ADSRModifier()
{
    state.removeListener (this);
    notifyListenersOfDeletion();

    edit.removeModifierTimer (*modifierTimer);

    for (auto p : getAutomatableParameters())
        p->detachFromCurrentValue();

    deleteAutomatableParameters();
}

void ADSRModifier::initialise()
{
    // Do this here in case the audio code starts using the parameters before the constructor has finished
    modifierTimer = std::make_unique<ADSRModifierTimer> (*this);
    edit.addModifierTimer (*modifierTimer);

    restoreChangedParametersFromState();
}

//==============================================================================
float ADSRModifier::getCurrentValue()
{
    return currentValue.load (std::memory_order_acquire);
}

float ADSRModifier::getCurrentEnvelopeValue() const noexcept
{
    return currentEnvelopeValue.load (std::memory_order_acquire);
}

ADSRModifier::Stage ADSRModifier::getCurrentStage() const noexcept
{
    return (Stage) currentStage.load (std::memory_order_acquire);
}

float ADSRModifier::getCurrentStagePhase() const noexcept
{
    return stageProgress.load (std::memory_order_acquire);
}

AutomatableParameter::ModifierAssignment* ADSRModifier::createAssignment (const juce::ValueTree& v)
{
    return new Assignment (v, *this);
}

void ADSRModifier::applyToBuffer (const PluginRenderContext& prc)
{
    if (prc.bufferForMidiMessages == nullptr)
        return;

    // When the gate is driven externally (cross-track sidechain via
    // setGated()/triggerNoteOn()), ignore the destination track's own MIDI
    if (skipNativeResync_.load (std::memory_order_acquire))
        return;

    if (juce::roundToInt (syncTypeParam->getCurrentValue()) != ModifierCommon::note)
        return;

    for (auto& m : *prc.bufferForMidiMessages)
    {
        if (m.isNoteOn())
        {
            ++heldNotes_;
            gated_.store (false, std::memory_order_release);
        }
        else if (m.isNoteOff (true))
        {
            heldNotes_ = juce::jmax (0, heldNotes_ - 1);

            if (heldNotes_ == 0)
                gated_.store (true, std::memory_order_release);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldNotes_ = 0;
            gated_.store (true, std::memory_order_release);
        }
    }
}

void ADSRModifier::triggerNoteOn (bool forceZeroValue)
{
    // Zero-delay: enter the attack stage and recompute the value immediately
    // instead of deferring to the next updateStreamTime() block.
    // Safe because callers (SidechainMonitorPlugin) run on the audio
    // thread before the instrument plugin reads the assignment value.
    gated_.store (false, std::memory_order_release);
    modifierTimer->noteOn (forceZeroValue);
}

//==============================================================================
ADSRModifier::Assignment::Assignment (const juce::ValueTree& v, const ADSRModifier& adsr)
    : AutomatableParameter::ModifierAssignment (adsr.edit, v),
      adsrModifierID (adsr.itemID)
{
}

bool ADSRModifier::Assignment::isForModifierSource (const ModifierSource& source) const
{
    if (auto* mod = dynamic_cast<const ADSRModifier*> (&source))
        return mod->itemID == adsrModifierID;

    return false;
}

ADSRModifier::Ptr ADSRModifier::Assignment::getModifier() const
{
    if (auto mod = findModifierTypeForID<ADSRModifier> (edit, adsrModifierID))
        return mod;

    return {};
}

//==============================================================================
void ADSRModifier::valueTreeChanged()
{
    if (! changedTimer.isTimerRunning())
        changedTimer.startTimerHz (30);
}

}} // namespace tracktion { inline namespace engine
