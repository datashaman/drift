#include "App/MainComponent.h"

#include "UI/BridgeProtocol.h"
#include "UI/UiResourceProvider.h"

#include <algorithm>
#include <utility>

namespace drift::app
{
namespace
{
juce::File getPackagedUiDirectory()
{
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    return executable.getParentDirectory()
                     .getParentDirectory()
                     .getChildFile ("Resources")
                     .getChildFile ("ui");
}
} // namespace

MainComponent::MainComponent()
    : browser (drift::ui::makeBrowserOptions (
          getPackagedUiDirectory(),
          [this] (juce::var command) { handleCommand (std::move (command)); }))
{
    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
    setSize (1100, 720);
}

void MainComponent::resized()
{
    browser.setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    if (--midiRefreshCountdown <= 0)
    {
        engine.refreshMidiOutputs();
        midiRefreshCountdown = 30;
    }

    if (uiReady)
        publishState();
}

void MainComponent::handleCommand (juce::var command)
{
    const drift::ui::CommandHandlers handlers {
        [this] {
            uiReady = true;
            engine.recordBridgeReconnect();
        },
        [this] { engine.play(); },
        [this] { engine.stop(); },
        [this] (double bpm) { engine.setBpm (bpm); },
        [this] (const std::string& outputId) { engine.selectMidiOutput (outputId); },
        [this] (const std::string& outputId) {
            const auto state = engine.snapshot();
            return std::any_of (
                state.midiOutput.outputs.begin(),
                state.midiOutput.outputs.end(),
                [&outputId] (const auto& output) { return output.id == outputId; });
        },
    };

    const auto result = drift::ui::dispatchCommandEnvelope (command, handlers);

    if (result.rejection.has_value())
    {
        publishEvent ("command.rejected",
                      drift::ui::makeCommandRejectedPayload (*result.rejection));
        return;
    }

    if (result.command->type == drift::ui::BridgeCommandType::appConnect)
    {
        publishReady();
        publishState();
        return;
    }

    if (uiReady)
        publishState();
}

void MainComponent::publishReady()
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("protocolVersion", drift::ui::bridgeProtocolVersion);
    publishEvent ("app.ready", juce::var { payload });
}

void MainComponent::publishState()
{
    const auto state = engine.snapshot();
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("playing", state.transport.playing);
    payload->setProperty ("bpm", state.transport.bpm);
    payload->setProperty ("beatPosition", state.transport.beatPosition);
    payload->setProperty ("bar", state.transport.bar);
    payload->setProperty ("beat", state.transport.beat);
    payload->setProperty (
        "scheduledEventCount", static_cast<juce::int64> (state.transport.scheduledEventCount));

    juce::Array<juce::var> outputs;
    for (const auto& output : state.midiOutput.outputs)
    {
        auto* outputObject = new juce::DynamicObject();
        outputObject->setProperty ("id", juce::String { output.id });
        outputObject->setProperty ("name", juce::String { output.name });
        outputs.add (juce::var { outputObject });
    }

    payload->setProperty ("midiOutputs", juce::var { outputs });
    payload->setProperty (
        "selectedMidiOutputId", juce::String { state.midiOutput.selectedOutputId });
    payload->setProperty (
        "midiStatus", drift::music::midiOutputStatusName (state.midiOutput.status));
    payload->setProperty ("midiError", juce::String { state.midiOutput.errorMessage });

    auto* diagnostics = new juce::DynamicObject();
    diagnostics->setProperty (
        "schedulingWatermarkBeat", state.transport.diagnostics.schedulingWatermarkBeat);
    diagnostics->setProperty (
        "lateMidiEventCount",
        static_cast<juce::int64> (state.transport.diagnostics.lateMidiEventCount));
    diagnostics->setProperty (
        "maximumEngineLatenessMs",
        state.transport.diagnostics.maximumEngineLatenessSeconds * 1000.0);
    diagnostics->setProperty (
        "bridgeReconnectCount",
        static_cast<juce::int64> (state.transport.diagnostics.bridgeReconnectCount));
    payload->setProperty ("diagnostics", juce::var { diagnostics });

    publishEvent ("transport.state", juce::var { payload });
    publishWorldSnapshot (state);
}

void MainComponent::publishWorldSnapshot (const drift::engine::ControllerSnapshot& state)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("sequence", static_cast<juce::int64> (++worldSnapshotSequence));
    payload->setProperty ("engineTimeMs", state.transport.engineTimeSeconds * 1000.0);

    auto* transport = new juce::DynamicObject();
    transport->setProperty ("playing", state.transport.playing);
    transport->setProperty ("bpm", state.transport.bpm);
    transport->setProperty ("bar", state.transport.bar);
    transport->setProperty ("beat", state.transport.beat);
    payload->setProperty ("transport", juce::var { transport });

    juce::Array<juce::var> phrases;
    for (const auto& phrase : state.transport.phrases)
    {
        auto* phraseObject = new juce::DynamicObject();
        phraseObject->setProperty ("id", juce::String { phrase.id });
        phraseObject->setProperty ("name", juce::String { phrase.name });
        phraseObject->setProperty (
            "role", drift::music::phraseRoleName (phrase.role));
        phraseObject->setProperty (
            "currentVariantId", juce::String { phrase.currentVariantId });
        phraseObject->setProperty ("midiChannel", phrase.midiChannel);
        phraseObject->setProperty ("playing", phrase.playing);

        auto* position = new juce::DynamicObject();
        position->setProperty ("x", phrase.position.x);
        position->setProperty ("y", phrase.position.y);
        phraseObject->setProperty ("position", juce::var { position });
        phrases.add (juce::var { phraseObject });
    }

    payload->setProperty ("phrases", juce::var { phrases });
    publishEvent ("world.snapshot", juce::var { payload });
}

void MainComponent::publishEvent (const juce::String& type, juce::var payload)
{
    browser.emitEventIfBrowserIsVisible (
        "drift.event",
        drift::ui::makeEventEnvelope (nextEventId(), type, std::move (payload)));
}

juce::String MainComponent::nextEventId()
{
    return "native-" + juce::String { ++eventSequence };
}
} // namespace drift::app
