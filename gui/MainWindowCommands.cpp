// gui/MainWindowCommands.cpp - Actions, background worker thread and browser operations (Zero CRT)
#include "gui/MainWindow.h"
#include "gui/Dialogs.h"
#include "app/OrchestratorCore.h"
#include "core/BrowserProcessManager.h"
#include "core/CommunicationLayer.h"
#include "support/RuntimeSupport.h"
#include "support/Text.h"
#include "support/syscalls.h"
#include <shellapi.h>
#include <ShObjIdl.h>

namespace kvc::gui
{
    using namespace MainWindowDetail;

    namespace
    {
        void LogCallbackForwarder(kvc::DiagnosticLevel level, const char* message, void* context)
        {
            auto* pMain = static_cast<MainWindow*>(context);
            if (!pMain)
                return;

            std::wstring wide = kvc::text::Utf8ToWide(message ? message : "");
            if (wide.empty())
                return;

            // Allocate wide string on process heap to pass across thread boundary
            const std::size_t count = wide.size() + 1;
            auto* heapBuf = new (std::nothrow) wchar_t[count];
            if (heapBuf)
            {
                memcpy(heapBuf, wide.c_str(), count * sizeof(wchar_t));
                if (!PostMessageW(pMain->hwnd(), WM_APP_LOG, static_cast<WPARAM>(level),
                                  reinterpret_cast<LPARAM>(heapBuf)))
                    delete[] heapBuf;
            }
        }
    }

    void MainWindow::DetectInstalledBrowsers()
    {
        Console dummyConsole(false);
        BrowserPathResolver resolver(dummyConsole);

        const std::wstring chromePath = resolver.resolve(L"chrome.exe");
        const std::wstring edgePath   = resolver.resolve(L"msedge.exe");
        const std::wstring bravePath  = resolver.resolve(L"brave.exe");

        m_chromeInstalled = !chromePath.empty();
        m_edgeInstalled   = !edgePath.empty();
        m_braveInstalled  = !bravePath.empty();

        int detectedCount = 0;
        if (m_chromeInstalled) ++detectedCount;
        if (m_edgeInstalled)   ++detectedCount;
        if (m_braveInstalled)  ++detectedCount;

        m_log.Append(Level::Info, Text::Format(IDS_LOG_DETECTED_BROWSERS, detectedCount));
    }

