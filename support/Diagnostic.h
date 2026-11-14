#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace kvc
{
    enum class DiagnosticLevel : std::uint8_t
    {
        Info,
        Success,
        Warning,
        Error,
        Debug
    };

    enum class DiagnosticEvent : std::uint16_t
    {
        ExtractionStarted = 15000,
        EdgeComAttempt,
        EdgeComSuccess,
        EdgeComFailed,
        EdgeDpapiFallback,
        EdgeNoKey,
        SingleKeyStart,
        SingleKeyFailed,
        SingleKeySuccess,
        DatabaseHandshakeComplete,
        ProfilesFound,
        ProcessingProfile,
        UsingComKey,
        ExtractionComplete,
        ReadVerboseFailed,
        ReadOutputPathFailed,
        OutputPathConfigured,
        ReadDpapiKeyFailed,
        DpapiKeyNotProvided,
        DpapiKeyReceived,
        DpapiKeyInvalid,
        CriticalModuleError,
        DiscoveringProfiles,
        DatabaseOpenFailed,
        DatabasePrepareFailed,
        OutputDirectoryFailed,
        DataExtracted,
        ReadingLocalState,
        UsingComElevation,
        ComModeChanged,
        ComInitialized,
        RequestingMasterKey,
        ComKeyDecrypted,
        ComProxyFailed,
        ComDecryptFailed,
        DpapiDecryptFailed,
        DpapiSizeMismatch,
        ComOperationDebug,
        GenericError,
        DatabaseHandshakeFailed,
        WorkerSyscallInit = 15100,
        WorkerSyscallFailed,
        WorkerSqliteFallback,
        WorkerSqliteMissing,
        WorkerModuleMissing,
        WorkerStartingAll,
        WorkerNoBrowsers,
        WorkerProcessingBrowser,
        WorkerBrowserExecutableMissing,
        WorkerBrowserComplete,
        WorkerBrowserFailed,
        WorkerTargetExecutable,
        ServerModuleEnded,
        EdgeDpapiSuccess,
        OrchestratorStartingAll,
        OrchestratorNoBrowsers,
        OrchestratorProcessingCount,
        OrchestratorCompletedSummary,
        OrchestratorProcessingBrowser,
        NoDataExtracted,
        OutputStored,
        ExtractionSummary
    };

    struct DiagnosticRecord
    {
        DiagnosticLevel level = DiagnosticLevel::Info;
        DiagnosticEvent event = DiagnosticEvent::GenericError;
        std::array<std::string_view, 4> arguments{};
        std::size_t argumentCount = 0;
        std::string_view fallback;
    };

    inline constexpr char kDiagnosticWireMarker = '\x1e';
    inline constexpr char kDiagnosticArgumentSeparator = '\x1f';

    inline char DiagnosticWireCode(DiagnosticLevel level) noexcept
    {
        switch (level)
        {
        case DiagnosticLevel::Success: return 'S';
        case DiagnosticLevel::Warning: return 'W';
        case DiagnosticLevel::Error:   return 'E';
        case DiagnosticLevel::Debug:   return 'D';
        default:                       return 'I';
        }
    }

    inline const char* DiagnosticLabel(DiagnosticLevel level) noexcept
    {
        switch (level)
        {
        case DiagnosticLevel::Success: return "SUCCESS";
        case DiagnosticLevel::Warning: return "WARNING";
        case DiagnosticLevel::Error:   return "ERROR";
        case DiagnosticLevel::Debug:   return "DEBUG";
        default:                       return "INFO";
        }
    }

    inline const wchar_t* DiagnosticLabelW(DiagnosticLevel level) noexcept
    {
        switch (level)
        {
        case DiagnosticLevel::Success: return L"SUCCESS";
        case DiagnosticLevel::Warning: return L"WARNING";
        case DiagnosticLevel::Error:   return L"ERROR";
        case DiagnosticLevel::Debug:   return L"DEBUG";
        default:                       return L"INFO";
        }
    }

    inline std::string EncodeDiagnosticWire(
        DiagnosticLevel level, DiagnosticEvent event,
        std::initializer_list<std::string_view> arguments = {})
    {
        std::string wire;
        wire.reserve(32);
        wire.push_back(kDiagnosticWireMarker);
        wire.push_back(DiagnosticWireCode(level));

        char digits[8]{};
        std::size_t digitCount = 0;
        auto value = static_cast<std::uint16_t>(event);
        do
        {
            digits[digitCount++] = static_cast<char>('0' + value % 10);
            value = static_cast<std::uint16_t>(value / 10);
        } while (value != 0);
        while (digitCount > 0)
            wire.push_back(digits[--digitCount]);

        wire.push_back('|');
        bool first = true;
        for (std::string_view argument : arguments)
        {
            if (!first)
                wire.push_back(kDiagnosticArgumentSeparator);
            wire.append(argument);
            first = false;
        }
        return wire;
    }

    inline DiagnosticRecord DecodeDiagnosticWire(std::string_view message) noexcept
    {
        DiagnosticRecord record;
        if (message.size() < 5 || message[0] != kDiagnosticWireMarker)
        {
            record.fallback = message;
            return record;
        }

        switch (message[1])
        {
        case 'S': record.level = DiagnosticLevel::Success; break;
        case 'W': record.level = DiagnosticLevel::Warning; break;
        case 'E': record.level = DiagnosticLevel::Error;   break;
        case 'D': record.level = DiagnosticLevel::Debug;   break;
        default:  break;
        }

        std::size_t cursor = 2;
        std::uint32_t eventId = 0;
        while (cursor < message.size() && message[cursor] >= '0' && message[cursor] <= '9')
        {
            eventId = eventId * 10u + static_cast<std::uint32_t>(message[cursor] - '0');
            ++cursor;
        }
        if (cursor >= message.size() || message[cursor] != '|')
        {
            record.fallback = message;
            return record;
        }

        record.event = static_cast<DiagnosticEvent>(eventId);
        ++cursor;
        std::size_t argumentStart = cursor;
        while (cursor <= message.size())
        {
            if (cursor == message.size() || message[cursor] == kDiagnosticArgumentSeparator)
            {
                if (record.argumentCount < record.arguments.size())
                    record.arguments[record.argumentCount++] = message.substr(argumentStart,
                                                                             cursor - argumentStart);
                argumentStart = cursor + 1;
            }
            ++cursor;
        }
        return record;
    }
}
