#include "PerfectApiPch.h"
#include "PerfectClient.h"
#include "PerfectCrypto.h"
#include "PerfectParser.h"
#include "PerfectJson.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/base.h>

#include <string>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;

namespace Perfect
{
    namespace
    {
        constexpr std::wstring_view kOverviewUrl =
            L"https://pwaweblogin.wmpvp.com/user-info/overview";
        constexpr std::wstring_view kSeasonStatsUrl =
            L"https://pwaweblogin.wmpvp.com/user-info/season-stats";
        constexpr std::wstring_view kSeasonListUrl =
            L"https://pwaweblogin.wmpvp.com/user-info/season-ladder-score-list";
        constexpr std::wstring_view kReferer = L"https://client.wmpvp.com";

        // URL 编码。
        std::wstring UrlEncode(std::wstring_view text)
        {
            static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
            std::wstring out;
            out.reserve(text.size() * 3);
            for (const wchar_t ch : text)
            {
                const bool unreserved =
                    (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
                    (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.' || ch == L'~';
                if (unreserved)
                {
                    out.push_back(ch);
                }
                else
                {
                    const auto byte = static_cast<unsigned int>(static_cast<uint16_t>(ch));
                    out.push_back(L'%');
                    out.push_back(kHex[(byte >> 4) & 0xF]);
                    out.push_back(kHex[byte & 0xF]);
                }
            }
            return out;
        }

        // 逐个请求构造 HttpRequestMessage：与 5E 那边同样的教训，
        // 用 HttpClient::DefaultRequestHeaders 追加自定义头容易出问题。
        HttpRequestMessage MakeRequest(Uri const& uri, PerfectCredentials const& credentials)
        {
            HttpRequestMessage request;
            request.RequestUri(uri);
            request.Method(HttpMethod::Get());
            auto headers = request.Headers();
            headers.Append(L"Referer", std::wstring(kReferer));
            headers.Append(L"PwaSteamId", credentials.selfSteamId);
            return request;
        }
    }

    PerfectClient::PerfectClient() = default;

    IAsyncAction PerfectClient::GetPlayerAsync(
        std::wstring steamId,
        PerfectCredentials const& credentials,
        PerfectPlayer& out)
    {
        if (steamId.empty())
        {
            throw hresult_invalid_argument(L"steamId 不能为空");
        }
        if (credentials.accessToken.empty() || credentials.selfSteamId.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先登录完美平台");
        }

        HttpClient client;

        // ---- 1. overview ----
        const std::wstring overviewUrl =
            std::wstring(kOverviewUrl) + L"?access_token=" + UrlEncode(credentials.accessToken) +
            L"&recent_ladder_match=1&uid=" + UrlEncode(steamId);

        auto overviewRequest = MakeRequest(Uri(overviewUrl), credentials);
        auto overviewResponse = co_await client.SendRequestAsync(overviewRequest);
        const std::wstring overviewBody =
            (co_await overviewResponse.Content().ReadAsStringAsync()).c_str();

        // 解密（或透传错误响应）。
        const auto decrypted = DecryptResponse(overviewBody);
        if (!decrypted.ok)
        {
            throw hresult_error(E_FAIL,
                winrt::hstring(L"完美平台响应解密失败：" + decrypted.message));
        }

        // 解密后的错误响应（code != 0）会被原样返回，需在此检查。
        Json::JsonObjectRef envelope{ nullptr };
        if (Json::JsonObjectRef::TryParse(decrypted.json, envelope) && envelope)
        {
            if (auto code = Json::OptionalInt32(envelope, L"code"); code && *code != 0)
            {
                const std::wstring message = Json::OptionalString(envelope, L"msg").value_or(L"未知错误");
                throw hresult_error(E_FAIL,
                    winrt::hstring(L"完美平台接口错误 " + std::to_wstring(*code) + L"：" + message));
            }
        }

        PerfectPlayer player = Parse::PlayerFromOverviewJson(decrypted.json);
        player.rawOverviewJson = decrypted.json;

        // ---- 2. 确定赛季号 ----
        std::wstring season = player.currentRank.season;
        if (season.empty())
        {
            // 回退：查赛季列表，取编号最大的 Sxx。
            const std::wstring listUrl =
                std::wstring(kSeasonListUrl) + L"?access_token=" + UrlEncode(credentials.accessToken) +
                L"&uid=" + UrlEncode(steamId) + L"&ignore_season=";
            try
            {
                auto listRequest = MakeRequest(Uri(listUrl), credentials);
                auto listResponse = co_await client.SendRequestAsync(listRequest);
                const std::wstring listBody =
                    (co_await listResponse.Content().ReadAsStringAsync()).c_str();
                Json::JsonObjectRef listRoot{ nullptr };
                // 该接口不加密，直接解析。
                if (Json::JsonObjectRef::TryParse(listBody, listRoot) && listRoot)
                {
                    season = Parse::FindLatestSeason(listRoot);
                }
            }
            catch (hresult_error const&)
            {
                // 赛季发现失败不致命：season-stats 会尝试用空赛季。
            }
        }

        // ---- 3. season-stats ----
        // 失败时保留 overview 结果。
        if (!season.empty())
        {
            try
            {
                const std::wstring form =
                    L"access_token=" + UrlEncode(credentials.accessToken) +
                    L"&current_season=" + UrlEncode(season) +
                    L"&need_max_score=1" +
                    L"&season=" + UrlEncode(season) +
                    L"&stats_list=ladder,map,weapon,individual_peak" +
                    L"&uid=" + UrlEncode(steamId);

                HttpRequestMessage statsRequest;
                statsRequest.RequestUri(Uri(std::wstring(kSeasonStatsUrl)));
                statsRequest.Method(HttpMethod::Post());
                auto statsHeaders = statsRequest.Headers();
                statsHeaders.Append(L"Referer", std::wstring(kReferer));
                statsHeaders.Append(L"PwaSteamId", credentials.selfSteamId);
                HttpStringContent content(
                    winrt::hstring(form),
                    winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                    winrt::hstring(L"application/x-www-form-urlencoded"));
                statsRequest.Content(content);

                auto statsResponse = co_await client.SendRequestAsync(statsRequest);
                const std::wstring statsBody =
                    (co_await statsResponse.Content().ReadAsStringAsync()).c_str();

                const auto statsDecrypted = DecryptResponse(statsBody);
                if (!statsDecrypted.ok)
                {
                    player.partialFailure = true;
                    player.failureReason =
                        L"season-stats 解密失败：" + statsDecrypted.message;
                }
                else
                {
                    Json::JsonObjectRef statsRoot{ nullptr };
                    if (Json::JsonObjectRef::TryParse(statsDecrypted.json, statsRoot) && statsRoot)
                    {
                        if (auto code = Json::OptionalInt32(statsRoot, L"code"); code && *code != 0)
                        {
                            player.partialFailure = true;
                            player.failureReason = L"season-stats 业务错误 " + std::to_wstring(*code);
                        }
                        else
                        {
                            Parse::ApplySeasonStats(statsRoot, player);
                            player.rawSeasonStatsJson = statsDecrypted.json;
                        }
                    }
                }
            }
            catch (hresult_error const& ex)
            {
                player.partialFailure = true;
                player.failureReason = L"season-stats 请求失败：" + std::wstring(ex.message());
            }
        }
        else
        {
            player.partialFailure = true;
            player.failureReason = L"未能确定赛季号，跳过赛季统计";
        }

        out = std::move(player);
    }

    IAsyncAction PerfectClient::GetOverviewAsync(
        std::wstring steamId,
        PerfectCredentials const& credentials,
        PerfectPlayer& out)
    {
        if (steamId.empty())
        {
            throw hresult_invalid_argument(L"steamId 不能为空");
        }
        if (credentials.accessToken.empty() || credentials.selfSteamId.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先登录完美平台");
        }

        HttpClient client;
        const std::wstring overviewUrl =
            std::wstring(kOverviewUrl) + L"?access_token=" + UrlEncode(credentials.accessToken) +
            L"&recent_ladder_match=1&uid=" + UrlEncode(steamId);

        auto request = MakeRequest(Uri(overviewUrl), credentials);
        auto response = co_await client.SendRequestAsync(request);
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();

        const auto decrypted = DecryptResponse(body);
        if (!decrypted.ok)
        {
            throw hresult_error(E_FAIL,
                winrt::hstring(L"完美平台响应解密失败：" + decrypted.message));
        }

        Json::JsonObjectRef envelope{ nullptr };
        if (Json::JsonObjectRef::TryParse(decrypted.json, envelope) && envelope)
        {
            if (auto code = Json::OptionalInt32(envelope, L"code"); code && *code != 0)
            {
                const std::wstring message = Json::OptionalString(envelope, L"msg").value_or(L"未知错误");
                throw hresult_error(E_FAIL,
                    winrt::hstring(L"完美平台接口错误 " + std::to_wstring(*code) + L"：" + message));
            }
        }

        out = Parse::PlayerFromOverviewJson(decrypted.json);
        out.rawOverviewJson = decrypted.json;
    }

    IAsyncAction PerfectClient::GetSeasonListAsync(
        std::wstring steamId,
        PerfectCredentials const& credentials,
        std::vector<std::wstring>& out)
    {
        if (credentials.accessToken.empty() || credentials.selfSteamId.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先登录完美平台");
        }

        HttpClient client;
        const std::wstring url =
            std::wstring(kSeasonListUrl) + L"?access_token=" + UrlEncode(credentials.accessToken) +
            L"&uid=" + UrlEncode(steamId) + L"&ignore_season=";

        auto request = MakeRequest(Uri(url), credentials);
        auto response = co_await client.SendRequestAsync(request);
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();

        // 该接口不加密，直接解析。
        std::vector<std::wstring> seasons;
        Json::JsonObjectRef root{ nullptr };
        if (Json::JsonObjectRef::TryParse(body, root) && root)
        {
            const auto latest = Parse::FindLatestSeason(root);
            if (!latest.empty())
            {
                seasons.push_back(latest);
            }
        }
        out = std::move(seasons);
    }
}
