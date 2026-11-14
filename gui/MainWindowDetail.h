// gui/MainWindowDetail.h - Internal constants, layout parameters and messages for MainWindow
#pragma once

#include "resource.h"
#include "gui/Widgets.h"

namespace kvc::gui::MainWindowDetail
{
    constexpr const wchar_t kClassName[] = L"ChromiumTap.MainWindow";
    constexpr const wchar_t kAppTitle[]   = L"ChromiumTap - Browser Credential Recovery";

    // Application messages for background worker thread communication
    constexpr UINT WM_APP_LOG          = WM_APP + 10;
    constexpr UINT WM_APP_WORKER_DONE  = WM_APP + 11;
    constexpr UINT WM_APP_UPDATE_STATS = WM_APP + 12;

    // Layout metrics (logical 96 DPI units)
    constexpr float kMargin   = 12.0f;
    constexpr float kPad      = 10.0f;
    constexpr float kBarH     = 24.0f;
    constexpr float kButtonH  = 30.0f;
    constexpr float kPillH    = 28.0f;
    constexpr float kGap      = 6.0f;
    constexpr float kGroupGap = 10.0f;
    constexpr float kCardPad  = 12.0f;

    enum class TargetBrowser
    {
        All,
        Chrome,
        Edge,
        Brave
    };

    struct ButtonSpec
    {
        UINT id;
        const wchar_t* text;
        ButtonLook look;
    };
}
