#include "PerfectApiPch.h"
#include "PerfectJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <cmath>
#include <cwchar>
#include <string>

using namespace winrt::Windows::Data::Json;

namespace Perfect::Json
{
    namespace
    {
        std::wstring TrimQuotes(std::wstring_view text)
        {
            size_t begin = 0;
            size_t end = text.size();
            auto isSpace = [](wchar_t c) {
                return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
            };
            while (begin < end && isSpace(text[begin])) ++begin;
            while (end > begin && isSpace(text[end - 1])) --end;
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
            if (text.empty()) return false;
            wchar_t* end = nullptr;
            const double value = std::wcstod(text.c_str(), &end);
            if (end == text.c_str()) return false;
            while (end && *end != L'\0' &&
                   (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n'))
            {
                ++end;
            }
            if (end && *end != L'\0') return false;
            if (std::isnan(value) || std::isinf(value)) return false;
            out = value;
            return true;
        }
    }

    bool IsNull(JsonValueRef const& value)
    {
        return !value || value.ValueType() == JsonValueType::Null;
    }

    JsonValueRef Get(JsonObjectRef const& object, std::wstring_view key)
    {
        if (!object) return nullptr;
        const std::wstring owned(key);
        if (!object.HasKey(owned)) return nullptr;
        return object.GetNamedValue(owned);
    }

    std::wstring String(JsonValueRef const& value)
    {
        if (IsNull(value)) return {};
        switch (value.ValueType())
        {
        case JsonValueType::String:
            return std::wstring(value.GetString());
        case JsonValueType::Number:
        {
            const double number = value.GetNumber();
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
        case JsonValueType::Boolean:
            return value.GetBoolean() ? L"true" : L"false";
        default:
            try { return std::wstring(value.Stringify()); }
            catch (winrt::hresult_error const&) { return {}; }
        }
    }

    std::optional<std::wstring> OptionalString(JsonObjectRef const& object, std::wstring_view key)
    {
        const auto value = Get(object, key);
        if (IsNull(value)) return std::nullopt;
        std::wstring text = String(value);
        if (text.empty()) return std::nullopt;
        return text;
    }

    std::optional<double> OptionalDouble(JsonObjectRef const& object, std::wstring_view key)
    {
        const auto value = Get(object, key);
        if (IsNull(value)) return std::nullopt;
        switch (value.ValueType())
        {
        case JsonValueType::Number:
            return value.GetNumber();
        case JsonValueType::String:
        {
            double parsed = 0.0;
            if (TryParseWideNumber(TrimQuotes(value.GetString()), parsed)) return parsed;
            return std::nullopt;
        }
        case JsonValueType::Boolean:
            return value.GetBoolean() ? 1.0 : 0.0;
        default:
            return std::nullopt;
        }
    }

    std::optional<int64_t> OptionalInt64(JsonObjectRef const& object, std::wstring_view key)
    {
        const auto number = OptionalDouble(object, key);
        if (!number) return std::nullopt;
        return static_cast<int64_t>(std::llround(*number));
    }

    std::optional<int32_t> OptionalInt32(JsonObjectRef const& object, std::wstring_view key)
    {
        const auto number = OptionalInt64(object, key);
        if (!number) return std::nullopt;
        return static_cast<int32_t>(*number);
    }

    std::optional<bool> OptionalBool(JsonObjectRef const& object, std::wstring_view key)
    {
        const auto value = Get(object, key);
        if (IsNull(value)) return std::nullopt;
        switch (value.ValueType())
        {
        case JsonValueType::Boolean:
            return value.GetBoolean();
        case JsonValueType::Number:
            return value.GetNumber() != 0.0;
        case JsonValueType::String:
        {
            const std::wstring text = TrimQuotes(value.GetString());
            if (text == L"true" || text == L"1") return true;
            if (text == L"false" || text == L"0" || text.empty()) return false;
            return std::nullopt;
        }
        default:
            return std::nullopt;
        }
    }
}
