#pragma once

// 完美世界对战平台 —— 比赛记录查询。
//
// 与 PerfectClient（玩家资料）分开，因为鉴权方式不同：
//   - PerfectClient 走 pwaweblogin.wmpvp.com，用 ?access_token= 查询参数；
//   - 本客户端走 api.wmpvp.com，用 token 请求头，并要求 mySteamId 与 token 同源。
//
// 关键点：mySteamId 必须是 token 所属账号，toSteamId 才是"要查谁"。
// 因此只要自己登录过，就能查询任意玩家的最近对局。

#include "PerfectMatchModels.h"

#include <winrt/Windows.Foundation.h>

#include <string>
#include <vector>

namespace Perfect
{
    // 登录凭据。accessToken 与 selfSteamId 必须相互匹配。
    struct PerfectMatchCredentials
    {
        std::wstring accessToken;   // token
        std::wstring selfSteamId;   // token 所属账号的 SteamID64
    };

    class PerfectMatchClient
    {
    public:
        PerfectMatchClient();

        // 与 token 配套的请求头版本号（服务端会校验，实测 3.5.4.172 可用）。
        static constexpr wchar_t const* kAppVersion = L"3.5.4.172";

        // 查询指定玩家的最近对局列表。
        winrt::Windows::Foundation::IAsyncAction GetMatchListAsync(
            PerfectMatchQuery const& query,
            PerfectMatchCredentials const& credentials,
            std::vector<PerfectMatchSummary>& out);

        // 查询单场详情（含全部 10 名玩家）。
        winrt::Windows::Foundation::IAsyncAction GetMatchDetailAsync(
            std::wstring matchId,
            int32_t dataSource,
            PerfectMatchCredentials const& credentials,
            PerfectMatchDetail& out);

        // 按关键字搜索玩家。keyword 可以是完美昵称、完美编号或完整 SteamID；
        // 完全匹配的会排在结果第一个。
        //
        // 注意：该接口用 code==1 表示成功（与其他接口的 code==0 相反），
        // 属于平台侧的不一致，实测确认。
        // 拉取玩家完整资料（段位、本季战绩、累计、武器、地图、趋势）。
        winrt::Windows::Foundation::IAsyncAction GetPlayerProfileAsync(
            std::wstring steamId,
            PerfectMatchCredentials const& credentials,
            PerfectPlayerProfile& out);
        winrt::Windows::Foundation::IAsyncAction SearchUserAsync(
            std::wstring keyword,
            PerfectMatchCredentials const& credentials,
            std::vector<PerfectSearchHit>& out);
    };
}
