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

    if (uiReady)
        publishState();
}

void MainComponent::publishState()
{
    const auto state = engine.snapshot();
    auto* object = new juce::DynamicObject();
    object->setProperty ("playing", state.playing);
    object->setProperty ("bpm", state.bpm);
    object->setProperty ("beatPosition", state.beatPosition);
    object->setProperty ("bar", state.bar);
    object->setProperty ("beat", state.beat);
    object->setProperty ("scheduledEventCount", static_cast<juce::int64> (state.scheduledEventCount));

    browser.emitEventIfBrowserIsVisible ("drift.state", juce::var { object });
}
} // namespace drift::app
