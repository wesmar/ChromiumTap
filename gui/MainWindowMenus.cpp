// gui/MainWindowMenus.cpp - Menu bar setup, accelerators, context menu and language refresh (Zero CRT)
#include "gui/MainWindow.h"
#include "support/Text.h"

namespace kvc::gui
{
    using namespace MainWindowDetail;

    void MainWindow::BuildMenus()
    {
        auto& m = m_menu;

        // 1. File Menu
        HMENU file = m.AddMenu(S(IDS_MENU_FILE));
        m.AddItem(file, IDM_FILE_OPEN_OUTPUT,   S(IDS_MI_OPEN_OUTPUT), L"Ctrl+O");
        m.AddItem(file, IDM_FILE_BROWSE_OUTPUT, S(IDS_MI_SELECT_OUTPUT));
        m.AddSeparator(file);
        m.AddItem(file, IDM_FILE_EXIT,          S(IDS_MI_EXIT), L"Ctrl+Q");

        // 2. Extraction Menu
        HMENU extraction = m.AddMenu(S(IDS_MENU_EXTRACTION));
        m.AddItem(extraction, IDM_EXTRACT_ALL,         S(IDS_MI_TARGET_ALL), L"Ctrl+Enter");
        m.AddItem(extraction, IDM_EXTRACT_CHROME,      S(IDS_MI_TARGET_CHROME));
        m.AddItem(extraction, IDM_EXTRACT_EDGE,        S(IDS_MI_TARGET_EDGE));
        m.AddItem(extraction, IDM_EXTRACT_BRAVE,       S(IDS_MI_TARGET_BRAVE));
        m.AddSeparator(extraction);
        m.AddItem(extraction, IDM_EXTRACT_KILL_LOCKS,  S(IDS_MI_KILL_LOCKS));
        m.AddItem(extraction, IDM_EXTRACT_FIX_TYPELIB, S(IDS_MI_FIX_TYPELIB));

        // 3. View Menu
        HMENU view = m.AddMenu(S(IDS_MENU_VIEW));
        m.AddItem(view, IDM_VIEW_VERBOSE,   S(IDS_MI_VERBOSE), L"", true);
        m.AddItem(view, IDM_VIEW_REFRESH,   S(IDS_MI_REFRESH_BROWSERS), L"F5");
        m.AddSeparator(view);

        HMENU lang = m.AddSubmenu(view, S(IDS_MI_LANGUAGE));
        const std::wstring autoLabel = Text::Format(IDS_LANGUAGE_AUTO, Text::Detect());
        m.AddItem(lang, IDM_LANGUAGE_AUTO, autoLabel, L"", true);
        m.AddSeparator(lang);
        const auto& langs = Text::Available();
        for (std::size_t i = 0; i < langs.size() && (IDM_LANGUAGE_FIRST + i) <= IDM_LANGUAGE_LAST; ++i)
        {
            m.AddItem(lang, static_cast<UINT>(IDM_LANGUAGE_FIRST + i), langs[i].name, L"", true);
        }

        m.AddSeparator(view);
        m.AddItem(view, IDM_VIEW_COPY_LOG,  S(IDS_MI_COPY_LOG), L"Ctrl+C");
        m.AddItem(view, IDM_VIEW_CLEAR_LOG, S(IDS_MI_CLEAR_LOG), L"Ctrl+L");

        // 4. Help Menu
        HMENU help = m.AddMenu(S(IDS_MENU_HELP));
        m.AddItem(help, IDM_HELP_CLI,   S(IDS_MI_HELP_CLI), L"F1");
        m.AddSeparator(help);
        m.AddItem(help, IDM_HELP_ABOUT, S(IDS_MI_ABOUT));
    }

    void MainWindow::BuildAccelerators()
    {
        ACCEL keys[] = {
            {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN_OUTPUT},
            {FVIRTKEY | FCONTROL, 'Q', IDM_FILE_EXIT},
            {FVIRTKEY | FCONTROL, 'L', IDM_VIEW_CLEAR_LOG},
            {FVIRTKEY | FCONTROL, 'C', IDM_VIEW_COPY_LOG},
            {FVIRTKEY | FCONTROL, VK_RETURN, IDM_EXTRACT_ALL},
            {FVIRTKEY, VK_F5, IDM_VIEW_REFRESH},
            {FVIRTKEY, VK_F1, IDM_HELP_CLI}
        };
        m_accel.reset(CreateAcceleratorTableW(keys, static_cast<int>(sizeof(keys) / sizeof(ACCEL))));
    }

