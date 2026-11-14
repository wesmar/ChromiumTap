// gui/Widgets.cpp - Owner-drawn buttons, Scroller and LogView implementation (Zero CRT)
#include "gui/Widgets.h"
#include "RuntimeSupport.h"
#include <CommCtrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

namespace kvc::gui
{
    namespace
    {
        HINSTANCE Instance() { return GetModuleHandleW(nullptr); }

        HWND g_hotButton = nullptr;

        LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR)
        {
            switch (message)
            {
            case WM_MOUSEMOVE:
                if (g_hotButton != hwnd)
                {
                    if (HWND old = g_hotButton)
                    {
                        g_hotButton = hwnd;
                        InvalidateRect(old, nullptr, FALSE);
                    }
                    else
                    {
                        g_hotButton = hwnd;
                    }
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                    TrackMouseEvent(&tme);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case WM_MOUSELEAVE:
                if (g_hotButton == hwnd)
                {
                    g_hotButton = nullptr;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case WM_LBUTTONDBLCLK:
                return DefSubclassProc(hwnd, WM_LBUTTONDOWN, wParam, lParam);
            case WM_ERASEBKGND:
                return 1;
            case WM_ENABLE:
                if (!wParam && g_hotButton == hwnd)
                    g_hotButton = nullptr;
                break;
            case WM_NCDESTROY:
                if (g_hotButton == hwnd)
                    g_hotButton = nullptr;
                RemoveWindowSubclass(hwnd, ButtonSubclassProc, id);
                break;
            }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }

        inline float MinFloat(float a, float b) noexcept { return a < b ? a : b; }
        inline float MaxFloat(float a, float b) noexcept { return a > b ? a : b; }
    }

    // ---------------------------------------------------------------------------
    // Buttons
    // ---------------------------------------------------------------------------
    HWND CreateButton(HWND parent, int id, const wchar_t* text)
    {
        HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 Instance(), nullptr);
        if (b)
            SetWindowSubclass(b, ButtonSubclassProc, 1, 0);
        return b;
    }

