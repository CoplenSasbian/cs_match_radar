#include "FiveEApiPch.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <cmath>
#include <cwchar>
#include <ctime>
#include <string>
#include <string_view>


namespace FiveE::Json
{
    namespace
    {
        // 去掉字符串两端的空白，并剥离包裹的引号（5E 偶尔返回带引号的数字）。
        std::wstring TrimQuotes(std::wstring_view text)
        {
            size_t begin = 0;
            size_t end = text.size();
            auto isSpace = [](wchar_t c) {
                return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
            };
            while (begin < end && isSpace(text[begin]))
            {
                ++begin;
            }
            while (end > begin && isSpace(text[end - 1]))
            {
                --end;
            }
            if (end - begin >= 2)
            {
                const wchar_t first = text[begin];
                const wchar_t last = text[end - 1];
                if ((first == L'"' && last == L'"') || (first == L'\'' && last == L'\''))
                {
                    ++begin;
                    --end;
                }
            }
            return std::wstring(text.substr(begin, end - begin));
        }

        bool TryParseWideNumber(std::wstring const& text, double& out)
        {
            if (text.empty())
            {
                return false;
            }
            wchar_t* end = nullptr;
            const double value = std::wcstod(text.c_str(), &end);
            if (end == text.c_str())
            {
                return false;
            }
            // 允许尾部残留空白，其余残留视为不是纯数字。
            while (end && *end != L'\0' && (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n'))
            {
                ++end;
            }
            if (end && *end != L'\0')
            {
                return false;
            }
            if (std::isnan(value) || std::isinf(value))
            {
                return false;
            }
            out = value;
            return true;
        }
    }

    bool IsNull(JsonValueRef const& value)
    {
        return !value || value.ValueType() == JsonValueKind::Null;
    }

    JsonValueRef Get(JsonObjectRef const& object, std::wstring_view key)
    {
        if (!object)
        {
            return nullptr;
        }
        const std::wstring owned(key);
        if (!object.HasKey(owned))
        {
            return nullptr;
        }
        return object.GetNamedValue(owned);
    }

    JsonValueRef GetAt(JsonArrayRef const& array, uint32_t index)
    {
        if (!array || index >= array.Size())
        {
            return nullptr;
        }
        return array.GetAt(index);
    }

    std::wstring String(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return {};
        }
        switch (value.ValueType())
        {
        case JsonValueKind::String:
            return std::wstring(value.GetString());
        case JsonValueKind::Number:
        {
            const double number = value.GetNumber();
            // 整数值不显示小数部分，避免出现 "1234.000000"。
            if (std::floor(number) == number && std::fabs(number) < 1e15)
            {
                wchar_t buffer[32]{};
                swprintf_s(buffer, L"%.0f", number);
                return std::wstring(buffer);
            }
            wchar_t buffer[64]{};
            swprintf_s(buffer, L"%g", number);
            return std::wstring(buffer);
        }
        case JsonValueKind::Boolean:
            return value.GetBoolean() ? L"true" : L"false";
        default:
            return ToJsonText(value);
        }
    }

    std::optional<std::wstring> OptionalString(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return std::nullopt;
        }
        std::wstring text = String(value);
        if (text.empty())
        {
            return std::nullopt;
        }
        return text;
    }

