#pragma once

// 基于 Windows.Data.Json 的取值辅助函数。
// 5E 接口返回的数字有时是字符串（例如 /match/list 的 adr = "85.3"），
// 因此这里统一做“宽进”转换：数字、字符串形式的数字都能取到。

#include "FiveEModels.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
// IJsonValue 等接口类型只在 impl/Windows.Data.Json.0.h 中声明，
// 而 Windows.Data.Json.h 只引入 .2.h，因此这里显式引入 0/1/2 三个片段。
#include <winrt/impl/Windows.Data.Json.0.h>
#include <winrt/impl/Windows.Data.Json.1.h>
#include <winrt/impl/Windows.Data.Json.2.h>

#include <optional>
#include <string>
#include <ctime>
#include <string_view>
#include <vector>

namespace FiveE::Json
{
    // 统一使用投影类型（JsonObject / JsonArray）：
    // 它们提供 HasKey / Size / GetAt / 迭代等便利成员，
    // 而 IJsonObject / IJsonArray 接口本身只有 ABI 级方法。
    //
    // 注意：Windows.Data.Json 里同时存在 JsonValue / JsonObject / JsonArray
    // 这三个“投影类”，因此这里不能把别名也命名为 JsonValue，否则会遮蔽
    // 投影类并导致 JsonValue::CreateStringValue 之类的静态方法无法解析。
    using JsonValueRef = winrt::Windows::Data::Json::IJsonValue;
    using JsonObjectRef = winrt::Windows::Data::Json::JsonObject;
    using JsonArrayRef = winrt::Windows::Data::Json::JsonArray;
    using JsonValueKind = winrt::Windows::Data::Json::JsonValueType;

    // 安全取成员：对象存在且成员存在时返回该成员，否则返回 null。
    JsonValueRef Get(JsonObjectRef const& object, std::wstring_view key);

    // 安全取数组元素。
    JsonValueRef GetAt(JsonArrayRef const& array, uint32_t index);

    bool IsNull(JsonValueRef const& value);

    // ---- 基础类型 ----
    std::wstring String(JsonValueRef const& value);
    std::optional<std::wstring> OptionalString(JsonValueRef const& value);
    std::optional<int32_t> OptionalInt32(JsonValueRef const& value);
    std::optional<int64_t> OptionalInt64(JsonValueRef const& value);
    std::optional<double> OptionalDouble(JsonValueRef const& value);
    std::optional<bool> OptionalBool(JsonValueRef const& value);

    // 从对象按 key 取值，任一环节缺失都返回 nullopt。
    std::optional<std::wstring> OptionalString(JsonObjectRef const& object, std::wstring_view key);
    std::optional<int32_t> OptionalInt32(JsonObjectRef const& object, std::wstring_view key);
    std::optional<int64_t> OptionalInt64(JsonObjectRef const& object, std::wstring_view key);
    std::optional<double> OptionalDouble(JsonObjectRef const& object, std::wstring_view key);
    std::optional<bool> OptionalBool(JsonObjectRef const& object, std::wstring_view key);

    // ---- 领域转换 ----
    Side ParseSide(std::wstring_view text);
    MatchStatus ParseStatus(JsonValueRef const& value);

    // 把对象的所有顶层标量成员收集到 ScalarMap；嵌套对象 / 数组转为 JSON 文本。
    ScalarMap CollectScalars(JsonObjectRef const& object);

    // 把 JSON 值原样序列化为 JSON 文本（解析失败返回空串）。
    std::wstring ToJsonText(JsonValueRef const& value);

    // 解析 5E 的 *uids 字段：可能是数组，也可能是逗号 / 分号分隔的字符串。
    std::vector<std::wstring> SplitPlayerIds(JsonValueRef const& value);

    // 解析 5E 的 Unix 时间戳（秒级）。
    std::optional<int64_t> ParseUnixSeconds(std::wstring_view text);

    // 把 Unix 秒级时间戳格式化为本地时间 "YYYY-MM-DD HH:MM:SS"。
    // inline：调用方（宿主应用）需要用它做显示格式化，
    // 实现必须可见，否则会依赖 DLL 内部符号。
    inline std::wstring FormatUnixSeconds(int64_t seconds)
    {
        const std::time_t raw = static_cast<std::time_t>(seconds);
        std::tm local{};
        if (localtime_s(&local, &raw) != 0)
        {
            return {};
        }
        wchar_t buffer[32]{};
        if (std::wcsftime(buffer, 32, L"%Y-%m-%d %H:%M:%S", &local) == 0)
        {
            return {};
        }
        return std::wstring(buffer);
    }
}