    void MainWindow::SelectTarget(TargetBrowser target)
    {
        m_target = target;
        m_lookTargetAll.checked    = (target == TargetBrowser::All);
        m_lookTargetChrome.checked = (target == TargetBrowser::Chrome);
        m_lookTargetEdge.checked   = (target == TargetBrowser::Edge);
        m_lookTargetBrave.checked  = (target == TargetBrowser::Brave);

        std::wstring name = S(IDS_BTN_ALL_BROWSERS);
        if (target == TargetBrowser::Chrome) name = L"Google Chrome";
        else if (target == TargetBrowser::Edge) name = L"Microsoft Edge";
        else if (target == TargetBrowser::Brave) name = L"Brave Browser";

        const std::wstring status = Text::Format(IDS_STATUS_TARGET_SELECTED, name);
        SetStatus(status.c_str(), Level::Info);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void MainWindow::UpdateControls()
    {
        EnableWindow(GetDlgItem(m_hwnd, IDC_BTN_EXTRACT), !m_isExtracting);
        EnableWindow(GetDlgItem(m_hwnd, IDC_BTN_CANCEL),  m_isExtracting);
        EnableWindow(GetDlgItem(m_hwnd, IDC_BTN_BROWSE),  !m_isExtracting);

        m_lookChkVerbose.checked = m_verbose;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void MainWindow::SetStatus(const wchar_t* text, Level level)
    {
        m_statusText = text ? text : L"";
        m_statusLevel = level;

        RECT sr{
            static_cast<LONG>(m_statusRect.left) - 2,
            static_cast<LONG>(m_statusRect.top) - 2,
            static_cast<LONG>(m_statusRect.right) + 2,
            static_cast<LONG>(m_statusRect.bottom) + 2
        };
        InvalidateRect(m_hwnd, &sr, FALSE);
    }

    void MainWindow::BrowseOutputFolder()
    {
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(dialog.GetAddressOf()))))
            return;

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        if (FAILED(dialog->Show(m_hwnd)))
            return;

        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(item.GetAddressOf())))
            return;

        PWSTR pPath = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pPath)) && pPath)
        {
            m_outputPath = pPath;
            CoTaskMemFree(pPath);

            m_log.Append(Level::Info, Text::Format(IDS_LOG_OUTPUT_CONFIGURED, m_outputPath));
            const std::wstring updated = S(IDS_STATUS_OUTPUT_UPDATED);
            SetStatus(updated.c_str(), Level::Info);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void MainWindow::OpenOutputFolder()
    {
        kvc::paths::EnsureDirectory(m_outputPath);
        ShellExecuteW(m_hwnd, L"open", m_outputPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MainWindow::KillDatabaseLocks()
    {
        Console console(m_verbose);
        console.SetLogListener(LogCallbackForwarder, this);

        Configuration cfg;
        cfg.verbose = m_verbose;

        // Process locks for supported browsers
        for (const auto* exe : {L"chrome.exe", L"msedge.exe", L"brave.exe"})
        {
            cfg.browserProcessName = exe;
            KillBrowserNetworkService(cfg, console, false);
            KillBrowserProcesses(cfg, console);
        }
        const std::wstring msg = S(IDS_STATUS_LOCKS_TERMINATED);
        m_log.Append(Level::Success, msg);
        SetStatus(msg.c_str(), Level::Success);
    }

    void MainWindow::FixTypeLib()
    {
        Console console(m_verbose);
        console.SetLogListener(LogCallbackForwarder, this);

        BrowserPathResolver resolver(console);
        std::wstring chromePath = resolver.resolve(L"chrome.exe");
        if (chromePath.empty())
            chromePath = resolver.resolve(L"brave.exe");

        if (!chromePath.empty())
        {
            FixChromeTypeLibPaths(chromePath, console);
            const std::wstring okMsg = S(IDS_STATUS_TYPELIB_FIXED);
            m_log.Append(Level::Success, okMsg);
            SetStatus(okMsg.c_str(), Level::Success);
        }
        else
        {
            const std::wstring warnMsg = S(IDS_STATUS_TYPELIB_WARN);
            m_log.Append(Level::Warning, warnMsg);
            SetStatus(warnMsg.c_str(), Level::Warning);
        }
    }

    void MainWindow::ShowAbout()
    {
        ShowAboutDialog(m_hwnd, m_fonts);
    }

    void MainWindow::ShowCliHelp()
    {
        ShowCliHelpDialog(m_hwnd, m_fonts);
    }

    void MainWindow::StartExtraction()
    {
        if (m_isExtracting.load(std::memory_order_acquire))
            return;

        if (m_workerThread)
        {
            WaitForSingleObject(m_workerThread.get(), INFINITE);
            m_workerThread.reset();
        }

        m_isExtracting.store(true, std::memory_order_release);
        m_cancelRequested.store(false, std::memory_order_release);
        if (m_cancelEvent)
            ResetEvent(m_cancelEvent.get());

        m_animStart = GetTickCount64();

        const std::wstring extractingMsg = S(IDS_STATUS_EXTRACTING);
        SetStatus(extractingMsg.c_str(), Level::Info);
        SetTimer(m_hwnd, 101, 30, nullptr); // smooth progress bar animation
        UpdateControls();

        m_workerThread.reset(CreateThread(nullptr, 0, WorkerThreadEntry, this, 0, nullptr));
        if (!m_workerThread)
        {
            m_isExtracting.store(false, std::memory_order_release);
            KillTimer(m_hwnd, 101);
            const std::wstring errMsg = S(IDS_STATUS_DONE_ISSUES);
            SetStatus(errMsg.c_str(), Level::Error);
            UpdateControls();
        }
    }

    void MainWindow::CancelExtraction()
    {
        if (m_isExtracting.load(std::memory_order_acquire) &&
            !m_cancelRequested.exchange(true, std::memory_order_acq_rel))
        {
            if (m_cancelEvent)
                SetEvent(m_cancelEvent.get());

            const std::wstring cancelMsg = S(IDS_STATUS_STOPPING);
            m_log.Append(Level::Warning, cancelMsg);
            SetStatus(cancelMsg.c_str(), Level::Warning);
        }
    }

    DWORD WINAPI MainWindow::WorkerThreadEntry(LPVOID param)
    {
        auto* self = static_cast<MainWindow*>(param);
        if (self)
            self->WorkerThreadProc();
        return 0;
    }

    void MainWindow::WorkerThreadProc()
    {
        Console console(m_verbose);
        console.SetLogListener(LogCallbackForwarder, this);

        console.Localized(kvc::DiagnosticLevel::Info,
                          static_cast<UINT>(kvc::DiagnosticEvent::WorkerSyscallInit));
        if (!InitializeSyscalls(m_verbose))
        {
            console.Localized(kvc::DiagnosticLevel::Error,
                              static_cast<UINT>(kvc::DiagnosticEvent::WorkerSyscallFailed));
            PostMessageW(m_hwnd, WM_APP_WORKER_DONE, 0, 0);
            return;
        }

        // Verify SQLite availability
        if (!CheckWinSQLite3Available())
        {
            console.Localized(kvc::DiagnosticLevel::Warning,
                              static_cast<UINT>(kvc::DiagnosticEvent::WorkerSqliteFallback));
            if (!kvc::paths::FileExists(L"sqlite3.dll"))
            {
                console.Localized(kvc::DiagnosticLevel::Error,
                                  static_cast<UINT>(kvc::DiagnosticEvent::WorkerSqliteMissing));
                PostMessageW(m_hwnd, WM_APP_WORKER_DONE, 0, 0);
                return;
            }
        }

        if (!m_embeddedModule.Valid())
        {
            console.Localized(kvc::DiagnosticLevel::Error,
                              static_cast<UINT>(kvc::DiagnosticEvent::WorkerModuleMissing));
            PostMessageW(m_hwnd, WM_APP_WORKER_DONE, 0, 0);
            return;
        }
        const std::wstring& modulePath = m_embeddedModule.Path();

        // Ensure output path exists
        kvc::paths::EnsureDirectory(m_outputPath);

        bool allOk = true;

        if (m_target == TargetBrowser::All)
        {
            console.Localized(kvc::DiagnosticLevel::Info,
                              static_cast<UINT>(kvc::DiagnosticEvent::WorkerStartingAll));
            BrowserPathResolver resolver(console);
            auto installed = resolver.findAllInstalledBrowsers();

            if (installed.empty())
            {
                console.Localized(kvc::DiagnosticLevel::Warning,
                                  static_cast<UINT>(kvc::DiagnosticEvent::WorkerNoBrowsers));
                PostMessageW(m_hwnd, WM_APP_WORKER_DONE, 0, 0);
                return;
            }

            for (const auto& [bType, bPath] : installed)
            {
                if (m_cancelRequested.load(std::memory_order_acquire))
                    break;

                Configuration config;
                config.verbose = m_verbose;
                config.outputPath = m_outputPath;
                config.browserType = bType;
                config.browserDefaultExePath = bPath;

                if (bType == L"chrome")
                {
                    config.browserProcessName = L"chrome.exe";
                    config.browserDisplayName = "Chrome";
                }
                else if (bType == L"edge")
                {
                    config.browserProcessName = L"msedge.exe";
                    config.browserDisplayName = "Edge";
                }
                else if (bType == L"brave")
                {
                    config.browserProcessName = L"brave.exe";
                    config.browserDisplayName = "Brave";
                }

                console.Localized(kvc::DiagnosticLevel::Info,
                                  static_cast<UINT>(kvc::DiagnosticEvent::WorkerProcessingBrowser),
                                  {config.browserDisplayName});
                PipeCommunicator::ExtractionStats stats;
                if (RunInjectionWorkflow(config, console, modulePath, stats, m_cancelEvent.get()))
                {
                    m_statPasswords.fetch_add(stats.totalPasswords, std::memory_order_relaxed);
                    m_statCookies.fetch_add(stats.totalCookies, std::memory_order_relaxed);
                    m_statCards.fetch_add(stats.totalPayments, std::memory_order_relaxed);
                    m_statProfiles.fetch_add(stats.profileCount, std::memory_order_relaxed);
                    PostMessageW(m_hwnd, WM_APP_UPDATE_STATS, 0, 0);
                }
                else
                {
                    allOk = false;
                }
            }
        }
        else
        {
            Configuration config;
            config.verbose = m_verbose;
            config.outputPath = m_outputPath;

            if (m_target == TargetBrowser::Chrome)
            {
                config.browserType = L"chrome";
                config.browserProcessName = L"chrome.exe";
                config.browserDisplayName = "Chrome";
            }
            else if (m_target == TargetBrowser::Edge)
            {
                config.browserType = L"edge";
                config.browserProcessName = L"msedge.exe";
                config.browserDisplayName = "Edge";
            }
            else if (m_target == TargetBrowser::Brave)
            {
                config.browserType = L"brave";
                config.browserProcessName = L"brave.exe";
                config.browserDisplayName = "Brave";
            }

            BrowserPathResolver resolver(console);
            config.browserDefaultExePath = resolver.resolve(config.browserProcessName);

            if (config.browserDefaultExePath.empty())
            {
                console.Localized(kvc::DiagnosticLevel::Error,
                                  static_cast<UINT>(kvc::DiagnosticEvent::WorkerBrowserExecutableMissing));
                allOk = false;
            }
            else
            {
                const std::string executable = Utils::WStringToUtf8(config.browserDefaultExePath);
                console.Localized(kvc::DiagnosticLevel::Debug,
                                  static_cast<UINT>(kvc::DiagnosticEvent::WorkerTargetExecutable),
                                  {executable});
                PipeCommunicator::ExtractionStats stats;
                if (RunInjectionWorkflow(config, console, modulePath, stats, m_cancelEvent.get()))
                {
                    m_statPasswords.fetch_add(stats.totalPasswords, std::memory_order_relaxed);
                    m_statCookies.fetch_add(stats.totalCookies, std::memory_order_relaxed);
                    m_statCards.fetch_add(stats.totalPayments, std::memory_order_relaxed);
                    m_statProfiles.fetch_add(stats.profileCount, std::memory_order_relaxed);
                    PostMessageW(m_hwnd, WM_APP_UPDATE_STATS, 0, 0);

                    console.Localized(kvc::DiagnosticLevel::Success,
                                      static_cast<UINT>(kvc::DiagnosticEvent::WorkerBrowserComplete),
                                      {config.browserDisplayName});
                }
                else
                {
                    console.Localized(kvc::DiagnosticLevel::Error,
                                      static_cast<UINT>(kvc::DiagnosticEvent::WorkerBrowserFailed),
                                      {config.browserDisplayName});
                    allOk = false;
                }
            }
        }

        const bool completedOk = allOk && !m_cancelRequested.load(std::memory_order_acquire);
        PostMessageW(m_hwnd, WM_APP_WORKER_DONE, completedOk ? 1 : 0, 0);
    }

    void MainWindow::OnCommand(UINT id, UINT /*code*/, HWND /*control*/)
    {
        if (id == IDM_LANGUAGE_AUTO)
        {
            Text::Use(L"auto");
            ApplyLanguage();
            m_log.Append(Level::Info, Text::Format(IDS_LOG_LANGUAGE_CHANGED, Text::Current()));
            return;
        }
        if (id >= IDM_LANGUAGE_FIRST && id <= IDM_LANGUAGE_LAST)
        {
            const auto& langs = Text::Available();
            const std::size_t idx = id - IDM_LANGUAGE_FIRST;
            if (idx < langs.size())
            {
                Text::Use(langs[idx].code);
                ApplyLanguage();
                m_log.Append(Level::Info, Text::Format(IDS_LOG_LANGUAGE_CHANGED, langs[idx].name));
            }
            return;
        }

        switch (id)
        {
        case IDC_TARGET_ALL:         SelectTarget(TargetBrowser::All); break;
        case IDC_TARGET_CHROME:      SelectTarget(TargetBrowser::Chrome); break;
        case IDC_TARGET_EDGE:        SelectTarget(TargetBrowser::Edge); break;
        case IDC_TARGET_BRAVE:       SelectTarget(TargetBrowser::Brave); break;

        case IDC_BTN_EXTRACT:        StartExtraction(); break;
        case IDC_BTN_CANCEL:         CancelExtraction(); break;
        case IDC_BTN_BROWSE:         BrowseOutputFolder(); break;
        case IDC_BTN_OPEN_OUTPUT:    OpenOutputFolder(); break;

        case IDC_CHK_VERBOSE:
            m_verbose = !m_verbose;
            UpdateControls();
            break;

        case IDM_FILE_OPEN_OUTPUT:   OpenOutputFolder(); break;
        case IDM_FILE_BROWSE_OUTPUT: BrowseOutputFolder(); break;
        case IDM_FILE_EXIT:          DestroyWindow(m_hwnd); break;

        case IDM_EXTRACT_ALL:        SelectTarget(TargetBrowser::All); StartExtraction(); break;
        case IDM_EXTRACT_CHROME:     SelectTarget(TargetBrowser::Chrome); StartExtraction(); break;
        case IDM_EXTRACT_EDGE:       SelectTarget(TargetBrowser::Edge); StartExtraction(); break;
        case IDM_EXTRACT_BRAVE:      SelectTarget(TargetBrowser::Brave); StartExtraction(); break;
        case IDM_EXTRACT_KILL_LOCKS: KillDatabaseLocks(); break;
        case IDM_EXTRACT_FIX_TYPELIB:FixTypeLib(); break;

        case IDM_VIEW_VERBOSE:
            m_verbose = !m_verbose;
            UpdateControls();
            break;

        case IDM_VIEW_REFRESH:
            DetectInstalledBrowsers();
            break;

        case IDM_VIEW_CLEAR_LOG:
            m_log.Clear();
            break;

        case IDM_VIEW_COPY_LOG:
            CopyToClipboard(m_hwnd, m_log.Text());
            break;

        case IDM_HELP_CLI:
            ShowCliHelp();
            break;

        case IDM_HELP_ABOUT:
            ShowAbout();
            break;
        }
    }
}
