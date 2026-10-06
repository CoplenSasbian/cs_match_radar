#pragma once

#include "RadarPlayerCard.g.h"

namespace winrt::App1::implementation
{
    // 雷达玩家卡片。绑定一个 RadarPlayer 行，点击按钮打开
    // 5E 或完美的玩家页面。INotifyPropertyChanged 已由 IDL 声明，
    // 生成基类 RadarPlayerCardT 自带该接口实现。
    struct RadarPlayerCard : RadarPlayerCardT<RadarPlayerCard>
    {
        RadarPlayerCard();

        winrt::App1::RadarPlayer Player() const { return m_player; }
        void Player(winrt::App1::RadarPlayer const& value);

        // IDL declares INotifyPropertyChanged; the generated base provides the
        // ABI implementation. Keep these forwarding methods explicit for the
        // C++ implementation used by the binding layer.
        winrt::event_token PropertyChanged(
            winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        void CardPointerEntered(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void CardPointerExited(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void CardPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnOpenFiveE(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnOpenPerfect(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::App1::RadarPlayer m_player{ nullptr };
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;

        void OpenUrl(std::wstring const& url);
    };
}

namespace winrt::App1::factory_implementation
{
    struct RadarPlayerCard : RadarPlayerCardT<RadarPlayerCard, implementation::RadarPlayerCard> {};
}
