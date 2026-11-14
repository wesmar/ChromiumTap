// CommunicationLayer.cpp - Console and pipe communication implementation
#include "CommunicationLayer.h"
#include "RuntimeSupport.h"
#include "support/Text.h"
#include "syscalls.h"
#include <ShlObj.h>
#include <Rpc.h>

#pragma comment(lib, "Rpcrt4.lib")

constexpr DWORD MODULE_COMPLETION_TIMEOUT_MS = 60000;
constexpr DWORD DATABASE_READY_TIMEOUT_MS = 30000;
constexpr DWORD PIPE_BUFFER_SIZE = 65536;
constexpr const char* DATABASE_READY_SIGNAL = "__DB_READY__";
constexpr const char* DATABASE_READY_ACK = "DB_READY_ACK";
constexpr const char* MODULE_COMPLETION_SIGNAL = "__DLL_PIPE_COMPLETION_SIGNAL__";

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace
{
    WORD ColorForLevel(kvc::DiagnosticLevel level) noexcept
    {
        switch (level)
        {
        case kvc::DiagnosticLevel::Success: return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case kvc::DiagnosticLevel::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case kvc::DiagnosticLevel::Error:   return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case kvc::DiagnosticLevel::Debug:   return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        default:                            return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        }
    }

    kvc::DiagnosticLevel DecodeLegacyMessage(std::string_view& message) noexcept
    {
        if (message.empty() || message[0] != '[')
            return kvc::DiagnosticLevel::Info;

        if (message.size() >= 3 && message[2] == ']')
        {
            kvc::DiagnosticLevel legacy = kvc::DiagnosticLevel::Info;
            switch (message[1])
            {
            case '+': legacy = kvc::DiagnosticLevel::Success; break;
            case '-': legacy = kvc::DiagnosticLevel::Error;   break;
            case '!': legacy = kvc::DiagnosticLevel::Warning; break;
            case '#': legacy = kvc::DiagnosticLevel::Debug;   break;
            default: break;
            }
            message.remove_prefix(3);
            if (!message.empty() && message.front() == ' ')
                message.remove_prefix(1);
            return legacy;
        }
        return kvc::DiagnosticLevel::Info;
    }
}

// Utility function implementations
namespace Utils
{
    std::string path_to_api_string(std::wstring_view path)
    {
        return kvc::text::WideToUtf8(path);
    }
    
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

    std::string WStringToUtf8(std::wstring_view w_sv)
    {
        return kvc::text::WideToUtf8(w_sv);
    }

    std::string PtrToHexStr(const void* ptr) noexcept
    {
        std::string result;
        kvc::text::AppendHex(result, reinterpret_cast<uintptr_t>(ptr), true);
        return result;
    }

    std::string NtStatusToString(NTSTATUS status) noexcept
    {
        std::string result;
        kvc::text::AppendHex(result, static_cast<std::uint32_t>(status), true);
        return result;
    }

    std::wstring GenerateUniquePipeName()
    {
        UUID uuid;
        UuidCreate(&uuid);
        wchar_t* uuidStrRaw = nullptr;
        UuidToStringW(&uuid, (RPC_WSTR*)&uuidStrRaw);
        std::wstring pipeName = L"\\\\.\\pipe\\";
        if (uuidStrRaw)
            pipeName.append(uuidStrRaw);
        RpcStringFreeW((RPC_WSTR*)&uuidStrRaw);
        return pipeName;
    }

    std::string Capitalize(const std::string& str)
    {
        if (str.empty()) return str;
        std::string result = str;
        if (result[0] >= 'a' && result[0] <= 'z')
            result[0] = static_cast<char>(result[0] - ('a' - 'A'));
        return result;
    }
}

// Console implementation
Console::Console(bool verbose) : m_verbose(verbose), m_hConsole(GetStdHandle(STD_OUTPUT_HANDLE))
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
    if (m_hConsole && m_hConsole != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(m_hConsole, &consoleInfo))
        m_originalAttributes = consoleInfo.wAttributes;
    else
        m_originalAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

