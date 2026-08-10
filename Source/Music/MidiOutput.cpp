#include "Music/MidiOutput.h"

#include <algorithm>
#include <utility>

namespace drift::music
{
MidiOutputService::MidiOutputService (MidiOutputProvider& providerIn)
    : provider (providerIn)
{
    refreshOutputs();
}

MidiOutputService::~MidiOutputService()
{
    shutdown();
}

void MidiOutputService::refreshOutputs()
{
    try
    {
        outputs = provider.availableOutputs();
    }
    catch (...)
    {
        fail ("Could not enumerate MIDI outputs");
        outputs.clear();
        return;
    }

    if (device == nullptr)
        return;

    const auto selectedStillExists = std::any_of (
        outputs.begin(), outputs.end(), [this] (const auto& output) {
            return output.id == selectedOutputId;
        });

    if (! selectedStillExists)
        fail ("The selected MIDI output is no longer available");
}

bool MidiOutputService::selectOutput (const std::string& outputId)
{
    if (device != nullptr && outputId == selectedOutputId)
        return true;

    closeCurrentDevice();
    selectedOutputId.clear();
    errorMessage.clear();
    scheduledMessageCount = 0;
    status = MidiOutputStatus::disconnected;

    if (outputId.empty())
    {
        status = MidiOutputStatus::disconnected;
        return true;
    }

    refreshOutputs();

    if (status == MidiOutputStatus::error)
        return false;

    const auto exists = std::any_of (outputs.begin(), outputs.end(), [&outputId] (const auto& output) {
        return output.id == outputId;
    });

    if (! exists)
    {
        fail ("The selected MIDI output is unavailable");
        return false;
    }

    try
    {
        device = provider.openOutput (outputId);
    }
    catch (...)
    {
        device.reset();
    }

    if (device == nullptr)
    {
        fail ("Could not open the selected MIDI output");
        return false;
    }

    selectedOutputId = outputId;
    status = MidiOutputStatus::connected;
    return true;
}

void MidiOutputService::shutdown()
{
    closeCurrentDevice();
    selectedOutputId.clear();
    errorMessage.clear();
    scheduledMessageCount = 0;
    status = MidiOutputStatus::disconnected;
}

MidiOutputSnapshot MidiOutputService::snapshot() const
{
    return { outputs, selectedOutputId, status, errorMessage };
}

void MidiOutputService::schedule (const ScheduledMidiMessage& message)
{
    if (device == nullptr)
        return;

    try
    {
        if (device->schedule (message))
        {
            ++scheduledMessageCount;
            return;
        }
    }
    catch (...)
    {
    }

    fail ("The MIDI output stopped accepting messages");
}

void MidiOutputService::clear()
{
    silenceCurrentDevice();
    scheduledMessageCount = 0;
}

std::size_t MidiOutputService::messageCount() const
{
    return scheduledMessageCount;
}

void MidiOutputService::silenceCurrentDevice() noexcept
{
    if (device == nullptr)
        return;

    try
    {
        device->clearPendingMessages();
    }
    catch (...)
    {
    }

    try
    {
        device->panic();
    }
    catch (...)
    {
    }
}

void MidiOutputService::closeCurrentDevice() noexcept
{
    silenceCurrentDevice();
    device.reset();
}

void MidiOutputService::fail (std::string message)
{
    closeCurrentDevice();
    selectedOutputId.clear();
    scheduledMessageCount = 0;
    status = MidiOutputStatus::error;
    errorMessage = std::move (message);
}

const char* midiOutputStatusName (MidiOutputStatus status) noexcept
{
    switch (status)
    {
        case MidiOutputStatus::disconnected: return "disconnected";
        case MidiOutputStatus::connected: return "connected";
        case MidiOutputStatus::error: return "error";
    }

    return "error";
}
} // namespace drift::music
