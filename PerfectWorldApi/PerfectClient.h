#pragma once

// 完美世界对战平台玩家查询客户端。
//
// 两个接口都需要登录凭据（access_token + 登录账号自身 SteamID）。
// overview 与 season-stats 的响应是 AES-256-ECB 加密的，
// 先由 PerfectCrypto 解密，再交给 PerfectParser 解析。
//
// season-ladder-score-list 不加密，用于 overview 里没有赛季号时兜底。

#include "PerfectModels.h"

#include <winrt/Windows.Foundation.h>

#include <string>
#include <vector>

namespace Perfect
{
    // 登录凭据。由调用方（应用）负责获取与持久化。
    struct PerfectCredentials
    {
        std::wstring accessToken;   // 登录 token
        std::wstring selfSteamId;   // 登录账号自身的 SteamID64
    };

    // 查询失败信息。
    struct PerfectError
    {
        std::wstring message;
        int32_t httpStatus = 0;
        int32_t bizCode = 0;        // 1002 / 1033 表示登录态失效
    };

    class PerfectClient
    {
    public:
        PerfectClient();

        // 查询一名玩家：overview（身份、当前分）+ season-stats（段位、统计）。
        //
        // season-stats 失败时保留 overview 结果，并标记 partialFailure，
        // 这样界面至少能显示昵称与基础分。
        winrt::Windows::Foundation::IAsyncAction GetPlayerAsync(
            std::wstring steamId,
            PerfectCredentials const& credentials,
            PerfectPlayer& out);

        // 只查 overview（轻量，用于批量拉取身份与基础分）。
        winrt::Windows::Foundation::IAsyncAction GetOverviewAsync(
            std::wstring steamId,
            PerfectCredentials const& credentials,
            PerfectPlayer& out);

        // 查询赛季号列表（该接口不加密），返回按编号升序的赛季名。
        winrt::Windows::Foundation::IAsyncAction GetSeasonListAsync(
            std::wstring steamId,
            PerfectCredentials const& credentials,
            std::vector<std::wstring>& out);
    };
}
