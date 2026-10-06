#pragma once

// 5E 开放接口客户端。
//
// 接口来源（社区逆向整理，非官方文档，字段可能随平台更新而变化）：
//   GET  https://gate.5eplay.com/crane/http/api/data/match/list
//   GET  https://gate.5eplay.com/crane/http/api/data/match/{match_id}
//   GET  https://gate.5eplay.com/crane/http/api/data/match/advanced/{match_id}
//   GET  https://gate.5eplay.com/crane/http/api/match/leetify_rating/{match_id}
//   GET  https://gate.5eplay.com/crane/http/api/data/vip_plus_match_data/{match_id}
//   POST https://gate.5eplay.com/userinterface/http/v1/userinterface/idTransfer
//
// 该 DLL 为 UWP / AppContainer 组件，网络访问依赖调用方应用清单中的
// internetClient（以及访问非默认端口时的 privateNetworkClientServer）能力。

#include "FiveEModels.h"
#include "FiveEPlayerModels.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace FiveE
{
    // 客户端配置。
    struct FiveEClientOptions
    {
        // 可选 Bearer 令牌。部分接口（如 match/list）匿名即可访问；
        // 如需登录态数据，由宿主应用提供。
        std::wstring bearerToken;

        // 单次请求超时，默认 15 秒。
        int32_t timeoutSeconds = 15;

        // 每次请求之间的最小间隔，避免触发平台限流。默认 250ms。
        int32_t minRequestIntervalMs = 250;
    };

    // 5E API 客户端。
    //
    // 生命周期：winrt::Windows::Web::Http 类型必须在 apartment 内使用。
    // 典型用法是在 UI 线程创建，随后任意线程 co_await 其成员协程。
    class FiveEClient
    {
    public:
        explicit FiveEClient(FiveEClientOptions options = {});

        FiveEClient(FiveEClient const&) = delete;
        FiveEClient& operator=(FiveEClient const&) = delete;

        // 按昵称 / 5E ID 搜索玩家。
        // 接口返回的 uuid 可能为空，命中结果会再调 idTransfer 补齐，
        // 因此结果里的 uuid 可直接用于后续查询。
        winrt::Windows::Foundation::IAsyncAction SearchPlayerAsync(
            std::wstring keyword,
            std::vector<SearchHit>& out);
        // 通过 5E 用户 ID（domain，形如 "11814738gjdwn7"）换取 uuid。
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetUuidAsync(std::wstring userId);

        // 以下方法返回非 WinRT 类型（自定义结构体 / std::vector），
        // 无法放进 IAsyncOperation<T>，因此采用“IAsyncAction + 输出参数”的形式：
        //
        //   std::vector<FiveE::MatchSummary> matches;
        //   co_await client.GetMatchListAsync(query, matches);
        //
        // 协程正常返回即表示成功，失败通过抛 FiveE::ApiError 传播。
        // out 参数只在成功时被写入，失败时保持不变。

        // 获取比赛列表（单页）。
        winrt::Windows::Foundation::IAsyncAction GetMatchListAsync(
            MatchListQuery const& query,
            std::vector<MatchSummary>& out);

        // 获取比赛列表（自动翻页）。maxPages <= 0 表示不限制页数。
        winrt::Windows::Foundation::IAsyncAction GetAllMatchesAsync(
            MatchListQuery const& query,
            std::vector<MatchSummary>& out,
            int32_t maxPages = 10);

        // 获取单场比赛详情（含双方队伍与全部玩家数据）。
        winrt::Windows::Foundation::IAsyncAction GetMatchDetailAsync(
            std::wstring matchId,
            MatchDetail& out);

        // 在详情基础上追加 advanced / leetify_rating / vip_plus 三个可选接口的数据。
        // 任一可选接口失败都不会让整体失败，失败项不出现在 extraRaw 中。
        winrt::Windows::Foundation::IAsyncAction GetMatchDetailWithExtrasAsync(
            std::wstring matchId,
            MatchDetail& out);

        // 汇总入口：给定 5E 用户 ID，拉取最近若干场对局详情。
        // limit 为比赛场次上限，maxPages 为列表翻页上限。
        winrt::Windows::Foundation::IAsyncAction GetRecentMatchDetailsAsync(
            std::wstring userId,
            std::vector<MatchDetail>& out,
            int32_t limit = 30,
            int32_t maxPages = 10);

        // 取单场比赛的 Demo 下载地址，没有则返回空串。
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetDemoUrlAsync(
            std::wstring matchId);

        // ---- 原始 JSON 访问 ----
        // 返回接口响应的原始 JSON 文本，不做模型转换，便于调用方自行解析
        // 或排查 5E 字段变更。

        // 取 match/list 的原始 JSON 文本。
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> FetchMatchListRawAsync(
            MatchListQuery const& query);

        // 取 match/{id} 的原始 JSON 文本。
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> FetchMatchDetailRawAsync(
            std::wstring matchId);

        // 取任意 5E 接口的原始 JSON 文本（完整 URL）。
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> FetchRawAsync(std::wstring url);

        // ---- 玩家资料（Gate HMAC 签名接口）----
        //
        // 这是"对局雷达"的核心数据源：给定玩家 UUID，返回其
        // 昵称、头像、ELO、段位、排名、信用分、赛季统计。
        //
        // 注：该接口使用 5E 网关的 HMAC 签名（见 FiveEGateSign.h），
        // 无需登录态。实测 HTTP 200。
        winrt::Windows::Foundation::IAsyncAction GetPlayerProfileAsync(
            std::wstring uuid,
            PlayerHome& out);

        // 批量查询多名玩家的资料（串行 + 限流，避免触发风控）。
        winrt::Windows::Foundation::IAsyncAction GetPlayerProfilesAsync(
            std::vector<std::wstring> const& uuids,
            std::vector<PlayerHome>& out);

        // ---- 统计辅助（纯函数，便于单元测试） ----

        // 从详情中找出“我”（指定 uid）所在队伍。
        static std::optional<TeamGroup> FindPlayerGroup(MatchDetail const& detail, std::wstring_view playerUid);

        // 从列表摘要判断指定玩家是否取胜。
        static bool IsWinForPlayer(MatchSummary const& summary);

    private:
        FiveEClientOptions m_options;
        winrt::Windows::Web::Http::HttpClient m_httpClient{ nullptr };
        // 上一次请求发起的时刻，用于限流。
        std::chrono::steady_clock::time_point m_lastRequest{};
    };
}
