#include "pch.h"
#include "RadarPlayerCard.xaml.h"
#if __has_include("RadarPlayerCard.g.cpp")
#include "RadarPlayerCard.g.cpp"
#endif

#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Media.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::App1::implementation
{
    RadarPlayerCard::RadarPlayerCard()
    {
        InitializeComponent();
    }

    void RadarPlayerCard::Player(winrt::App1::RadarPlayer const& value)
    {
        m_player = value;
        if (Bindings)
        {
            Bindings->Update();
        }
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(L"Player"));
    }

    winrt::event_token RadarPlayerCard::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void RadarPlayerCard::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void RadarPlayerCard::CardPointerEntered(IInspectable const&, PointerRoutedEventArgs const&)
    {
        if (auto border = CardBorder())
        {
            try
            {
                if (auto app = Microsoft::UI::Xaml::Application::Current())
                {
                    if (auto brush = app.Resources().TryLookup(winrt::box_value(L"AccentFillColorDefaultBrush")).try_as<Media::Brush>())
                    {
                        border.BorderBrush(brush);
                    }
                }
            }
            catch (...) {}
        }
    }

    void RadarPlayerCard::CardPointerExited(IInspectable const&, PointerRoutedEventArgs const&)
    {
        if (auto border = CardBorder())
        {
            try
            {
                if (auto app = Microsoft::UI::Xaml::Application::Current())
                {
                    if (auto brush = app.Resources().TryLookup(winrt::box_value(L"CardStrokeColorDefaultBrush")).try_as<Media::Brush>())
                    {
                        border.BorderBrush(brush);
                    }
                }
            }
            catch (...) {}
        }
    }

    void RadarPlayerCard::CardPointerPressed(IInspectable const&, PointerRoutedEventArgs const&)
    {
        if (auto border = CardBorder())
        {
            try
            {
                if (auto app = Microsoft::UI::Xaml::Application::Current())
                {
                    if (auto brush = app.Resources().TryLookup(winrt::box_value(L"LayerFillColorDefaultBrush")).try_as<Media::Brush>())
                    {
                        border.Background(brush);
                    }
                }
            }
            catch (...) {}
        }
    }

    void RadarPlayerCard::OnOpenFiveE(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_player || !m_player.HasFiveE()) { return; }
        // 5E 玩家主页（按 UUID）。
        OpenUrl(L"https://arena.5eplay.com/player/" +
            std::wstring(m_player.FiveEUuid().c_str()));
    }

    void RadarPlayerCard::OnOpenPerfect(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_player || !m_player.HasPerfect()) { return; }
        // 完美世界玩家主页（按 SteamID64）。
        OpenUrl(L"https://pvp.wanmei.com/csgo/steam/" +
            std::wstring(m_player.PerfectSteamId().c_str()));
    }

    void RadarPlayerCard::OpenUrl(std::wstring const& url)
    {
        if (url.empty()) { return; }
        try
        {
            Windows::System::Launcher::LaunchUriAsync(
                Windows::Foundation::Uri(hstring(url)));
        }
        catch (...) { /* 用户取消或无默认浏览器时忽略 */ }
    }
}
