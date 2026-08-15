#include "Engine/Clock.h"
#include "Engine/TransportEngine.h"
#include "Music/MidiSink.h"
#include "UI/BridgeProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double simulatedDurationSeconds = 600.0;
constexpr double engineTickSeconds = 0.002;
constexpr double bpm = 120.0;
constexpr double beatsPerSecond = bpm / 60.0;
constexpr auto totalEngineTicks = static_cast<int> (
    simulatedDurationSeconds / engineTickSeconds);

class FakeClock final : public drift::engine::Clock
{
public:
    double nowSeconds() const override { return currentTime; }
    void set (double seconds) { currentTime = seconds; }

private:
    double currentTime = 0.0;
};

struct SimulationResult
{
    drift::engine::EngineSnapshot snapshot;
    std::vector<drift::music::ScheduledMidiMessage> messages;
    double maximumPhaseErrorBeats = 0.0;
    double maximumTimestampErrorSeconds = 0.0;
    bool watermarkMovedBackward = false;
    int uiStallCount = 0;
    int uiObservationCount = 0;
    int dragMoveCount = 0;
    int throwCount = 0;
};

juce::var makeConnectCommand (int sequence)
{
    auto* payload = new juce::DynamicObject();
    auto* envelope = new juce::DynamicObject();
    envelope->setProperty ("protocolVersion", drift::ui::bridgeProtocolVersion);
    envelope->setProperty ("messageId", "stress-" + juce::String { sequence });
    envelope->setProperty ("type", "app.connect");
    envelope->setProperty ("payload", juce::var { payload });
    return juce::var { envelope };
}

SimulationResult runSimulation (bool stressUi)
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };
    const drift::ui::CommandHandlers handlers {
        [&engine] {
            engine.endAllPhraseDrags();
            engine.recordBridgeReconnect();
        },
        {},
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
    SimulationResult result;
    auto previousWatermark = 0.0;
    auto wasUiStalled = false;
    auto reconnectSequence = 0;

    engine.play();

    for (auto tick = 1; tick <= totalEngineTicks; ++tick)
    {
        const auto now = static_cast<double> (tick) * engineTickSeconds;
        clock.set (now);

        if (stressUi)
        {
            const auto dragPhase = tick % 3000;
            const std::array<std::string, 4> phraseIds {
                "drums", "bass", "chords", "melody"
            };
            const auto cycle = tick / 3000;
            const auto& phraseId = phraseIds[static_cast<std::size_t> (cycle) % phraseIds.size()];
            if (dragPhase == 1)
                engine.beginPhraseDrag (phraseId);
            else if (dragPhase > 1 && dragPhase < 1000)
            {
                const auto movement = static_cast<double> (dragPhase) / 1000.0;
                if (engine.moveDraggedPhrase (phraseId, { movement, 1.0 - movement }))
                    ++result.dragMoveCount;
            }
            else if (dragPhase == 1000)
            {
                const auto direction = cycle % 2 == 0 ? 1.0 : -1.0;
                if (engine.throwPhrase (phraseId, { 0.8 * direction, -0.45 }))
                    ++result.throwCount;
            }
        }

        engine.tick();

        const auto state = engine.snapshot();
        const auto expectedBeat = now * beatsPerSecond;
        result.maximumPhaseErrorBeats = std::max (
            result.maximumPhaseErrorBeats,
            std::abs (state.beatPosition - expectedBeat));

        if (state.diagnostics.schedulingWatermarkBeat
            + drift::engine::TransportEngine::timingToleranceSeconds < previousWatermark)
        {
            result.watermarkMovedBackward = true;
        }

        previousWatermark = state.diagnostics.schedulingWatermarkBeat;

        const auto uiStalled = stressUi && (tick % 25000) < 500;
        if (uiStalled && ! wasUiStalled)
            ++result.uiStallCount;
        if (! uiStalled)
            ++result.uiObservationCount;
        wasUiStalled = uiStalled;

        if (stressUi && tick % 15000 == 0)
        {
            ++reconnectSequence;
            const auto reconnect = drift::ui::dispatchCommandEnvelope (
                makeConnectCommand (reconnectSequence), handlers);

            if (! reconnect.command.has_value())
                result.watermarkMovedBackward = true;
        }
    }

    result.snapshot = engine.snapshot();
    result.messages = sink.messages();

    for (const auto& message : result.messages)
    {
        const auto expectedTimestamp = message.beat / beatsPerSecond;
        result.maximumTimestampErrorSeconds = std::max (
            result.maximumTimestampErrorSeconds,
            std::abs (message.deliveryTimeSeconds - expectedTimestamp));
    }

    return result;
}

