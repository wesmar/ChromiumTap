// gui/Menus.h - Client-area dark MenuBar and owner-drawn popup menus (Zero CRT)
#pragma once

#include "gui/Theme.h"
#include "gui/Handle.h"
#include <string>
#include <vector>
#include <memory>

namespace kvc::gui
{
    class MenuBar
    {
    public:
        explicit MenuBar(const Fonts& fonts);
        ~MenuBar();
        MenuBar(const MenuBar&) = delete;
        MenuBar& operator=(const MenuBar&) = delete;

        HMENU AddMenu(std::wstring label);
        HMENU CreatePopup();
        void AddItem(HMENU menu, UINT id, std::wstring text, std::wstring shortcut = {}, bool radio = false, bool column = false);
        HMENU AddSubmenu(HMENU menu, std::wstring text);
        void AddSeparator(HMENU menu);
        void Remeasure();
        void Reset();

        void Layout(float left, float top, float height);
        void Paint(Canvas& c) const;
        [[nodiscard]] D2D1_RECT_F Bounds() const;
        [[nodiscard]] int HitTest(POINT client) const;
        bool SetHot(int index);

        void Open(HWND owner, int index, bool keyboard);
        bool OpenByMnemonic(HWND owner, wchar_t key);
        void TrackContext(HWND owner, HMENU menu, POINT screen);

        void Measure(MEASUREITEMSTRUCT& mis) const;
        void Draw(const DRAWITEMSTRUCT& dis) const;
        void OnMenuSelect(WPARAM wParam, LPARAM lParam);

    private:
        struct Data
        {
            std::wstring text;
            std::wstring shortcut;
            bool separator = false;
            bool radio = false;
            bool submenu = false;
        };

        struct Top
        {
            std::wstring label;
            wchar_t mnemonic = 0;
            MenuHandle menu;
            D2D1_RECT_F rect{};
        };

        static LRESULT CALLBACK Filter(int code, WPARAM wParam, LPARAM lParam);
        void Prepare(HMENU menu);
        void Invalidate() const;

        const Fonts& m_fonts;
        Gdi<HBRUSH> m_background;
        std::vector<Top> m_tops;
        std::vector<MenuHandle> m_popups;
        std::vector<std::unique_ptr<Data>> m_data;
        int m_hot = -1;
        int m_open = -1;
        int m_switchTo = -1;
        bool m_switchByKeyboard = false;
        bool m_selectionIsSubmenu = false;
        HMENU m_activePopup = nullptr;
        HWND m_owner = nullptr;
    };
}
