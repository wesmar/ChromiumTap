// CommunicationModule.h - Inter-process communication and utilities
#ifndef COMMUNICATION_MODULE_H
#define COMMUNICATION_MODULE_H

#include <Windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "RuntimeSupport.h"
#include "support/Diagnostic.h"

// String utility functions for internal module use
namespace StringUtils
{
    inline std::string path_to_string(std::wstring_view path)
    {
        return kvc::text::WideToUtf8(path);
    }
}

namespace SecurityComponents
{
    // Utility functions for encoding and formatting
    namespace Utils
    {
        // Retrieves Local AppData directory path
        std::wstring GetLocalAppDataPath();

        // Decodes Base64 encoded string
        std::optional<std::vector<uint8_t>> Base64Decode(std::string_view input);

        // Converts bytes to hexadecimal string
        std::string BytesToHexString(const std::vector<uint8_t>& bytes);

        // Escapes special characters for JSON serialization
        std::string EscapeJson(std::string_view s);
    }

    // Manages named pipe communication with orchestrator
    class PipeLogger
    {
    public:
        explicit PipeLogger(LPCWSTR pipeName);
        ~PipeLogger();

        bool isValid() const noexcept { return m_pipe != INVALID_HANDLE_VALUE; }
        void Log(kvc::DiagnosticLevel level, kvc::DiagnosticEvent event,
                 std::initializer_list<std::string_view> arguments = {});
        void LogControl(std::string_view message);
        bool ReadMessage(std::string& message, DWORD timeoutMs, std::string& error);
        bool WaitForMessage(std::string_view expected, DWORD timeoutMs,
                            std::string& error);

    private:
        bool writeRaw(std::string_view message, DWORD timeoutMs) noexcept;
        HANDLE m_pipe = INVALID_HANDLE_VALUE;
        HANDLE m_ioEvent = nullptr;
    };
}

#endif // COMMUNICATION_MODULE_H
