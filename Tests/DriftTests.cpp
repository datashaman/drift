#include "Engine/Clock.h"
#include "Engine/CollisionVariantMapping.h"
#include "Engine/EngineCommandQueue.h"
#include "Engine/SpatialWorld.h"
#include "Engine/TransportEngine.h"
#include "Engine/SpeedActivityMapping.h"
#include "Engine/ProximityRhythmMapping.h"
#include "Music/MidiSink.h"
#include "Music/MidiOutput.h"
#include "Music/PhraseScheduler.h"
#include "Music/Quantizer.h"
#include "Music/Transport.h"
#include "UI/BridgeProtocol.h"
#include "UI/UiResourceProvider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
class FakeClock final : public drift::engine::Clock
{
public:
    double nowSeconds() const override { return currentTime; }
    void advance (double seconds) { currentTime += seconds; }

private:
    double currentTime = 0.0;
};

struct FakeMidiDeviceState
{
    std::string id;
    std::vector<drift::music::ScheduledMidiMessage> messages;
    bool failScheduling = false;
};

class FakeMidiOutputDevice final : public drift::music::MidiOutputDevice
{
public:
    FakeMidiOutputDevice (std::shared_ptr<FakeMidiDeviceState> stateIn,
                          std::vector<std::string>& actionsIn)
        : state (std::move (stateIn)), actions (actionsIn)
    {
    }

    ~FakeMidiOutputDevice() override
    {
        actions.push_back (state->id + ":close");
    }

    bool schedule (const drift::music::ScheduledMidiMessage& message) override
    {
        actions.push_back (state->id + ":schedule");

        if (state->failScheduling)
            return false;

        state->messages.push_back (message);
        return true;
    }

    void clearPendingMessages() override
    {
        actions.push_back (state->id + ":clear");
    }

    void panic() override
    {
        actions.push_back (state->id + ":panic");
    }

private:
    std::shared_ptr<FakeMidiDeviceState> state;
    std::vector<std::string>& actions;
};

class FakeMidiOutputProvider final : public drift::music::MidiOutputProvider
{
public:
    std::vector<drift::music::MidiOutputInfo> availableOutputs() override
    {
        return outputs;
    }

    std::unique_ptr<drift::music::MidiOutputDevice> openOutput (
        const std::string& outputId) override
    {
        actions.push_back ("open:" + outputId);

        if (outputId == failingOpenId)
            return {};

        return std::make_unique<FakeMidiOutputDevice> (stateFor (outputId), actions);
    }

    std::shared_ptr<FakeMidiDeviceState> stateFor (const std::string& outputId)
    {
        const auto existing = std::find_if (states.begin(), states.end(), [&outputId] (const auto& state) {
            return state->id == outputId;
        });

        if (existing != states.end())
            return *existing;

        auto state = std::make_shared<FakeMidiDeviceState>();
        state->id = outputId;
        states.push_back (state);
        return state;
    }

    std::vector<drift::music::MidiOutputInfo> outputs;
    std::vector<std::string> actions;
    std::string failingOpenId;

private:
    std::vector<std::shared_ptr<FakeMidiDeviceState>> states;
};

int failures = 0;

void expect (bool condition, const std::string& message)
{
    if (condition)
        return;

    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expectNear (double actual, double expected, const std::string& message)
{
    expect (std::abs (actual - expected) < 1.0e-8, message);
}

bool midiMessagesEqual (const std::vector<drift::music::ScheduledMidiMessage>& left,
                        const std::vector<drift::music::ScheduledMidiMessage>& right)
{
    if (left.size() != right.size())
        return false;

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto& a = left[index];
        const auto& b = right[index];

        if (a.type != b.type || a.channel != b.channel || a.note != b.note
            || a.velocity != b.velocity || std::abs (a.beat - b.beat) >= 1.0e-8
            || std::abs (a.deliveryDelaySeconds - b.deliveryDelaySeconds) >= 1.0e-8
            || std::abs (a.scheduledAtSeconds - b.scheduledAtSeconds) >= 1.0e-8
            || std::abs (a.deliveryTimeSeconds - b.deliveryTimeSeconds) >= 1.0e-8)
        {
            return false;
        }
    }

    return true;
}

juce::var makeObject()
{
    return juce::var { new juce::DynamicObject() };
}

juce::var makeCommandEnvelope (const juce::String& messageId,
                               const juce::String& type,
                               juce::var payload,
                               int version = drift::ui::bridgeProtocolVersion)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("protocolVersion", version);
    object->setProperty ("messageId", messageId);
    object->setProperty ("type", type);
    object->setProperty ("payload", std::move (payload));
    return juce::var { object };
}

juce::var makeTempoPayload (juce::var bpm)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("bpm", std::move (bpm));
    return juce::var { object };
}

juce::var makeMotionPayload (juce::var paused)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("paused", std::move (paused));
    return juce::var { object };
}

juce::var makeProximityModePayload (juce::var mode)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("mode", std::move (mode));
    return juce::var { object };
}

juce::var makeOutputPayload (const juce::String& outputId)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("outputId", outputId);
    return juce::var { object };
}

juce::var makePhrasePayload (const juce::String& phraseId,
                             const juce::String& dragSessionId = "drag-1")
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("phraseId", phraseId);
    object->setProperty ("dragSessionId", dragSessionId);
    return juce::var { object };
}

juce::var makeMovePayload (const juce::String& phraseId,
                           double x,
                           double y,
                           const juce::String& dragSessionId = "drag-1")
{
    auto* position = new juce::DynamicObject();
    position->setProperty ("x", x);
    position->setProperty ("y", y);
    auto* object = new juce::DynamicObject();
    object->setProperty ("phraseId", phraseId);
    object->setProperty ("dragSessionId", dragSessionId);
    object->setProperty ("position", juce::var { position });
    return juce::var { object };
}

juce::var makeThrowPayload (const juce::String& phraseId,
                            double x,
                            double y,
                            const juce::String& dragSessionId = "drag-1")
{
    auto* velocity = new juce::DynamicObject();
    velocity->setProperty ("x", x);
    velocity->setProperty ("y", y);
    auto* object = new juce::DynamicObject();
    object->setProperty ("phraseId", phraseId);
    object->setProperty ("dragSessionId", dragSessionId);
    object->setProperty ("velocity", juce::var { velocity });
    return juce::var { object };
}

void testUiResourceProvider()
{
    const juce::File uiRoot { DRIFT_TEST_UI_DIR };

    expect (drift::ui::contentTypeForPath ("index.html") == "text/html",
            "HTML resources use the HTML content type");
    expect (drift::ui::contentTypeForPath ("assets/app.js") == "text/javascript",
            "JavaScript resources use the JavaScript content type");
    expect (drift::ui::contentTypeForPath ("assets/app.css") == "text/css",
            "CSS resources use the CSS content type");

    const auto index = drift::ui::loadResource (uiRoot, "/");
    expect (index.has_value(), "The provider serves index.html at the resource root");
    expect (index && index->mimeType == "text/html", "The index response has an HTML content type");
    expect (drift::ui::loadResource (uiRoot, "/assets/app.js").has_value(),
            "The provider serves the packaged JavaScript bundle");
    expect (! drift::ui::loadResource (uiRoot, "/../package.json").has_value(),
            "The provider rejects parent-directory traversal");
}

void testTransportUsesMonotonicClock()
{
    FakeClock clock;
    drift::music::Transport transport { clock };

    transport.play();
    clock.advance (1.0);
    auto state = transport.snapshot();
    expectNear (state.beatPosition, 2.0, "One second at 120 BPM advances two beats");
    expect (state.bar == 1, "Two beats remain in bar one");
    expectNear (state.beat, 3.0, "Beat display is one-based");

    expect (transport.setBpm (60.0), "A tempo inside the supported range is accepted");
    clock.advance (2.0);
    state = transport.snapshot();
    expectNear (state.beatPosition, 4.0, "Changing tempo preserves position and uses the new rate");
    expect (state.bar == 2, "Four elapsed beats enter bar two");
    expectNear (state.beat, 1.0, "Bar boundaries display beat one");

    expect (! transport.setBpm (39.0), "Tempo below the supported range is rejected");
    expect (! transport.setBpm (241.0), "Tempo above the supported range is rejected");

    transport.stop();
    state = transport.snapshot();
    expect (! state.playing, "Stop clears the playing state");
    expectNear (state.beatPosition, 0.0, "Stop resets transport position");
}

void testQuantizationBoundaries()
{
    expectNear (drift::music::quantizeForward (3.999, 4.0), 4.0,
                "A position before the bar boundary quantizes to that boundary");
    expectNear (drift::music::quantizeForward (4.0, 4.0), 4.0,
                "A position on the bar boundary remains on that boundary");
    expectNear (drift::music::quantizeForward (4.001, 4.0), 8.0,
                "A position after the bar boundary quantizes to the next bar");
    expectNear (drift::music::quantizeForward (0.999, 1.0), 1.0,
                "A position before a beat quantizes to that beat");
    expectNear (drift::music::quantizeForward (1.0, 1.0), 1.0,
                "A position on a beat remains on that beat");
    expectNear (drift::music::quantizeForward (1.001, 1.0), 2.0,
                "A position after a beat quantizes to the next beat");
}

void testPhraseSchedulingAcrossLoopAndBar()
{
    const drift::music::Phrase phrase {
        "boundary-test",
        4.0,
        1,
        {
            { 0.0, 36, 100, 0.5 },
            { 3.75, 38, 90, 0.5 },
        },
        "BOUNDARY TEST",
        drift::music::PhraseRole::bass,
        "A",
        { 0.5, 0.5 },
        { 0.0, 0.0 },
        0.045,
        1.0,
        {},
        std::nullopt,
        std::nullopt,
    };
    drift::music::RecordingMidiSink sink;
    drift::music::PhraseScheduler scheduler;

    scheduler.scheduleRange (phrase, 3.5, 4.5, sink);
    const auto& messages = sink.messages();

    expect (messages.size() == 4, "The range schedules note pairs on both sides of the loop boundary");
    expect (std::any_of (messages.begin(), messages.end(), [] (const auto& message) {
        return message.type == drift::music::MidiMessageType::noteOn
               && std::abs (message.beat - 3.75) < 1.0e-8;
    }), "The final event in the first loop is scheduled");
    expect (std::any_of (messages.begin(), messages.end(), [] (const auto& message) {
        return message.type == drift::music::MidiMessageType::noteOn
               && std::abs (message.beat - 4.0) < 1.0e-8;
    }), "The first event in the next loop is scheduled");

    const auto noteOnCount = std::count_if (messages.begin(), messages.end(), [] (const auto& message) {
        return message.type == drift::music::MidiMessageType::noteOn;
    });
    const auto noteOffCount = std::count_if (messages.begin(), messages.end(), [] (const auto& message) {
        return message.type == drift::music::MidiMessageType::noteOff;
    });
    expect (noteOnCount == noteOffCount, "Every scheduled note-on has a note-off");
    expect (sink.activeNoteCount() == 0, "Paired scheduling leaves no unmatched active notes");
}

