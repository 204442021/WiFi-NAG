#pragma once

#include <cstdint>
#include <cstdlib>

#include "shared_types.h"

#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

enum NagChangeSource : uint8_t
{
    NAG_SOURCE_BOOT = 0,
    NAG_SOURCE_LOCAL_WEBUI = 1,
    NAG_SOURCE_FSD_REMOTE = 2,
    NAG_SOURCE_RECOVERY = 3,
    NAG_SOURCE_PERIODIC_SYNC = 4,
};

enum NagResultCode : uint8_t
{
    NAG_RESULT_OK = 0,
    NAG_RESULT_BAD_COMMAND = 1,
    NAG_RESULT_REVISION_CONFLICT = 2,
    NAG_RESULT_BUSY_OTA = 3,
    NAG_RESULT_CAN_UNAVAILABLE = 4,
    NAG_RESULT_UNSUPPORTED = 5,
    NAG_RESULT_INTERNAL_ERROR = 6,
    NAG_RESULT_BUSY = 7,
};

struct NagRemoteCommand
{
    bool desiredEnabled = false;
    bool persist = true;
    uint8_t source = 1;
    uint16_t commandId = 0;
    uint32_t expectedRevision = 0;
};

struct NagStateView
{
    bool configuredEnabled = false;
    bool runtimeEffective = false;
    NagResultCode result = NAG_RESULT_OK;
    NagChangeSource source = NAG_SOURCE_BOOT;
    uint16_t ackCommandId = 0;
    uint32_t revision = 0;
    uint32_t reportGeneration = 0;
    uint32_t setCommandCount = 0;
    uint32_t duplicateCommandCount = 0;
    uint32_t revisionConflictCount = 0;
    uint32_t commandRejectCount = 0;
    bool hasLastCommand = false;
    uint16_t lastCommandId = 0;
    bool lastCommandDesired = false;
    NagResultCode lastCommandResult = NAG_RESULT_OK;
};

using NagApplyCallback = NagResultCode (*)(bool desiredEnabled,
                                           bool persist,
                                           NagChangeSource source);
using NagRuntimeCallback = bool (*)();

class NagStateController
{
public:
    void configureCallbacks(NagApplyCallback applyCallback,
                            NagRuntimeCallback runtimeCallback)
    {
        lock();
        applyCallback_ = applyCallback;
        runtimeCallback_ = runtimeCallback;
        unlock();
    }

    void begin(bool configuredEnabled, bool incrementBootRevision = false)
    {
#ifdef ESP_PLATFORM
        if (!mutex_)
            mutex_ = xSemaphoreCreateMutex();
#endif
        lock();
        configuredEnabled_ = configuredEnabled;
        revision_ = loadRevision();
        if (incrementBootRevision)
        {
            const uint32_t nextRevision =
                static_cast<uint32_t>(revision_) + 1U;
            revision_ = nextRevision;
            persistRevision(nextRevision);
        }
        lastRuntimeEffective_ = runtimeCallback_ ? runtimeCallback_() : configuredEnabled;
        lastResult_ = NAG_RESULT_OK;
        lastSource_ = NAG_SOURCE_BOOT;
        lastAckCommandId_ = 0;
        reportGeneration_ = 1;
        initialized_ = true;
        unlock();
    }

    bool initialized() const { return static_cast<bool>(initialized_); }

    NagStateView applyLocal(bool desiredEnabled,
                            NagChangeSource source = NAG_SOURCE_LOCAL_WEBUI,
                            bool persist = true)
    {
        return applyInternal(desiredEnabled, persist, source, false, nullptr);
    }

    NagStateView applyRemote(const NagRemoteCommand &command)
    {
        return applyInternal(command.desiredEnabled,
                             command.persist,
                             NAG_SOURCE_FSD_REMOTE,
                             true,
                             &command);
    }

    NagStateView reportRemoteRejection(const NagRemoteCommand &command,
                                       NagResultCode result)
    {
        lock();
        setCommandCount_ = static_cast<uint32_t>(setCommandCount_) + 1U;
        commandRejectCount_ = static_cast<uint32_t>(commandRejectCount_) + 1U;
        rememberCommand(command, result);
        lastResult_ = result;
        lastSource_ = NAG_SOURCE_FSD_REMOTE;
        lastAckCommandId_ = command.commandId;
        reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
        unlock();
        return view();
    }