void Console::Info(const std::string& msg) const { print(kvc::DiagnosticLevel::Info, msg); }
void Console::Success(const std::string& msg) const { print(kvc::DiagnosticLevel::Success, msg); }
void Console::Error(const std::string& msg) const { print(kvc::DiagnosticLevel::Error, msg); }
void Console::Warn(const std::string& msg) const { print(kvc::DiagnosticLevel::Warning, msg); }

void Console::Debug(const std::string& msg) const
{
    if (m_verbose)
        print(kvc::DiagnosticLevel::Debug, msg);
}

void Console::Localized(kvc::DiagnosticLevel level, UINT id,
                        std::initializer_list<std::string_view> arguments) const
{
    if (level == kvc::DiagnosticLevel::Debug && !m_verbose)
        return;

    const std::wstring text = kvc::Text::Format(id, arguments.begin(), arguments.size());
    print(level, kvc::text::WideToUtf8(text));
}

void Console::Relay(const std::string& message) const
{
    std::string_view text(message);
    kvc::DiagnosticLevel level = kvc::DiagnosticLevel::Info;
    const kvc::DiagnosticRecord record = kvc::DecodeDiagnosticWire(message);
    if (record.fallback.empty())
    {
        level = record.level;
        const std::wstring localized = kvc::Text::Format(
            static_cast<UINT>(record.event), record.arguments.data(), record.argumentCount);
        const std::string utf8 = kvc::text::WideToUtf8(localized);
        text = utf8;
        if (level == kvc::DiagnosticLevel::Debug && !m_verbose)
            return;
        print(level, std::string(text));
        return;
    }

    level = DecodeLegacyMessage(text);
    if (level == kvc::DiagnosticLevel::Debug && !m_verbose)
        return;
    print(level, std::string(text));
}

void Console::print(kvc::DiagnosticLevel level, const std::string& msg) const
{
    if (m_listener)
        m_listener(level, msg.c_str(), m_listenerContext);

    if (m_hConsole && m_hConsole != INVALID_HANDLE_VALUE)
    {
        const char* label = kvc::DiagnosticLabel(level);
        const std::string_view labelView(label);
        SetColor(ColorForLevel(level));
        kvc::console::WriteUtf8(m_hConsole, labelView);
        ResetColor();
        constexpr std::size_t labelWidth = 8;
        for (std::size_t i = labelView.size(); i < labelWidth; ++i)
            kvc::console::WriteUtf8(m_hConsole, " ");
        kvc::console::WriteUtf8(m_hConsole, msg);
        kvc::console::WriteUtf8(m_hConsole, "\n");
    }
}

void Console::BlankLine() const
{
    kvc::console::WriteUtf8(m_hConsole, "\n");
}

void Console::SetColor(WORD attributes) const noexcept { SetConsoleTextAttribute(m_hConsole, attributes); }
void Console::ResetColor() const noexcept { SetConsoleTextAttribute(m_hConsole, m_originalAttributes); }

// PipeCommunicator implementation
PipeCommunicator::PipeCommunicator(const std::wstring& pipeName, const Console& console, HANDLE cancelEvent) 
    : m_pipeName(pipeName), m_console(console), m_cancelEvent(cancelEvent) {}

bool PipeCommunicator::create(std::string& error)
{
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev)
    {
        error = "CreateEventW failed. Error: ";
        kvc::text::AppendUnsigned(error, GetLastError());
        return false;
    }
    m_ioEvent.reset(ev);

    HANDLE pipe = CreateNamedPipeW(m_pipeName.c_str(),
                                   PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   1, PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        error = "CreateNamedPipeW failed. Error: ";
        kvc::text::AppendUnsigned(error, GetLastError());
        return false;
    }
    m_pipeHandle.reset(pipe);

    m_console.Debug("Named pipe server created: " + Utils::WStringToUtf8(m_pipeName));
    return true;
}