void testInitialCompositionIsAuthoritative()
{
    const auto phrases = drift::music::makeInitialComposition();
    expect (phrases.size() == 4, "The initial composition contains four phrases");

    const std::vector<std::string> expectedIds { "drums", "bass", "chords", "melody" };
    const std::vector<int> expectedChannels { 10, 1, 2, 3 };
    std::set<std::string> uniqueIds;

    for (std::size_t index = 0; index < phrases.size(); ++index)
    {
        const auto& phrase = phrases[index];
        expect (phrase.id == expectedIds[index], "Phrase IDs are stable and role-specific");
        expect (phrase.midiChannel == expectedChannels[index],
                "Every role uses its required MIDI channel");
        expect (phrase.currentVariantId == "A", "Every phrase begins on authored variant A");
        expect (! phrase.pendingVariantId && ! phrase.pendingVariantApplyBeat,
                "Every phrase begins without a pending variant transition");
        expect (drift::music::findVariant (phrase, "A") != nullptr,
                "Every phrase exposes authored variant A");
        expect (phrase.variants.size() == 3,
                "Every phrase exposes exactly three authored variants");
        expect (phrase.variants[0].id == "A" && phrase.variants[1].id == "B"
                    && phrase.variants[2].id == "C",
                "Every role uses stable A, B, C variant IDs in cycle order");
        for (const auto& variant : phrase.variants)
        {
            expect (! variant.events.empty(), "Every authored variant contains notes");
            expect (std::isfinite (variant.activity) && variant.activity >= 0.0
                        && variant.activity <= 1.0,
                    "Every authored variant has normalized activity metadata");
            expect (std::all_of (
                        variant.events.begin(), variant.events.end(), [] (const auto& event) {
                            return event.beat >= 0.0 && event.beat < 4.0
                                   && event.note >= 0 && event.note <= 127
                                   && event.velocity > 0 && event.velocity <= 127
                                   && event.durationBeats > 0.0;
                        }),
                    "Every authored event is valid inside the shared four-beat loop");
        }
        expect (phrase.lengthBeats == 4.0, "All initial phrases share a four-beat loop");
        expect (! phrase.events.empty(), "Every initial phrase contains authored notes");
        expect (phrase.position.x >= 0.0 && phrase.position.x <= 1.0
                    && phrase.position.y >= 0.0 && phrase.position.y <= 1.0,
                "Every phrase begins at a normalized world position");
        expect (std::isfinite (phrase.velocity.x) && std::isfinite (phrase.velocity.y),
                "Every phrase begins with a finite native-world velocity");
        expect (phrase.radius > 0.0 && phrase.radius <= 0.5,
                "Every phrase has a usable normalized collision radius");
        expect (phrase.mass > 0.0, "Every phrase has positive mass");
        uniqueIds.insert (phrase.id);
    }

    expect (uniqueIds.size() == phrases.size(), "Phrase IDs are unique");
}

void testSpeedActivityMappingAndSmoothing()
{
    using drift::engine::ActivityBand;
    using drift::engine::SpeedActivityTracker;
    using drift::engine::activityBandAfterObservation;

    expect (std::string { drift::engine::variantIdForActivityBand (ActivityBand::sparse) }
                == "C"
            && std::string { drift::engine::variantIdForActivityBand (ActivityBand::normal) }
                   == "A"
            && std::string { drift::engine::variantIdForActivityBand (ActivityBand::active) }
                   == "B",
            "Sparse, normal, and active bands map to authored C, A, and B variants");
    expect (activityBandAfterObservation (ActivityBand::normal, 0.015)
                == ActivityBand::sparse
            && activityBandAfterObservation (ActivityBand::sparse, 0.039)
                   == ActivityBand::sparse
            && activityBandAfterObservation (ActivityBand::sparse, 0.040)
                   == ActivityBand::normal,
            "Sparse hysteresis includes both exact thresholds and holds between them");
    expect (activityBandAfterObservation (ActivityBand::normal, 0.450)
                == ActivityBand::active
            && activityBandAfterObservation (ActivityBand::active, 0.301)
                   == ActivityBand::active
            && activityBandAfterObservation (ActivityBand::active, 0.300)
                   == ActivityBand::normal,
            "Active hysteresis includes both exact thresholds and holds between them");

    SpeedActivityTracker smoothing;
    smoothing.observe (0.03, 0, false);
    smoothing.observe (1.0, 1, false);
    const auto alpha = 1.0 - std::exp (
        -drift::engine::SpatialWorld::fixedStepSeconds
        / SpeedActivityTracker::smoothingTimeConstantSeconds);
    expectNear (smoothing.smoothedNormalizedSpeed(), 0.03 + alpha * 0.97,
                "Speed smoothing advances by the deterministic 120 Hz exponential step");
    const auto beforeSuspension = smoothing.smoothedNormalizedSpeed();
    smoothing.observe (0.0, 1000, true);
    expectNear (smoothing.smoothedNormalizedSpeed(), beforeSuspension,
                "Suspended speed observation does not advance smoothing or bands");

    SpeedActivityTracker jitter;
    jitter.observe (0.03, 0, false);
    for (auto step = 0; step < 1000; ++step)
        jitter.observe (step % 2 == 0 ? 0.02 : 0.039, 1, false);
    expect (jitter.stableBand() == ActivityBand::normal,
            "Speed jitter inside the hysteresis window does not chatter bands");
}

void testProximityMappingAndTransformations()
{
    using drift::engine::CouplingLevel;
    using drift::engine::PhraseBody;
    using drift::engine::ProximityTracker;

    const PhraseBody first { "first", { 0.1, 0.5 }, {}, 0.05, 1.0 };
    expectNear (drift::engine::normalizedPairProximity (
                    first, { "touching", { 0.2, 0.5 }, {}, 0.05, 1.0 }),
                1.0, "Touching body surfaces have maximum normalized proximity");
    expectNear (drift::engine::normalizedPairProximity (
                    first, { "half", { 0.4, 0.5 }, {}, 0.05, 1.0 }),
                0.5, "A 0.20 surface gap maps to half proximity");
    expectNear (drift::engine::normalizedPairProximity (
                    first, { "far", { 0.6, 0.5 }, {}, 0.05, 1.0 }),
                0.0, "A 0.40 surface gap clamps to zero proximity");

    using drift::engine::couplingLevelAfterObservation;
    expect (couplingLevelAfterObservation (CouplingLevel::loose, 0.40)
                == CouplingLevel::linked
            && couplingLevelAfterObservation (CouplingLevel::linked, 0.31)
                   == CouplingLevel::linked
            && couplingLevelAfterObservation (CouplingLevel::linked, 0.30)
                   == CouplingLevel::loose,
            "Loose/linked hysteresis includes both exact thresholds");
    expect (couplingLevelAfterObservation (CouplingLevel::linked, 0.75)
                == CouplingLevel::tight
            && couplingLevelAfterObservation (CouplingLevel::tight, 0.66)
                   == CouplingLevel::tight
            && couplingLevelAfterObservation (CouplingLevel::tight, 0.65)
                   == CouplingLevel::linked,
            "Linked/tight hysteresis includes both exact thresholds");

    ProximityTracker smoothing;
    smoothing.observe (0.1, 0);
    smoothing.observe (0.9);
    const auto alpha = 1.0 - std::exp (
        -drift::engine::SpatialWorld::fixedStepSeconds
        / ProximityTracker::smoothingTimeConstantSeconds);
    expectNear (smoothing.smoothedProximity(), 0.1 + alpha * 0.8,
                "Proximity smoothing advances by one deterministic physics step");
    ProximityTracker jitter;
    jitter.observe (0.35, 0);
    for (auto step = 0; step < 1000; ++step)
        jitter.observe (step % 2 == 0 ? 0.31 : 0.39);
    expect (jitter.observedLevel() == CouplingLevel::loose,
            "Proximity jitter below the entry threshold cannot flap coupling state");

    const std::vector<drift::music::NoteEvent> source {
        { 0.25, 60, 80, 0.25 },
        { 0.75, 61, 81, 0.5 },
        { 3.75, 60, 90, 0.75 },
    };
    const auto linked = drift::engine::applyRhythmProfile (
        source, CouplingLevel::linked, 4.0);
    expect (linked.size() == source.size()
                && linked[0].beat == 0.0
                && linked[1].beat == 0.5
                && linked[2].beat == 1.0,
            "Linked profiles snap to half beats, wrap the loop, and sort deterministically");
    const auto tight = drift::engine::applyRhythmProfile (
        source, CouplingLevel::tight, 4.0);
    expect (tight.size() == 2 && tight[0].note == 60 && tight[0].velocity == 90
                && tight[0].durationBeats == 0.75 && tight[1].beat == 1.0,
            "Tight profiles snap to beats and coalesce exact pitch/onset duplicates");
    expect (drift::engine::sharedAccentBoost (0.0, source, CouplingLevel::tight) == 0
                && drift::engine::sharedAccentBoost (
                       0.75, source, CouplingLevel::linked) == 12
                && drift::engine::sharedAccentBoost (
                       0.75, source, CouplingLevel::tight) == 24,
            "Shared accents preserve weak pairs and apply the exact linked/tight boosts");

    for (const auto& phrase : drift::music::makeInitialComposition())
    {
        for (const auto& variant : phrase.variants)
        {
            for (const auto level : { CouplingLevel::loose,
                                      CouplingLevel::linked,
                                      CouplingLevel::tight })
            {
                const auto transformed = drift::engine::applyRhythmProfile (
                    variant.events, level, phrase.lengthBeats);
                expect (std::all_of (
                            transformed.begin(), transformed.end(), [&phrase] (const auto& event) {
                                return event.beat >= 0.0 && event.beat < phrase.lengthBeats
                                       && event.note >= 0 && event.note <= 127
                                       && event.velocity > 0 && event.velocity <= 127
                                       && event.durationBeats > 0.0;
                            }),
                        "Every role/activity/profile combination remains valid MIDI material");
            }
        }
    }
}

void testCollisionVariantRulesAndCycle()
{
    const std::array<std::array<const char*, 3>, 6> expected {{
        { "bass", "chords", "chords" },
        { "bass", "drums", "bass" },
        { "bass", "melody", "bass" },
        { "chords", "drums", "drums" },
        { "chords", "melody", "chords" },
        { "drums", "melody", "melody" },
    }};

    const auto& rules = drift::engine::collisionVariantRules();
    expect (rules.size() == expected.size(), "Every unique phrase pair has one mapping");
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        expect (std::string { rules[index].firstPhraseId } == expected[index][0]
                    && std::string { rules[index].secondPhraseId } == expected[index][1]
                    && std::string { rules[index].targetPhraseId } == expected[index][2],
                "Collision mappings have stable lexicographic order and explicit targets");
        const auto* reversed = drift::engine::findCollisionVariantRule (
            expected[index][1], expected[index][0]);
        expect (reversed != nullptr
                    && std::string { reversed->targetPhraseId } == expected[index][2],
                "Pair selection is independent of contact argument order");
    }

    auto phrase = drift::music::makeInitialComposition().front();
    expect (drift::engine::nextVariantId (phrase) == "B", "Variant A advances to B");
    drift::music::applyVariant (phrase, "B");
    expect (drift::engine::nextVariantId (phrase) == "C", "Variant B advances to C");
    drift::music::applyVariant (phrase, "C");
    expect (drift::engine::nextVariantId (phrase) == "A", "Variant C wraps to A");
}