    void pollRuntime()
    {
        if (!initialized())
            return;
        const bool current = runtimeCallback_
                                 ? runtimeCallback_()
                                 : static_cast<bool>(configuredEnabled_);
        lock();
        if (current != static_cast<bool>(lastRuntimeEffective_))
        {
            lastRuntimeEffective_ = current;
            lastResult_ = NAG_RESULT_OK;
            lastSource_ = NAG_SOURCE_RECOVERY;
            lastAckCommandId_ = 0;
            reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
        }
        unlock();
    }

    void forceReport(NagChangeSource source = NAG_SOURCE_PERIODIC_SYNC)
    {
        lock();
        lastResult_ = NAG_RESULT_OK;
        lastSource_ = source;
        lastAckCommandId_ = 0;
        reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
        unlock();
    }

    NagStateView view() const
    {
        NagStateView state;
        state.configuredEnabled = static_cast<bool>(configuredEnabled_);
        state.runtimeEffective = static_cast<bool>(lastRuntimeEffective_);
        state.result = static_cast<NagResultCode>(static_cast<uint8_t>(lastResult_));
        state.source = static_cast<NagChangeSource>(static_cast<uint8_t>(lastSource_));
        state.ackCommandId = static_cast<uint16_t>(lastAckCommandId_);
        state.revision = static_cast<uint32_t>(revision_);
        state.reportGeneration = static_cast<uint32_t>(reportGeneration_);
        state.setCommandCount = static_cast<uint32_t>(setCommandCount_);
        state.duplicateCommandCount = static_cast<uint32_t>(duplicateCommandCount_);
        state.revisionConflictCount = static_cast<uint32_t>(revisionConflictCount_);
        state.commandRejectCount = static_cast<uint32_t>(commandRejectCount_);
        state.hasLastCommand = static_cast<bool>(hasLastCommand_);
        state.lastCommandId = static_cast<uint16_t>(lastCommandId_);
        state.lastCommandDesired = static_cast<bool>(lastCommandDesired_);
        state.lastCommandResult = static_cast<NagResultCode>(static_cast<uint8_t>(lastCommandResult_));
        return state;
    }

private:
    NagStateView applyInternal(bool desiredEnabled,
                               bool persist,
                               NagChangeSource source,
                               bool remote,
                               const NagRemoteCommand *command)
    {
        lock();
        if (!static_cast<bool>(initialized_))
        {
            lastResult_ = NAG_RESULT_INTERNAL_ERROR;
            lastSource_ = source;
            lastAckCommandId_ = command ? command->commandId : 0;
            reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
            unlock();
            return view();
        }

        if (remote && command)
        {
            setCommandCount_ = static_cast<uint32_t>(setCommandCount_) + 1U;
            if (static_cast<bool>(hasLastCommand_) &&
                command->commandId == static_cast<uint16_t>(lastCommandId_))
            {
                duplicateCommandCount_ = static_cast<uint32_t>(duplicateCommandCount_) + 1U;
                const NagResultCode duplicateResult =
                    command->desiredEnabled == static_cast<bool>(lastCommandDesired_)
                        ? static_cast<NagResultCode>(static_cast<uint8_t>(lastCommandResult_))
                        : NAG_RESULT_BAD_COMMAND;
                if (duplicateResult != NAG_RESULT_OK)
                    commandRejectCount_ = static_cast<uint32_t>(commandRejectCount_) + 1U;
                lastResult_ = duplicateResult;
                lastSource_ = NAG_SOURCE_FSD_REMOTE;
                lastAckCommandId_ = command->commandId;
                reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
                unlock();
                return view();
            }

            if (command->expectedRevision != static_cast<uint32_t>(revision_) &&
                command->desiredEnabled != static_cast<bool>(configuredEnabled_))
            {
                revisionConflictCount_ = static_cast<uint32_t>(revisionConflictCount_) + 1U;
                commandRejectCount_ = static_cast<uint32_t>(commandRejectCount_) + 1U;
                rememberCommand(*command, NAG_RESULT_REVISION_CONFLICT);
                lastResult_ = NAG_RESULT_REVISION_CONFLICT;
                lastSource_ = NAG_SOURCE_FSD_REMOTE;
                lastAckCommandId_ = command->commandId;
                reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
                unlock();
                return view();
            }
        }

        NagResultCode result = NAG_RESULT_OK;
        if (desiredEnabled != static_cast<bool>(configuredEnabled_))
        {
            result = applyCallback_
                         ? applyCallback_(desiredEnabled, persist, source)
                         : NAG_RESULT_INTERNAL_ERROR;
            if (result == NAG_RESULT_OK)
            {
                configuredEnabled_ = desiredEnabled;
                const uint32_t nextRevision = static_cast<uint32_t>(revision_) + 1U;
                revision_ = nextRevision;
                persistRevision(nextRevision);
            }
            else
            {
                commandRejectCount_ = static_cast<uint32_t>(commandRejectCount_) + 1U;
            }
        }

        if (remote && command)
            rememberCommand(*command, result);

        lastResult_ = result;
        lastSource_ = source;
        lastAckCommandId_ = command ? command->commandId : 0;
        lastRuntimeEffective_ = runtimeCallback_
                                    ? runtimeCallback_()
                                    : static_cast<bool>(configuredEnabled_);
        reportGeneration_ = static_cast<uint32_t>(reportGeneration_) + 1U;
        unlock();
        return view();
    }