    void DrawButton(const DRAWITEMSTRUCT& dis, const Fonts& f, const ButtonLook& look)
    {
        const bool disabled = (dis.itemState & ODS_DISABLED) != 0;
        const bool pressed  = (dis.itemState & ODS_SELECTED) != 0 && !disabled;
        const bool focus    = (dis.itemState & ODS_FOCUS) != 0 && !(dis.itemState & ODS_NOFOCUSRECT);
        const bool hot      = (g_hotButton == dis.hwndItem) && !disabled;

        wchar_t labelBuf[256]{};
        GetWindowTextW(dis.hwndItem, labelBuf, static_cast<int>(sizeof(labelBuf) / sizeof(wchar_t)));
        const std::wstring_view text(labelBuf);

        auto& g = Graphics::Get();
        Paint(dis.hDC, dis.rcItem, f, [&](Canvas& c) {
            const D2D1_RECT_F r = ToF(dis.rcItem);
            c.Clear(look.onCard ? g.Surface(Color::Card) : g.Base());
            const float radius = f.F(5.0f);

            // Background & stroke colors
            D2D1_COLOR_F bg = look.onCard ? Color::FieldOnCard : Color::Control;
            D2D1_COLOR_F stroke = Color::ControlStroke;
            D2D1_COLOR_F fg = Color::Text;

            if (look.kind == ButtonKind::Accent)
            {
                bg = pressed ? Color::AccentPressed : (hot ? Color::AccentHot : Color::Accent);
                stroke = Color::AccentHot;
                fg = White(1.0f);
            }
            else if (look.kind == ButtonKind::Danger)
            {
                bg = pressed ? Color::DangerHot : (hot ? Color::DangerHot : Color::Danger);
                stroke = Color::Danger;
                fg = White(1.0f);
            }
            else if (look.kind == ButtonKind::Segment)
            {
                if (look.checked)
                {
                    bg = Color::Selected;
                    stroke = Color::AccentText;
                    fg = Color::AccentText;
                }
                else
                {
                    bg = pressed ? Color::ControlPressed : (hot ? Color::ControlHot : Color::Control);
                    stroke = Color::ControlStroke;
                    fg = hot ? Color::Text : Color::Text2;
                }
            }
            else
            {
                // Standard
                if (pressed)
                    bg = Color::ControlPressed;
                else if (hot)
                    bg = Color::ControlHot;
                else
                    bg = look.onCard ? Color::FieldOnCard : Color::Control;

                if (focus)
                    stroke = Color::AccentText;
            }

            if (disabled)
            {
                bg = Color::ControlDisabled;
                fg = Color::TextDisabled;
                stroke = White(0.04f);
            }

            c.Fill(r, bg, radius);
            c.Stroke(r, stroke, radius, 1.0f);

            // Icon + Label
            const float pad = f.F(8.0f);
            float iconW = 0.0f;
            if (look.icon)
            {
                const wchar_t iconStr[2] = {look.icon, L'\0'};
                iconW = c.Width(iconStr, Font::Icon) + f.F(6.0f);
            }
            const float textW = c.Width(text, Font::Strong);
            const float totalW = iconW + textW;

            float x = r.left + MaxFloat((r.right - r.left - totalW) * 0.5f, pad);
            const float y = r.top;
            const float h = r.bottom - r.top;

            if (look.icon)
            {
                const wchar_t iconStr[2] = {look.icon, L'\0'};
                c.Text(iconStr, {x, y, x + iconW, y + h}, Font::Icon, fg, Align::Left);
                x += iconW;
            }

            if (!text.empty())
            {
                c.Text(text, {x, y, r.right - pad, y + h}, Font::Strong, fg, Align::Left);
            }
        });
    }

    // ---------------------------------------------------------------------------
    // Scroller
    // ---------------------------------------------------------------------------
    void Scroller::Clamp()
    {
        const float m = Max();
        if (offset < 0.0f) offset = 0.0f;
        if (offset > m) offset = m;
    }

    void Scroller::EnsureVisible(float top, float bottom)
    {
        if (top < offset)
            offset = top;
        else if (bottom > offset + view)
            offset = bottom - view;
        Clamp();
    }

    D2D1_RECT_F Scroller::Track(const RECT& client, const Fonts& f) const
    {
        const float w = f.F(6.0f);
        const float right = static_cast<float>(client.right) - f.F(2.0f);
        return {right - w, static_cast<float>(client.top) + f.F(2.0f), right, static_cast<float>(client.bottom) - f.F(2.0f)};
    }

    D2D1_RECT_F Scroller::Thumb(const RECT& client, const Fonts& f) const
    {
        const D2D1_RECT_F t = Track(client, f);
        if (content <= 0.0f || view <= 0.0f)
            return t;
        const float trackH = t.bottom - t.top;
        const float thumbH = MaxFloat(trackH * (view / content), f.F(20.0f));
        const float travel = trackH - thumbH;
        const float top = t.top + (Max() > 0.0f ? travel * (offset / Max()) : 0.0f);
        return {t.left, top, t.right, top + thumbH};
    }

    void Scroller::Draw(Canvas& c, const RECT& client) const
    {
        if (!Visible())
            return;
        const D2D1_RECT_F thumb = Thumb(client, Fonts{});
        c.Fill(thumb, hot || dragging ? Color::Selected : Color::Track, 3.0f);
    }