void testSpatialWorldFixedStepAndBoundaries()
{
    using drift::engine::PhraseBody;
    using drift::engine::SpatialWorld;

    SpatialWorld integrationWorld {
        { PhraseBody { "moving", { 0.5, 0.5 }, { 0.12, -0.24 }, 0.05, 1.0 } },
        0.0,
    };
    integrationWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    const auto& moving = integrationWorld.bodies().front();
    expectNear (moving.position.x, 0.501,
                "A fixed 120 Hz step integrates horizontal velocity");
    expectNear (moving.position.y, 0.498,
                "A fixed 120 Hz step integrates vertical velocity");
    expect (integrationWorld.revision() == 1,
            "Every integrated fixed step advances the world revision");

    SpatialWorld boundaryWorld {
        {
            PhraseBody { "left", { 0.0501, 0.5 }, { -0.12, 0.0 }, 0.05, 1.0 },
            PhraseBody { "right", { 0.9499, 0.5 }, { 0.12, 0.0 }, 0.05, 1.0 },
            PhraseBody { "top", { 0.5, 0.0501 }, { 0.0, -0.12 }, 0.05, 1.0 },
            PhraseBody { "bottom", { 0.5, 0.9499 }, { 0.0, 0.12 }, 0.05, 1.0 },
            PhraseBody { "corner", { 0.0501, 0.0501 }, { -0.12, -0.12 }, 0.05, 1.0 },
        },
        0.0,
    };
    boundaryWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    const auto& bodies = boundaryWorld.bodies();
    expect (bodies[0].velocity.x > 0.0, "The left edge reflects horizontal velocity");
    expect (bodies[1].velocity.x < 0.0, "The right edge reflects horizontal velocity");
    expect (bodies[2].velocity.y > 0.0, "The top edge reflects vertical velocity");
    expect (bodies[3].velocity.y < 0.0, "The bottom edge reflects vertical velocity");
    expect (bodies[4].velocity.x > 0.0 && bodies[4].velocity.y > 0.0,
            "A corner collision reflects both velocity axes");
    expect (std::all_of (bodies.begin(), bodies.end(), [] (const auto& body) {
                return body.position.x >= body.radius && body.position.x <= 1.0 - body.radius
                       && body.position.y >= body.radius
                       && body.position.y <= 1.0 - body.radius;
            }),
            "No edge or corner collision lets a phrase escape normalized bounds");
}

void testSpatialWorldBoundsCatchUpWork()
{
    drift::engine::SpatialWorld world {
        { drift::engine::PhraseBody { "delayed", { 0.94, 0.94 }, { 2.0, 2.0 }, 0.05, 1.0 } },
        0.0,
    };

    world.advanceTo (1.0);
    const auto& diagnostics = world.diagnostics();
    const auto& body = world.bodies().front();
    expect (diagnostics.physicsStepCount
                == static_cast<std::size_t> (drift::engine::SpatialWorld::maximumCatchUpSteps),
            "A delayed observer performs only the bounded number of physics steps");
    expect (diagnostics.physicsCatchUpStepCount
                == static_cast<std::size_t> (
                    drift::engine::SpatialWorld::maximumCatchUpSteps - 1),
            "Catch-up diagnostics count extra work beyond the ordinary step");
    expect (diagnostics.physicsCatchUpLimitHitCount == 1,
            "A capped physics backlog is visible in diagnostics");
    expect (body.position.x >= body.radius && body.position.x <= 1.0 - body.radius
                && body.position.y >= body.radius && body.position.y <= 1.0 - body.radius,
            "Bounded catch-up cannot eject a fast phrase from the world");
}

void testSpatialWorldDragLifecycle()
{
    drift::engine::SpatialWorld world {
        { drift::engine::PhraseBody { "dragged", { 0.5, 0.5 }, { 0.12, -0.24 }, 0.05, 1.0 } },
        0.0,
    };

    expect (! world.moveDraggedPhrase ("dragged", { 0.2, 0.2 }),
            "A move without a drag lifecycle is stale");
    expect (world.beginDrag ("dragged"), "A known free phrase can begin a drag");
    expect (! world.beginDrag ("dragged"), "A duplicate drag start is stale");
    world.advanceTo (10.0 * drift::engine::SpatialWorld::fixedStepSeconds);
    expectNear (world.bodies().front().position.x, 0.5,
                "Autonomous integration cannot fight direct manipulation on x");
    expectNear (world.bodies().front().position.y, 0.5,
                "Autonomous integration cannot fight direct manipulation on y");

    expect (world.moveDraggedPhrase ("dragged", { 0.0, 1.0 }),
            "An active drag accepts normalized pointer position");
    expectNear (world.bodies().front().position.x, 0.05,
                "Native authority clamps a dragged phrase to its left radius");
    expectNear (world.bodies().front().position.y, 0.95,
                "Native authority clamps a dragged phrase to its bottom radius");
    expect (world.endDrag ("dragged"), "An active drag can end");
    expect (! world.endDrag ("dragged"), "A duplicate drag end is stale");

    world.advanceTo (11.0 * drift::engine::SpatialWorld::fixedStepSeconds);
    expect (world.bodies().front().position.x > 0.05,
            "Autonomous velocity resumes after direct manipulation ends");

    expect (world.beginDrag ("dragged"), "The phrase can begin another drag");
    world.endAllDrags();
    expect (! world.bodies().front().dragged,
            "A reconnect can release every abandoned native drag");
}

void testSpatialWorldThrowLifecycle()
{
    using drift::engine::PhraseBody;
    using drift::engine::SpatialWorld;

    SpatialWorld stationaryWorld {
        { PhraseBody { "stationary", { 0.5, 0.5 }, { 0.2, 0.1 }, 0.05, 1.0 } },
        0.0,
    };
    expect (stationaryWorld.beginDrag ("stationary"), "A stationary throw can begin");
    expect (stationaryWorld.throwPhrase ("stationary", { 0.001, -0.001 }),
            "A finite stationary release ends its drag");
    expectNear (stationaryWorld.bodies().front().velocity.x, 0.0,
                "Negligible horizontal release velocity is suppressed");
    expectNear (stationaryWorld.bodies().front().velocity.y, 0.0,
                "Negligible vertical release velocity is suppressed");
    stationaryWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    expectNear (stationaryWorld.bodies().front().position.x, 0.5,
                "A stationary release does not drift after release");

    SpatialWorld slowWorld {
        { PhraseBody { "slow", { 0.5, 0.5 }, {}, 0.05, 1.0 } },
        0.0,
    };
    SpatialWorld fastWorld {
        { PhraseBody { "fast", { 0.5, 0.5 }, {}, 0.05, 1.0 } },
        0.0,
    };
    slowWorld.beginDrag ("slow");
    fastWorld.beginDrag ("fast");
    expect (slowWorld.throwPhrase ("slow", { 0.2, 0.0 }), "A slow throw is accepted");
    expect (fastWorld.throwPhrase ("fast", { 0.8, 0.0 }), "A fast throw is accepted");
    slowWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    fastWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    expect (fastWorld.bodies().front().position.x > slowWorld.bodies().front().position.x,
            "A faster release visibly travels farther in the same fixed step");

    SpatialWorld clampedWorld {
        { PhraseBody { "clamped", { 0.5, 0.5 }, {}, 0.05, 1.0 } },
        0.0,
    };
    clampedWorld.beginDrag ("clamped");
    expect (clampedWorld.throwPhrase ("clamped", { 30.0, 40.0 }),
            "Native authority safely accepts an excessive direct velocity");
    expectNear (std::hypot (clampedWorld.bodies().front().velocity.x,
                            clampedWorld.bodies().front().velocity.y),
                SpatialWorld::maximumThrowSpeed,
                "Native authority clamps excessive throw magnitude");

    SpatialWorld edgeWorld {
        { PhraseBody { "corner", { 0.0501, 0.0501 }, {}, 0.05, 1.0 } },
        0.0,
    };
    edgeWorld.beginDrag ("corner");
    edgeWorld.throwPhrase ("corner", { -1.0, -1.0 });
    edgeWorld.advanceTo (SpatialWorld::fixedStepSeconds);
    const auto& corner = edgeWorld.bodies().front();
    expect (corner.velocity.x > 0.0 && corner.velocity.y > 0.0,
            "A thrown phrase reflects both axes at a corner");
    expect (corner.position.x >= corner.radius && corner.position.y >= corner.radius,
            "A corner throw remains inside normalized bounds");

    edgeWorld.beginDrag ("corner");
    expect (! edgeWorld.throwPhrase (
                "corner", { std::numeric_limits<double>::infinity(), 0.0 }),
            "Non-finite native velocity is rejected");
    expect (edgeWorld.bodies().front().dragged,
            "A rejected throw does not interrupt the active drag");
}

void testSpatialWorldMotionPauseLifecycle()
{
    using drift::engine::PhraseBody;
    using drift::engine::SpatialWorld;

    SpatialWorld world {
        {
            PhraseBody { "repositioned", { 0.2, 0.3 }, { 0.3, 0.1 }, 0.05, 1.0 },
            PhraseBody { "untouched", { 0.8, 0.7 }, { -0.2, -0.1 }, 0.05, 1.0 },
        },
        0.0,
    };
    world.setMotionPaused (true);
    expect (world.motionPaused(), "The world publishes its authoritative paused state");
    world.advanceTo (10.0 * SpatialWorld::fixedStepSeconds);
    expectNear (world.bodies()[0].position.x, 0.2,
                "Paused motion preserves a phrase's horizontal position");
    expectNear (world.bodies()[1].position.y, 0.7,
                "Paused motion preserves every autonomous position");
    expectNear (world.bodies()[1].velocity.x, -0.2,
                "An untouched phrase retains its velocity while paused");

    expect (world.beginDrag ("repositioned"),
            "A paused phrase remains directly manipulable");
    expect (world.moveDraggedPhrase ("repositioned", { 0.75, 0.7 }),
            "A paused phrase can be repositioned");
    expect (world.throwPhrase ("repositioned", { 1.0, -0.5 }),
            "Releasing a paused phrase completes its drag");
    expectNear (world.bodies()[0].velocity.x, 0.0,
                "A phrase manipulated while paused settles horizontally");
    expectNear (world.bodies()[0].velocity.y, 0.0,
                "A phrase manipulated while paused settles vertically");

    world.advanceTo (11.0 * SpatialWorld::fixedStepSeconds);
    expect (world.consumeCollisionBegins().size() == 1,
            "Manual contacts remain active while autonomous motion is paused");

    const auto untouchedBeforeResume = world.bodies()[1].position;
    world.setMotionPaused (false);
    world.advanceTo (12.0 * SpatialWorld::fixedStepSeconds);
    expect (! world.motionPaused(), "Motion can resume independently");
    expect (world.bodies()[1].position.x < untouchedBeforeResume.x,
            "An untouched phrase resumes its preserved velocity");
    expectNear (world.bodies()[0].position.x, 0.75,
                "A repositioned phrase stays settled after resume");
}

