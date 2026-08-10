#include "App/MainComponent.h"

#include "UI/UiResourceProvider.h"

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
    const auto* object = command.getDynamicObject();

    if (object == nullptr)
        return;

    const auto type = object->getProperty ("type").toString();

    if (type == "ui.ready")
    {
        uiReady = true;
    }
    else if (type == "transport.play")
    {
        engine.play();
    }
    else if (type == "transport.stop")
    {
        engine.stop();
    }
    else if (type == "transport.setTempo")
    {
        const auto bpm = object->getProperty ("bpm");

        if (bpm.isInt() || bpm.isInt64() || bpm.isDouble())
            engine.setBpm (static_cast<double> (bpm));
    }
    else if (type == "midi.selectOutput")
    {
        engine.selectMidiOutput (object->getProperty ("outputId").toString().toStdString());
    }

    if (uiReady)
        publishState();
}

void MainComponent::publishState()
{
    const auto state = engine.snapshot();
    auto* object = new juce::DynamicObject();
    object->setProperty ("playing", state.transport.playing);
    object->setProperty ("bpm", state.transport.bpm);
    object->setProperty ("beatPosition", state.transport.beatPosition);
    object->setProperty ("bar", state.transport.bar);
    object->setProperty ("beat", state.transport.beat);
    object->setProperty (
        "scheduledEventCount", static_cast<juce::int64> (state.transport.scheduledEventCount));

    juce::Array<juce::var> outputs;
    for (const auto& output : state.midiOutput.outputs)
    {
        auto* outputObject = new juce::DynamicObject();
        outputObject->setProperty ("id", juce::String { output.id });
        outputObject->setProperty ("name", juce::String { output.name });
        outputs.add (juce::var { outputObject });
    }

    object->setProperty ("midiOutputs", juce::var { outputs });
    object->setProperty (
        "selectedMidiOutputId", juce::String { state.midiOutput.selectedOutputId });
    object->setProperty ("midiStatus", drift::music::midiOutputStatusName (state.midiOutput.status));
    object->setProperty ("midiError", juce::String { state.midiOutput.errorMessage });

    browser.emitEventIfBrowserIsVisible ("drift.state", juce::var { object });
}
} // namespace drift::app