bool PipeCommunicator::waitOverlapped(OVERLAPPED& ov, DWORD timeoutMs, DWORD& bytesTransferred,
                                      std::string& error, bool& timedOut)
{
    timedOut = false;
    bytesTransferred = 0;

    HANDLE handles[2] = { m_ioEvent.get(), m_cancelEvent };
    const DWORD count = m_cancelEvent ? 2u : 1u;
    const DWORD waitRes = WaitForMultipleObjects(count, handles, FALSE, timeoutMs);
    const DWORD waitError = waitRes == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;

    if (waitRes == WAIT_OBJECT_0)
    {
        if (GetOverlappedResult(m_pipeHandle.get(), &ov, &bytesTransferred, FALSE))
            return true;

        const DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE)
            error = "Security module closed the pipe.";
        else
        {
            error = "Pipe I/O failed. Error: ";
            kvc::text::AppendUnsigned(error, err);
        }
        return false;
    }

    // Cancelled or timed out: cancel pending I/O and wait for kernel to release OVERLAPPED
    CancelIoEx(m_pipeHandle.get(), &ov);
    DWORD dummy = 0;
    GetOverlappedResult(m_pipeHandle.get(), &ov, &dummy, TRUE);

    if (waitRes == WAIT_OBJECT_0 + 1)
    {
        error = "Operation cancelled.";
        return false;
    }
    if (waitRes == WAIT_TIMEOUT)
    {
        timedOut = true;
        return false;
    }

    error = "WaitForMultipleObjects failed. Error: ";
    kvc::text::AppendUnsigned(error, waitError);
    return false;
}

bool PipeCommunicator::waitForClient(std::string& error)
{
    m_console.Debug("Waiting for security module to connect to named pipe.");

    ResetEvent(m_ioEvent.get());
    OVERLAPPED ov{};
    ov.hEvent = m_ioEvent.get();

    if (!ConnectNamedPipe(m_pipeHandle.get(), &ov))
    {
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED)
        {
            m_console.Debug("Security module connected to named pipe.");
            return true;
        }
        if (err != ERROR_IO_PENDING)
        {
            error = "ConnectNamedPipe failed. Error: ";
            kvc::text::AppendUnsigned(error, err);
            return false;
        }

        DWORD transferred = 0;
        bool timedOut = false;
        if (!waitOverlapped(ov, DATABASE_READY_TIMEOUT_MS, transferred, error, timedOut))
        {
            if (timedOut)
                error = "Timed out waiting for security module pipe connection.";
            return false;
        }
    }

    m_console.Debug("Security module connected to named pipe.");
    return true;
}

bool PipeCommunicator::sendInitialData(bool isVerbose, std::wstring_view outputPath,
                                       const std::vector<uint8_t>& edgeDpapiKey,
                                       std::string& error)
{
    if (!writeMessage(isVerbose ? "VERBOSE_TRUE" : "VERBOSE_FALSE", error))
        return false;
    if (!writeMessage(Utils::path_to_api_string(outputPath), error))
        return false;
    
    // Send DPAPI key as hex string (or "NONE" if empty)
    if (!edgeDpapiKey.empty())
    {
        std::string key;
        key.reserve(10 + edgeDpapiKey.size() * 2);
        key.append("DPAPI_KEY:");
        for (uint8_t byte : edgeDpapiKey)
            kvc::text::AppendUnsigned(key, byte, 16, 2);
        return writeMessage(key, error);
    }
    return writeMessage("DPAPI_KEY:NONE", error);
}

