#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace drift::app
{
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    juce::WebBrowserComponent browser;
};
} // namespace drift::app
