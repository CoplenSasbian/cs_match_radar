#pragma once

// 把 5E 接口返回的原始 JSON 映射为 FiveEModels 中的结构体。
// 解析函数与网络层分离，便于用固定 JSON 样本单独测试。

#include "FiveEModels.h"
#include "FiveEJson.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace FiveE::Parse
{
    // 依次返回第一个存在且非空的字符串。
    std::wstring FirstNonEmpty(std::initializer_list<std::optional<std::wstring>> values);

    // 解析 /match/list 的单个条目（data 数组中的一项）。
    MatchSummary MatchSummaryFromJson(FiveE::Json::JsonObjectRef const& object);

    // 解析 /match/list 响应的 data 数组。
    std::vector<MatchSummary> MatchListFromJson(FiveE::Json::JsonValueRef const& data);

    // 解析 /match/{id} 响应的 data 对象。
    MatchDetail MatchDetailFromJson(FiveE::Json::JsonObjectRef const& data);

    // 解析 idTransfer 响应的 data.uuid。
    std::wstring UuidFromJson(FiveE::Json::JsonObjectRef const& data);
}