bool PipeCommunicator::waitForDatabaseReady(std::string& error)
{
    m_console.Debug("Waiting for security module to decrypt its database key.");
    const DWORD startTime = GetTickCount();

    while (GetTickCount() - startTime < DATABASE_READY_TIMEOUT_MS)
    {
        const DWORD elapsed = GetTickCount() - startTime;
        const DWORD remaining = elapsed < DATABASE_READY_TIMEOUT_MS
                                    ? DATABASE_READY_TIMEOUT_MS - elapsed
                                    : 0;
        std::string message;
        bool timedOut = false;
        if (!readMessage(message, remaining, error, timedOut))
        {
            if (timedOut)
                error = "Timed out waiting for database-ready signal.";
            if (!error.empty())
                m_console.Error(error);
            return false;
        }

        if (message == DATABASE_READY_SIGNAL)
        {
            m_console.Debug("Security module is ready for database access.");
            return true;
        }

        if (message == MODULE_COMPLETION_SIGNAL)
        {
            error = kvc::text::WideToUtf8(
                kvc::Text::Get(static_cast<UINT>(kvc::DiagnosticEvent::ServerModuleEnded)));
            m_console.Error(error);
            return false;
        }

        parseExtractionMessage(message);
        if (!message.empty())
            m_console.Relay(message);
    }

    error = "Timed out waiting for database-ready signal.";
    m_console.Error(error);
    return false;
}

bool PipeCommunicator::acknowledgeDatabaseReady(std::string& error)
{
    if (!writeMessage(DATABASE_READY_ACK, error))
        return false;
    m_console.Debug("Security module released to open browser databases.");
    return true;
}

void PipeCommunicator::relayMessages()
{
    m_console.Debug("Waiting for security module execution. (Pipe: " +
                    Utils::WStringToUtf8(m_pipeName) + ")");

    m_console.BlankLine();

    const DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < MODULE_COMPLETION_TIMEOUT_MS)
    {
        const DWORD elapsed = GetTickCount() - startTime;
        const DWORD remaining = elapsed < MODULE_COMPLETION_TIMEOUT_MS
                                    ? MODULE_COMPLETION_TIMEOUT_MS - elapsed
                                    : 0;
        std::string message;
        std::string error;
        bool timedOut = false;
        if (!readMessage(message, remaining, error, timedOut))
        {
            if (!timedOut && !error.empty())
                m_console.Error(error);
            break;
        }

        if (message == MODULE_COMPLETION_SIGNAL)
        {
            m_console.Debug("Security module completion signal received.");
            break;
        }

        parseExtractionMessage(message);
        if (!message.empty())
            m_console.Relay(message);
    }

    m_console.BlankLine();
    m_console.Debug("Security module signaled completion or pipe interaction ended.");
}

bool PipeCommunicator::readMessage(std::string& message, DWORD timeoutMs,
                                   std::string& error, bool& timedOut)
{
    timedOut = false;
    if (m_cancelEvent && WaitForSingleObject(m_cancelEvent, 0) == WAIT_OBJECT_0)
    {
        error = "Operation cancelled.";
        return false;
    }

    std::vector<char> buffer(PIPE_BUFFER_SIZE);
    DWORD bytesRead = 0;

    ResetEvent(m_ioEvent.get());
    OVERLAPPED ov{};
    ov.hEvent = m_ioEvent.get();

    if (!ReadFile(m_pipeHandle.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytesRead, &ov))
    {
        const DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE)
        {
            error = "Security module closed the pipe.";
            return false;
        }
        if (err != ERROR_IO_PENDING)
        {
            error = "ReadFile from pipe failed. Error: ";
            kvc::text::AppendUnsigned(error, err);
            return false;
        }

        if (!waitOverlapped(ov, timeoutMs, bytesRead, error, timedOut))
            return false;
    }

    if (bytesRead == 0)
    {
        error = "Security module closed the pipe.";
        return false;
    }

    message.assign(buffer.data(), bytesRead);
    if (!message.empty() && message.back() == '\0')
        message.pop_back();
    return true;
}

