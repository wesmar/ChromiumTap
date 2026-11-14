// gui/Dialogs.cpp - Dark theme Mica dialogs implementation (Zero CRT)
#include "gui/Dialogs.h"
#include "gui/Handle.h"
#include "gui/Widgets.h"
#include "resources/resource.h"
#include "support/Text.h"
#include <uxtheme.h>
#include <windowsx.h>

namespace kvc::gui
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"ChromiumTap.ModalDialog";
        constexpr int kOkId = IDOK;
        constexpr int kExtraId = 1002;
        constexpr int kMonoId = 1003;

        std::wstring NormalizeCrLf(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size() + 16);
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
                    out.push_back(L'\r');
                out.push_back(text[i]);
            }
            return out;
        }

        class ModalDialog
        {
        public:
            explicit ModalDialog(const DialogSpec& spec) : m_spec(spec) {}

            void Show(HWND owner)
            {
                const HINSTANCE inst = GetModuleHandleW(nullptr);
                Register(inst);

                const UINT dpi = owner ? GetDpiForWindow(owner) : 96;
                m_fonts.Create(dpi);

                const float w = m_fonts.F(static_cast<float>(m_spec.width));
                const float pad = m_fonts.F(22.0f);
                const float textW = w - 2.0f * pad;
                const float headH = TextHeight(m_fonts, Font::Title, m_spec.heading, textW);
                const float bodyH = TextHeight(m_fonts, Font::Body, m_spec.message, textW);
                const float monoH = m_spec.monospace.empty() ? 0.0f : m_fonts.F(250.0f);
                const float footerH = m_fonts.F(56.0f);
                const float h = pad + headH + m_fonts.F(12.0f) + bodyH +
                                (monoH > 0.0f ? m_fonts.F(14.0f) + monoH : 0.0f) +
                                m_fonts.F(20.0f) + footerH;

                RECT rc{0, 0, static_cast<LONG>(w + 0.5f), static_cast<LONG>(h + 0.5f)};
                const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
                const DWORD exStyle = WS_EX_DLGMODALFRAME;
                AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);
                const int winW = rc.right - rc.left;
                const int winH = rc.bottom - rc.top;

                RECT parentRc{};
                if (owner && GetWindowRect(owner, &parentRc))
                {
                    rc.left = parentRc.left + ((parentRc.right - parentRc.left) - winW) / 2;
                    rc.top  = parentRc.top  + ((parentRc.bottom - parentRc.top) - winH) / 2;
                }
                else
                {
                    rc.left = CW_USEDEFAULT;
                    rc.top  = CW_USEDEFAULT;
                }

                const std::wstring title(m_spec.title);
                m_hwnd = CreateWindowExW(exStyle, kClassName, title.c_str(), style,
                                         rc.left, rc.top, winW, winH, owner, nullptr, inst, this);
                if (!m_hwnd)
                    return;

                if (owner)
                    EnableWindow(owner, FALSE);
                ShowWindow(m_hwnd, SW_SHOW);
                UpdateWindow(m_hwnd);
                SetFocus(GetDlgItem(m_hwnd, kOkId));

                MSG msg{};
                while (!m_done && GetMessageW(&msg, nullptr, 0, 0) > 0)
                {
                    if ((msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) ||
                        (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetFocus() != m_mono))
                    {
                        Close();
                        break;
                    }
                    if (!IsDialogMessageW(m_hwnd, &msg))
                    {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }

                if (owner)
                {
                    EnableWindow(owner, TRUE);
                    SetActiveWindow(owner);
                }
                if (IsWindow(m_hwnd))
                    DestroyWindow(m_hwnd);
            }

        private:
            static void Register(HINSTANCE inst)
            {
                static bool registered = false;
                if (registered)
                    return;
                WNDCLASSEXW wc{sizeof(wc)};
                wc.lpfnWndProc = Proc;
                wc.hInstance = inst;
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                wc.lpszClassName = kClassName;
                RegisterClassExW(&wc);
                registered = true;
            }

            static LRESULT CALLBACK Proc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
            {
                ModalDialog* self = nullptr;
                if (m == WM_NCCREATE)
                {
                    self = static_cast<ModalDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
                    self->m_hwnd = hwnd;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                else
                {
                    self = reinterpret_cast<ModalDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                }
                return self ? self->Handle(m, w, l) : DefWindowProcW(hwnd, m, w, l);
            }

            void Close()
            {
                m_done = true;
            }

            LRESULT Handle(UINT m, WPARAM w, LPARAM l)
            {
                switch (m)
                {
                case WM_CREATE:
                    ApplyBackdrop(m_hwnd);
                    m_darkBrush.reset(CreateSolidBrush(RGB(18, 22, 29)));
                    if (!m_spec.monospace.empty())
                    {
                        const int logH = -MulDiv(10, static_cast<int>(m_fonts.dpi()), 72);
                        m_monoFont.reset(CreateFontW(logH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono"));
                        const std::wstring text(m_spec.monospace);
                        m_mono = CreateWindowExW(0, L"EDIT", text.c_str(),
                                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                                 ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                                 0, 0, 0, 0, m_hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMonoId)),
                                                 GetModuleHandleW(nullptr), nullptr);
                        SendMessageW(m_mono, WM_SETFONT, reinterpret_cast<WPARAM>(m_monoFont.get()), FALSE);
                        SendMessageW(m_mono, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
                        SetWindowTheme(m_mono, L"DarkMode_Explorer", nullptr);
                    }
                    if (!m_spec.extra.empty())
                    {
                        const std::wstring label(m_spec.extra);
                        CreateButton(m_hwnd, kExtraId, label.c_str());
                    }
                    {
                        const std::wstring ok(m_spec.ok);
                        CreateButton(m_hwnd, kOkId, ok.c_str());
                    }
                    Layout();
                    return 0;

                case WM_SIZE:
                    Layout();
                    return 0;

                case WM_ERASEBKGND:
                    return 1;

                case WM_CTLCOLORSTATIC:
                case WM_CTLCOLOREDIT:
                {
                    auto hdc = reinterpret_cast<HDC>(w);
                    SetTextColor(hdc, RGB(226, 232, 240));
                    SetBkColor(hdc, RGB(18, 22, 29));
                    return reinterpret_cast<LRESULT>(m_darkBrush.get());
                }

                case WM_DRAWITEM:
                {
                    auto& dis = *reinterpret_cast<DRAWITEMSTRUCT*>(l);
                    if (dis.CtlID == kOkId)
                    {
                        ButtonLook okLook{ButtonKind::Accent, Icon::Check, false, false, {}};
                        DrawButton(dis, m_fonts, okLook);
                        return TRUE;
                    }
                    if (dis.CtlID == kExtraId)
                    {
                        ButtonLook extraLook{ButtonKind::Standard, Icon::Copy, false, false, {}};
                        DrawButton(dis, m_fonts, extraLook);
                        return TRUE;
                    }
                    break;
                }

                case WM_COMMAND:
                    if (LOWORD(w) == kOkId || LOWORD(w) == IDCANCEL)
                    {
                        Close();
                        return 0;
                    }
                    if (LOWORD(w) == kExtraId)
                    {
                        const std::wstring toCopy(m_spec.monospace.empty() ? m_spec.message : m_spec.monospace);
                        CopyToClipboard(m_hwnd, toCopy);
                        return 0;
                    }
                    break;

                case WM_CLOSE:
                    Close();
                    return 0;

                case WM_PAINT:
                {
                    PaintScope ps(m_hwnd);
                    RECT rc{};
                    GetClientRect(m_hwnd, &rc);
                    Paint(ps.dc(), rc, m_fonts, [&](Canvas& c) {
                        const auto& f = m_fonts;
                        const float W = static_cast<float>(rc.right);
                        const float H = static_cast<float>(rc.bottom);
                        const float pad = f.F(22.0f);
                        const float footerH = f.F(56.0f);

                        c.Clear(Graphics::Get().Base());
                        c.Fill({0.0f, H - footerH, W, H}, Black(0.18f));
                        c.Line({0.0f, H - footerH}, {W, H - footerH}, Color::CardStroke);

                        float y = pad;
                        const float headH = TextHeight(f, Font::Title, m_spec.heading, W - 2.0f * pad);
                        c.Paragraph(m_spec.heading, {pad, y, W - pad, y + headH + 4.0f}, Font::Title, Color::Text);
                        y += headH + f.F(12.0f);

                        const float bodyH = TextHeight(f, Font::Body, m_spec.message, W - 2.0f * pad);
                        c.Paragraph(m_spec.message, {pad, y, W - pad, y + bodyH + 4.0f}, Font::Body, Color::Text2);

                        if (m_mono)
                        {
                            c.Fill(m_monoBox, Rgb(0x12161D), f.F(6.0f));
                            c.Stroke(m_monoBox, Color::CardStroke, f.F(6.0f));
                        }
                    });
                    return 0;
                }
                }
                return DefWindowProcW(m_hwnd, m, w, l);
            }

            void Layout()
            {
                RECT rc{};
                GetClientRect(m_hwnd, &rc);
                const auto& f = m_fonts;
                const float W = static_cast<float>(rc.right);
                const float H = static_cast<float>(rc.bottom);
                const float pad = f.F(22.0f);
                const float footerH = f.F(56.0f);
                const float btnW = f.F(124.0f);
                const float btnH = f.F(32.0f);
                const float btnY = H - (footerH + btnH) * 0.5f;

                SetWindowPos(GetDlgItem(m_hwnd, kOkId), nullptr,
                             static_cast<int>(W - pad - btnW), static_cast<int>(btnY),
                             static_cast<int>(btnW), static_cast<int>(btnH), SWP_NOZORDER);

                if (HWND extra = GetDlgItem(m_hwnd, kExtraId))
                {
                    SetWindowPos(extra, nullptr,
                                 static_cast<int>(W - pad - 2.0f * btnW - f.F(8.0f)), static_cast<int>(btnY),
                                 static_cast<int>(btnW), static_cast<int>(btnH), SWP_NOZORDER);
                }

                if (m_mono)
                {
                    const float headH = TextHeight(f, Font::Title, m_spec.heading, W - 2.0f * pad);
                    const float bodyH = TextHeight(f, Font::Body, m_spec.message, W - 2.0f * pad);
                    const float top = pad + headH + f.F(12.0f) + bodyH + f.F(14.0f);
                    m_monoBox = {pad, top, W - pad, H - footerH - f.F(16.0f)};
                    const D2D1_RECT_F inner = Inset(m_monoBox, f.F(6.0f), f.F(6.0f));
                    SetWindowPos(m_mono, nullptr,
                                 static_cast<int>(inner.left), static_cast<int>(inner.top),
                                 static_cast<int>(inner.right - inner.left), static_cast<int>(inner.bottom - inner.top),
                                 SWP_NOZORDER);
                }
            }

            const DialogSpec& m_spec;
            HWND m_hwnd = nullptr;
            HWND m_mono = nullptr;
            Fonts m_fonts;
            Gdi<HFONT> m_monoFont;
            Gdi<HBRUSH> m_darkBrush;
            D2D1_RECT_F m_monoBox{};
            bool m_done = false;
        };
    }

    void ShowDialog(HWND owner, const DialogSpec& spec)
    {
        ModalDialog(spec).Show(owner);
    }

    void ShowAboutDialog(HWND owner, const Fonts&)
    {
        const std::wstring title   = S(IDS_MI_ABOUT);
        const std::wstring heading = S(IDS_ABOUT_HEADING);
        const std::wstring body    = S(IDS_ABOUT_MESSAGE);
        const std::wstring ok      = S(IDS_OK);

        DialogSpec s;
        s.title = title;
        s.heading = heading;
        s.message = body;
        s.ok = ok;
        s.width = 540;
        ShowDialog(owner, s);
    }

    void ShowCliHelpDialog(HWND owner, const Fonts&)
    {
        const std::wstring title   = S(IDS_HELP_HEADING);
        const std::wstring heading = S(IDS_HELP_HEADING);
        const std::wstring intro   = S(IDS_HELP_MESSAGE);
        const std::wstring usage   = NormalizeCrLf(S(IDS_HELP_USAGE));
        const std::wstring close   = S(IDS_CLOSE);
        const std::wstring copy    = S(IDS_COPY);

        DialogSpec s;
        s.title = title;
        s.heading = heading;
        s.message = intro;
        s.monospace = usage;
        s.ok = close;
        s.extra = copy;
        s.width = 640;
        ShowDialog(owner, s);
    }

    void ShowMessageDialog(HWND owner, const Fonts&, const wchar_t* title,
                           const wchar_t* message, bool)
    {
        const std::wstring ok = S(IDS_OK);
        DialogSpec s;
        s.title = title ? title : L"ChromiumTap";
        s.heading = title ? title : L"ChromiumTap";
        s.message = message ? message : L"";
        s.ok = ok;
        s.width = 460;
        ShowDialog(owner, s);
    }
}
