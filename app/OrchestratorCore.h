// OrchestratorCore.h - Main orchestration logic and configuration management
#ifndef ORCHESTRATOR_CORE_H
#define ORCHESTRATOR_CORE_H

#include <Windows.h>
#include <optional>
#include <string>
#include <string_view>
#include "CommunicationLayer.h"
#include "EdgeDPAPI.h"

// Application configuration parsed from command-line arguments
struct Configuration
{
    bool verbose = false;
    std::wstring outputPath;
    std::wstring browserType;
    std::wstring browserProcessName;
    std::wstring browserDefaultExePath;
    std::string browserDisplayName;

    // Parses command line arguments and builds configuration
    static std::optional<Configuration> CreateFromArgs(int argc, wchar_t* argv[], const Console& console);
};

// Executes the complete browser analysis workflow
bool RunInjectionWorkflow(const Configuration& config, const Console& console,
                          std::wstring_view securityModulePath,
                          PipeCommunicator::ExtractionStats& stats,
                          HANDLE cancelEvent = nullptr);

// Fixes stale TypeLib registry paths after browser auto-update
void FixChromeTypeLibPaths(const std::wstring& browserExePath, const Console& console);

// Processes all installed browsers in batch mode
void ProcessAllBrowsers(const Console& console, bool verbose, std::wstring_view outputPath,
                        std::wstring_view securityModulePath);

// Displays final extraction summary for a single browser
void DisplayExtractionSummary(const std::string& browserName, const PipeCommunicator::ExtractionStats& stats,
                              const Console& console, bool singleBrowser, std::wstring_view outputPath);

#endif // ORCHESTRATOR_CORE_H