bool PipeCommunicator::writeMessage(const std::string& msg, std::string& error)
{
    const DWORD toWrite = static_cast<DWORD>(msg.length() + 1);
    DWORD bytesWritten = 0;

    ResetEvent(m_ioEvent.get());
    OVERLAPPED ov{};
    ov.hEvent = m_ioEvent.get();

    if (!WriteFile(m_pipeHandle.get(), msg.c_str(), toWrite, &bytesWritten, &ov))
    {
        const DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING)
        {
            error = "WriteFile to pipe failed. Error: ";
            kvc::text::AppendUnsigned(error, err);
            return false;
        }

        bool timedOut = false;
        if (!waitOverlapped(ov, DATABASE_READY_TIMEOUT_MS, bytesWritten, error, timedOut))
        {
            if (timedOut)
                error = "Timed out writing to pipe.";
            return false;
        }
    }

    if (bytesWritten != toWrite)
    {
        error = "Incomplete WriteFile to pipe.";
        return false;
    }

    m_console.Debug("Sent message to pipe: " + msg);
    return true;
}

void PipeCommunicator::parseExtractionMessage(const std::string& message)
{
    const kvc::DiagnosticRecord record = kvc::DecodeDiagnosticWire(message);
    if (record.fallback.empty())
    {
        if (record.event == kvc::DiagnosticEvent::ProfilesFound &&
            record.argumentCount >= 1)
        {
            int profiles = 0;
            if (kvc::text::ParseInt(record.arguments[0], profiles))
                m_stats.profileCount = profiles;
        }
        else if (record.event == kvc::DiagnosticEvent::DataExtracted &&
                 record.argumentCount >= 2)
        {
            int count = 0;
            if (kvc::text::ParseInt(record.arguments[0], count))
            {
                if (record.arguments[1] == "cookies")
                    m_stats.totalCookies += count;
                else if (record.arguments[1] == "passwords")
                    m_stats.totalPasswords += count;
                else if (record.arguments[1] == "payments")
                    m_stats.totalPayments += count;
            }
        }
        return;
    }

    // Compatibility with older extraction modules that sent plain text.
    std::string_view body(message);
    (void)DecodeLegacyMessage(body);

    auto extractNumber = [&body](const std::string& prefix, const std::string& suffix) -> int
    {
        size_t start = body.find(prefix);
        if (start == std::string::npos) return 0;
        start += prefix.length();
        size_t end = body.find(suffix, start);
        if (end == std::string::npos) return 0;
        
        int value = 0;
        return kvc::text::ParseInt(body.substr(start, end - start), value)
                   ? value
                   : 0;
    };

    if (body.find("Found ") != std::string::npos && body.find("profile(s)") != std::string::npos)
        m_stats.profileCount = extractNumber("Found ", " profile(s)");

    if (body.find("Decrypted AES Key: ") != std::string::npos)
    {
        const std::size_t pos = body.find("Decrypted AES Key: ") + 19;
        m_stats.aesKey.assign(body.substr(pos));
    }

    if (body.find(" cookies extracted to ") != std::string::npos)
        m_stats.totalCookies += extractNumber("", " cookies");

    if (body.find(" passwords extracted to ") != std::string::npos)
        m_stats.totalPasswords += extractNumber("", " passwords");

    if (body.find(" payments extracted to ") != std::string::npos)
        m_stats.totalPayments += extractNumber("", " payments");
}

// BrowserPathResolver implementation
BrowserPathResolver::BrowserPathResolver(const Console& console) : m_console(console) {}

std::wstring BrowserPathResolver::resolve(const std::wstring& browserExeName)
{
    m_console.Debug("Searching Registry for: " + Utils::WStringToUtf8(browserExeName));

    const std::wstring registryPaths[] = {
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + browserExeName,
        L"\\Registry\\Machine\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + browserExeName
    };

    for (const auto& regPath : registryPaths)
    {
        std::wstring path = queryRegistryDefaultValue(regPath);
        if (!path.empty() && kvc::paths::FileExists(path))
        {
            m_console.Debug("Found at: " + Utils::WStringToUtf8(path));
            return path;
        }
    }

    m_console.Debug("Not found in Registry");
    return L"";
}

