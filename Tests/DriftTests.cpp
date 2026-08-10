#include "Engine/Clock.h"
#include "Engine/TransportEngine.h"
#include "Music/MidiSink.h"
#include "Music/PhraseScheduler.h"
#include "Music/Quantizer.h"
#include "Music/Transport.h"
#include "UI/UiResourceProvider.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

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
} // namespace

int main()
{
    testUiResourceProvider();
    testTransportUsesMonotonicClock();
    testQuantizationBoundaries();
    testPhraseSchedulingAcrossLoopAndBar();
    testEngineIsIndependentOfUiUpdateTiming();

    if (failures == 0)
        std::cout << "All Drift native tests passed\n";

    return failures == 0 ? 0 : 1;
}
