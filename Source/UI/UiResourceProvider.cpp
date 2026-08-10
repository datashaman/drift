#include "UI/UiResourceProvider.h"

#include <cstring>
#include <utility>
#include <vector>

namespace drift::ui
{
juce::String contentTypeForPath (const juce::String& path)
{
    if (path.endsWithIgnoreCase (".html"))
        return "text/html";
    if (path.endsWithIgnoreCase (".js"))
        return "text/javascript";
    if (path.endsWithIgnoreCase (".css"))
        return "text/css";
    if (path.endsWithIgnoreCase (".svg"))
        return "image/svg+xml";
    if (path.endsWithIgnoreCase (".png"))
        return "image/png";
    if (path.endsWithIgnoreCase (".woff2"))
        return "font/woff2";

    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> loadResource (
    const juce::File& root,
    const juce::String& requestedPath)
{
    auto relativePath = requestedPath == "/"
                            ? juce::String { "index.html" }
                            : requestedPath.trimCharactersAtStart ("/");

    if (relativePath.isEmpty() || relativePath.contains ("..") || relativePath.startsWithChar ('/'))
        return std::nullopt;

    const auto file = root.getChildFile (relativePath);

    if (! file.isAChildOf (root) || ! file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock contents;

    if (! file.loadFileAsData (contents))
        return std::nullopt;

    std::vector<std::byte> bytes (contents.getSize());
    std::memcpy (bytes.data(), contents.getData(), contents.getSize());

    return juce::WebBrowserComponent::Resource {
        std::move (bytes),
        contentTypeForPath (relativePath),
    };
}

juce::WebBrowserComponent::Options makeBrowserOptions (
    const juce::File& root,
    NativeEventListener commandListener)
{
    auto options = juce::WebBrowserComponent::Options {}.withResourceProvider (
        [root] (const juce::String& path) { return loadResource (root, path); });

    if (commandListener)
    {
        options = options.withNativeIntegrationEnabled()
                         .withEventListener ("drift.command", std::move (commandListener));
    }

    return options;
}
} // namespace drift::ui