std::vector<std::pair<std::wstring, std::wstring>> BrowserPathResolver::findAllInstalledBrowsers()
{
    std::vector<std::pair<std::wstring, std::wstring>> installedBrowsers;

    const std::pair<std::wstring, std::wstring> supportedBrowsers[] = {
        {L"chrome", L"chrome.exe"},
        {L"edge", L"msedge.exe"},
        {L"brave", L"brave.exe"}
    };

    m_console.Debug("Enumerating installed browsers...");

    for (const auto& [browserType, exeName] : supportedBrowsers)
    {
        std::wstring path = resolve(exeName);
        if (!path.empty())
        {
            installedBrowsers.push_back({browserType, path});
            m_console.Debug("Found " + Utils::Capitalize(Utils::WStringToUtf8(browserType)) + 
                          " at: " + Utils::WStringToUtf8(path));
        }
    }

    if (installedBrowsers.empty())
    {
        m_console.Warn("No supported browsers found installed on this system");
    }
    else
    {
        std::string message = "Found ";
        kvc::text::AppendUnsigned(message, installedBrowsers.size());
        message.append(" browser(s) to process");
        m_console.Debug(message);
    }

    return installedBrowsers;
}

std::wstring BrowserPathResolver::queryRegistryDefaultValue(const std::wstring& keyPath)
{
    std::vector<wchar_t> pathBuffer(keyPath.begin(), keyPath.end());
    pathBuffer.push_back(L'\0');

    UNICODE_STRING_SYSCALLS keyName;
    keyName.Buffer = pathBuffer.data();
    keyName.Length = static_cast<USHORT>(keyPath.length() * sizeof(wchar_t));
    keyName.MaximumLength = static_cast<USHORT>(pathBuffer.size() * sizeof(wchar_t));

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &keyName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE hKey = nullptr;
    NTSTATUS status = NtOpenKey_syscall(&hKey, KEY_READ, &objAttr);

    if (!NT_SUCCESS(status))
    {
        if (status != (NTSTATUS)0xC0000034) // STATUS_OBJECT_NAME_NOT_FOUND
            m_console.Debug("Registry access failed: " + Utils::NtStatusToString(status));
        return L"";
    }

    // RAII guard for key handle
    struct KeyGuard {
        HANDLE h;
        ~KeyGuard() { if (h) NtClose_syscall(h); }
    } keyGuard{hKey};

    UNICODE_STRING_SYSCALLS valueName = {0, 0, nullptr};
    ULONG bufferSize = 4096;
    std::vector<BYTE> buffer(bufferSize);
    ULONG resultLength = 0;

    status = NtQueryValueKey_syscall(hKey, &valueName, KeyValuePartialInformation,
                                   buffer.data(), bufferSize, &resultLength);
    
    if (status == STATUS_BUFFER_TOO_SMALL || status == STATUS_BUFFER_OVERFLOW)
    {
        buffer.resize(resultLength);
        bufferSize = resultLength;
        status = NtQueryValueKey_syscall(hKey, &valueName, KeyValuePartialInformation,
                                       buffer.data(), bufferSize, &resultLength);
    }

    if (!NT_SUCCESS(status))
        return L"";

    auto kvpi = reinterpret_cast<PKEY_VALUE_PARTIAL_INFORMATION>(buffer.data());

    if (kvpi->Type != REG_SZ && kvpi->Type != REG_EXPAND_SZ)
        return L"";
    if (kvpi->DataLength < sizeof(wchar_t) * 2)
        return L"";

    size_t charCount = kvpi->DataLength / sizeof(wchar_t);
    std::wstring path(reinterpret_cast<wchar_t*>(kvpi->Data), charCount);
    
    while (!path.empty() && path.back() == L'\0')
        path.pop_back();

    if (path.empty())
        return L"";

    if (kvpi->Type == REG_EXPAND_SZ)
    {
        std::vector<wchar_t> expanded(MAX_PATH * 2);
        DWORD size = ExpandEnvironmentStringsW(path.c_str(), expanded.data(), 
                                             static_cast<DWORD>(expanded.size()));
        if (size > 0 && size <= expanded.size())
            path = std::wstring(expanded.data());
    }

    return path;
}
