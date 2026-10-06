#pragma once

// 把 5E 模型序列化为 JSON 文本，供 C 接口跨边界传递。
//
// 字段名与 C++ 模型成员一一对应（camelCase），调用方按需解析。
// 之所以序列化而不是重定义一套 DTO：模型会随接口字段变化而演进，
// 重新定义 DTO 会导致两处同步维护。

#include "FiveEModels.h"
#include "FiveEPlayerModels.h"

// 导出宏：DLL 侧导出，应用侧导入。
#if defined(MY5EAPI_EXPORTS) || defined(FIVEEAPI_EXPORTS)
#define FIVEE_SERIALIZE_API __declspec(dllexport)
#else
#define FIVEE_SERIALIZE_API __declspec(dllimport)
#endif

#include <string>
#include <vector>

namespace FiveE::Json
{
    // 单个对象。
    std::wstring ToJson(MatchSummary const& value);
    std::wstring ToJson(MatchDetail const& value);
    std::wstring ToJson(PlayerHome const& value);

    // 数组。
    std::wstring ToJson(std::vector<MatchSummary> const& values);

    // ---- 反序列化 ----
    // 供 C++ 轻封装把 C 接口返回的 JSON 还原成强类型模型。
    // 只解析 ToJson 写出的字段；缺失字段保持默认值。

    // 这三个由 5eApi.dll 导出（反序列化逻辑体量较大，
    // 不适合做成 header-inline 让每个调用方各自展开）。
    // 调用方与 DLL 使用同一套 MSVC / 同一 CRT，
    // 因此跨边界传 std::wstring / std::vector 是安全的。
    FIVEE_SERIALIZE_API std::vector<MatchSummary> MatchListFromJsonText(std::wstring const& text);
    FIVEE_SERIALIZE_API MatchDetail MatchDetailFromJsonText(std::wstring const& text);
    FIVEE_SERIALIZE_API PlayerHome PlayerHomeFromJsonText(std::wstring const& text);
}
