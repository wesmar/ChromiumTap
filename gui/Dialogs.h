// gui/Dialogs.h - Dark theme Mica dialogs (About, CLI Help, Messages) (Zero CRT)
#pragma once

#include "gui/Theme.h"
#include <string>
#include <string_view>

namespace kvc::gui
{
    struct DialogSpec
    {
        std::wstring_view title;
        std::wstring_view heading;
        std::wstring_view message;
        std::wstring_view monospace{};
        std::wstring_view ok = L"OK";
        std::wstring_view extra{};
        int width = 540;
    };

    void ShowDialog(HWND owner, const DialogSpec& spec);
    void ShowAboutDialog(HWND owner, const Fonts& fonts);
    void ShowCliHelpDialog(HWND owner, const Fonts& fonts);
    void ShowMessageDialog(HWND owner, const Fonts& fonts, const wchar_t* title,
                           const wchar_t* message, bool isError = false);
}