    void rememberCommand(const NagRemoteCommand &command, NagResultCode result)
    {
        hasLastCommand_ = true;
        lastCommandId_ = command.commandId;
        lastCommandDesired_ = command.desiredEnabled;
        lastCommandResult_ = result;
    }

    uint32_t loadRevision() const
    {
#ifdef ESP_PLATFORM
        Preferences preferences;
        if (!preferences.begin("bleBridge", false))
            return 0;
        const String stored = preferences.getString("revision", "0");
        const uint32_t value = static_cast<uint32_t>(strtoul(stored.c_str(), nullptr, 10));
        preferences.end();
        return value;
#else
        return 0;
#endif
    }

    void persistRevision(uint32_t revision)
    {
#ifdef ESP_PLATFORM
        Preferences preferences;
        if (!preferences.begin("bleBridge", false))
            return;
        preferences.putString("revision", String(static_cast<unsigned long>(revision)));
        preferences.end();
#else
        (void)revision;
#endif
    }

    void lock() const
    {
#ifdef ESP_PLATFORM
        if (mutex_)
            xSemaphoreTake(mutex_, portMAX_DELAY);
#endif
    }

    void unlock() const
    {
#ifdef ESP_PLATFORM
        if (mutex_)
            xSemaphoreGive(mutex_);
#endif
    }

#ifdef ESP_PLATFORM
    mutable SemaphoreHandle_t mutex_ = nullptr;
#endif
    NagApplyCallback applyCallback_ = nullptr;
    NagRuntimeCallback runtimeCallback_ = nullptr;

    Shared<bool> initialized_{false};
    Shared<bool> configuredEnabled_{false};
    Shared<bool> lastRuntimeEffective_{false};
    Shared<uint32_t> revision_{0};
    Shared<uint32_t> reportGeneration_{0};
    Shared<uint8_t> lastResult_{NAG_RESULT_OK};
    Shared<uint8_t> lastSource_{NAG_SOURCE_BOOT};
    Shared<uint16_t> lastAckCommandId_{0};

    Shared<bool> hasLastCommand_{false};
    Shared<uint16_t> lastCommandId_{0};
    Shared<bool> lastCommandDesired_{false};
    Shared<uint8_t> lastCommandResult_{NAG_RESULT_OK};

    Shared<uint32_t> setCommandCount_{0};
    Shared<uint32_t> duplicateCommandCount_{0};
    Shared<uint32_t> revisionConflictCount_{0};
    Shared<uint32_t> commandRejectCount_{0};
};

inline NagStateController nagStateController;
