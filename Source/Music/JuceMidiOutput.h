#pragma once

#include "Music/MidiOutput.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace drift::music
{
class JuceMidiOutputDevice final : public MidiOutputDevice
{
public:
    explicit JuceMidiOutputDevice (std::unique_ptr<juce::MidiOutput> outputIn);
    ~JuceMidiOutputDevice() override;

    bool schedule (const ScheduledMidiMessage& message) override;
    void clearPendingMessages() override;
    void panic() override;

private:
    std::unique_ptr<juce::MidiOutput> output;
};

class JuceMidiOutputProvider final : public MidiOutputProvider
{
public:
    std::vector<MidiOutputInfo> availableOutputs() override;
    std::unique_ptr<MidiOutputDevice> openOutput (const std::string& outputId) override;
};
} // namespace drift::music