void testSpatialWorldCollisionContactLifecycle()
{
    using drift::engine::PhraseBody;
    using drift::engine::SpatialWorld;

    SpatialWorld world {
        {
            PhraseBody { "bass", { 0.2, 0.5 }, {}, 0.05, 1.0 },
            PhraseBody { "drums", { 0.8, 0.5 }, {}, 0.05, 1.0 },
        },
        0.0,
    };
    expect (world.beginDrag ("drums"), "The collision fixture can manipulate drums");
    world.moveDraggedPhrase ("drums", { 0.25, 0.5 });
    world.advanceTo (SpatialWorld::fixedStepSeconds);

    const auto firstContacts = world.consumeCollisionBegins();
    expect (firstContacts.size() == 1,
            "A new overlap emits one collision contact-begin event");
    expect (firstContacts.size() == 1
                && firstContacts.front().firstPhraseId == "bass"
                && firstContacts.front().secondPhraseId == "drums",
            "Collision pair IDs have deterministic ordering");
    expect (world.collisionPairState ("drums", "bass").touching,
            "The authoritative pair state exposes sustained contact");

    world.advanceTo (2.0 * SpatialWorld::fixedStepSeconds);
    expect (world.consumeCollisionBegins().empty(),
            "Sustained overlap cannot emit another contact begin");

    world.moveDraggedPhrase ("drums", { 0.8, 0.5 });
    world.advanceTo (3.0 * SpatialWorld::fixedStepSeconds);
    expect (! world.collisionPairState ("bass", "drums").touching,
            "Separation ends authoritative pair contact");
    world.moveDraggedPhrase ("drums", { 0.25, 0.5 });
    world.advanceTo (4.0 * SpatialWorld::fixedStepSeconds);
    expect (world.consumeCollisionBegins().empty(),
            "Re-contact during cooldown is suppressed");

    world.moveDraggedPhrase ("drums", { 0.8, 0.5 });
    auto now = 4.0 * SpatialWorld::fixedStepSeconds;
    const auto cooldownSteps = static_cast<int> (
        std::ceil (SpatialWorld::collisionCooldownSeconds / SpatialWorld::fixedStepSeconds));
    for (auto step = 0; step <= cooldownSteps; ++step)
    {
        now += SpatialWorld::fixedStepSeconds;
        world.advanceTo (now);
    }
    world.moveDraggedPhrase ("drums", { 0.25, 0.5 });
    now += SpatialWorld::fixedStepSeconds;
    world.advanceTo (now);
    expect (world.consumeCollisionBegins().size() == 1,
            "Separation and re-contact after cooldown emits one new contact begin");
    expect (world.diagnostics().collisionContactBeginCount == 2,
            "Accepted collision contact begins are observable in diagnostics");
}

void testEngineCommandQueueCoalescingAndPressure()
{
    using drift::engine::CommandEnqueueResult;
    using drift::engine::EngineCommand;
    using drift::engine::EngineCommandQueue;
    using drift::engine::EngineCommandType;

    const auto makeEngineCommand = [] (EngineCommandType type,
                                       std::string messageId,
                                       std::string phraseId = {},
                                       std::string dragSessionId = "drag-1") {
        EngineCommand command;
        command.type = type;
        command.messageId = std::move (messageId);
        command.phraseId = std::move (phraseId);
        command.dragSessionId = std::move (dragSessionId);
        return command;
    };

    EngineCommandQueue queue { 32 };
    expect (queue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseMove, "stale", "bass"))
                == CommandEnqueueResult::staleDrag,
            "A move before drag start is rejected as stale");
    expect (queue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseDragStart, "start-bass", "bass"))
                == CommandEnqueueResult::accepted,
            "A discrete drag start enters the command queue");
    expect (queue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseDragStart, "start-drums", "drums"))
                == CommandEnqueueResult::accepted,
            "Independent phrase drags retain discrete ordering");

    auto bassMove = makeEngineCommand (EngineCommandType::phraseMove, "move-bass-1", "bass");
    bassMove.position = { 0.3, 0.4 };
    auto drumsMove = makeEngineCommand (
        EngineCommandType::phraseMove, "move-drums", "drums");
    drumsMove.position = { 0.7, 0.6 };
    auto latestBassMove = makeEngineCommand (
        EngineCommandType::phraseMove, "move-bass-2", "bass");
    latestBassMove.position = { 0.8, 0.2 };
    expect (queue.tryEnqueue (bassMove) == CommandEnqueueResult::accepted,
            "The first move enters the queue");
    expect (queue.tryEnqueue (drumsMove) == CommandEnqueueResult::accepted,
            "Moves for different phrases can coexist");
    expect (queue.tryEnqueue (latestBassMove) == CommandEnqueueResult::coalesced,
            "A stale move coalesces by phrase across other move commands");
    expect (queue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseMove, "wrong-session", "bass", "drag-old"))
                == CommandEnqueueResult::staleDrag,
            "A move from an obsolete drag session is rejected");
    expect (queue.tryEnqueue (makeEngineCommand (EngineCommandType::transportPlay, "play"))
                == CommandEnqueueResult::accepted,
            "A transport command remains an ordered discrete barrier");

    auto postBarrierMove = makeEngineCommand (
        EngineCommandType::phraseMove, "move-bass-3", "bass");
    postBarrierMove.position = { 0.6, 0.6 };
    expect (queue.tryEnqueue (postBarrierMove) == CommandEnqueueResult::accepted,
            "A move cannot coalesce backward across a discrete command");

    std::vector<EngineCommand> drained;
    while (const auto command = queue.tryDequeue())
        drained.push_back (*command);
    expect (drained.size() == 6, "Coalescing removes only the stale move command");
    expect (drained[2].messageId == "move-bass-2",
            "The coalesced slot retains the newest bass position");
    expect (drained[4].type == EngineCommandType::transportPlay
                && drained[5].messageId == "move-bass-3",
            "Discrete ordering is retained around later movement");
    expect (queue.diagnostics().coalescedMoveCount == 1,
            "Move coalescing is observable in queue diagnostics");

    EngineCommandQueue throwQueue;
    throwQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseDragStart, "start", "bass", "throw-session"));
    auto throwCommand = makeEngineCommand (
        EngineCommandType::phraseThrow, "throw", "bass", "throw-session");
    throwCommand.velocity = { 0.7, -0.2 };
    expect (throwQueue.tryEnqueue (throwCommand) == CommandEnqueueResult::accepted,
            "A throw matching the active drag session is accepted");
    expect (throwQueue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseMove, "after-throw", "bass", "throw-session"))
                == CommandEnqueueResult::staleDrag,
            "An accepted throw closes the intended pointer lifecycle");

    EngineCommandQueue pressureQueue { 2 };
    expect (pressureQueue.tryEnqueue (
                makeEngineCommand (EngineCommandType::transportPlay, "play"))
                == CommandEnqueueResult::accepted,
            "The bounded queue accepts its first discrete command");
    expect (pressureQueue.tryEnqueue (
                makeEngineCommand (EngineCommandType::transportStop, "stop"))
                == CommandEnqueueResult::accepted,
            "The bounded queue accepts discrete commands to capacity");
    expect (pressureQueue.tryEnqueue (
                makeEngineCommand (EngineCommandType::appConnect, "connect"))
                == CommandEnqueueResult::queueFull,
            "A saturated discrete queue rejects explicitly rather than dropping silently");
    expect (pressureQueue.diagnostics().maximumQueueDepth == 2
                && pressureQueue.diagnostics().pressureEventCount == 1,
            "Queue depth and saturation pressure are observable");

    EngineCommandQueue reservedQueue { 18 };
    reservedQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseDragStart, "start-bass", "bass"));
    auto reservedMove = makeEngineCommand (
        EngineCommandType::phraseMove, "move-bass", "bass");
    reservedQueue.tryEnqueue (reservedMove);
    reservedQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseDragStart, "start-drums", "drums"));
    auto overflowingMove = makeEngineCommand (
        EngineCommandType::phraseMove, "move-drums", "drums");
    expect (reservedQueue.tryEnqueue (overflowingMove) == CommandEnqueueResult::queueFull,
            "Move pressure stops before consuming reserved discrete capacity");
    expect (reservedQueue.tryEnqueue (
                makeEngineCommand (EngineCommandType::transportStop, "stop"))
                == CommandEnqueueResult::accepted,
            "A transport command remains enqueueable under move pressure");

    EngineCommandQueue evictionQueue { 18 };
    evictionQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseDragStart, "start", "bass"));
    evictionQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseMove, "move", "bass"));
    for (auto index = 0; index < 16; ++index)
    {
        evictionQueue.tryEnqueue (makeEngineCommand (
            EngineCommandType::transportPlay, "play-" + std::to_string (index)));
    }
    expect (evictionQueue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseThrow, "throw", "bass"))
                == CommandEnqueueResult::accepted,
            "A full queue evicts stale movement rather than dropping a throw");

    EngineCommandQueue reconnectQueue;
    reconnectQueue.tryEnqueue (makeEngineCommand (
        EngineCommandType::phraseDragStart, "start", "bass"));
    reconnectQueue.tryEnqueue (makeEngineCommand (EngineCommandType::appConnect, "reload"));
    expect (reconnectQueue.tryEnqueue (makeEngineCommand (
                EngineCommandType::phraseMove, "old-ui", "bass"))
                == CommandEnqueueResult::staleDrag,
            "Reconnect invalidates movement from an abandoned pointer lifecycle");
}