bool messagesMatch (const std::vector<drift::music::ScheduledMidiMessage>& left,
                    const std::vector<drift::music::ScheduledMidiMessage>& right)
{
    if (left.size() != right.size())
        return false;

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto& a = left[index];
        const auto& b = right[index];

        if (a.type != b.type || a.channel != b.channel || a.note != b.note
            || a.velocity != b.velocity
            || std::abs (a.beat - b.beat)
                   > drift::engine::TransportEngine::timingToleranceSeconds
            || std::abs (a.deliveryTimeSeconds - b.deliveryTimeSeconds)
                   > drift::engine::TransportEngine::timingToleranceSeconds
            || std::abs (a.scheduledAtSeconds - b.scheduledAtSeconds)
                   > drift::engine::TransportEngine::timingToleranceSeconds)
        {
            return false;
        }
    }

    return true;
}

bool hasPairedNotes (const std::vector<drift::music::ScheduledMidiMessage>& messages)
{
    std::array<std::array<int, 128>, 16> noteBalances {};

    for (const auto& message : messages)
    {
        const auto channel = std::clamp (message.channel, 1, 16) - 1;
        const auto note = std::clamp (message.note, 0, 127);
        noteBalances[static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)]
            += message.type == drift::music::MidiMessageType::noteOn ? 1 : -1;
    }

    return std::all_of (noteBalances.begin(), noteBalances.end(), [] (const auto& channel) {
        return std::all_of (channel.begin(), channel.end(), [] (auto balance) {
            return balance == 0;
        });
    });
}

bool hasUniqueLoopBoundaries (const std::vector<drift::music::ScheduledMidiMessage>& messages,
                              double schedulingWatermarkBeat)
{
    std::set<long long> observedBoundaries;

    for (const auto& message : messages)
    {
        if (message.type != drift::music::MidiMessageType::noteOn
            || message.channel != 1 || message.note != 36)
            continue;

        const auto loop = std::llround (message.beat / 4.0);
        const auto boundaryBeat = static_cast<double> (loop) * 4.0;

        if (std::abs (message.beat - boundaryBeat)
            > drift::engine::TransportEngine::timingToleranceSeconds)
        {
            continue;
        }

        if (! observedBoundaries.insert (loop).second)
            return false;
    }

    const auto expectedBoundaryCount = static_cast<std::size_t> (
        std::floor (schedulingWatermarkBeat / 4.0) + 1.0);
    return observedBoundaries.size() == expectedBoundaryCount;
}
} // namespace

