#pragma once

#include "BoolToVisibility.g.h"
#include "InverseBoolToVisibility.g.h"

namespace winrt::App1::implementation
{
    // bool -> Visibility：true 显示。
    struct BoolToVisibility : BoolToVisibilityT<BoolToVisibility>
    {
        BoolToVisibility() = default;
        Windows::Foundation::IInspectable Convert(
            Windows::Foundation::IInspectable const& value,
            winrt::Windows::UI::Xaml::Interop::TypeName const& targetType,
            Windows::Foundation::IInspectable const& parameter,
            winrt::hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(
            Windows::Foundation::IInspectable const& value,
            winrt::Windows::UI::Xaml::Interop::TypeName const& targetType,
            Windows::Foundation::IInspectable const& parameter,
            winrt::hstring const& language);
    };

    // bool -> Visibility（取反）：true 隐藏。
    struct InverseBoolToVisibility : InverseBoolToVisibilityT<InverseBoolToVisibility>
    {
        InverseBoolToVisibility() = default;
        Windows::Foundation::IInspectable Convert(
            Windows::Foundation::IInspectable const& value,
            winrt::Windows::UI::Xaml::Interop::TypeName const& targetType,
            Windows::Foundation::IInspectable const& parameter,
            winrt::hstring const& language);
        Windows::Foundation::IInspectable ConvertBack(
            Windows::Foundation::IInspectable const& value,
            winrt::Windows::UI::Xaml::Interop::TypeName const& targetType,
            Windows::Foundation::IInspectable const& parameter,
            winrt::hstring const& language);
    };
}

namespace winrt::App1::factory_implementation
{
    struct BoolToVisibility : BoolToVisibilityT<BoolToVisibility, implementation::BoolToVisibility> {};
    struct InverseBoolToVisibility : InverseBoolToVisibilityT<InverseBoolToVisibility, implementation::InverseBoolToVisibility> {};
}
