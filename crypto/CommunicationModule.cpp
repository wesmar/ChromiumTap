// CommunicationModule.cpp - Pipe communication and utility functions
#include "CommunicationModule.h"
#include <ShlObj.h>
#include <Wincrypt.h>

#pragma comment(lib, "Crypt32.lib")

namespace SecurityComponents
{
    namespace
    {
        constexpr DWORD kPipeMessageCapacity = 65536;
    }

    namespace Utils
    {
        // Retrieves Local AppData path
        std::wstring GetLocalAppDataPath()
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path)))
            {
                std::wstring result = path;
                CoTaskMemFree(path);
                return result;
            }
            return {};
        }

        // Decodes Base64 string into byte vector
        std::optional<std::vector<uint8_t>> Base64Decode(std::string_view input)
        {
            const std::string inputCopy(input);
            DWORD size = 0;
            if (!CryptStringToBinaryA(inputCopy.c_str(), 0, CRYPT_STRING_BASE64,
                                      nullptr, &size, nullptr, nullptr))
                return std::nullopt;
            
            std::vector<uint8_t> data(size);
            if (!CryptStringToBinaryA(inputCopy.c_str(), 0, CRYPT_STRING_BASE64,
                                      data.data(), &size, nullptr, nullptr))
                return std::nullopt;
            
            data.resize(size);
            return data;
        }

        // Converts byte array to hex string
        std::string BytesToHexString(const std::vector<uint8_t>& bytes)
        {
            std::string result;
            result.reserve(bytes.size() * 2);
            for (uint8_t byte : bytes)
                kvc::text::AppendUnsigned(result, byte, 16, 2);
            return result;
        }

        // Escapes JSON special characters
        std::string EscapeJson(std::string_view s)
        {
            std::string result;
            result.reserve(s.size());
            for (char c : s)
            {
                switch (c)
                {
                case '"':  result.append("\\\""); break;
                case '\\': result.append("\\\\"); break;
                case '\b': result.append("\\b"); break;
                case '\f': result.append("\\f"); break;
                case '\n': result.append("\\n"); break;
                case '\r': result.append("\\r"); break;
                case '\t': result.append("\\t"); break;
                default:
                    if ('\x00' <= c && c <= '\x1f')
                    {
                        result.append("\\u");
                        kvc::text::AppendUnsigned(result, static_cast<unsigned char>(c), 16, 4);
                    }
                    else
                    {
                        result.push_back(c);
                    }
                }
            }
            return result;
        }
    }

    // PipeLogger implementation
    PipeLogger::PipeLogger(LPCWSTR pipeName)
    {
        m_ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_ioEvent)
            return;

        m_pipe = CreateFileW(pipeName, GENERIC_WRITE | GENERIC_READ, 0, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            DWORD readMode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(m_pipe, &readMode, nullptr, nullptr))
            {
                CloseHandle(m_pipe);
                m_pipe = INVALID_HANDLE_VALUE;
            }
        }

        if (m_pipe == INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_ioEvent);
            m_ioEvent = nullptr;
        }
    }

    PipeLogger::~PipeLogger()
    {
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            LogControl("__DLL_PIPE_COMPLETION_SIGNAL__");
            FlushFileBuffers(m_pipe);
            CloseHandle(m_pipe);
        }
        if (m_ioEvent)
            CloseHandle(m_ioEvent);
    }

    bool PipeLogger::writeRaw(std::string_view message, DWORD timeoutMs) noexcept
    {
        if (!isValid() || !m_ioEvent)
            return false;

        std::string terminated(message);
        terminated.push_back('\0');
        DWORD bytesWritten = 0;
        ResetEvent(m_ioEvent);
        OVERLAPPED ov{};
        ov.hEvent = m_ioEvent;

        if (!WriteFile(m_pipe, terminated.data(), static_cast<DWORD>(terminated.size()),
                       &bytesWritten, &ov))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING)
                return false;

            if (WaitForSingleObject(m_ioEvent, timeoutMs) != WAIT_OBJECT_0)
            {
                CancelIoEx(m_pipe, &ov);
                DWORD ignored = 0;
                GetOverlappedResult(m_pipe, &ov, &ignored, TRUE);
                return false;
            }

            if (!GetOverlappedResult(m_pipe, &ov, &bytesWritten, FALSE))
                return false;
        }

        return bytesWritten == terminated.size();
    }

    void PipeLogger::Log(kvc::DiagnosticLevel level, kvc::DiagnosticEvent event,
                         std::initializer_list<std::string_view> arguments)
    {
        const std::string wire = kvc::EncodeDiagnosticWire(level, event, arguments);
        (void)writeRaw(wire, 5000);
    }

    void PipeLogger::LogControl(std::string_view message)
    {
        (void)writeRaw(message, 5000);
    }

    bool PipeLogger::ReadMessage(std::string& message, DWORD timeoutMs,
                                 std::string& error)
    {
        message.clear();
        if (!isValid() || !m_ioEvent)
        {
            error = "Named pipe is not connected.";
            return false;
        }

        std::vector<char> buffer(kPipeMessageCapacity);
        DWORD bytesRead = 0;
        ResetEvent(m_ioEvent);
        OVERLAPPED ov{};
        ov.hEvent = m_ioEvent;

        if (!ReadFile(m_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &bytesRead, &ov))
        {
            const DWORD readError = GetLastError();
            if (readError == ERROR_BROKEN_PIPE)
            {
                error = "Orchestrator closed the named pipe.";
                return false;
            }
            if (readError != ERROR_IO_PENDING)
            {
                error = "ReadFile from named pipe failed. Error: ";
                kvc::text::AppendUnsigned(error, readError);
                return false;
            }

            const DWORD wait = WaitForSingleObject(m_ioEvent, timeoutMs);
            if (wait != WAIT_OBJECT_0)
            {
                CancelIoEx(m_pipe, &ov);
                DWORD ignored = 0;
                GetOverlappedResult(m_pipe, &ov, &ignored, TRUE);
                error = wait == WAIT_TIMEOUT
                            ? "Timed out waiting for named-pipe data."
                            : "Named-pipe read was cancelled.";
                return false;
            }

            if (!GetOverlappedResult(m_pipe, &ov, &bytesRead, FALSE))
            {
                error = "Named-pipe read failed. Error: ";
                kvc::text::AppendUnsigned(error, GetLastError());
                return false;
            }
        }

        if (bytesRead == 0)
        {
            error = "Orchestrator closed the named pipe.";
            return false;
        }

        message.assign(buffer.data(), bytesRead);
        if (!message.empty() && message.back() == '\0')
            message.pop_back();
        return true;
    }

    bool PipeLogger::WaitForMessage(std::string_view expected, DWORD timeoutMs,
                                    std::string& error)
    {
        std::string message;
        if (!ReadMessage(message, timeoutMs, error))
            return false;
        if (message == expected)
            return true;

        error = "Unexpected named-pipe acknowledgment: ";
        error.append(message);
        return false;
    }
}