void testCollisionQueuesAndAppliesBassVariantAtBar()
{
    const auto advanceToBeat = [] (FakeClock& clock,
                                   drift::engine::TransportEngine& engine,
                                   double targetBeat) {
        while (engine.snapshot().beatPosition + 1.0e-9 < targetBeat)
        {
            const auto remainingBeats = targetBeat - engine.snapshot().beatPosition;
            clock.advance (std::min (0.002, remainingBeats * 0.5));
            engine.tick();
        }
    };
    const auto phraseById = [] (const drift::engine::EngineSnapshot& snapshot,
                                const std::string& phraseId) -> const drift::engine::PhraseSnapshot& {
        const auto phrase = std::find_if (
            snapshot.phrases.begin(), snapshot.phrases.end(), [&phraseId] (const auto& candidate) {
                return candidate.id == phraseId;
            });
        if (phrase == snapshot.phrases.end())
            throw std::runtime_error ("Missing phrase snapshot");
        return *phrase;
    };

    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    engine.play();
    expect (engine.beginPhraseDrag ("drums"),
            "The collision transition fixture can hold the drums phrase");
    advanceToBeat (clock, engine, 0.5);
    const auto bassPosition = phraseById (engine.snapshot(), "bass").position;
    engine.moveDraggedPhrase ("drums", bassPosition);
    clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    engine.tick();

    auto state = engine.snapshot();
    auto bass = phraseById (state, "bass");
    expect (bass.currentVariantId == "A",
            "A collision leaves the current bass variant active before quantization");
    expect (bass.pendingVariantId && *bass.pendingVariantId == "B",
            "A drums-bass contact queues deterministic bass variant B");
    expect (bass.pendingVariantApplyBeat.has_value(),
            "The pending transition publishes its authoritative beat");
    if (bass.pendingVariantApplyBeat)
        expectNear (*bass.pendingVariantApplyBeat, 4.0,
                    "An early collision targets the next unscheduled bar");
    expect (state.diagnostics.collisionContactBeginCount == 1
                && state.diagnostics.collisionIntentQueuedCount == 1
                && state.diagnostics.collisionTransitionAppliedCount == 0,
            "Contact and queued intent are observable before application");
    expect (std::none_of (
                sink.messages().begin(), sink.messages().end(), [] (const auto& message) {
                    return message.type == drift::music::MidiMessageType::noteOn
                           && message.channel == 1 && message.note == 31
                           && std::abs (message.beat - 0.75) < 1.0e-8;
                }),
            "A collision never emits variant B notes immediately or unquantized");

    advanceToBeat (clock, engine, 3.999);
    bass = phraseById (engine.snapshot(), "bass");
    expect (bass.currentVariantId == "A" && bass.pendingVariantId,
            "The transition remains pending immediately before the bar");

    advanceToBeat (clock, engine, 4.0);
    state = engine.snapshot();
    bass = phraseById (state, "bass");
    expect (bass.currentVariantId == "B" && ! bass.pendingVariantId
                && ! bass.pendingVariantApplyBeat,
            "The pending variant applies and clears exactly on the bar");
    expect (state.diagnostics.collisionTransitionAppliedCount == 1,
            "Exactly one applied transition is observable");

    advanceToBeat (clock, engine, 4.9);
    const auto variantBNoteCount = std::count_if (
        sink.messages().begin(), sink.messages().end(), [] (const auto& message) {
            return message.type == drift::music::MidiMessageType::noteOn
                   && message.channel == 1 && message.note == 31
                   && std::abs (message.beat - 5.0) < 1.0e-8;
        });
    expect (variantBNoteCount == 1,
            "Variant B composes with tight profile snapping exactly once");
    expect (sink.activeNoteCount() == 0,
            "The quantized variant transition preserves paired note-on and note-off output");
    expect (state.diagnostics.lateMidiEventCount == 0,
            "The collision transition introduces no late MIDI events");

    const auto pendingBeatForCollisionAt = [&advanceToBeat, &phraseById] (
        double collisionBeat) -> double {
        FakeClock localClock;
        drift::music::RecordingMidiSink localSink;
        drift::engine::TransportEngine localEngine { localClock, localSink };
        localEngine.play();
        localEngine.beginPhraseDrag ("drums");
        advanceToBeat (localClock, localEngine, collisionBeat);
        const auto position = phraseById (localEngine.snapshot(), "bass").position;
        localEngine.moveDraggedPhrase ("drums", position);
        localClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        localEngine.tick();
        const auto pending = phraseById (localEngine.snapshot(), "bass")
                                 .pendingVariantApplyBeat;
        return pending.value_or (-1.0);
    };

    expectNear (pendingBeatForCollisionAt (3.7), 4.0,
                "A collision before the look-ahead reaches the bar can apply at that bar");
    expectNear (pendingBeatForCollisionAt (3.99), 8.0,
                "A collision immediately before an already-scheduled bar targets the next one");
    expectNear (pendingBeatForCollisionAt (4.0), 8.0,
                "A collision on an already-scheduled bar targets the next one");
    expectNear (pendingBeatForCollisionAt (4.01), 8.0,
                "A collision immediately after a bar targets the following bar");
}

void testEveryCollisionPairQueuesItsMappedTarget()
{
    const auto phraseById = [] (const drift::engine::EngineSnapshot& snapshot,
                                const std::string& phraseId)
        -> const drift::engine::PhraseSnapshot& {
        const auto phrase = std::find_if (
            snapshot.phrases.begin(), snapshot.phrases.end(), [&] (const auto& candidate) {
                return candidate.id == phraseId;
            });
        if (phrase == snapshot.phrases.end())
            throw std::runtime_error ("Missing phrase snapshot");
        return *phrase;
    };

    for (const auto& rule : drift::engine::collisionVariantRules())
    {
        FakeClock clock;
        drift::music::RecordingMidiSink sink;
        drift::engine::TransportEngine engine { clock, sink };
        engine.play();
        expect (engine.beginPhraseDrag (rule.firstPhraseId),
                "Every mapped pair can begin a deterministic collision fixture");
        const auto secondPosition = phraseById (engine.snapshot(), rule.secondPhraseId).position;
        expect (engine.moveDraggedPhrase (rule.firstPhraseId, secondPosition),
                "Every mapped pair can be moved into contact");
        clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        engine.tick();

        const auto snapshot = engine.snapshot();
        const auto& target = phraseById (snapshot, rule.targetPhraseId);
        expect (target.currentVariantId == "A" && target.pendingVariantId == "B",
                "Every unique pair queues variant B on its explicit target");
        expect (target.pendingVariantApplyBeat == 4.0,
                "Every pair applies at the same next eligible bar");
        expect (snapshot.diagnostics.collisionContactBeginCount == 1
                    && snapshot.diagnostics.collisionIntentQueuedCount == 1,
                "Every pair produces exactly one observable intent");
    }
}

void testSimultaneousCollisionsUseStablePriority()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    engine.play();

    const auto snapshotBefore = engine.snapshot();
    const auto commonPosition = snapshotBefore.phrases.front().position;
    for (const auto& phrase : snapshotBefore.phrases)
    {
        expect (engine.beginPhraseDrag (phrase.id),
                "The simultaneous fixture can hold every phrase");
        expect (engine.moveDraggedPhrase (phrase.id, commonPosition),
                "The simultaneous fixture can co-locate every phrase");
    }

    clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    engine.tick();
    auto snapshot = engine.snapshot();
    expect (snapshot.collisions.size() == 6,
            "The snapshot explains all six unique pair states");
    expect (std::all_of (
                snapshot.collisions.begin(), snapshot.collisions.end(), [] (const auto& pair) {
                    return pair.touching
                           && pair.firstPhraseId < pair.secondPhraseId
                           && ! pair.targetPhraseId.empty();
                }),
            "Every simultaneous contact publishes its ordered pair and target");
    expect (snapshot.diagnostics.collisionContactBeginCount == 6,
            "A four-way overlap observes all six contacts");
    expect (snapshot.diagnostics.collisionIntentQueuedCount == 4,
            "Stable pair priority queues at most one change per target phrase");
    expect (std::all_of (
                snapshot.phrases.begin(), snapshot.phrases.end(), [] (const auto& phrase) {
                    return phrase.currentVariantId == "A" && phrase.pendingVariantId == "B"
                           && phrase.pendingVariantApplyBeat == 4.0;
                }),
            "Simultaneous contacts leave one non-contradictory pending B per phrase");

    while (engine.snapshot().beatPosition + 1.0e-9 < 4.0)
    {
        clock.advance (0.001);
        engine.tick();
    }
    snapshot = engine.snapshot();
    expect (snapshot.diagnostics.collisionTransitionAppliedCount == 4,
            "Every accepted simultaneous intent applies exactly once");
    expect (std::all_of (
                snapshot.phrases.begin(), snapshot.phrases.end(), [] (const auto& phrase) {
                    return phrase.currentVariantId == "B" && ! phrase.pendingVariantId
                           && ! phrase.pendingVariantApplyBeat;
                }),
            "All four transitions clear pending state at their shared boundary");
    expect (sink.activeNoteCount() == 0,
            "Simultaneous variant changes preserve paired MIDI output");
}

void testSpeedActivityQueuesQuantizedVariantsAndCollisionWins()
{
    const auto phraseById = [] (const drift::engine::EngineSnapshot& snapshot,
                                const std::string& phraseId)
        -> const drift::engine::PhraseSnapshot& {
        const auto phrase = std::find_if (
            snapshot.phrases.begin(), snapshot.phrases.end(), [&] (const auto& candidate) {
                return candidate.id == phraseId;
            });
        if (phrase == snapshot.phrases.end())
            throw std::runtime_error ("Missing phrase snapshot");
        return *phrase;
    };

    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    engine.play();
    expect (engine.beginPhraseDrag ("bass")
                && engine.throwPhrase ("bass", { 0.0, -1.5 }),
            "A maximum native throw can drive the bass activity tracker");

    for (auto step = 0; step < 60; ++step)
    {
        clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        engine.tick();
    }

    auto snapshot = engine.snapshot();
    auto bass = phraseById (snapshot, "bass");
    expect (bass.activityBand == drift::engine::ActivityBand::active
                && bass.pendingActivityBand == drift::engine::ActivityBand::active,
            "A fast phrase settles into active and publishes its pending speed band");
    expect (bass.currentVariantId == "A" && bass.pendingVariantId == "B"
                && bass.pendingVariantApplyBeat == 4.0,
            "Active speed queues authored variant B at the next unscheduled bar");
    expect (snapshot.diagnostics.speedBandChangeCount == 1
                && snapshot.diagnostics.speedIntentQueuedCount == 1,
            "A stable speed crossing emits exactly one observable intent");

    expect (engine.beginPhraseDrag ("drums"),
            "The priority fixture can move drums into the fast bass");
    expect (engine.moveDraggedPhrase ("drums", bass.position),
            "The priority fixture creates a bass-drums contact");
    clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    engine.tick();
    snapshot = engine.snapshot();
    bass = phraseById (snapshot, "bass");
    expect (bass.pendingVariantId == "B" && ! bass.pendingActivityBand,
            "A collision replaces a speed intent aimed at the same musical boundary");
    expect (snapshot.diagnostics.collisionIntentQueuedCount == 1,
            "The replacing collision is counted as the accepted boundary intent");

    while (engine.snapshot().beatPosition + 1.0e-9 < 4.0)
    {
        clock.advance (0.002);
        engine.tick();
    }
    snapshot = engine.snapshot();
    expect (phraseById (snapshot, "bass").currentVariantId == "B"
                && snapshot.diagnostics.collisionTransitionAppliedCount == 1
                && snapshot.diagnostics.speedTransitionAppliedCount == 0,
            "The boundary applies the collision transition once, not the displaced speed intent");
    expect (sink.activeNoteCount() == 0 && snapshot.diagnostics.lateMidiEventCount == 0,
            "Speed/collision arbitration preserves paired, on-time MIDI");

    FakeClock sameTickClock;
    drift::music::RecordingMidiSink sameTickSink;
    drift::engine::TransportEngine sameTickEngine { sameTickClock, sameTickSink };
    sameTickEngine.play();
    sameTickEngine.beginPhraseDrag ("bass");
    sameTickEngine.throwPhrase ("bass", { 0.0, -1.5 });
    for (auto step = 0; step < 16; ++step)
    {
        sameTickClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        sameTickEngine.tick();
    }
    expect (phraseById (sameTickEngine.snapshot(), "bass").activityBand
                == drift::engine::ActivityBand::normal,
            "The simultaneous fixture stops immediately below the active threshold");
    sameTickEngine.beginPhraseDrag ("drums");
    sameTickEngine.moveDraggedPhrase (
        "drums", phraseById (sameTickEngine.snapshot(), "bass").position);
    sameTickClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    sameTickEngine.tick();
    const auto sameTick = sameTickEngine.snapshot();
    const auto& sameTickBass = phraseById (sameTick, "bass");
    expect (sameTickBass.activityBand == drift::engine::ActivityBand::active
                && sameTickBass.pendingVariantId == "B"
                && ! sameTickBass.pendingActivityBand,
            "A same-tick collision wins while the observed speed band still advances");
    expect (sameTick.diagnostics.speedIntentSuppressedCount == 1
                && sameTick.diagnostics.collisionIntentQueuedCount == 1,
            "Same-tick collision priority records the suppressed speed intent exactly once");

    FakeClock pausedClock;
    drift::music::RecordingMidiSink pausedSink;
    drift::engine::TransportEngine pausedEngine { pausedClock, pausedSink };
    pausedEngine.play();
    pausedEngine.beginPhraseDrag ("melody");
    pausedEngine.throwPhrase ("melody", { 1.5, 0.0 });
    pausedEngine.setMotionPaused (true);
    for (auto step = 0; step < 120; ++step)
    {
        pausedClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        pausedEngine.tick();
    }
    expect (phraseById (pausedEngine.snapshot(), "melody").activityBand
                == drift::engine::ActivityBand::normal,
            "Freeze Motion suspends speed-band observation without changing musical activity");
    pausedEngine.setMotionPaused (false);
    for (auto step = 0; step < 60; ++step)
    {
        pausedClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        pausedEngine.tick();
    }
    expect (phraseById (pausedEngine.snapshot(), "melody").activityBand
                == drift::engine::ActivityBand::active,
            "Speed observation resumes from stored native velocity after motion resumes");

    FakeClock draggedClock;
    drift::music::RecordingMidiSink draggedSink;
    drift::engine::TransportEngine draggedEngine { draggedClock, draggedSink };
    draggedEngine.play();
    draggedEngine.beginPhraseDrag ("chords");
    draggedEngine.throwPhrase ("chords", { -1.5, 0.0 });
    draggedEngine.beginPhraseDrag ("chords");
    for (auto step = 0; step < 120; ++step)
    {
        draggedClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        draggedEngine.tick();
    }
    expect (phraseById (draggedEngine.snapshot(), "chords").activityBand
                == drift::engine::ActivityBand::normal,
            "Direct dragging suspends speed observation even while native velocity is retained");
    draggedEngine.endPhraseDrag ("chords");
    for (auto step = 0; step < 60; ++step)
    {
        draggedClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        draggedEngine.tick();
    }
    expect (phraseById (draggedEngine.snapshot(), "chords").activityBand
                == drift::engine::ActivityBand::active,
            "Ending a drag resumes observation from the retained native velocity");
}

