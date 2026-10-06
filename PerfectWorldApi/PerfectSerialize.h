#pragma once

// 把完美平台模型序列化为 JSON，供 C 接口跨边界传递。

#include "PerfectModels.h"

#include <string>
#include <vector>

namespace Perfect::Json
{
    std::wstring ToJson(PerfectPlayer const& value);

    // 只序列化 overview 部分（身份 + 当前分）。
    std::wstring ToOverviewJson(PerfectPlayer const& value);

    // 赛季号数组。
    std::wstring ToJson(std::vector<std::wstring> const& values);
}
