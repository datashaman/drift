#pragma once

#include "Engine/Clock.h"
#include "Engine/SpatialWorld.h"
#include "Music/MidiSink.h"
#include "Music/Phrase.h"
#include "Music/PhraseScheduler.h"
#include "Music/Transport.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace drift::engine
{
struct EngineDiagnostics
{
    double schedulingWatermarkBeat = 0.0;
    std::size_t lateMidiEventCount = 0;
    double maximumEngineLatenessSeconds = 0.0;
    std::size_t bridgeReconnectCount = 0;
    std::size_t physicsStepCount = 0;
    std::size_t physicsCatchUpStepCount = 0;
    std::size_t physicsCatchUpLimitHitCount = 0;
    std::size_t collisionContactBeginCount = 0;
    std::size_t collisionIntentQueuedCount = 0;
    std::size_t collisionTransitionAppliedCount = 0;
};

struct PhraseSnapshot
{
    std::string id;
    std::string name;
    music::PhraseRole role = music::PhraseRole::bass;
    std::string currentVariantId;
    std::optional<std::string> pendingVariantId;
    std::optional<double> pendingVariantApplyBeat;
    int midiChannel = 1;
    music::NormalizedPosition position;
    music::NormalizedVelocity velocity;
    double radius = 0.045;
    double mass = 1.0;
    bool dragged = false;
    bool playing = false;
};

struct CollisionSnapshot
{
    std::string firstPhraseId;
    std::string secondPhraseId;
    std::string targetPhraseId;
    bool touching = false;
    double cooldownRemainingSeconds = 0.0;
};

struct EngineSnapshot
{
    bool playing = false;
    double bpm = 120.0;
    double beatPosition = 0.0;
    int bar = 1;
    double beat = 1.0;
    double engineTimeSeconds = 0.0;
    std::size_t worldRevision = 0;
    std::size_t scheduledEventCount = 0;
    std::vector<PhraseSnapshot> phrases;
    std::vector<CollisionSnapshot> collisions;
    EngineDiagnostics diagnostics;
};

enum class MusicalIntentType
{
    changeVariant,
};

struct MusicalIntent
{
    std::string phraseId;
    MusicalIntentType type = MusicalIntentType::changeVariant;
    std::string variantId;
    double applyAtBeat = 0.0;
};

class TransportEngine
{
public:
    static constexpr double timingToleranceSeconds = 1.0e-6;

    TransportEngine (Clock& clockIn, music::MidiSink& sinkIn);

    void play();
    void stop();
    bool setBpm (double bpm);
    void reschedule();
    void recordBridgeReconnect();
    void tick();
    bool beginPhraseDrag (const std::string& phraseId);
    bool moveDraggedPhrase (const std::string& phraseId,
                            music::NormalizedPosition position);
    bool endPhraseDrag (const std::string& phraseId);
    bool throwPhrase (const std::string& phraseId,
                      music::NormalizedVelocity velocity);
    void endAllPhraseDrags();
    bool containsPhrase (const std::string& phraseId) const noexcept;

    EngineSnapshot snapshot() const;

private:
    static constexpr double lookAheadSeconds = 0.1;
    static constexpr double barLengthBeats = 4.0;

    void processCollisionContacts (double currentBeat);
    bool queueIntent (const MusicalIntent& intent);
    void applyDueIntents (double currentBeat);
    void schedulePhraseRange (const music::Phrase& phrase,
                              double startBeat,
                              double endBeat,
                              music::MidiSink& targetSink) const;
    music::Phrase* findPhrase (const std::string& phraseId) noexcept;

    music::MidiSink& sink;
    Clock& clock;
    music::Transport transport;
    std::vector<music::Phrase> phrases;
    SpatialWorld world;
    music::PhraseScheduler scheduler;
    double scheduledThroughBeat = 0.0;
    EngineDiagnostics diagnostics;
};
} // namespace drift::engine
