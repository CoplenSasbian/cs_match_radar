#pragma once

// 解析完美平台解密后的 JSON 为 PerfectPlayer。

#include "PerfectModels.h"
#include "PerfectJson.h"

#include <string>

namespace Perfect::Parse
{
    // 解析 overview 解密后的 JSON（只填身份与基础分）。
    void ApplyOverview(Json::JsonObjectRef const& root, PerfectPlayer& out);

    // 解析 season-stats 解密后的 JSON（段位、ladder、地图、武器、雷达）。
    void ApplySeasonStats(Json::JsonObjectRef const& root, PerfectPlayer& out);

    // 解析 season-ladder-score-list（未加密）以发现最新赛季号。
    // 返回空串表示未找到。
    std::wstring FindLatestSeason(Json::JsonObjectRef const& root);

    // 便捷入口：直接解析解密后的文本。
    PerfectPlayer PlayerFromOverviewJson(std::wstring const& decryptedJson);
}