int main()
{
    const auto stressed = runSimulation (true);
    const auto repeatedStress = runSimulation (true);
    std::vector<std::string> failures;
    const auto tolerance = drift::engine::TransportEngine::timingToleranceSeconds;

    const auto require = [&failures] (bool condition, std::string failure) {
        if (! condition)
            failures.push_back (std::move (failure));
    };

    require (std::abs (stressed.snapshot.beatPosition - simulatedDurationSeconds * beatsPerSecond)
                 <= tolerance,
             "transport phase exceeded tolerance");
    require (stressed.maximumPhaseErrorBeats <= tolerance,
             "phase drift accumulated during the run");
    require (stressed.maximumTimestampErrorSeconds <= tolerance,
             "recorded MIDI timestamp exceeded tolerance");
    require (messagesMatch (stressed.messages, repeatedStress.messages),
             "identical collision stress produced different MIDI or timestamps");
    require (hasPairedNotes (stressed.messages), "an outbound note was unpaired");
    require (hasUniqueLoopBoundaries (
                 stressed.messages, stressed.snapshot.diagnostics.schedulingWatermarkBeat),
             "a loop boundary was duplicated or omitted");
    require (! stressed.watermarkMovedBackward, "scheduling watermark moved backward");
    require (stressed.snapshot.diagnostics.lateMidiEventCount == 0,
             "late MIDI events were recorded");
    require (stressed.snapshot.diagnostics.maximumEngineLatenessSeconds <= tolerance,
             "engine lateness exceeded tolerance");
    require (stressed.snapshot.diagnostics.bridgeReconnectCount == 20,
             "not all simulated bridge reconnects were recorded");
    require (stressed.snapshot.diagnostics.physicsStepCount == 72000,
             "the native world did not integrate at exactly 120 Hz");
    require (stressed.snapshot.diagnostics.physicsCatchUpLimitHitCount == 0,
             "regular engine ticks unexpectedly capped physics catch-up");
    require (std::all_of (
                 stressed.snapshot.phrases.begin(),
                 stressed.snapshot.phrases.end(),
                 [] (const auto& phrase) {
                     return phrase.position.x >= phrase.radius
                            && phrase.position.x <= 1.0 - phrase.radius
                            && phrase.position.y >= phrase.radius
                            && phrase.position.y <= 1.0 - phrase.radius;
                 }),
             "a phrase escaped normalized world bounds during the stress run");
    require (stressed.uiStallCount >= 10, "the harness did not introduce enough UI stalls");
    require (stressed.dragMoveCount > 90000,
             "the harness did not generate sustained drag pressure");
    require (stressed.throwCount >= 95,
             "the harness did not rapidly throw all four phrases");
    require (stressed.snapshot.diagnostics.collisionIntentQueuedCount >= 1
                 && stressed.snapshot.diagnostics.collisionTransitionAppliedCount >= 1,
             "the stress run did not exercise the collision transition path");

    std::cout << std::fixed << std::setprecision (9)
              << "Drift deterministic timing stress report\n"
              << "Result: " << (failures.empty() ? "PASS" : "FAIL") << '\n'
              << "Simulated duration: " << simulatedDurationSeconds << " s\n"
              << "Engine ticks: " << totalEngineTicks << '\n'
              << "UI stalls: " << stressed.uiStallCount << '\n'
              << "UI observations: " << stressed.uiObservationCount << '\n'
              << "Drag moves: " << stressed.dragMoveCount << '\n'
              << "Throws: " << stressed.throwCount << '\n'
              << "Bridge reconnects: "
              << stressed.snapshot.diagnostics.bridgeReconnectCount << '\n'
              << "Physics steps: " << stressed.snapshot.diagnostics.physicsStepCount << '\n'
              << "Physics catch-up steps: "
              << stressed.snapshot.diagnostics.physicsCatchUpStepCount << '\n'
              << "Physics catch-up caps: "
              << stressed.snapshot.diagnostics.physicsCatchUpLimitHitCount << '\n'
              << "Collision contacts: "
              << stressed.snapshot.diagnostics.collisionContactBeginCount << '\n'
              << "Collision intents queued: "
              << stressed.snapshot.diagnostics.collisionIntentQueuedCount << '\n'
              << "Collision transitions applied: "
              << stressed.snapshot.diagnostics.collisionTransitionAppliedCount << '\n'
              << "Recorded MIDI messages: " << stressed.messages.size() << '\n'
              << "Scheduling watermark: "
              << stressed.snapshot.diagnostics.schedulingWatermarkBeat << " beats\n"
              << "Late MIDI events: " << stressed.snapshot.diagnostics.lateMidiEventCount << '\n'
              << "Maximum engine lateness: "
              << stressed.snapshot.diagnostics.maximumEngineLatenessSeconds * 1000.0
              << " ms\n"
              << "Maximum phase error: " << stressed.maximumPhaseErrorBeats << " beats\n"
              << "Maximum MIDI timestamp error: "
              << stressed.maximumTimestampErrorSeconds * 1000.0 << " ms\n"
              << "Tolerance: " << tolerance * 1000.0 << " ms\n";

    for (const auto& failure : failures)
        std::cerr << "FAIL: " << failure << '\n';

    return failures.empty() ? 0 : 1;
}
