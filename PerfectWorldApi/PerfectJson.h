#pragma once

// Windows.Data.Json 取值辅助（PerfectWorldApi 自用）。
//
// 这里刻意不依赖 5eApi 项目：两个平台模块是独立的 DLL，
// 共用头文件会让它们产生不必要的耦合与编译顺序依赖。
// 实现与 5eApi/FiveEJson 一致（同一平台上两个平台都要处理
// "数字有时是字符串" 这种返回）。

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
// IJsonValue 等接口类型只在 impl/Windows.Data.Json.0.h 中声明，
// 而 Windows.Data.Json.h 只引入 .2.h，因此这里显式引入。
#include <winrt/impl/Windows.Data.Json.0.h>
#include <winrt/impl/Windows.Data.Json.1.h>
#include <winrt/impl/Windows.Data.Json.2.h>

#include <optional>
#include <string>
#include <string_view>

namespace Perfect::Json
{
    using JsonValueRef = winrt::Windows::Data::Json::IJsonValue;
    using JsonObjectRef = winrt::Windows::Data::Json::JsonObject;
    using JsonArrayRef = winrt::Windows::Data::Json::JsonArray;
    using JsonValueKind = winrt::Windows::Data::Json::JsonValueType;

    // 安全取成员；不存在时返回 null。
    JsonValueRef Get(JsonObjectRef const& object, std::wstring_view key);

    bool IsNull(JsonValueRef const& value);

    std::wstring String(JsonValueRef const& value);
    std::optional<std::wstring> OptionalString(JsonObjectRef const& object, std::wstring_view key);
    std::optional<int32_t> OptionalInt32(JsonObjectRef const& object, std::wstring_view key);
    std::optional<int64_t> OptionalInt64(JsonObjectRef const& object, std::wstring_view key);
    std::optional<double> OptionalDouble(JsonObjectRef const& object, std::wstring_view key);
    std::optional<bool> OptionalBool(JsonObjectRef const& object, std::wstring_view key);
}
