#include "Music/JuceMidiOutput.h"

#include <algorithm>

namespace drift::music
{
namespace
{
juce::MidiMessage toJuceMessage (const ScheduledMidiMessage& message)
{
    if (message.type == MidiMessageType::noteOn)
    {
        return juce::MidiMessage::noteOn (
            message.channel,
            message.note,
            static_cast<juce::uint8> (std::clamp (message.velocity, 0, 127)));
    }

    return juce::MidiMessage::noteOff (message.channel, message.note);
}
} // namespace

JuceMidiOutputDevice::JuceMidiOutputDevice (std::unique_ptr<juce::MidiOutput> outputIn)
    : output (std::move (outputIn))
{
    output->startBackgroundThread();
}

JuceMidiOutputDevice::~JuceMidiOutputDevice()
{
    output->clearAllPendingMessages();
    output->stopBackgroundThread();
}

bool JuceMidiOutputDevice::schedule (const ScheduledMidiMessage& message)
{
    const auto juceMessage = toJuceMessage (message);

    if (message.deliveryDelaySeconds <= 0.001)
    {
        output->sendMessageNow (juceMessage);
        return true;
    }

    juce::MidiBuffer buffer;
    buffer.addEvent (juceMessage, 0);
    const auto deliveryTimeMs = juce::Time::getMillisecondCounterHiRes()
                                + (message.deliveryDelaySeconds * 1000.0);
    output->sendBlockOfMessages (buffer, deliveryTimeMs, 1000.0);
    return true;
}

void JuceMidiOutputDevice::clearPendingMessages()
{
    output->clearAllPendingMessages();
}

void JuceMidiOutputDevice::panic()
{
    for (auto channel = 1; channel <= 16; ++channel)
    {
        output->sendMessageNow (juce::MidiMessage::allNotesOff (channel));
        output->sendMessageNow (juce::MidiMessage::allSoundOff (channel));
    }
}

std::vector<MidiOutputInfo> JuceMidiOutputProvider::availableOutputs()
{
    std::vector<MidiOutputInfo> result;

    for (const auto& output : juce::MidiOutput::getAvailableDevices())
        result.push_back ({ output.identifier.toStdString(), output.name.toStdString() });

    return result;
}

std::unique_ptr<MidiOutputDevice> JuceMidiOutputProvider::openOutput (const std::string& outputId)
{
    auto output = juce::MidiOutput::openDevice (juce::String { outputId });

    if (output == nullptr)
        return {};

    return std::make_unique<JuceMidiOutputDevice> (std::move (output));
}
} // namespace drift::music
