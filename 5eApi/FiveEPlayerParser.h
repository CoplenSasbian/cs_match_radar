#pragma once

// 解析 5E player/home 响应为 PlayerHome。

#include "FiveEPlayerModels.h"
#include "FiveEJson.h"

namespace FiveE::Parse
{
    // 解析 player/home 响应体（完整 JSON 文本）。
    // 失败时返回 identity.uuid 为空的 profile。
    PlayerHome PlayerHomeFromJson(std::wstring const& responseBody);

    // 解析 data 对象（测试用）。
    PlayerHome PlayerHomeFromData(Json::JsonObjectRef const& data);
}
