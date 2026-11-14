// gui/MainWindow.h - Main application window for ChromiumTap (Zero CRT)
#pragma once

#include "gui/Theme.h"
#include "gui/Handle.h"
#include "gui/Menus.h"
#include "gui/Widgets.h"
#include "gui/MainWindowDetail.h"
#include "support/EmbeddedModule.h"
#include <atomic>
#include <string>
#include <vector>

namespace kvc::gui
{
    using namespace MainWindowDetail;

    class MainWindow
    {
    public:
        explicit MainWindow(HINSTANCE instance);
        ~MainWindow();
        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        bool Create(int showCommand);
        int Run();
        [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

        // Lifecycle & Layout
        void OnCreate();
        void BuildMenus();
        void BuildAccelerators();
        void ApplyMenuState(HMENU menu);
        void ShowContextMenu(POINT screenPt);
        void ApplyLanguage();
        void Layout();
        void Paint();
        void PaintTargetCard(Canvas& c);
        void PaintActionCard(Canvas& c);
        void PaintStatsCard(Canvas& c);
        void RestorePlacement(int showCommand);
        void SavePlacement();
        void UpdateControls();
        void SetStatus(const wchar_t* text, Level level);

        // Actions & Commands
        void OnCommand(UINT id, UINT code, HWND control);
        void SelectTarget(TargetBrowser target);
        void StartExtraction();
        void CancelExtraction();
        void StopWorkerAndJoin();
        void BrowseOutputFolder();
        void OpenOutputFolder();
        void KillDatabaseLocks();
        void FixTypeLib();
        void ShowAbout();
        void ShowCliHelp();
        void DetectInstalledBrowsers();

        // Worker thread callbacks
        static DWORD WINAPI WorkerThreadEntry(LPVOID param);
        void WorkerThreadProc();

        HINSTANCE m_instance;
        HWND m_hwnd = nullptr;
        Fonts m_fonts;
        MenuBar m_menu{m_fonts};
        AccelHandle m_accel;
        IconHandle m_icon, m_smallIcon;
        LogView m_log{m_fonts};
        bool m_trackingMouse = false;
        EmbeddedModule m_embeddedModule;
        std::string m_embeddedModuleError;

        // Configuration
        TargetBrowser m_target = TargetBrowser::All;
        std::wstring m_outputPath;
        bool m_verbose = false;

        // Browser Detection
        bool m_chromeInstalled = false;
        bool m_edgeInstalled = false;
        bool m_braveInstalled = false;

        // Worker state & thread-safe atomics
        KernelHandle m_workerThread;
        KernelHandle m_cancelEvent;
        std::atomic<bool> m_isExtracting{false};
        std::atomic<bool> m_cancelRequested{false};
        std::wstring m_statusText;
        Level m_statusLevel = Level::Info;
        ULONGLONG m_animStart = 0;

        // Statistics (atomic for lock-free cross-thread updates)
        std::atomic<int> m_statPasswords{0};
        std::atomic<int> m_statCookies{0};
        std::atomic<int> m_statCards{0};
        std::atomic<int> m_statProfiles{0};

        // Layout rects (logical pixels)
        D2D1_RECT_F m_targetCard{};
        D2D1_RECT_F m_actionCard{};
        D2D1_RECT_F m_statsCard{};
        D2D1_RECT_F m_logCard{};
        D2D1_RECT_F m_pathLabelRect{};
        D2D1_RECT_F m_pathBox{};
        D2D1_RECT_F m_progressRect{};
        D2D1_RECT_F m_statusRect{};

        // Button Looks
        ButtonLook m_lookTargetAll;
        ButtonLook m_lookTargetChrome;
        ButtonLook m_lookTargetEdge;
        ButtonLook m_lookTargetBrave;
        ButtonLook m_lookBtnExtract;
        ButtonLook m_lookBtnCancel;
        ButtonLook m_lookBtnBrowse;
        ButtonLook m_lookBtnOpenOutput;
        ButtonLook m_lookChkVerbose;
    };

    // Main entry point for GUI mode
    int RunGuiApp(HINSTANCE hInstance, int nCmdShow);
}