    std::optional<double> OptionalDouble(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return std::nullopt;
        }
        switch (value.ValueType())
        {
        case JsonValueKind::Number:
            return value.GetNumber();
        case JsonValueKind::String:
        {
            double parsed = 0.0;
            if (TryParseWideNumber(TrimQuotes(value.GetString()), parsed))
            {
                return parsed;
            }
            return std::nullopt;
        }
        case JsonValueKind::Boolean:
            return value.GetBoolean() ? 1.0 : 0.0;
        default:
            return std::nullopt;
        }
    }

    std::optional<int64_t> OptionalInt64(JsonValueRef const& value)
    {
        const auto number = OptionalDouble(value);
        if (!number)
        {
            return std::nullopt;
        }
        return static_cast<int64_t>(std::llround(*number));
    }

    std::optional<int32_t> OptionalInt32(JsonValueRef const& value)
    {
        const auto number = OptionalInt64(value);
        if (!number)
        {
            return std::nullopt;
        }
        return static_cast<int32_t>(*number);
    }

    std::optional<bool> OptionalBool(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return std::nullopt;
        }
        switch (value.ValueType())
        {
        case JsonValueKind::Boolean:
            return value.GetBoolean();
        case JsonValueKind::Number:
            return value.GetNumber() != 0.0;
        case JsonValueKind::String:
        {
            const std::wstring text = TrimQuotes(value.GetString());
            if (text == L"true" || text == L"1")
            {
                return true;
            }
            if (text == L"false" || text == L"0" || text.empty())
            {
                return false;
            }
            return std::nullopt;
        }
        default:
            return std::nullopt;
        }
    }

    std::optional<std::wstring> OptionalString(JsonObjectRef const& object, std::wstring_view key)
    {
        return OptionalString(Get(object, key));
    }

    std::optional<int32_t> OptionalInt32(JsonObjectRef const& object, std::wstring_view key)
    {
        return OptionalInt32(Get(object, key));
    }

    std::optional<int64_t> OptionalInt64(JsonObjectRef const& object, std::wstring_view key)
    {
        return OptionalInt64(Get(object, key));
    }

    std::optional<double> OptionalDouble(JsonObjectRef const& object, std::wstring_view key)
    {
        return OptionalDouble(Get(object, key));
    }

    std::optional<bool> OptionalBool(JsonObjectRef const& object, std::wstring_view key)
    {
        return OptionalBool(Get(object, key));
    }

    Side ParseSide(std::wstring_view text)
    {
        if (text == L"t" || text == L"T" || text == L"terrorist" || text == L"Terrorist")
        {
            return Side::Terrorist;
        }
        if (text == L"ct" || text == L"CT" || text == L"counter_terrorist" ||
            text == L"CounterTerrorist" || text == L"counterterrorist")
        {
            return Side::CounterTerrorist;
        }
        return Side::Unknown;
    }

    MatchStatus ParseStatus(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return MatchStatus::Unknown;
        }
        // 5E 的 status 既可能是数字也可能是文本，两种都尝试。
        if (value.ValueType() == JsonValueKind::Number)
        {
            const int32_t code = static_cast<int32_t>(value.GetNumber());
            switch (code)
            {
            case 2:
            case 3:
            case 4:
                return MatchStatus::Finished;
            case 1:
                return MatchStatus::Ongoing;
            case 0:
                return MatchStatus::Cancelled;
            default:
                return MatchStatus::Unknown;
            }
        }
        const std::wstring text = String(value);
        if (text == L"finished" || text == L"2" || text == L"3" || text == L"ended")
        {
            return MatchStatus::Finished;
        }
        if (text == L"ongoing" || text == L"1" || text == L"playing")
        {
            return MatchStatus::Ongoing;
        }
        if (text == L"cancelled" || text == L"canceled" || text == L"0" || text == L"waiver")
        {
            return MatchStatus::Cancelled;
        }
        return MatchStatus::Unknown;
    }

    ScalarMap CollectScalars(JsonObjectRef const& object)
    {
        ScalarMap result;
        if (!object)
        {
            return result;
        }
        for (auto const& pair : object)
        {
            const std::wstring key(pair.Key());
            const JsonValueRef value = pair.Value();
            if (IsNull(value))
            {
                continue;
            }
            switch (value.ValueType())
            {
            case JsonValueKind::Object:
            case JsonValueKind::Array:
                // 嵌套结构不适合放进标量表，转成 JSON 文本保留。
                result[key] = ToJsonText(value);
                break;
            default:
                result[key] = String(value);
                break;
            }
        }
        return result;
    }

    std::wstring ToJsonText(JsonValueRef const& value)
    {
        if (IsNull(value))
        {
            return {};
        }
        try
        {
            return std::wstring(value.Stringify());
        }
        catch (winrt::hresult_error const&)
        {
            return {};
        }
    }

    std::vector<std::wstring> SplitPlayerIds(JsonValueRef const& value)
    {
        std::vector<std::wstring> ids;
        if (IsNull(value))
        {
            return ids;
        }
        if (value.ValueType() == JsonValueKind::Array)
        {
            const JsonArrayRef array = value.GetArray();
            for (uint32_t i = 0; i < array.Size(); ++i)
            {
                auto item = OptionalString(array.GetAt(i));
                if (item && !item->empty())
                {
                    ids.push_back(*item);
                }
            }
            return ids;
        }

        std::wstring text = String(value);
        for (wchar_t& ch : text)
        {
            if (ch == L';' || ch == L'|' || ch == L' ')
            {
                ch = L',';
            }
        }
        size_t start = 0;
        while (start <= text.size())
        {
            const size_t comma = text.find(L',', start);
            const size_t end = (comma == std::wstring::npos) ? text.size() : comma;
            std::wstring token = text.substr(start, end - start);
            if (!token.empty())
            {
                ids.push_back(std::move(token));
            }
            if (comma == std::wstring::npos)
            {
                break;
            }
            start = comma + 1;
        }
        return ids;
    }

    std::optional<int64_t> ParseUnixSeconds(std::wstring_view text)
    {
        double parsed = 0.0;
        if (!TryParseWideNumber(TrimQuotes(text), parsed))
        {
            return std::nullopt;
        }
        return static_cast<int64_t>(parsed);
    }

}
