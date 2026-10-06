#include "pch.h"
#include "PlayerCard.xaml.h"
#if __has_include("PlayerCard.g.cpp")
#include "PlayerCard.g.cpp"
#endif

#include "Fmt.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace winrt::App1::implementation
{
    PlayerCard::PlayerCard()
    {
        InitializeComponent();
        // 默认隐藏：有数据时才显示。
        Visibility(Visibility::Collapsed);
    }

    PlayerCard::~PlayerCard() = default;

    void PlayerCard::Reset(std::wstring const& hint)
    {
        Nickname().Text(AppFmt::H(L"—"));
        IdLine().Text(AppFmt::H(hint));
        RankText().Text(L"—");
        SeasonText().Text(L"—");
        TitleBadge().Visibility(Visibility::Collapsed);
        AvatarImage().Visibility(Visibility::Collapsed);
        AvatarFallback().Text(L"");
        Section1().Visibility(Visibility::Collapsed);
        Section2().Visibility(Visibility::Collapsed);
        Section3().Visibility(Visibility::Collapsed);
        Section4().Visibility(Visibility::Collapsed);
        WeaponsExpander().Visibility(Visibility::Collapsed);
        MapsExpander().Visibility(Visibility::Collapsed);
        ViewMatchesButton().Visibility(Visibility::Collapsed);
        Visibility(Visibility::Collapsed);
    }

    void PlayerCard::SetIdentity(
        std::wstring const& nickname, std::wstring const& avatarUrl,
        std::wstring const& title, std::wstring const& idLine,
        std::wstring const& rank, std::wstring const& season)
    {
        Visibility(Visibility::Visible);
        Nickname().Text(AppFmt::H(nickname.empty() ? L"未知玩家" : nickname));
        IdLine().Text(AppFmt::H(idLine));
        RankText().Text(AppFmt::H(rank.empty() ? L"—" : rank));
        SeasonText().Text(AppFmt::H(season.empty() ? L"—" : season));

        if (title.empty())
        {
            TitleBadge().Visibility(Visibility::Collapsed);
        }
        else
        {
            TitleText().Text(AppFmt::H(title));
            TitleBadge().Visibility(Visibility::Visible);
        }

        if (avatarUrl.empty())
        {
            AvatarImage().Visibility(Visibility::Collapsed);
            // 无图时用昵称首字作为占位。
            AvatarFallback().Text(nickname.empty()
                ? L"" : AppFmt::H(std::wstring(1, nickname[0])));
        }
        else
        {
            AvatarFallback().Text(L"");
            try
            {
                auto bitmap = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
                bitmap.UriSource(winrt::Windows::Foundation::Uri(winrt::hstring(avatarUrl)));
                AvatarImage().Source(bitmap);
                AvatarImage().Visibility(Visibility::Visible);
            }
            catch (...)
            {
                // 头像加载失败不影响其余信息，退回首字占位。
                AvatarImage().Visibility(Visibility::Collapsed);
                AvatarFallback().Text(nickname.empty()
                    ? L"" : AppFmt::H(std::wstring(1, nickname[0])));
            }
        }

        // 查询成功后才显示「查看对局」。
        ViewMatchesButton().Visibility(Visibility::Visible);
    }

    void PlayerCard::SetStats(
        int32_t sectionId, std::wstring const& title,
        std::vector<std::pair<std::wstring, std::wstring>> const& items)
    {
        Border section = nullptr;
        TextBlock header = nullptr;
        ItemsControl host = nullptr;
        switch (sectionId)
        {
        case 1: section = Section1(); header = Section1Title(); host = SeasonStats(); break;
        case 2: section = Section2(); header = Section2Title(); host = CareerStats(); break;
        case 3: section = Section3(); header = nullptr;        host = AbilityStats(); break;
        default: return;
        }
        if (!section || !host)
        {
            return;
        }

        if (items.empty())
        {
            section.Visibility(Visibility::Collapsed);
            return;
        }

        if (header)
        {
            header.Text(AppFmt::H(title));
        }

        auto rows = single_threaded_observable_vector<IInspectable>();
        for (auto const& pair : items)
        {
            // make 返回 ABI 类型；用 get_self 取实现类型才能调 SetLabel/SetValue
            //（与 MainWindow 里构造 MatchRow 的写法一致）。
            auto row = make<winrt::App1::implementation::StatItem>();
            auto* impl = get_self<winrt::App1::implementation::StatItem>(row);
            impl->SetLabel(AppFmt::H(pair.first));
            impl->SetValue(AppFmt::H(pair.second));
            rows.Append(row);
        }
        host.ItemsSource(rows);
        section.Visibility(Visibility::Visible);
    }

    void PlayerCard::SetTrend(
        std::wstring const& caption, std::vector<double> const& points,
        std::wstring const& /*axisLabel*/)
    {
        if (points.size() < 2)
        {
            Section4().Visibility(Visibility::Collapsed);
            return;
        }
        Section4().Visibility(Visibility::Visible);
        TrendCaption().Text(AppFmt::H(caption));
        DrawTrend(points, caption);
    }

    void PlayerCard::DrawTrend(std::vector<double> const& points, std::wstring const& axisLabel)
    {
        auto canvas = TrendCanvas();
        if (!canvas)
        {
            return;
        }
        canvas.Children().Clear();

        const float width = 640.0f;
        const float height = 110.0f;
        const float padding = 8.0f;

        double minValue = points.front();
        double maxValue = points.front();
        for (double v : points)
        {
            minValue = (std::min)(minValue, v);
            maxValue = (std::max)(maxValue, v);
        }
        if (maxValue - minValue < 1e-6)
        {
            maxValue = minValue + 1.0;
        }

        // 基线。
        auto brush = SolidColorBrush(winrt::Windows::UI::Colors::Gray());
        brush.Opacity(0.35);
        auto line = winrt::Microsoft::UI::Xaml::Shapes::Line();
        line.X1(0); line.X2(width);
        line.Y1(height - padding); line.Y2(height - padding);
        line.Stroke(brush);
        line.StrokeThickness(1.0);
        canvas.Children().Append(line);

        // 端点标注当前值。
        auto lastText = Microsoft::UI::Xaml::Controls::TextBlock();
        lastText.Text(AppFmt::H(
            std::wstring(L"最近 ") + axisLabel + L" " +
            AppFmt::FormatDouble(points.back(), 2)));
        lastText.FontSize(11);
        Microsoft::UI::Xaml::Controls::Canvas::SetLeft(lastText, padding);
        Microsoft::UI::Xaml::Controls::Canvas::SetTop(lastText, 0.0);
        canvas.Children().Append(lastText);
    }

    void PlayerCard::SetWeapons(std::vector<winrt::App1::WeaponItem> const& items)
    {
        if (items.empty())
        {
            WeaponsExpander().Visibility(Visibility::Collapsed);
            return;
        }
        auto rows = single_threaded_observable_vector<IInspectable>();
        for (auto const& item : items)
        {
            rows.Append(item);
        }
        WeaponList().ItemsSource(rows);
        WeaponsExpander().Visibility(Visibility::Visible);
    }

    void PlayerCard::SetMaps(std::vector<winrt::App1::MapItem> const& items)
    {
        if (items.empty())
        {
            MapsExpander().Visibility(Visibility::Collapsed);
            return;
        }
        auto rows = single_threaded_observable_vector<IInspectable>();
        for (auto const& item : items)
        {
            rows.Append(item);
        }
        MapList().ItemsSource(rows);
        MapsExpander().Visibility(Visibility::Visible);
    }

    void PlayerCard::OnViewMatchesClick(
        IInspectable const& /*sender*/, RoutedEventArgs const& /*args*/)
    {
        if (m_viewMatches)
        {
            m_viewMatches();
        }
    }
}
