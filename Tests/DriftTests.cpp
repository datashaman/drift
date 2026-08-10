#include "Engine/Clock.h"
#include "Engine/TransportEngine.h"
#include "Music/MidiSink.h"
#include "Music/MidiOutput.h"
#include "Music/PhraseScheduler.h"
#include "Music/Quantizer.h"
#include "Music/Transport.h"
#include "UI/BridgeProtocol.h"
#include "UI/UiResourceProvider.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
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
            || std::abs (a.deliveryDelaySeconds - b.deliveryDelaySeconds) >= 1.0e-8)
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

juce::var makeOutputPayload (const juce::String& outputId)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("outputId", outputId);
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

void testEngineIsIndependentOfUiUpdateTiming()
{
    FakeClock clock;
    drift::music::RecordingMidiSink sink;
    drift::engine::TransportEngine engine { clock, sink };

    engine.play();
    const auto eventsAfterPlay = sink.messageCount();
    expect (eventsAfterPlay == 2, "Play schedules the first note pair immediately");

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
    expect (deviceState->messages.size() == 2,
            "Starting transport routes the first note-on and note-off to the output");
    expect (deviceState->messages[0].type == drift::music::MidiMessageType::noteOn,
            "The routed phrase begins with note-on");
    expect (deviceState->messages[1].type == drift::music::MidiMessageType::noteOff,
            "The routed phrase includes its paired note-off");
    expectNear (deviceState->messages[0].deliveryDelaySeconds, 0.0,
                "The first note is delivered immediately");
    expectNear (deviceState->messages[1].deliveryDelaySeconds, 0.375,
                "The note-off receives a tempo-derived delivery timestamp");

    expect (output.selectOutput ("backup"), "Playback can move to a replacement output");
    engine.reschedule();
    const auto replacementState = provider.stateFor ("backup");
    expect (replacementState->messages.size() == 2,
            "The replacement output receives a freshly timestamped note pair");

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
    };

    const auto playResult = drift::ui::dispatchCommandEnvelope (
        makeCommandEnvelope ("ui-play", "transport.play", makeObject()), handlers);
    expect (playResult.command.has_value(), "A valid versioned Play envelope is accepted");
    expect (engine.snapshot().playing, "The accepted Play command reaches transport state");
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
        [&connectCount] { ++connectCount; },
        [&engine] { engine.play(); },
        [&engine] { engine.stop(); },
        [&engine] (double bpm) { engine.setBpm (bpm); },
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
