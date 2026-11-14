// gui/Widgets.h - Owner-drawn buttons, Scroller and LogView (Zero CRT)
#pragma once

#include "gui/Theme.h"
#include "support/Diagnostic.h"
#include <string>
#include <string_view>
#include <vector>

namespace kvc::gui
{
    using Level = kvc::DiagnosticLevel;

    enum class ButtonKind
    {
        Standard,
        Accent,
        Danger,
        Segment,
        Expander,
        IconOnly
    };

    struct ButtonLook
    {
        ButtonKind kind = ButtonKind::Standard;
        wchar_t icon = 0;       // Segoe Fluent Icons glyph
        bool onCard = true;     // sits on a card layer
        bool checked = false;   // Segment pill active state
        std::wstring badge;     // badge text
    };

    namespace Icon
    {
        inline constexpr wchar_t Play      = 0xE768;
        inline constexpr wchar_t Stop      = 0xE71A;
        inline constexpr wchar_t Folder    = 0xE8B7;
        inline constexpr wchar_t Refresh   = 0xE72C;
        inline constexpr wchar_t Shield    = 0xEA18;
        inline constexpr wchar_t Clear     = 0xE894;
        inline constexpr wchar_t Check     = 0xE73E;
        inline constexpr wchar_t Settings  = 0xE713;
        inline constexpr wchar_t Apps      = 0xE71D;
        inline constexpr wchar_t Warning   = 0xE7BA;
        inline constexpr wchar_t Info      = 0xE946;
        inline constexpr wchar_t Copy      = 0xE8C8;
        inline constexpr wchar_t Key       = 0xE8D7;
        inline constexpr wchar_t Lock      = 0xE72E;
        inline constexpr wchar_t Browser   = 0xE774;
    }

    HWND CreateButton(HWND parent, int id, const wchar_t* text);
    void DrawButton(const DRAWITEMSTRUCT& dis, const Fonts& fonts, const ButtonLook& look);

    // ---------------------------------------------------------------------------
    // Scroller - smooth vertical scrollbar for lists and logs
    // ---------------------------------------------------------------------------
    struct Scroller
    {
        float offset = 0.0f;
        float content = 0.0f;
        float view = 0.0f;
        bool dragging = false;
        bool hot = false;
        float grabY = 0.0f;
        float grabOffset = 0.0f;

        [[nodiscard]] float Max() const { return content > view ? content - view : 0.0f; }
        [[nodiscard]] bool Visible() const { return content > view + 0.5f; }
        void Clamp();
        void ScrollTo(float y) { offset = y; Clamp(); }
        void EnsureVisible(float top, float bottom);
        [[nodiscard]] D2D1_RECT_F Track(const RECT& client, const Fonts& f) const;
        [[nodiscard]] D2D1_RECT_F Thumb(const RECT& client, const Fonts& f) const;
        void Draw(Canvas& c, const RECT& client) const;
        bool MouseDown(POINT p, const RECT& client, const Fonts& f);
        bool MouseMove(POINT p, const RECT& client, const Fonts& f);
        bool Wheel(int delta, float step);
    };

    // ---------------------------------------------------------------------------
    // LogView - custom styled timestamped, colored activity view
    // ---------------------------------------------------------------------------
    class LogView
    {
    public:
        explicit LogView(const Fonts& fonts) : m_fonts(fonts) {}
        LogView(const LogView&) = delete;
        LogView& operator=(const LogView&) = delete;

        HWND Create(HWND parent, int id);
        [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }

        static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);

        void Append(Level level, std::wstring text);
        void Clear();
        [[nodiscard]] std::wstring Text() const;
        [[nodiscard]] std::size_t Count() const noexcept { return m_lines.size(); }
        void Relayout();

    private:
        struct Line
        {
            std::wstring time;
            std::wstring text;
            Level level;
            float height = 0.0f;
        };

        LRESULT Handle(UINT, WPARAM, LPARAM);
        void Measure();
        void Paint();

        const Fonts& m_fonts;
        HWND m_hwnd = nullptr;
        std::vector<Line> m_lines;
        float m_measuredWidth = -1.0f;
        bool m_follow = true;
        Scroller m_scroll;
    };

    bool CopyToClipboard(HWND owner, const std::wstring& text);
}
