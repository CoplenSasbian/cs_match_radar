#include "pch.h"
#include "Converters.h"

// 生成文件必须紧跟在定义实现类型的头文件之后编译：
// .g.h 只声明模板基类，实现结构体在 Converters.h 中定义，
// 而 .g.cpp 需要这两者都可见才能完成 factory 与构造函数注册。
#include "BoolToVisibility.g.cpp"
#include "InverseBoolToVisibility.g.cpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using winrt::Windows::UI::Xaml::Interop::TypeName;

namespace winrt::App1::implementation
{
    IInspectable BoolToVisibility::Convert(
        IInspectable const& value, TypeName const&, IInspectable const&, hstring const&)
    {
        bool b = false;
        if (auto v = value.try_as<IPropertyValue>())
        {
            if (v.Type() == PropertyType::Boolean) { b = v.GetBoolean(); }
        }
        return box_value(b ? Visibility::Visible : Visibility::Collapsed);
    }

    IInspectable BoolToVisibility::ConvertBack(
        IInspectable const& value, TypeName const&, IInspectable const&, hstring const&)
    {
        bool b = (unbox_value<Microsoft::UI::Xaml::Visibility>(value) == Microsoft::UI::Xaml::Visibility::Visible);
        return box_value(b);
    }

    IInspectable InverseBoolToVisibility::Convert(
        IInspectable const& value, TypeName const&, IInspectable const&, hstring const&)
    {
        bool b = false;
        if (auto v = value.try_as<IPropertyValue>())
        {
            if (v.Type() == PropertyType::Boolean) { b = v.GetBoolean(); }
        }
        return box_value(b ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible);
    }

    IInspectable InverseBoolToVisibility::ConvertBack(
        IInspectable const& value, TypeName const&, IInspectable const&, hstring const&)
    {
        bool b = (unbox_value<Microsoft::UI::Xaml::Visibility>(value) == Microsoft::UI::Xaml::Visibility::Visible);
        return box_value(!b);
    }
}