    void MainWindow::ApplyMenuState(HMENU menu)
    {
        const UINT enableIdle = m_isExtracting ? (MF_BYCOMMAND | MF_GRAYED) : (MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, IDM_FILE_BROWSE_OUTPUT,  enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_ALL,         enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_CHROME,      enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_EDGE,        enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_BRAVE,       enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_KILL_LOCKS,  enableIdle);
        EnableMenuItem(menu, IDM_EXTRACT_FIX_TYPELIB, enableIdle);
        EnableMenuItem(menu, IDM_VIEW_REFRESH,        enableIdle);

        CheckMenuItem(menu, IDM_VIEW_VERBOSE,
                      MF_BYCOMMAND | (m_verbose ? MF_CHECKED : MF_UNCHECKED));

        const bool autoLang = Text::Automatic();
        const std::wstring curLang = Text::Current();
        CheckMenuItem(menu, IDM_LANGUAGE_AUTO,
                      MF_BYCOMMAND | (autoLang ? MF_CHECKED : MF_UNCHECKED));

        const auto& langs = Text::Available();
        for (std::size_t i = 0; i < langs.size() && (IDM_LANGUAGE_FIRST + i) <= IDM_LANGUAGE_LAST; ++i)
        {
            const bool active = !autoLang && langs[i].code == curLang;
            CheckMenuItem(menu, static_cast<UINT>(IDM_LANGUAGE_FIRST + i),
                          MF_BYCOMMAND | (active ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    void MainWindow::ShowContextMenu(POINT screenPt)
    {
        HMENU popup = m_menu.CreatePopup();
        m_menu.AddItem(popup, IDM_VIEW_COPY_LOG,    S(IDS_MI_COPY_LOG), L"Ctrl+C");
        m_menu.AddItem(popup, IDM_VIEW_CLEAR_LOG,   S(IDS_MI_CLEAR_LOG), L"Ctrl+L");
        m_menu.AddSeparator(popup);
        m_menu.AddItem(popup, IDM_FILE_OPEN_OUTPUT, S(IDS_MI_OPEN_OUTPUT), L"Ctrl+O");

        const bool hasText = !m_log.Text().empty();
        EnableMenuItem(popup, IDM_VIEW_COPY_LOG,  MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(popup, IDM_VIEW_CLEAR_LOG, MF_BYCOMMAND | (hasText ? MF_ENABLED : MF_GRAYED));

        m_menu.TrackContext(m_hwnd, popup, screenPt);
    }

    void MainWindow::ApplyLanguage()
    {
        const std::wstring title = S(IDS_APP_TITLE);
        SetWindowTextW(m_hwnd, title.c_str());

        m_menu.Reset();
        BuildMenus();

        SetWindowTextW(GetDlgItem(m_hwnd, IDC_TARGET_ALL),      S(IDS_BTN_ALL_BROWSERS).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_TARGET_CHROME),   S(IDS_BTN_CHROME).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_TARGET_EDGE),     S(IDS_BTN_EDGE).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_TARGET_BRAVE),    S(IDS_BTN_BRAVE).c_str());

        SetWindowTextW(GetDlgItem(m_hwnd, IDC_BTN_EXTRACT),     S(IDS_BTN_START_EXTRACT).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_BTN_CANCEL),      S(IDS_BTN_STOP).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_CHK_VERBOSE),     S(IDS_BTN_VERBOSE).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_BTN_BROWSE),      S(IDS_BTN_BROWSE).c_str());
        SetWindowTextW(GetDlgItem(m_hwnd, IDC_BTN_OPEN_OUTPUT), S(IDS_BTN_OPEN_FOLDER).c_str());

        if (!m_isExtracting)
        {
            const std::wstring ready = S(IDS_STATUS_READY);
            SetStatus(ready.c_str(), Level::Info);
        }

        Layout();
        RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}
