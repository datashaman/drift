#pragma once

#include "Music/MidiSink.h"

#include <memory>
#include <string>
#include <vector>

namespace drift::music
{
struct MidiOutputInfo
{
    std::string id;
    std::string name;
};

enum class MidiOutputStatus
{
    disconnected,
    connected,
    error,
};

struct MidiOutputSnapshot
{
    std::vector<MidiOutputInfo> outputs;
    std::string selectedOutputId;
    MidiOutputStatus status = MidiOutputStatus::disconnected;
    std::string errorMessage;
};

class MidiOutputDevice
{
public:
    virtual ~MidiOutputDevice() = default;

    virtual bool schedule (const ScheduledMidiMessage& message) = 0;
    virtual void clearPendingMessages() = 0;
    virtual void panic() = 0;
};

class MidiOutputProvider
{
public:
    virtual ~MidiOutputProvider() = default;

    virtual std::vector<MidiOutputInfo> availableOutputs() = 0;
    virtual std::unique_ptr<MidiOutputDevice> openOutput (const std::string& outputId) = 0;
};

class MidiOutputService final : public MidiSink
{
public:
    explicit MidiOutputService (MidiOutputProvider& providerIn);
    ~MidiOutputService() override;

    void refreshOutputs();
    bool selectOutput (const std::string& outputId);
    void shutdown();

    MidiOutputSnapshot snapshot() const;

    void schedule (const ScheduledMidiMessage& message) override;
    void clear() override;
    std::size_t messageCount() const override;

private:
    void silenceCurrentDevice() noexcept;
    void closeCurrentDevice() noexcept;
    void fail (std::string message);

    MidiOutputProvider& provider;
    std::vector<MidiOutputInfo> outputs;
    std::unique_ptr<MidiOutputDevice> device;
    std::string selectedOutputId;
    MidiOutputStatus status = MidiOutputStatus::disconnected;
    std::string errorMessage;
    std::size_t scheduledMessageCount = 0;
};

const char* midiOutputStatusName (MidiOutputStatus status) noexcept;
} // namespace drift::music
