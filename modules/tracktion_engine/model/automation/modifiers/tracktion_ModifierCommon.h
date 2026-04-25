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

namespace ModifierCommon
{
    // Ordered slow → fast, with each note grouped (Dotted → plain → Triplet).
    // Musical divisions are a contiguous block from sixteenBars..sixtyFourthT
    // so the range checks in the modifier timers stay simple. hertz lives
    // outside the block because it's the "free running" mode, not a division.
    enum RateType
    {
        hertz,

        sixteenBars,
        eightBars,
        fourBars,
        twoBars,
        bar,

        halfD,
        half,
        halfT,

        quarterD,
        quarter,
        quarterT,

        eighthD,
        eighth,
        eighthT,

        sixteenthD,
        sixteenth,
        sixteenthT,

        thirtySecondD,
        thirtySecond,
        thirtySecondT,

        sixtyFourthD,
        sixtyFourth,
        sixtyFourthT
    };

    inline juce::StringArray getRateTypeChoices()
    {
        return { TRANS("Hertz"),
                 TRANS("16 Bars"),
                 TRANS("8 Bars"),
                 TRANS("4 Bars"),
                 TRANS("2 Bars"),
                 TRANS("1 Bar"),
                 TRANS("1/2 D"),
                 TRANS("1/2"),
                 TRANS("1/2 T"),
                 TRANS("1/4 D"),
                 TRANS("1/4"),
                 TRANS("1/4 T"),
                 TRANS("1/8 D"),
                 TRANS("1/8"),
                 TRANS("1/8 T"),
                 TRANS("1/16 D"),
                 TRANS("1/16"),
                 TRANS("1/16 T"),
                 TRANS("1/32 D"),
                 TRANS("1/32"),
                 TRANS("1/32 T"),
                 TRANS("1/64 D"),
                 TRANS("1/64"),
                 TRANS("1/64 T") };
    }

    /** Returns the fraction of a bar to be used for a given rate type. */
    double getBarFraction (RateType) noexcept;

    enum SyncType
    {
        free,
        transport,
        note
    };

    inline juce::StringArray getSyncTypeChoices()
    {
        return { TRANS("Free"), TRANS("Transport"), TRANS("Note") };
    }
}

}} // namespace tracktion { inline namespace engine