void testProximityAuditionQuantizesWhileMotionIsFrozen()
{
    const auto phraseById = [] (const drift::engine::EngineSnapshot& snapshot,
                                const std::string& phraseId)
        -> const drift::engine::PhraseSnapshot& {
        const auto phrase = std::find_if (
            snapshot.phrases.begin(), snapshot.phrases.end(), [&] (const auto& candidate) {
                return candidate.id == phraseId;
            });
        if (phrase == snapshot.phrases.end())
            throw std::runtime_error ("Missing phrase snapshot");
        return *phrase;
    };
    const auto pairByIds = [] (const drift::engine::EngineSnapshot& snapshot,
                               const std::string& first,
                               const std::string& second)
        -> const drift::engine::ProximityPairSnapshot& {
        const auto pair = std::find_if (
            snapshot.proximityPairs.begin(), snapshot.proximityPairs.end(),
            [&] (const auto& candidate) {
                return candidate.firstPhraseId == first
                       && candidate.secondPhraseId == second;
            });
        if (pair == snapshot.proximityPairs.end())
            throw std::runtime_error ("Missing proximity pair snapshot");
        return *pair;
    };
    const auto advanceToBeat = [] (FakeClock& clock,
                                   drift::engine::TransportEngine& engine,
                                   double targetBeat) {
        while (engine.snapshot().beatPosition + 1.0e-9 < targetBeat)
        {
            clock.advance (0.002);
            engine.tick();
        }
    };

    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    engine.play();
    engine.setMotionPaused (true);
    expect (engine.beginPhraseDrag ("bass")
                && engine.moveDraggedPhrase ("bass", { 0.30, 0.50 }),
            "The proximity fixture can reposition bass during Freeze Motion");
    expect (engine.beginPhraseDrag ("chords")
                && engine.moveDraggedPhrase ("chords", { 0.38, 0.50 }),
            "The proximity fixture can reposition chords during Freeze Motion");

    for (auto step = 0; step < 120; ++step)
    {
        clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        engine.tick();
    }

    auto snapshot = engine.snapshot();
    auto pair = pairByIds (snapshot, "bass", "chords");
    expect (snapshot.motionPaused && phraseById (snapshot, "bass").dragged,
            "Frozen, directly dragged phrases remain under user control during observation");
    expect (pair.rawProximity == 1.0 && pair.smoothedProximity > 0.95
                && pair.couplingLevel == drift::engine::CouplingLevel::loose
                && pair.pendingCouplingLevel == drift::engine::CouplingLevel::tight
                && pair.pendingApplyBeat == 4.0,
            "Close frozen phrases settle to a tight transition at the next bar");
    expect (snapshot.diagnostics.proximityLevelChangeCount == 2
                && snapshot.diagnostics.proximityIntentQueuedCount == 1
                && snapshot.diagnostics.proximityIntentCoalescedCount == 1,
            "Linked-to-tight settling coalesces into one unambiguous boundary state");
    expect (phraseById (snapshot, "chords").pendingVariantId == "B",
            "The same contact independently preserves its collision variant intent");

    advanceToBeat (clock, engine, 4.0);
    snapshot = engine.snapshot();
    pair = pairByIds (snapshot, "bass", "chords");
    expect (pair.couplingLevel == drift::engine::CouplingLevel::tight
                && ! pair.pendingCouplingLevel && ! pair.pendingApplyBeat
                && snapshot.diagnostics.proximityTransitionAppliedCount == 1,
            "The tight coupling state applies and clears exactly at the bar");
    expect (snapshot.playing && snapshot.motionPaused
                && snapshot.diagnostics.lateMidiEventCount == 0,
            "Proximity application does not interrupt frozen-motion playback or MIDI timing");

    advanceToBeat (clock, engine, 6.1);
    const auto snappedBassNote = std::count_if (
        sink.messages().begin(), sink.messages().end(), [] (const auto& message) {
            return message.type == drift::music::MidiMessageType::noteOn
                   && message.channel == 1 && message.note == 36
                   && std::abs (message.beat - 6.0) < 1.0e-8;
        });
    const auto unsnappedBassNote = std::count_if (
        sink.messages().begin(), sink.messages().end(), [] (const auto& message) {
            return message.type == drift::music::MidiMessageType::noteOn
                   && message.channel == 1 && message.note == 36
                   && std::abs (message.beat - 5.5) < 1.0e-8;
        });
    expect (snappedBassNote == 1 && unsnappedBassNote == 0,
            "Tight rhythm-profile scheduling snaps bass onsets without duplicate delivery");

    FakeClock accentClock;
    drift::music::RecordingMidiSink accentSink;
    drift::engine::TransportEngine accentEngine { accentClock, accentSink };
    accentEngine.play();
    accentEngine.setMotionPaused (true);
    accentEngine.setProximityAuditionMode (
        drift::engine::ProximityAuditionMode::sharedAccents);
    accentEngine.beginPhraseDrag ("bass");
    accentEngine.moveDraggedPhrase ("bass", { 0.30, 0.50 });
    accentEngine.beginPhraseDrag ("chords");
    accentEngine.moveDraggedPhrase ("chords", { 0.38, 0.50 });
    for (auto step = 0; step < 120; ++step)
    {
        accentClock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
        accentEngine.tick();
    }
    auto accentSnapshot = accentEngine.snapshot();
    expect (accentSnapshot.proximityMode
                == drift::engine::ProximityAuditionMode::rhythmProfiles
                && accentSnapshot.pendingProximityMode
                       == drift::engine::ProximityAuditionMode::sharedAccents
                && accentSnapshot.pendingProximityModeApplyBeat == 4.0,
            "Audition mode changes publish pending state at the same musical boundary");
    advanceToBeat (accentClock, accentEngine, 4.1);
    accentSnapshot = accentEngine.snapshot();
    const auto accentedBass = std::count_if (
        accentSink.messages().begin(), accentSink.messages().end(), [] (const auto& message) {
            return message.type == drift::music::MidiMessageType::noteOn
                   && message.channel == 1 && message.note == 36
                   && message.velocity == 124
                   && std::abs (message.beat - 4.0) < 1.0e-8;
        });
    expect (accentSnapshot.proximityMode
                == drift::engine::ProximityAuditionMode::sharedAccents
                && ! accentSnapshot.pendingProximityMode
                && accentSnapshot.diagnostics.proximityModeAppliedCount == 1,
            "Shared-accent mode becomes authoritative exactly at the pending bar");
    expect (accentedBass == 1,
            "Tight shared accents add exactly 24 velocity without changing the onset");
    expect (accentSink.activeNoteCount() == 0
                && accentSnapshot.diagnostics.lateMidiEventCount == 0,
            "Both audition mappings retain paired, on-time MIDI output");
}

void testFourPhrasesShareOneTransport()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };

    engine.play();
    const std::set<int> expectedChannels { 1, 2, 3, 10 };
    std::set<int> channelsAtFirstBeat;

    for (const auto& message : sink.messages())
    {
        if (message.type == drift::music::MidiMessageType::noteOn
            && std::abs (message.beat) < 1.0e-8)
        {
            channelsAtFirstBeat.insert (message.channel);
        }
    }

    expect (channelsAtFirstBeat == expectedChannels,
            "All four roles start from the same transport boundary");
    const auto firstSnapshot = engine.snapshot();
    expect (firstSnapshot.phrases.size() == 4,
            "The engine snapshot exposes all four authoritative phrases");
    expect (std::all_of (
                firstSnapshot.phrases.begin(), firstSnapshot.phrases.end(), [] (const auto& phrase) {
                    return phrase.playing;
                }),
            "Every authoritative phrase reflects the shared playing state");

    clock.advance (2.0);
    engine.tick();
    std::set<int> channelsAtSecondLoop;
    std::array<int, 17> noteOns {};
    std::array<int, 17> noteOffs {};

    for (const auto& message : sink.messages())
    {
        if (message.type == drift::music::MidiMessageType::noteOn)
        {
            ++noteOns[static_cast<std::size_t> (message.channel)];
            if (std::abs (message.beat - 4.0) < 1.0e-8)
                channelsAtSecondLoop.insert (message.channel);
        }
        else
        {
            ++noteOffs[static_cast<std::size_t> (message.channel)];
        }
    }

    expect (channelsAtSecondLoop == expectedChannels,
            "All four roles remain aligned at the next loop boundary");
    for (const auto channel : expectedChannels)
    {
        expect (noteOns[static_cast<std::size_t> (channel)]
                    == noteOffs[static_cast<std::size_t> (channel)],
                "Every role schedules paired note-on and note-off events");
    }

    engine.stop();
    expect (sink.activeNoteCount() == 0,
            "Stopping the four-role composition leaves no active notes");
}

