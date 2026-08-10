#pragma once

#include "Engine/EngineController.h"
#include "UI/BridgeProtocol.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <string>
#include <unordered_set>

namespace drift::app
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();

    void resized() override;

private:
    void timerCallback() override;
    void handleCommand (juce::var command);
    void publishReady();
    void publishRejection (drift::ui::CommandRejection rejection,
                           bool alreadyCounted = false);
    void publishState();
    void publishWorldSnapshot (const drift::engine::ControllerSnapshot& state);
    void publishEvent (const juce::String& type, juce::var payload);
    juce::String nextEventId();

    drift::engine::EngineController engine;
    juce::WebBrowserComponent browser;
    bool uiReady = false;
    int midiRefreshCountdown = 30;
    juce::uint64 eventSequence = 0;
    juce::uint64 worldSnapshotSequence = 0;
    std::size_t lastPublishedWorldRevision = 0;
    std::size_t droppedWorldSnapshotCount = 0;
    double lastWorldPublicationSeconds = 0.0;
    double maximumWorldPublicationIntervalSeconds = 0.0;
    std::size_t bridgeRejectedCommandCount = 0;
    std::unordered_set<std::string> availableMidiOutputIds;
};
} // namespace drift::app