    bool Scroller::MouseDown(POINT p, const RECT& client, const Fonts& f)
    {
        if (!Visible())
            return false;
        const D2D1_RECT_F th = Thumb(client, f);
        const float px = static_cast<float>(p.x);
        const float py = static_cast<float>(p.y);
        if (px >= th.left && px <= th.right && py >= th.top && py <= th.bottom)
        {
            dragging = true;
            grabY = py;
            grabOffset = offset;
            return true;
        }
        const D2D1_RECT_F tr = Track(client, f);
        if (px >= tr.left && px <= tr.right && py >= tr.top && py <= tr.bottom)
        {
            ScrollTo(py < th.top ? offset - view : offset + view);
            return true;
        }
        return false;
    }

    bool Scroller::MouseMove(POINT p, const RECT& client, const Fonts& f)
    {
        if (dragging)
        {
            const D2D1_RECT_F tr = Track(client, f);
            const D2D1_RECT_F th = Thumb(client, f);
            const float travel = (tr.bottom - tr.top) - (th.bottom - th.top);
            if (travel > 0.0f)
            {
                const float dy = static_cast<float>(p.y) - grabY;
                offset = grabOffset + (dy / travel) * Max();
                Clamp();
            }
            return true;
        }
        const D2D1_RECT_F th = Thumb(client, f);
        const float px = static_cast<float>(p.x);
        const float py = static_cast<float>(p.y);
        const bool newHot = (px >= th.left && px <= th.right && py >= th.top && py <= th.bottom);
        if (newHot != hot)
        {
            hot = newHot;
            return true;
        }
        return false;
    }

    bool Scroller::Wheel(int delta, float step)
    {
        if (!Visible())
            return false;
        const float old = offset;
        offset -= (static_cast<float>(delta) / WHEEL_DELTA) * step;
        Clamp();
        return offset != old;
    }

    // ---------------------------------------------------------------------------
    // LogView
    // ---------------------------------------------------------------------------
    namespace
    {
        constexpr wchar_t kLogViewClass[] = L"ChromiumTap.LogView";

        void RegisterLogViewClassOnce()
        {
            WNDCLASSEXW wc{sizeof(wc)};
            if (GetClassInfoExW(Instance(), kLogViewClass, &wc))
                return;
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = LogView::Proc;
            wc.hInstance = Instance();
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wc.lpszClassName = kLogViewClass;
            RegisterClassExW(&wc);
        }
    }

    HWND LogView::Create(HWND parent, int id)
    {
        RegisterLogViewClassOnce();
        m_hwnd = CreateWindowExW(0, kLogViewClass, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                                 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 Instance(), this);
        return m_hwnd;
    }

