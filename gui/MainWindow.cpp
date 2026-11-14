// gui/MainWindow.cpp - Main application window implementation (Zero CRT)
#include "gui/MainWindow.h"
#include "gui/Dialogs.h"
#include "support/RuntimeSupport.h"
#include "support/Text.h"
#include <CommCtrl.h>
#include <windowsx.h>

namespace kvc::gui
{
    using namespace MainWindowDetail;

    MainWindow::MainWindow(HINSTANCE instance)
        : m_instance(instance)
        , m_cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        // Default output path is <current directory>\output
        m_outputPath = kvc::paths::Join(kvc::paths::CurrentDirectory(), L"output");
        m_embeddedModule.Extract(instance, m_embeddedModuleError);
    }

    MainWindow::~MainWindow()
    {
        StopWorkerAndJoin();
    }

    void MainWindow::StopWorkerAndJoin()
    {
        if (m_workerThread)
        {
            m_cancelRequested.store(true, std::memory_order_release);
            if (m_cancelEvent)
                SetEvent(m_cancelEvent.get());

            WaitForSingleObject(m_workerThread.get(), INFINITE);
            m_workerThread.reset();
        }
        m_isExtracting.store(false, std::memory_order_release);
    }

    bool MainWindow::Create(int showCommand)
    {
        Text::UseSaved();

        INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icex);

        if (!Graphics::Get().Ready())
            return false;

        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = m_instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)); // Mica transparency
        wc.lpszClassName = kClassName;

        LoadIconWithScaleDown(m_instance, MAKEINTRESOURCEW(IDI_ICON1),
                              GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), m_icon.put());
        LoadIconWithScaleDown(m_instance, MAKEINTRESOURCEW(IDI_ICON1),
                              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), m_smallIcon.put());

        wc.hIcon = m_icon ? m_icon.get() : LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = m_smallIcon.get();

        if (!RegisterClassExW(&wc))
            return false;

        const std::wstring title = S(IDS_APP_TITLE);
        m_hwnd = CreateWindowExW(0, kClassName, title.c_str(),
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 820, 660,
                                 nullptr, nullptr, m_instance, this);
        if (!m_hwnd)
            return false;

        RestorePlacement(showCommand);
        return true;
    }

    int MainWindow::Run()
    {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            // Alt+key opens the client-area menu bar
            if (msg.message == WM_SYSCHAR && GetAncestor(msg.hwnd, GA_ROOT) == m_hwnd &&
                m_menu.OpenByMnemonic(m_hwnd, static_cast<wchar_t>(msg.wParam)))
                continue;

            if (m_accel && TranslateAcceleratorW(m_hwnd, m_accel.get(), &msg))
                continue;

            if (IsDialogMessageW(m_hwnd, &msg))
                continue;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    LRESULT CALLBACK MainWindow::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self = nullptr;
        if (message == WM_NCCREATE)
        {
            self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return self ? self->Handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void MainWindow::OnCreate()
    {
        ApplyBackdrop(m_hwnd);
        m_fonts.Create(GetDpiForWindow(m_hwnd));
        m_statusText = S(IDS_STATUS_READY);
        BuildMenus();
        BuildAccelerators();

        // Initialize Button Looks
        m_lookTargetAll.kind = ButtonKind::Segment;
        m_lookTargetAll.icon = Icon::Apps;
        m_lookTargetAll.checked = true;
        m_lookTargetAll.onCard = true;

        m_lookTargetChrome.kind = ButtonKind::Segment;
        m_lookTargetChrome.icon = Icon::Browser;
        m_lookTargetChrome.onCard = true;

        m_lookTargetEdge.kind = ButtonKind::Segment;
        m_lookTargetEdge.icon = Icon::Browser;
        m_lookTargetEdge.onCard = true;

        m_lookTargetBrave.kind = ButtonKind::Segment;
        m_lookTargetBrave.icon = Icon::Browser;
        m_lookTargetBrave.onCard = true;

        m_lookBtnExtract.kind = ButtonKind::Accent;
        m_lookBtnExtract.icon = Icon::Play;
        m_lookBtnExtract.onCard = true;

        m_lookBtnCancel.kind = ButtonKind::Standard;
        m_lookBtnCancel.icon = Icon::Stop;
        m_lookBtnCancel.onCard = true;

        m_lookBtnBrowse.kind = ButtonKind::Standard;
        m_lookBtnBrowse.icon = Icon::Folder;
        m_lookBtnBrowse.onCard = true;

        m_lookBtnOpenOutput.kind = ButtonKind::Standard;
        m_lookBtnOpenOutput.icon = Icon::Folder;
        m_lookBtnOpenOutput.onCard = true;

        m_lookChkVerbose.kind = ButtonKind::Segment;
        m_lookChkVerbose.icon = Icon::Settings;
        m_lookChkVerbose.onCard = true;

        // Create child button windows
        CreateButton(m_hwnd, IDC_TARGET_ALL,      S(IDS_BTN_ALL_BROWSERS).c_str());
        CreateButton(m_hwnd, IDC_TARGET_CHROME,   S(IDS_BTN_CHROME).c_str());
        CreateButton(m_hwnd, IDC_TARGET_EDGE,     S(IDS_BTN_EDGE).c_str());
        CreateButton(m_hwnd, IDC_TARGET_BRAVE,    S(IDS_BTN_BRAVE).c_str());

        CreateButton(m_hwnd, IDC_BTN_EXTRACT,     S(IDS_BTN_START_EXTRACT).c_str());
        CreateButton(m_hwnd, IDC_BTN_CANCEL,      S(IDS_BTN_STOP).c_str());
        CreateButton(m_hwnd, IDC_CHK_VERBOSE,     S(IDS_BTN_VERBOSE).c_str());
        CreateButton(m_hwnd, IDC_BTN_BROWSE,      S(IDS_BTN_BROWSE).c_str());
        CreateButton(m_hwnd, IDC_BTN_OPEN_OUTPUT, S(IDS_BTN_OPEN_FOLDER).c_str());

        // Create custom LogView window
        m_log.Create(m_hwnd, IDC_LOG_VIEW);

        if (!m_embeddedModuleError.empty())
            m_log.Append(Level::Error, kvc::text::Utf8ToWide(m_embeddedModuleError));

        DetectInstalledBrowsers();

        m_log.Append(Level::Info, S(IDS_LOG_INIT_MICA));
        m_log.Append(Level::Success, S(IDS_LOG_SYSCALLS_ARMED));
        m_log.Append(Level::Info, Text::Format(IDS_LOG_OUTPUT_CONFIGURED, m_outputPath));

        UpdateControls();
    }

    LRESULT MainWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            OnCreate();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            Paint();
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
                Layout();
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = m_fonts.dpi();
            RECT r{0, 0, MulDiv(720, static_cast<int>(dpi), 96), MulDiv(540, static_cast<int>(dpi), 96)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
            info->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }

        case WM_DPICHANGED:
        {
            m_fonts.Create(HIWORD(wParam));
            m_menu.Remeasure();
            m_log.Relayout();
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            ApplyBackdrop(m_hwnd);
            RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return 0;

        case WM_MOUSEMOVE:
        {
            if (!m_trackingMouse)
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
                TrackMouseEvent(&tme);
                m_trackingMouse = true;
            }
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (m_menu.SetHot(m_menu.HitTest(pt)))
            {
                const auto b = m_menu.Bounds();
                RECT mr{static_cast<LONG>(b.left), static_cast<LONG>(b.top),
                        static_cast<LONG>(b.right + 2.0f), static_cast<LONG>(b.bottom + 2.0f)};
                InvalidateRect(m_hwnd, &mr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            m_trackingMouse = false;
            if (m_menu.SetHot(-1))
            {
                const auto b = m_menu.Bounds();
                RECT mr{static_cast<LONG>(b.left), static_cast<LONG>(b.top),
                        static_cast<LONG>(b.right + 2.0f), static_cast<LONG>(b.bottom + 2.0f)};
                InvalidateRect(m_hwnd, &mr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN:
        {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int hit = m_menu.HitTest(pt);
            if (hit >= 0)
            {
                m_menu.Open(m_hwnd, hit, false);
                return 0;
            }
            break;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
            {
                if (lParam == 0)
                    m_menu.Open(m_hwnd, 0, true);
                else
                    m_menu.OpenByMnemonic(m_hwnd, static_cast<wchar_t>(lParam));
                return 0;
            }
            break;

        case WM_INITMENUPOPUP:
            ApplyMenuState(reinterpret_cast<HMENU>(wParam));
            return 0;

        case WM_CONTEXTMENU:
        {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1)
            {
                RECT r{};
                GetWindowRect(m_log.hwnd(), &r);
                pt = {r.left + 20, r.top + 20};
            }
            RECT logRc{};
            GetWindowRect(m_log.hwnd(), &logRc);
            if (PtInRect(&logRc, pt))
            {
                ShowContextMenu(pt);
                return 0;
            }
            break;
        }

        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;

        case WM_DRAWITEM:
        {
            auto& dis = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis.CtlType == ODT_BUTTON)
            {
                switch (dis.CtlID)
                {
                case IDC_TARGET_ALL:         DrawButton(dis, m_fonts, m_lookTargetAll); return TRUE;
                case IDC_TARGET_CHROME:      DrawButton(dis, m_fonts, m_lookTargetChrome); return TRUE;
                case IDC_TARGET_EDGE:        DrawButton(dis, m_fonts, m_lookTargetEdge); return TRUE;
                case IDC_TARGET_BRAVE:       DrawButton(dis, m_fonts, m_lookTargetBrave); return TRUE;
                case IDC_BTN_EXTRACT:        DrawButton(dis, m_fonts, m_lookBtnExtract); return TRUE;
                case IDC_BTN_CANCEL:         DrawButton(dis, m_fonts, m_lookBtnCancel); return TRUE;
                case IDC_BTN_BROWSE:         DrawButton(dis, m_fonts, m_lookBtnBrowse); return TRUE;
                case IDC_BTN_OPEN_OUTPUT:    DrawButton(dis, m_fonts, m_lookBtnOpenOutput); return TRUE;
                case IDC_CHK_VERBOSE:        DrawButton(dis, m_fonts, m_lookChkVerbose); return TRUE;
                }
            }
            else if (dis.CtlType == ODT_MENU)
            {
                m_menu.Draw(dis);
                return TRUE;
            }
            break;
        }

        case WM_MEASUREITEM:
        {
            auto& mis = *reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis.CtlType == ODT_MENU)
            {
                m_menu.Measure(mis);
                return TRUE;
            }
            break;
        }

        case WM_MENUSELECT:
            m_menu.OnMenuSelect(wParam, lParam);
            return 0;

        case WM_TIMER:
            if (m_isExtracting)
            {
                // Invalidate progress and status area for smooth animation
                RECT pr{
                    static_cast<LONG>(m_progressRect.left) - 2,
                    static_cast<LONG>(m_progressRect.top) - 2,
                    static_cast<LONG>(m_progressRect.right) + 2,
                    static_cast<LONG>(m_progressRect.bottom) + 2
                };
                InvalidateRect(m_hwnd, &pr, FALSE);
            }
            return 0;

        case WM_APP_LOG:
        {
            // Worker thread posted a log line: wParam = Level, lParam = wchar_t* heap string
            auto lvl = static_cast<Level>(wParam);
            auto* pText = reinterpret_cast<wchar_t*>(lParam);
            if (pText)
            {
                m_log.Append(lvl, pText);
                delete[] pText;
            }
            return 0;
        }

        case WM_APP_UPDATE_STATS:
        {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_APP_WORKER_DONE:
        {
            const bool success = (wParam != 0);
            m_isExtracting.store(false, std::memory_order_release);
            KillTimer(m_hwnd, 101);

            if (m_workerThread)
            {
                WaitForSingleObject(m_workerThread.get(), INFINITE);
                m_workerThread.reset();
            }

            const std::wstring msg = S(success ? IDS_STATUS_DONE_OK : IDS_STATUS_DONE_ISSUES);
            SetStatus(msg.c_str(), success ? Level::Success : Level::Error);
            UpdateControls();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_DESTROY:
        {
            StopWorkerAndJoin();
            MSG pending{};
            while (PeekMessageW(&pending, m_hwnd, WM_APP_LOG, WM_APP_LOG, PM_REMOVE))
            {
                delete[] reinterpret_cast<wchar_t*>(pending.lParam);
            }
            SavePlacement();
            PostQuitMessage(0);
            return 0;
        }
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }

    int RunGuiApp(HINSTANCE hInstance, int nCmdShow)
    {
        MainWindow window(hInstance);
        if (!window.Create(nCmdShow))
            return 1;
        return window.Run();
    }
}
