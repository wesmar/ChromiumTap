// gui/MainWindowLayout.cpp - Layout calculations and card rendering (Zero CRT)
#include "gui/MainWindow.h"
#include "support/Text.h"
#include <windowsx.h>

namespace kvc::gui
{
    using namespace MainWindowDetail;

    namespace
    {
        inline float MinFloat(float a, float b) noexcept { return a < b ? a : b; }
        inline float MaxFloat(float a, float b) noexcept { return a > b ? a : b; }
    }

    void MainWindow::Layout()
    {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const float W = static_cast<float>(rc.right - rc.left);
        const float H = static_cast<float>(rc.bottom - rc.top);
        if (W <= 10.0f || H <= 10.0f)
            return;

        const auto& f = m_fonts;
        const float margin = f.F(kMargin);
        const float gap = f.F(kGap);

        auto place = [this](int id, D2D1_RECT_F r) {
            HWND h = GetDlgItem(m_hwnd, id);
            if (h)
            {
                SetWindowPos(h, nullptr,
                             static_cast<int>(r.left), static_cast<int>(r.top),
                             static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top),
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        };

        // 1. Client Menu Bar at the very top
        m_menu.Layout(f.F(6.0f), f.F(4.0f), f.F(kBarH));
        float y = f.F(4.0f + kBarH + 6.0f);

        // 2. Target Browsers Card
        const float cardW = W - 2.0f * margin;
        const float targetCardH = f.F(66.0f);
        m_targetCard = {margin, y, margin + cardW, y + targetCardH};

        // 4 Target buttons inside Target Card
        const float pillY = y + f.F(28.0f);
        const float pillH = f.F(28.0f);
        const float btnGap = f.F(8.0f);
        const float pillW = (cardW - f.F(20.0f) - 3.0f * btnGap) / 4.0f;
        float pillX = margin + f.F(10.0f);

        place(IDC_TARGET_ALL,    {pillX, pillY, pillX + pillW, pillY + pillH});
        pillX += pillW + btnGap;
        place(IDC_TARGET_CHROME, {pillX, pillY, pillX + pillW, pillY + pillH});
        pillX += pillW + btnGap;
        place(IDC_TARGET_EDGE,   {pillX, pillY, pillX + pillW, pillY + pillH});
        pillX += pillW + btnGap;
        place(IDC_TARGET_BRAVE,  {pillX, pillY, pillX + pillW, pillY + pillH});

        y += targetCardH + gap;

        // 3. Action & Settings Card
        const float actionCardH = f.F(122.0f);
        m_actionCard = {margin, y, margin + cardW, y + actionCardH};

        const float pad = f.F(10.0f);
        const float innerLeft = m_actionCard.left + pad;
        const float innerRight = m_actionCard.right - pad;
        const float innerW = innerRight - innerLeft;

        // Row 1: Output Path label + full-width recessed path box
        const float outY = y + f.F(10.0f);
        const float outH = f.F(28.0f);
        const float labelW = f.F(122.0f);
        m_pathLabelRect = {innerLeft, outY, innerLeft + labelW, outY + outH};
        m_pathBox       = {innerLeft + labelW + f.F(6.0f), outY, innerRight, outY + outH};

        // Row 2: All 5 action buttons aligned in ONE single horizontal row
        // [ Start Extraction ] [ Stop ] [ Verbose Mode ]    ...    [ Browse… ] [ Open Folder ]
        const float actY = outY + outH + f.F(10.0f);
        const float actBtnH = f.F(32.0f);

        float extractW = f.F(176.0f);
        float cancelW  = f.F(102.0f);
        float verboseW = f.F(152.0f);
        float browseW  = f.F(118.0f);
        float openW    = f.F(126.0f);

        const float totalDesiredBtns = extractW + cancelW + verboseW + browseW + openW;
        const float minGaps = 4.0f * btnGap;
        if (totalDesiredBtns + minGaps > innerW && totalDesiredBtns > 1.0f)
        {
            const float scale = (innerW - minGaps) / totalDesiredBtns;
            extractW *= scale;
            cancelW  *= scale;
            verboseW *= scale;
            browseW  *= scale;
            openW    *= scale;
        }

        float bx = innerLeft;
        place(IDC_BTN_EXTRACT, {bx, actY, bx + extractW, actY + actBtnH});
        bx += extractW + btnGap;
        place(IDC_BTN_CANCEL,  {bx, actY, bx + cancelW, actY + actBtnH});
        bx += cancelW + btnGap;
        place(IDC_CHK_VERBOSE, {bx, actY, bx + verboseW, actY + actBtnH});

        const float openX = innerRight - openW;
        const float browseX = MaxFloat(bx + verboseW + btnGap, openX - btnGap - browseW);
        place(IDC_BTN_BROWSE,      {browseX, actY, browseX + browseW, actY + actBtnH});
        place(IDC_BTN_OPEN_OUTPUT, {openX,   actY, openX + openW,     actY + actBtnH});

        // Row 3: Progress Bar & Status Text
        const float progY = actY + actBtnH + f.F(10.0f);
        m_progressRect = {innerLeft, progY, innerRight, progY + f.F(4.0f)};
        m_statusRect   = {innerLeft, progY + f.F(8.0f), innerRight, progY + f.F(24.0f)};

        y += actionCardH + gap;

        // 4. Statistics Card (4 KPI badges)
        const float statsCardH = f.F(54.0f);
        m_statsCard = {margin, y, margin + cardW, y + statsCardH};

        y += statsCardH + gap;

        // 5. Activity Log Card
        const float logCardH = MaxFloat(H - y - margin, f.F(140.0f));
        m_logCard = {margin, y, margin + cardW, y + logCardH};

        place(IDC_LOG_VIEW, Inset(m_logCard, f.F(4.0f), f.F(4.0f)));

        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void MainWindow::Paint()
    {
        PaintScope ps(m_hwnd);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        if (rc.right <= rc.left || rc.bottom <= rc.top)
            return;

        kvc::gui::Paint(ps.dc(), rc, m_fonts, [&](Canvas& c) {
            c.Clear(Graphics::Get().Base());

            // 1. Menu bar
            m_menu.Paint(c);

            // 2. Target browsers card
            PaintTargetCard(c);

            // 3. Action & settings card
            PaintActionCard(c);

            // 4. Statistics card
            PaintStatsCard(c);

            // 5. Activity log card background
            c.Fill(m_logCard, Color::Card, m_fonts.F(6.0f));
            c.Stroke(m_logCard, Color::CardStroke, m_fonts.F(6.0f));
        });
    }

    void MainWindow::PaintTargetCard(Canvas& c)
    {
        const auto& f = m_fonts;
        const float radius = f.F(6.0f);

        c.Fill(m_targetCard, Color::Card, radius);
        c.Stroke(m_targetCard, Color::CardStroke, radius);

        const float pad = f.F(10.0f);
        const std::wstring header = S(IDS_CARD_TARGETS);
        c.Text(header,
               {m_targetCard.left + pad, m_targetCard.top + f.F(8.0f), m_targetCard.right - pad, m_targetCard.top + f.F(24.0f)},
               Font::Label, Color::Text3);
    }

    void MainWindow::PaintActionCard(Canvas& c)
    {
        const auto& f = m_fonts;
        const float radius = f.F(6.0f);

        c.Fill(m_actionCard, Color::Card, radius);
        c.Stroke(m_actionCard, Color::CardStroke, radius);

        // Row 1: Output directory label + recessed path box
        const std::wstring outLabel = S(IDS_CARD_OUTPUT_LABEL);
        c.Text(outLabel, m_pathLabelRect, Font::Label, Color::Text3);

        c.Fill(m_pathBox, Color::Control, f.F(4.0f));
        c.Stroke(m_pathBox, Color::ControlStroke, f.F(4.0f));
        c.Text(m_outputPath, Inset(m_pathBox, f.F(10.0f), 0.0f), Font::Mono, Color::Text2);

        // Progress bar: track and fill
        const D2D1_RECT_F& pr = m_progressRect;
        const float trackRadius = (pr.bottom - pr.top) * 0.5f;
        c.Fill(pr, Color::Track, trackRadius);

        if (m_isExtracting)
        {
            // Indeterminate animated sliding glow segment
            const float trackW = pr.right - pr.left;
            const float segW = trackW * 0.32f;
            const float phase = static_cast<float>((GetTickCount64() - m_animStart) % 1500) / 1500.0f;
            const float segLeft = pr.left - segW + (trackW + segW) * phase;
            c.PushClip(pr);
            c.Fill({MaxFloat(segLeft, pr.left), pr.top, MinFloat(segLeft + segW, pr.right), pr.bottom},
                   Color::AccentText, trackRadius);
            c.PopClip();
        }
        else if (m_statusLevel == Level::Success)
        {
            c.Fill(pr, Color::Success, trackRadius);
        }
        else if (m_statusLevel == Level::Error)
        {
            c.Fill(pr, Color::Critical, trackRadius);
        }

        // Status text
        D2D1_COLOR_F statusColor = Color::Text;
        if (m_statusLevel == Level::Success) statusColor = Color::Success;
        else if (m_statusLevel == Level::Error) statusColor = Color::Critical;
        else if (m_statusLevel == Level::Warning) statusColor = Color::Caution;
        else if (m_isExtracting) statusColor = Color::AccentText;

        c.Text(m_statusText, m_statusRect, Font::Caption, statusColor);
    }

    void MainWindow::PaintStatsCard(Canvas& c)
    {
        const auto& f = m_fonts;
        const float radius = f.F(6.0f);

        c.Fill(m_statsCard, Color::Card, radius);
        c.Stroke(m_statsCard, Color::CardStroke, radius);

        const float cardW = m_statsCard.right - m_statsCard.left;
        const float colW = cardW / 4.0f;
        const float top = m_statsCard.top + f.F(6.0f);
        const float numH = f.F(24.0f);
        const float lblH = f.F(16.0f);

        const std::wstring lblPasswords = S(IDS_STAT_PASSWORDS);
        const std::wstring lblCookies   = S(IDS_STAT_COOKIES);
        const std::wstring lblCards     = S(IDS_STAT_CARDS);
        const std::wstring lblProfiles  = S(IDS_STAT_PROFILES);

        struct Metric
        {
            int value;
            std::wstring_view label;
            D2D1_COLOR_F color;
        };

        const Metric metrics[4] = {
            {m_statPasswords.load(std::memory_order_relaxed), lblPasswords, Color::AccentText},
            {m_statCookies.load(std::memory_order_relaxed),   lblCookies,   Color::Success},
            {m_statCards.load(std::memory_order_relaxed),     lblCards,     Color::Caution},
            {m_statProfiles.load(std::memory_order_relaxed),  lblProfiles,  Color::Text}
        };

        for (int i = 0; i < 4; ++i)
        {
            const float x = m_statsCard.left + static_cast<float>(i) * colW;
            const D2D1_RECT_F colNumRect{x, top, x + colW, top + numH};
            const D2D1_RECT_F colLblRect{x, top + numH, x + colW, top + numH + lblH};

            wchar_t valBuf[32]{};
            wsprintfW(valBuf, L"%d", metrics[i].value);

            c.Text(valBuf, colNumRect, Font::Title, metrics[i].color, Align::Center);
            c.Text(metrics[i].label, colLblRect, Font::Label, Color::Text3, Align::Center);

            // Separator between columns
            if (i > 0)
            {
                c.Line({x, top + f.F(4.0f)}, {x, m_statsCard.bottom - f.F(6.0f)}, Color::CardStroke);
            }
        }
    }

    void MainWindow::RestorePlacement(int showCommand)
    {
        const UINT dpi = GetDpiForWindow(m_hwnd);
        RECT desired{0, 0, MulDiv(820, static_cast<int>(dpi), 96), MulDiv(660, static_cast<int>(dpi), 96)};
        AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);

        const int w = desired.right - desired.left;
        const int h = desired.bottom - desired.top;

        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
        const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;

        SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        ShowWindow(m_hwnd, showCommand == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : showCommand);
        UpdateWindow(m_hwnd);
    }

    void MainWindow::SavePlacement()
    {
        // Window coordinates can optionally be saved
    }
}