void testMotionPauseDoesNotInterruptPlayback()
{
    const auto phraseById = [] (const drift::engine::EngineSnapshot& snapshot,
                                const std::string& phraseId)
        -> const drift::engine::PhraseSnapshot& {
        const auto phrase = std::find_if (
            snapshot.phrases.begin(), snapshot.phrases.end(), [&] (const auto& candidate) {
                return candidate.id == phraseId;
            });
        if (phrase == snapshot.phrases.end())
            throw std::runtime_error ("Missing phrase snapshot");
        return *phrase;
    };

    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    expect (! engine.snapshot().playing && engine.snapshot().motionPaused,
            "The stopped engine begins with world motion frozen");
    engine.setMotionPaused (false);
    expect (engine.snapshot().motionPaused,
            "Motion cannot resume independently while transport is stopped");

    engine.play();
    expect (engine.snapshot().playing && ! engine.snapshot().motionPaused,
            "Play starts transport and world motion together");
    for (auto tick = 0; tick < 50; ++tick)
    {
        clock.advance (0.002);
        engine.tick();
    }

    engine.setMotionPaused (true);
    const auto frozen = engine.snapshot();
    const auto messagesBeforePause = sink.messageCount();
    const auto watermarkBeforePause = frozen.diagnostics.schedulingWatermarkBeat;
    for (auto tick = 0; tick < 200; ++tick)
    {
        clock.advance (0.002);
        engine.tick();
    }

    auto paused = engine.snapshot();
    expect (paused.playing && paused.motionPaused,
            "Independent motion pause leaves musical transport playing");
    expect (paused.beatPosition > frozen.beatPosition
                && paused.diagnostics.schedulingWatermarkBeat > watermarkBeforePause,
            "Transport and scheduling advance while motion is frozen");
    expect (sink.messageCount() > messagesBeforePause,
            "MIDI scheduling continues without a pause or clear");
    expect (paused.diagnostics.lateMidiEventCount == 0,
            "Freezing motion introduces no late MIDI events");
    for (const auto& phrase : frozen.phrases)
    {
        const auto& still = phraseById (paused, phrase.id);
        expectNear (still.position.x, phrase.position.x,
                    "Frozen phrases keep their horizontal positions");
        expectNear (still.position.y, phrase.position.y,
                    "Frozen phrases keep their vertical positions");
        expectNear (still.velocity.x, phrase.velocity.x,
                    "Untouched phrases preserve horizontal velocity");
        expectNear (still.velocity.y, phrase.velocity.y,
                    "Untouched phrases preserve vertical velocity");
    }

    const auto bassPosition = phraseById (paused, "bass").position;
    expect (engine.beginPhraseDrag ("drums"),
            "A phrase can be caught while motion is frozen");
    expect (engine.moveDraggedPhrase ("drums", bassPosition),
            "A caught phrase can be repositioned while audio continues");
    clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    engine.tick();
    paused = engine.snapshot();
    expect (phraseById (paused, "bass").pendingVariantId == "B",
            "A manual collision still queues its musical transition while frozen");
    expect (engine.throwPhrase ("drums", { 1.0, -0.5 }),
            "A frozen reposition can complete through the ordinary release path");
    expectNear (phraseById (engine.snapshot(), "drums").velocity.x, 0.0,
                "A phrase repositioned while frozen has no resumed horizontal throw");
    expectNear (phraseById (engine.snapshot(), "drums").velocity.y, 0.0,
                "A phrase repositioned while frozen has no resumed vertical throw");

    const auto melodyBeforeResume = phraseById (engine.snapshot(), "melody");
    const auto drumsBeforeResume = phraseById (engine.snapshot(), "drums");
    engine.setMotionPaused (false);
    clock.advance (drift::engine::SpatialWorld::fixedStepSeconds);
    engine.tick();
    const auto resumed = engine.snapshot();
    expect (! resumed.motionPaused && resumed.playing,
            "Resume Motion leaves audio transport uninterrupted");
    expect (std::abs (phraseById (resumed, "melody").position.x
                     - melodyBeforeResume.position.x) > 1.0e-8,
            "An untouched phrase resumes its preserved motion");
    expectNear (phraseById (resumed, "drums").position.x, drumsBeforeResume.position.x,
                "A repositioned phrase remains catchable after motion resumes");

    engine.stop();
    expect (! engine.snapshot().playing && engine.snapshot().motionPaused,
            "Stop silences transport and freezes motion together");
    expect (sink.messageCount() == 0 && sink.activeNoteCount() == 0,
            "Coupled Stop still clears scheduled and active MIDI");
    engine.play();
    expect (engine.snapshot().playing && ! engine.snapshot().motionPaused,
            "Play always resumes transport and motion together");
}

void testEngineIsIndependentOfUiUpdateTiming()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };

    engine.play();
    const auto eventsAfterPlay = sink.messageCount();
    expect (eventsAfterPlay > 2, "Play schedules all four phrases immediately");

    clock.advance (0.01);
    engine.tick();
    engine.tick();
    expect (sink.messageCount() == eventsAfterPlay,
            "Frequent engine ticks do not reschedule an overlapping look-ahead range");

    clock.advance (1.99);
    engine.tick();
    const auto delayedState = engine.snapshot();
    expectNear (delayedState.beatPosition, 4.0,
                "Transport position follows the monotonic clock despite delayed observation");
    expect (delayedState.diagnostics.lateMidiEventCount > 0,
            "A delayed engine tick records late MIDI delivery");
    expect (delayedState.diagnostics.maximumEngineLatenessSeconds > 1.8,
            "Diagnostics retain the largest delayed-engine interval");
    expect (delayedState.diagnostics.schedulingWatermarkBeat
                >= 4.2 - drift::engine::TransportEngine::timingToleranceSeconds,
            "The scheduling watermark advances monotonically through catch-up");
    expect (delayedState.diagnostics.physicsCatchUpLimitHitCount > 0,
            "A delayed engine tick caps physics catch-up independently of musical time");
    expect (delayedState.worldRevision
                == static_cast<std::size_t> (drift::engine::SpatialWorld::maximumCatchUpSteps + 1),
            "The capped native world does not alter the monotonic transport result");

    const auto hasSecondLoopStart = std::any_of (
        sink.messages().begin(), sink.messages().end(), [] (const auto& message) {
            return message.type == drift::music::MidiMessageType::noteOn
                   && std::abs (message.beat - 4.0) < 1.0e-8;
        });
    expect (hasSecondLoopStart, "A delayed engine tick catches up using musical timestamps");

    engine.stop();
    expect (sink.messageCount() == 0, "Stop clears all future scheduled messages");
    expect (sink.activeNoteCount() == 0, "Stop leaves no active notes in the recording sink");
}

void testMidiOutputSelectionAndReplacement()
{
    FakeMidiOutputProvider provider;
    provider.outputs = { { "opaque-a", "Studio Synth" }, { "opaque-b", "Loopback Bus" } };
    drift::music::MidiOutputService output { provider };

    auto state = output.snapshot();
    expect (state.outputs.size() == 2, "MIDI discovery retains opaque IDs and display names");
    expect (output.selectOutput ("opaque-a"), "An available MIDI output can be selected");
    expect (output.snapshot().status == drift::music::MidiOutputStatus::connected,
            "Selecting an output reports a connected state");

    provider.actions.clear();
    expect (output.selectOutput ("opaque-b"), "A second MIDI output can replace the first");
    expect (provider.actions == std::vector<std::string> {
                "opaque-a:clear",
                "opaque-a:panic",
                "opaque-a:close",
                "open:opaque-b",
            },
            "Output replacement clears, panics, and closes before opening the new device");

    provider.actions.clear();
    output.clear();
    expect (provider.actions == std::vector<std::string> {
                "opaque-b:clear",
                "opaque-b:panic",
            },
            "Transport stop executes panic without closing the selected output");

    provider.actions.clear();
    output.shutdown();
    expect (provider.actions == std::vector<std::string> {
                "opaque-b:clear",
                "opaque-b:panic",
                "opaque-b:close",
            },
            "Application shutdown silences and closes the selected output");
}

void testMidiOutputRoutesTimestampedPhrase()
{
    FakeClock clock;
    FakeMidiOutputProvider provider;
    provider.outputs = { { "synth", "Test Synth" }, { "backup", "Backup Synth" } };
    drift::music::MidiOutputService output { provider };
    drift::engine::TransportEngine engine { clock, output };

    expect (output.selectOutput ("synth"), "The fake synth opens for routing");
    engine.play();

    const auto deviceState = provider.stateFor ("synth");
    expect (deviceState->messages.size() > 2,
            "Starting transport routes all four phrases to the output");
    const auto bassNoteOn = std::find_if (
        deviceState->messages.begin(), deviceState->messages.end(), [] (const auto& message) {
            return message.channel == 1 && message.note == 36
                   && message.type == drift::music::MidiMessageType::noteOn;
        });
    const auto bassNoteOff = std::find_if (
        deviceState->messages.begin(), deviceState->messages.end(), [] (const auto& message) {
            return message.channel == 1 && message.note == 36
                   && message.type == drift::music::MidiMessageType::noteOff;
        });
    expect (bassNoteOn != deviceState->messages.end(), "The routed bass phrase begins with note-on");
    expect (bassNoteOff != deviceState->messages.end(),
            "The routed bass phrase includes its paired note-off");
    if (bassNoteOn != deviceState->messages.end())
        expectNear (bassNoteOn->deliveryDelaySeconds, 0.0,
                    "The first bass note is delivered immediately");
    if (bassNoteOff != deviceState->messages.end())
        expectNear (bassNoteOff->deliveryDelaySeconds, 0.375,
                    "The bass note-off receives a tempo-derived delivery timestamp");

    expect (output.selectOutput ("backup"), "Playback can move to a replacement output");
    engine.reschedule();
    const auto replacementState = provider.stateFor ("backup");
    expect (replacementState->messages.size() == deviceState->messages.size(),
            "The replacement output receives the complete freshly timestamped composition");

    provider.actions.clear();
    engine.stop();
    expect (output.messageCount() == 0, "Stop clears the scheduled-message count");
    expect (provider.actions == std::vector<std::string> {
                "backup:clear",
                "backup:panic",
            },
            "Stopping routed playback clears pending events and executes panic");
}

void testMidiOutputFailureRecovery()
{
    FakeMidiOutputProvider provider;
    provider.outputs = { { "good", "Good Output" }, { "bad", "Broken Output" } };
    provider.failingOpenId = "bad";
    drift::music::MidiOutputService output { provider };

    expect (! output.selectOutput ("missing"), "An unavailable runtime ID is rejected");
    expect (output.snapshot().status == drift::music::MidiOutputStatus::error,
            "A missing output reports an error state");
    expect (! output.selectOutput ("bad"), "A device open failure is recoverable");
    expect (output.snapshot().errorMessage == "Could not open the selected MIDI output",
            "An open failure exposes a useful error message");

    expect (output.selectOutput ("good"), "The app can select another output after failure");
    expect (output.snapshot().errorMessage.empty(), "A successful selection clears the prior error");

    provider.actions.clear();
    provider.stateFor ("good")->failScheduling = true;
    output.schedule ({ drift::music::MidiMessageType::noteOn, 1, 60, 100, 0.0, 0.01 });
    expect (output.snapshot().status == drift::music::MidiOutputStatus::error,
            "A send failure disconnects the device and reports an error");
    expect (provider.actions == std::vector<std::string> {
                "good:schedule",
                "good:clear",
                "good:panic",
                "good:close",
            },
            "A send failure performs best-effort panic before closing the device");

    expect (output.selectOutput ("good"), "The app remains usable after a send failure");
}

