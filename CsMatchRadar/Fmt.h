#pragma once

// 界面共用的格式化辅助。
//
// 原本这些函数只写在 MainWindow.xaml.cpp 的匿名 namespace 里；
// MatchWindow 也需要同样的格式化（列表、比分、KDA 等），
// 故提取到本头文件，两个窗口共用，避免重复实现。

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

#include <optional>
#include <string>
#include <cstdio>

#include "FiveEJson.h"

namespace AppFmt
{
    using winrt::Microsoft::UI::Xaml::Application;
    using winrt::Microsoft::UI::Xaml::Media::Brush;
    using winrt::hstring;

    inline std::wstring FormatDouble(std::optional<double> const& value, int decimals)
    {
        if (!value.has_value())
        {
            return L"—";
        }
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"%.*f", decimals, *value);
        return buffer;
    }

    inline std::wstring FormatInt(std::optional<int32_t> const& value)
    {
        if (!value.has_value())
        {
            return L"—";
        }
        return std::to_wstring(*value);
    }

    // 也接受非 optional 的整数（完美接口很多字段是 0 而非空）。
    inline std::wstring FormatInt(int32_t value)
    {
        return std::to_wstring(value);
    }

    inline std::wstring FormatTime(std::optional<int64_t> const& value)
    {
        if (!value.has_value())
        {
            return L"";
        }
        return FiveE::Json::FormatUnixSeconds(*value);
    }

    inline std::wstring FormatDuration(
        std::optional<int64_t> const& start, std::optional<int64_t> const& end)
    {
        if (!start.has_value() || !end.has_value() || *end <= *start)
        {
            return L"—";
        }
        const int64_t minutes = (*end - *start) / 60;
        if (minutes < 60)
        {
            return std::to_wstring(minutes) + L" 分钟";
        }
        return std::to_wstring(minutes / 60) + L" 小时 " + std::to_wstring(minutes % 60) + L" 分";
    }

    // 胜负对应的强调色（自动适配浅色 / 深色主题）。
    inline Brush AccentBrush(bool isWin)
    {
        const wchar_t* key = isWin
            ? L"SystemFillColorSuccessBrush"
            : L"SystemFillColorCriticalBrush";
        if (auto found = Application::Current().Resources().TryLookup(winrt::box_value(key)))
        {
            if (auto brush = found.try_as<Brush>())
            {
                return brush;
            }
        }
        return nullptr;
    }

    inline hstring H(std::wstring const& text)
    {
        return hstring(text);
    }

    // std::exception::what() 是窄字符（通常是 UTF-8），转成宽字符。
    inline std::wstring Widen(std::string const& narrow)
    {
        if (narrow.empty())
        {
            return L"未知错误";
        }
        const int size = ::MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
        if (size <= 1)
        {
            return L"未知错误";
        }
        std::wstring wide(static_cast<size_t>(size - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wide.data(), size);
        return wide;
    }
}
