// gui/Theme.h - Direct2D/DirectWrite rendering foundation, dark palette and Mica backdrop (Zero CRT)
#pragma once

#include "gui/Handle.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string_view>

namespace kvc::gui
{
    constexpr D2D1_COLOR_F Rgb(unsigned hex, float alpha = 1.0f)
    {
        return {
            static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
            static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
            static_cast<float>(hex & 0xFF) / 255.0f,
            alpha
        };
    }

    constexpr D2D1_COLOR_F White(float alpha) { return {1.0f, 1.0f, 1.0f, alpha}; }
    constexpr D2D1_COLOR_F Black(float alpha) { return {0.0f, 0.0f, 0.0f, alpha}; }
    constexpr D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float alpha) { return {c.r, c.g, c.b, c.a * alpha}; }

    // Windows 11 Fluent dark layer palette
    namespace Color
    {
        inline constexpr D2D1_COLOR_F Card            = White(0.050f);
        inline constexpr D2D1_COLOR_F CardStroke      = White(0.075f);
        inline constexpr D2D1_COLOR_F Control         = White(0.061f);
        inline constexpr D2D1_COLOR_F ControlHot      = White(0.090f);
        inline constexpr D2D1_COLOR_F ControlPressed  = White(0.035f);
        inline constexpr D2D1_COLOR_F ControlDisabled = White(0.040f);
        inline constexpr D2D1_COLOR_F ControlStroke   = White(0.075f);
        inline constexpr D2D1_COLOR_F FieldOnCard     = White(0.105f);

        inline constexpr D2D1_COLOR_F Text            = White(1.000f);
        inline constexpr D2D1_COLOR_F Text2           = White(0.786f);
        inline constexpr D2D1_COLOR_F Text3           = White(0.545f);
        inline constexpr D2D1_COLOR_F TextDisabled    = White(0.363f);

        // Modern Cyan Accent
        inline constexpr D2D1_COLOR_F Accent          = Rgb(0x0E7490);
        inline constexpr D2D1_COLOR_F AccentHot       = Rgb(0x0F86A6);
        inline constexpr D2D1_COLOR_F AccentPressed   = Rgb(0x155E75);
        inline constexpr D2D1_COLOR_F AccentText      = Rgb(0x67E8F9);

        inline constexpr D2D1_COLOR_F Success         = Rgb(0x6CCB5F);
        inline constexpr D2D1_COLOR_F Caution         = Rgb(0xFCE100);
        inline constexpr D2D1_COLOR_F Critical        = Rgb(0xFF99A4);
        inline constexpr D2D1_COLOR_F Danger          = Rgb(0xC42B1C);
        inline constexpr D2D1_COLOR_F DangerHot       = Rgb(0xD13B2C);

        inline constexpr D2D1_COLOR_F Hover           = White(0.045f);
        inline constexpr D2D1_COLOR_F Selected        = White(0.080f);
        inline constexpr D2D1_COLOR_F Track           = White(0.100f);
        inline constexpr D2D1_COLOR_F Grid            = White(0.060f);

        inline constexpr D2D1_COLOR_F Flyout          = Rgb(0x2C2C2C);
        inline constexpr D2D1_COLOR_F FlyoutHot       = Rgb(0x3A3A3A);
        inline constexpr D2D1_COLOR_F FlyoutStroke    = Rgb(0x454545);
        inline constexpr D2D1_COLOR_F LogSurface      = Rgb(0x181A1F);
        inline constexpr D2D1_COLOR_F OpaqueBase      = Rgb(0x202020);
    }

    inline COLORREF ToColorRef(D2D1_COLOR_F c)
    {
        auto toByte = [](float v) -> BYTE {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<BYTE>(v * 255.0f + 0.5f);
        };
        return RGB(toByte(c.r), toByte(c.g), toByte(c.b));
    }

    inline D2D1_RECT_F ToF(const RECT& r)
    {
        return {static_cast<float>(r.left), static_cast<float>(r.top),
                static_cast<float>(r.right), static_cast<float>(r.bottom)};
    }

    inline D2D1_RECT_F Inset(const D2D1_RECT_F& r, float dx, float dy)
    {
        return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy};
    }

    enum class Font : std::size_t
    {
        Body,
        Strong,
        Caption,
        Label,
        Mono,
        Icon,
        IconSmall,
        Title,
        Hero,
        Count
    };

    enum class Align
    {
        Left,
        Center,
        Right
    };

    // DPI-scaled DirectWrite typography cache
    class Fonts
    {
    public:
        void Create(UINT dpi);
        [[nodiscard]] IDWriteTextFormat* operator[](Font f) const
        {
            return m_formats[static_cast<std::size_t>(f)].Get();
        }
        [[nodiscard]] UINT dpi() const noexcept { return m_dpi; }
        [[nodiscard]] int S(int v) const noexcept { return MulDiv(v, static_cast<int>(m_dpi), 96); }
        [[nodiscard]] float F(float v) const noexcept { return v * static_cast<float>(m_dpi) / 96.0f; }
        [[nodiscard]] float Size(Font f) const { return m_sizes[static_cast<std::size_t>(f)]; }
        [[nodiscard]] const wchar_t* UiFamily() const noexcept { return m_uiFamily; }
        [[nodiscard]] const wchar_t* MonoFamily() const noexcept { return m_monoFamily; }

    private:
        UINT m_dpi = 96;
        const wchar_t* m_uiFamily = L"Segoe UI";
        const wchar_t* m_monoFamily = L"Consolas";
        ComPtr<IDWriteTextFormat> m_formats[static_cast<std::size_t>(Font::Count)];
        float m_sizes[static_cast<std::size_t>(Font::Count)]{};
    };

    // Singleton managing Direct2D, DirectWrite, and reusable 32-bit DIB buffer
    class Graphics
    {
    public:
        static Graphics& Get();
        [[nodiscard]] bool Ready() const noexcept { return m_d2d && m_dwrite; }
        [[nodiscard]] ID2D1Factory* D2D() const noexcept { return m_d2d.Get(); }
        [[nodiscard]] IDWriteFactory* DWrite() const noexcept { return m_dwrite.Get(); }
        ID2D1DCRenderTarget* Target();
        void ResetTarget() { m_target.Reset(); }
        bool HasFamily(const wchar_t* family) const;

        bool mica = false;
        [[nodiscard]] D2D1_COLOR_F Base() const noexcept { return mica ? Black(0.0f) : Color::OpaqueBase; }
        [[nodiscard]] D2D1_COLOR_F Surface(D2D1_COLOR_F layer) const noexcept;
        [[nodiscard]] COLORREF GdiSurface(D2D1_COLOR_F layer) const noexcept;

        HDC Buffer(int width, int height);

    private:
        Graphics();
        ~Graphics();

        ComPtr<ID2D1Factory> m_d2d;
        ComPtr<IDWriteFactory> m_dwrite;
        ComPtr<ID2D1DCRenderTarget> m_target;
        MemoryDc m_memDc;
        Gdi<HBITMAP> m_dib;
        HGDIOBJ m_oldBitmap = nullptr;
        int m_bufferWidth = 0;
        int m_bufferHeight = 0;
    };

    // Fast 2D drawing canvas wrapping an ID2D1RenderTarget
    class Canvas
    {
    public:
        Canvas(ID2D1RenderTarget& target, const Fonts& fonts);

        void Clear(D2D1_COLOR_F c);
        void Fill(const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius = 0.0f);
        void Stroke(const D2D1_RECT_F& r, D2D1_COLOR_F c, float radius = 0.0f, float width = 1.0f);
        void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float width = 1.0f);
        void Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F c);
        void Text(std::wstring_view text, const D2D1_RECT_F& r, Font font, D2D1_COLOR_F c, Align align = Align::Left);
        float Paragraph(std::wstring_view text, const D2D1_RECT_F& r, Font font, D2D1_COLOR_F c);
        float Width(std::wstring_view text, Font font);
        float ParagraphHeight(std::wstring_view text, float width, Font font);
        void PushClip(const D2D1_RECT_F& r);
        void PopClip();

    private:
        ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c);

        ID2D1RenderTarget& m_rt;
        const Fonts& m_fonts;
        ComPtr<ID2D1SolidColorBrush> m_brush;
    };

    float TextWidth(const Fonts& fonts, Font font, std::wstring_view text);
    float TextHeight(const Fonts& fonts, Font font, std::wstring_view text, float width);

    // Enables Windows 11 Mica backdrop, dark caption and extended glass frame
    void ApplyBackdrop(HWND window);

    // Software double-buffered paint wrapper (Zero CRT)
    template <typename DrawFn>
    void Paint(HDC dc, const RECT& rect, const Fonts& fonts, DrawFn&& draw)
    {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0)
            return;

        auto& g = Graphics::Get();
        HDC buffer = g.Buffer(width, height);
        ID2D1DCRenderTarget* target = g.Target();
        if (!buffer || !target)
            return;

        const RECT local{0, 0, width, height};
        if (FAILED(target->BindDC(buffer, &local)))
            return;

        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-rect.left),
                                                           static_cast<float>(-rect.top)));
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        {
            Canvas canvas(*target, fonts);
            draw(canvas);
        }
        if (target->EndDraw() == D2DERR_RECREATE_TARGET)
            g.ResetTarget();

        BitBlt(dc, rect.left, rect.top, width, height, buffer, 0, 0, SRCCOPY);
    }
}
