#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

namespace drift::ui
{
juce::String contentTypeForPath (const juce::String& path);

std::optional<juce::WebBrowserComponent::Resource> loadResource (
    const juce::File& root,
    const juce::String& requestedPath);

juce::WebBrowserComponent::Options makeBrowserOptions (const juce::File& root);
} // namespace drift::ui