void testBridgeRejectsInvalidCommandsBeforeMutation()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    auto connectCount = 0;
    auto outputSelectionCount = 0;
    const drift::ui::CommandHandlers handlers {
        [&connectCount] { ++connectCount; },
        [&engine] { engine.play(); },
        [&engine] { engine.stop(); },
        [&engine] (double bpm) { engine.setBpm (bpm); },
        [&outputSelectionCount] (const std::string&) { ++outputSelectionCount; },
        [] (const std::string& outputId) { return outputId == "known-output"; },
        {},
        {},
        {},
        {},
        [] (const std::string& phraseId) { return phraseId == "bass"; },
        [&engine] (bool paused) { engine.setMotionPaused (paused); },
        [&engine] (const std::string& mode) {
            engine.setProximityAuditionMode (
                *drift::engine::proximityAuditionModeForName (mode));
        },
    };

    const auto playResult = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope ("ui-play", "transport.play", makeObject()), handlers);
    expect (playResult.command.has_value(), "A valid versioned Play envelope is accepted");
    expect (engine.snapshot().playing, "The accepted Play command reaches transport state");
    const auto freezeResult = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope (
            "ui-freeze", "world.setMotionPaused", makeMotionPayload (juce::var { true })),
        handlers);
    expect (freezeResult.command.has_value() && engine.snapshot().motionPaused
                && engine.snapshot().playing,
            "A valid motion command freezes only the world");
    drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope (
            "ui-resume", "world.setMotionPaused", makeMotionPayload (juce::var { false })),
        handlers);
    expect (! engine.snapshot().motionPaused && engine.snapshot().playing,
            "A valid motion command resumes only the world");
    const auto proximityModeResult = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope (
            "ui-proximity", "proximity.setAuditionMode",
            makeProximityModePayload (juce::var { "sharedAccents" })),
        handlers);
    expect (proximityModeResult.command.has_value()
                && engine.snapshot().pendingProximityMode
                       == drift::engine::ProximityAuditionMode::sharedAccents,
            "A valid audition command queues the requested native proximity mode");
    const auto scheduledBeforeRejections = sink.messages();

    const auto expectRejected = [&] (juce::var command,
                                     drift::ui::CommandRejectionCode expectedCode,
                                     const std::string& message) {
        const auto result = drift::ui::dispatchCommandEnvelope (command, handlers);
        expect (result.rejection.has_value(), message);
        expect (result.rejection && result.rejection->code == expectedCode,
                message + " uses the structured rejection code");
    };

    expectRejected (
        makeCommandEnvelope ("ui-version", "transport.stop", makeObject(), 2),
        drift::ui::CommandRejectionCode::unsupportedVersion,
        "An unknown protocol version is rejected");
    expectRejected (
        makeCommandEnvelope ("ui-type", "transport.rewind", makeObject()),
        drift::ui::CommandRejectionCode::unknownCommand,
        "An unknown command type is rejected");
    expectRejected (
        makeCommandEnvelope (
            "ui-motion", "world.setMotionPaused", makeMotionPayload (juce::var { "yes" })),
        drift::ui::CommandRejectionCode::invalidPayload,
        "A non-boolean motion state is rejected");
    expectRejected (
        makeCommandEnvelope (
            "ui-proximity-invalid", "proximity.setAuditionMode",
            makeProximityModePayload (juce::var { "random" })),
        drift::ui::CommandRejectionCode::invalidPayload,
        "An unknown proximity audition mode is rejected");
    expectRejected (
        makeCommandEnvelope ("bad message id", "transport.stop", makeObject()),
        drift::ui::CommandRejectionCode::invalidMessageId,
        "An invalid message ID is rejected");
    expectRejected (
        makeCommandEnvelope ("ui-tempo", "transport.setTempo", makeTempoPayload (300.0)),
        drift::ui::CommandRejectionCode::outOfRange,
        "An out-of-range tempo is rejected");
    expectRejected (
        makeCommandEnvelope ("ui-output", "midi.selectOutput", makeOutputPayload ("missing")),
        drift::ui::CommandRejectionCode::unknownId,
        "An unknown MIDI output ID is rejected");
    expectRejected (
        makeCommandEnvelope ("ui-phrase", "phrase.dragStart", makePhrasePayload ("unknown")),
        drift::ui::CommandRejectionCode::unknownId,
        "An unknown phrase ID is rejected");
    expectRejected (
        makeCommandEnvelope (
            "ui-position", "phrase.move", makeMovePayload ("bass", 1.1, 0.5)),
        drift::ui::CommandRejectionCode::outOfRange,
        "An out-of-range normalized position is rejected");
    expectRejected (
        makeCommandEnvelope (
            "ui-throw-unknown", "phrase.throw", makeThrowPayload ("unknown", 0.2, 0.1)),
        drift::ui::CommandRejectionCode::unknownId,
        "A throw for an unknown phrase is rejected");
    expectRejected (
        makeCommandEnvelope (
            "ui-throw-fast", "phrase.throw", makeThrowPayload ("bass", 2.0, 0.0)),
        drift::ui::CommandRejectionCode::outOfRange,
        "An unsafe throw velocity is rejected before native mutation");
    expectRejected (
        makeCommandEnvelope (
            "ui-throw-finite", "phrase.throw",
            makeThrowPayload ("bass", std::numeric_limits<double>::infinity(), 0.0)),
        drift::ui::CommandRejectionCode::outOfRange,
        "A non-finite throw velocity is rejected before native mutation");

    auto missingSession = makeThrowPayload ("bass", 0.2, 0.1);
    missingSession.getDynamicObject()->removeProperty ("dragSessionId");
    expectRejected (
        makeCommandEnvelope ("ui-throw-session", "phrase.throw", std::move (missingSession)),
        drift::ui::CommandRejectionCode::invalidPayload,
        "A throw without a stable drag session is malformed");

    const auto validMove = drift::ui::validateCommandEnvelope (
        makeCommandEnvelope (
            "ui-valid-move", "phrase.move", makeMovePayload ("bass", 0.25, 0.75)));
    expect (validMove.command.has_value(), "A normalized phrase move is accepted");
    expect (validMove.command && validMove.command->phraseId == "bass",
            "A validated move retains the stable phrase ID");
    if (validMove.command)
    {
        expectNear (validMove.command->positionX, 0.25,
                    "A validated move retains normalized x");
        expectNear (validMove.command->positionY, 0.75,
                    "A validated move retains normalized y");
    }

    const auto validThrow = drift::ui::validateCommandEnvelope (
        makeCommandEnvelope (
            "ui-valid-throw", "phrase.throw", makeThrowPayload ("bass", 0.6, -0.4)));
    expect (validThrow.command.has_value(), "A bounded phrase throw is accepted");
    if (validThrow.command)
    {
        expect (validThrow.command->phraseId == "bass"
                    && validThrow.command->dragSessionId == "drag-1",
                "A validated throw retains stable phrase and drag IDs");
        expectNear (validThrow.command->velocityX, 0.6,
                    "A validated throw retains normalized horizontal velocity");
        expectNear (validThrow.command->velocityY, -0.4,
                    "A validated throw retains normalized vertical velocity");
    }

    auto oversizedPayload = makeObject();
    oversizedPayload.getDynamicObject()->setProperty (
        "blob", juce::String::repeatedString ("x", 5000));
    expectRejected (
        makeCommandEnvelope ("ui-large", "transport.play", std::move (oversizedPayload)),
        drift::ui::CommandRejectionCode::payloadTooLarge,
        "An oversized command payload is rejected");

    expect (engine.snapshot().playing,
            "Rejected commands cannot stop or otherwise change the running transport");
    expectNear (engine.snapshot().bpm, 120.0,
                "Rejected numeric values cannot change authoritative tempo");
    expect (midiMessagesEqual (sink.messages(), scheduledBeforeRejections),
            "Rejected commands cannot change scheduled MIDI messages");
    expect (outputSelectionCount == 0,
            "An unknown output ID cannot reach the MIDI selection handler");
}

void testBridgeReconnectPreservesNativePlayback()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    auto connectCount = 0;
    const drift::ui::CommandHandlers handlers {
        [&connectCount, &engine] {
            ++connectCount;
            engine.recordBridgeReconnect();
        },
        [&engine] { engine.play(); },
        [&engine] { engine.stop(); },
        [&engine] (double bpm) { engine.setBpm (bpm); },
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {},
    };

    drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope ("ui-play", "transport.play", makeObject()), handlers);
    const auto scheduledBeforeReload = sink.messages();
    clock.advance (0.75);

    const auto firstConnect = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope ("ui-connect-1", "app.connect", makeObject()), handlers);
    const auto secondConnect = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope ("ui-connect-2", "app.connect", makeObject()), handlers);

    expect (firstConnect.command.has_value() && secondConnect.command.has_value(),
            "Every UI load can perform a fresh app.connect handshake");
    expect (connectCount == 2, "Repeated handshakes are accepted independently");
    expect (engine.snapshot().diagnostics.bridgeReconnectCount == 2,
            "Every accepted bridge handshake is visible in diagnostics");
    expect (engine.snapshot().phrases.size() == 4,
            "UI reconnect restores the complete authoritative phrase world");
    expect (engine.snapshot().playing, "UI reconnect does not stop native playback");
    expectNear (engine.snapshot().beatPosition, 1.5,
                "UI reconnect observes ongoing monotonic native position");
    expect (midiMessagesEqual (sink.messages(), scheduledBeforeReload),
            "UI reconnect does not alter already-scheduled event timestamps");

    auto* readyPayload = new juce::DynamicObject();
    readyPayload->setProperty ("protocolVersion", drift::ui::bridgeProtocolVersion);
    const auto readyEnvelope = drift::ui::makeEventEnvelope (
        "native-ready", "app.ready", juce::var { readyPayload });
    expect (static_cast<int> (readyEnvelope.getProperty ("protocolVersion", 0))
                == drift::ui::bridgeProtocolVersion,
            "Native app.ready uses the agreed protocol version");
    expect (readyEnvelope.getProperty ("type", juce::var {}).toString() == "app.ready",
            "Native app.ready uses the versioned event envelope");

    const drift::ui::CommandRejection rejection {
        "ui-invalid",
        drift::ui::CommandRejectionCode::unknownCommand,
        "The command type is unknown",
    };
    const auto rejectionPayload = drift::ui::makeCommandRejectedPayload (rejection);
    expect (rejectionPayload.getProperty ("code", juce::var {}).toString() == "unknown_command",
            "Native command.rejected exposes a stable structured error code");
}
} // namespace

int main()
{
    testUiResourceProvider();
    testTransportUsesMonotonicClock();
    testQuantizationBoundaries();
    testPhraseSchedulingAcrossLoopAndBar();
    testInitialCompositionIsAuthoritative();
    testSpeedActivityMappingAndSmoothing();
    testProximityMappingAndTransformations();
    testCollisionVariantRulesAndCycle();
    testSpatialWorldFixedStepAndBoundaries();
    testSpatialWorldBoundsCatchUpWork();
    testSpatialWorldDragLifecycle();
    testSpatialWorldThrowLifecycle();
    testSpatialWorldMotionPauseLifecycle();
    testSpatialWorldCollisionContactLifecycle();
    testEngineCommandQueueCoalescingAndPressure();
    testCollisionQueuesAndAppliesBassVariantAtBar();
    testEveryCollisionPairQueuesItsMappedTarget();
    testSimultaneousCollisionsUseStablePriority();
    testSpeedActivityQueuesQuantizedVariantsAndCollisionWins();
    testProximityAuditionQuantizesWhileMotionIsFrozen();
    testFourPhrasesShareOneTransport();
    testMotionPauseDoesNotInterruptPlayback();
    testEngineIsIndependentOfUiUpdateTiming();
    testMidiOutputSelectionAndReplacement();
    testMidiOutputRoutesTimestampedPhrase();
    testMidiOutputFailureRecovery();
    testBridgeRejectsInvalidCommandsBeforeMutation();
    testBridgeReconnectPreservesNativePlayback();

    if (failures == 0)
        std::cout << "All Drift native tests passed\n";

    return failures == 0 ? 0 : 1;
}