    LRESULT CALLBACK LogView::Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        LogView* self = nullptr;
        if (message == WM_NCCREATE)
        {
            self = static_cast<LogView*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<LogView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return self ? self->Handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void LogView::Measure()
    {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float w = static_cast<float>(rc.right - rc.left) - m_fonts.F(148.0f);
        if (w <= 10.0f)
            return;

        m_measuredWidth = w;
        float totalH = 0.0f;
        for (auto& l : m_lines)
        {
            l.height = MaxFloat(TextHeight(m_fonts, Font::Mono, l.text, w) + m_fonts.F(4.0f), m_fonts.F(20.0f));
            totalH += l.height;
        }
        m_scroll.content = totalH;
        m_scroll.view = static_cast<float>(rc.bottom - rc.top);
        if (m_follow)
            m_scroll.offset = m_scroll.Max();
        m_scroll.Clamp();
    }

    void LogView::Append(Level level, std::wstring text)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t timeBuf[32]{};
        wsprintfW(timeBuf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

        Line line;
        line.time = timeBuf;
        line.text = std::move(text);
        line.level = level;
        line.height = m_fonts.F(20.0f);

        m_lines.push_back(std::move(line));
        m_measuredWidth = -1.0f; // force remeasure

        if (m_hwnd)
        {
            Measure();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void LogView::Clear()
    {
        m_lines.clear();
        m_scroll.offset = 0.0f;
        m_scroll.content = 0.0f;
        if (m_hwnd)
            InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    std::wstring LogView::Text() const
    {
        std::wstring result;
        for (const auto& l : m_lines)
        {
            result.append(l.time);
            result.append(L"  ");
            const std::wstring_view label = kvc::DiagnosticLabelW(l.level);
            result.append(label);
            for (std::size_t i = label.size(); i < 8; ++i)
                result.push_back(L' ');
            result.append(l.text);
            result.append(L"\r\n");
        }
        return result;
    }

    void LogView::Relayout()
    {
        m_measuredWidth = -1.0f;
        if (m_hwnd)
        {
            Measure();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void LogView::Paint()
    {
        PaintScope ps(m_hwnd);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        if (rc.right <= rc.left || rc.bottom <= rc.top)
            return;

        if (m_measuredWidth < 0.0f)
            Measure();

        kvc::gui::Paint(ps.dc(), rc, m_fonts, [&](Canvas& c) {
            c.Clear(Color::LogSurface);

            const float padX = m_fonts.F(10.0f);
            const float timeW = m_fonts.F(60.0f);
            const float labelW = m_fonts.F(58.0f);
            const float textX = padX + timeW + m_fonts.F(8.0f) + labelW;
            const float textW = static_cast<float>(rc.right) - textX - m_fonts.F(14.0f);

            float y = -m_scroll.offset + m_fonts.F(6.0f);
            for (const auto& l : m_lines)
            {
                if (y + l.height >= 0.0f && y <= static_cast<float>(rc.bottom))
                {
                    // Timestamp
                    c.Text(l.time, {padX, y, padX + timeW, y + l.height}, Font::Caption, Color::Text3);

                    // Text color based on level
                    D2D1_COLOR_F color = Color::Text;
                    if (l.level == Level::Success)
                        color = Color::Success;
                    else if (l.level == Level::Error)
                        color = Color::Critical;
                    else if (l.level == Level::Warning)
                        color = Color::Caution;
                    else if (l.level == Level::Debug)
                        color = Color::Text3;
                    else
                        color = Color::AccentText;

                    c.Text(kvc::DiagnosticLabelW(l.level),
                           {padX + timeW + m_fonts.F(8.0f), y,
                            padX + timeW + m_fonts.F(8.0f) + labelW, y + l.height},
                           Font::Label, color);

                    c.Paragraph(l.text, {textX, y, textX + textW, y + l.height}, Font::Mono, color);
                }
                y += l.height;
            }

            // Scrollbar
            if (m_scroll.Visible())
            {
                const D2D1_RECT_F th = m_scroll.Thumb(rc, m_fonts);
                c.Fill(th, m_scroll.hot || m_scroll.dragging ? Color::Selected : Color::Track, 3.0f);
            }
        });
    }

    LRESULT LogView::Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            Measure();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_scroll.Wheel(delta, m_fonts.F(45.0f)))
            {
                m_follow = (m_scroll.offset >= m_scroll.Max() - 2.0f);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            SetFocus(m_hwnd);
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (m_scroll.MouseDown(p, rc, m_fonts))
            {
                SetCapture(m_hwnd);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (m_scroll.dragging)
            {
                m_scroll.dragging = false;
                ReleaseCapture();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
        {
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (m_scroll.MouseMove(p, rc, m_fonts))
            {
                m_follow = (m_scroll.offset >= m_scroll.Max() - 2.0f);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                CopyToClipboard(m_hwnd, Text());
                return 0;
            }
            break;
        }
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }

    bool CopyToClipboard(HWND owner, const std::wstring& text)
    {
        if (text.empty() || !OpenClipboard(owner))
            return false;

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hMem)
        {
            CloseClipboard();
            return false;
        }

        void* dst = GlobalLock(hMem);
        if (!dst)
        {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }

        memcpy(dst, text.data(), bytes);
        GlobalUnlock(hMem);

        if (!SetClipboardData(CF_UNICODETEXT, hMem))
        {
            GlobalFree(hMem);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }
}
