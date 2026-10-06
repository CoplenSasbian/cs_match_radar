#include "FiveEApiPch.h"
#include "FiveEClient.h"
#include "FiveEJson.h"
#include "FiveEParser.h"
#include "FiveEPlayerParser.h"
#include "FiveEGateSign.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/base.h>

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Headers;

namespace FiveE
{
    namespace
    {
        // 接口根地址。社区整理，非官方文档。
        constexpr std::wstring_view kGateBase = L"https://gate.5eplay.com";
        constexpr std::wstring_view kDataApiBase = L"https://gate.5eplay.com/crane/http/api/data";
        constexpr std::wstring_view kApiBase = L"https://gate.5eplay.com/crane/http/api";
        // 玩家资料接口（Gate HMAC 签名鉴权）。实测无需登录态。
        constexpr std::wstring_view kPlayerHomePath =
            L"/cranenew/http/api/data/v3/player/home";

        constexpr std::wstring_view kIdTransferUrl =
            L"https://gate.5eplay.com/userinterface/http/v1/userinterface/idTransfer";

        constexpr std::wstring_view kUserAgent =
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

        // 5E 的时间戳是秒级 Unix 时间。
        int64_t NowUnixSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        int64_t DaysAgoUnixSeconds(int32_t days)
        {
            return NowUnixSeconds() - static_cast<int64_t>(days) * 86400;
        }

        // URL 查询参数值转义。这里只处理会出现在参数里的字符。
        std::wstring UrlEncode(std::wstring_view text)
        {
            static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
            std::wstring out;
            out.reserve(text.size() * 3);
            for (const wchar_t ch : text)
            {
                const bool unreserved =
                    (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
                    (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' || ch == L'.' || ch == L'~';
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

        // 限流：保证两次请求之间至少间隔 minRequestIntervalMs。
        IAsyncAction ThrottleAsync(std::chrono::steady_clock::time_point& lastRequest, int32_t intervalMs)
        {
            if (intervalMs <= 0)
            {
                co_return;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest).count();
            if (lastRequest.time_since_epoch().count() != 0 && elapsed < intervalMs)
            {
                co_await winrt::resume_after(std::chrono::milliseconds(intervalMs - elapsed));
            }
            lastRequest = std::chrono::steady_clock::now();
        }

        // 说明：WinRT 的 HttpClient 没有 .NET 那样的 Timeout 属性，本 SDK 的
        // C++/WinRT 也不提供 CancellationTokenSource，因此超时依赖系统默认的
        // TCP/HTTP 超时。FiveEClientOptions::timeoutSeconds 目前仅作记录，
        // 如需严格超时，可在调用方用 resume_after 与请求竞争。

        // 5E 的业务返回码：data.code == 0 表示成功。
        // 部分接口不返回 code 字段，此时按成功处理。
        std::optional<int32_t> ExtractBizCode(Json::JsonObjectRef const& root)
        {
            if (!root)
            {
                return std::nullopt;
            }
            if (auto code = Json::OptionalInt32(root, L"code"))
            {
                return code;
            }
            return std::nullopt;
        }

        std::wstring ExtractBizMessage(Json::JsonObjectRef const& root)
        {
            if (auto message = Json::OptionalString(root, L"message"))
            {
                return *message;
            }
            if (auto message = Json::OptionalString(root, L"msg"))
            {
                return *message;
            }
            return {};
        }

        // 把整个响应体解析为 JSON 对象；失败抛 ApiError。
        Json::JsonObjectRef ParseRootObject(std::wstring const& body)
        {
            if (body.empty())
            {
                throw ApiError(ApiError::Kind::Parse, L"响应体为空");
            }
            Json::JsonObjectRef root{ nullptr };
            if (!Json::JsonObjectRef::TryParse(body, root) || !root)
            {
                throw ApiError(ApiError::Kind::Parse, L"响应不是合法的 JSON 对象");
            }
            return root;
        }
    }


    FiveEClient::FiveEClient(FiveEClientOptions options)
        : m_options(std::move(options)), m_httpClient(), m_lastRequest()
    {
        // 注意：WinRT 的 HttpClient 没有 .NET 那样的 Timeout 属性，
        // 超时依赖系统默认的 TCP/HTTP 超时（见文件顶部说明）。
        auto headers = m_httpClient.DefaultRequestHeaders();
        headers.UserAgent().TryParseAdd(kUserAgent);
        headers.Referer(winrt::Windows::Foundation::Uri(L"https://view-arena.5eplay.com/"));
        if (!m_options.bearerToken.empty())
        {
            headers.Authorization(HttpCredentialsHeaderValue(
                L"Bearer", m_options.bearerToken));
        }
    }

    IAsyncAction FiveEClient::SearchPlayerAsync(
        std::wstring keyword, std::vector<SearchHit>& out)
    {
        out.clear();

        {
            const size_t begin = keyword.find_first_not_of(L" \t\r\n");
            const size_t end = keyword.find_last_not_of(L" \t\r\n");
            keyword = (begin == std::wstring::npos)
                ? std::wstring{}
                : keyword.substr(begin, end - begin + 1);
        }
        if (keyword.empty())
        {
            co_return;
        }

        // URL 编码（搜索关键字可能含中文）。
        auto encode = [](std::wstring_view text) {
            static constexpr wchar_t hex[] = L"0123456789ABCDEF";
            std::wstring encoded;

            // 必须先把 wchar_t（UTF-16）转成 UTF-8 字节再逐字节转义。
            // 直接转义 UTF-16 码元会让中文变成 %11%A0 之类的乱码。
            const int count = ::WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                nullptr, 0, nullptr, nullptr);
            if (count <= 0)
            {
                return encoded;
            }
            std::string bytes(static_cast<size_t>(count), '\0');
            ::WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                bytes.data(), count, nullptr, nullptr);

            for (char raw : bytes)
            {
                const auto byte = static_cast<unsigned int>(
                    static_cast<unsigned char>(raw));
                const bool unreserved =
                    (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                    (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                    byte == '.' || byte == '~';
                if (unreserved)
                {
                    encoded.push_back(static_cast<wchar_t>(byte));
                }
                else
                {
                    encoded.push_back(L'%');
                    encoded.push_back(hex[(byte >> 4) & 0xF]);
                    encoded.push_back(hex[byte & 0xF]);
                }
            }
            return encoded;
        };

        const std::wstring encoded = encode(keyword);
        const std::wstring searchUrl =
            std::wstring(L"https://arena.5eplay.com/api/search/player/1/16?keywords=") + encoded;
        HttpClient client;

        HttpRequestMessage request;
        request.RequestUri(Uri(searchUrl));
        request.Method(HttpMethod::Get());
        auto headers = request.Headers();
        // 只设置非受限头：User-Agent / Referer 已在客户端默认头里设置过，
        // 对它们再 Append 会抛 E_INVALIDARG（"参数错误"）。
        headers.Append(L"X-Requested-With", L"XMLHttpRequest");

        const auto response = co_await client.SendRequestAsync(request);
        const std::wstring body =
            std::wstring(co_await response.Content().ReadAsStringAsync());

        if (body.empty())
        {
            throw hresult_error(E_FAIL,
                winrt::hstring(L"搜索请求返回空响应（HTTP " +
                    std::to_wstring(static_cast<int32_t>(response.StatusCode())) +
                    L"，URL " + searchUrl + L"）"));
        }
        winrt::Windows::Data::Json::JsonObject root{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(body, root) || !root)
        {
            // 带上响应片段，便于排查（可能被网关拦成 HTML 之类）。
            const std::wstring preview = body.substr(0, 200);
            throw hresult_error(E_FAIL,
                winrt::hstring(L"搜索结果不是合法 JSON：" + preview));
        }

        using namespace winrt::Windows::Data::Json;

        // data -> user -> list
        auto dataValue = Json::Get(root, L"data");
        if (!dataValue || dataValue.ValueType() != JsonValueType::Object) { co_return; }
        auto data = dataValue.GetObject();

        auto userValue = Json::Get(data, L"user");
        if (!userValue || userValue.ValueType() != JsonValueType::Object) { co_return; }
        auto user = userValue.GetObject();

        auto listValue = Json::Get(user, L"list");
        if (!listValue || listValue.ValueType() != JsonValueType::Array)
        {
            co_return;
        }

        std::vector<SearchHit> hits;
        for (auto const& element : listValue.GetArray())
        {
            if (!element || element.ValueType() != JsonValueType::Object)
            {
                continue;
            }
            auto item = element.GetObject();
            SearchHit hit;
            hit.username = Json::OptionalString(item, L"username").value_or(L"");
            hit.domain = Json::OptionalString(item, L"domain").value_or(L"");
            hit.uuid = Json::OptionalString(item, L"uuid").value_or(L"");
            if (hit.domain.empty() && hit.username.empty())
            {
                continue;
            }
            hits.push_back(std::move(hit));
        }

        // 完全匹配的排最前。
        auto lower = [](std::wstring_view s) {
            std::wstring r(s);
            for (auto& ch : r)
            {
                if (ch >= L'A' && ch <= L'Z') { ch = ch - L'A' + L'a'; }
            }
            return r;
        };
        const std::wstring key = lower(keyword);
        std::stable_sort(hits.begin(), hits.end(),
            [&key, &lower](SearchHit const& a, SearchHit const& b) {
                const bool aExact = lower(a.username) == key || lower(a.domain) == key;
                const bool bExact = lower(b.username) == key || lower(b.domain) == key;
                return aExact && !bExact;
            });

        // 搜索结果通常不带 uuid，用 domain 换一次。
        for (auto& hit : hits)
        {
            if (hit.uuid.empty() && !hit.domain.empty())
            {
                try
                {
                    hit.uuid = std::wstring(co_await GetUuidAsync(hit.domain));
                }
                catch (...)
                {
                    // 换取失败不致命：domain 仍可用于查询对局。
                }
            }
        }

        out = std::move(hits);
        co_return;
    }
    IAsyncOperation<winrt::hstring> FiveEClient::GetUuidAsync(std::wstring userId)
    {
        if (userId.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"userId 不能为空");
        }

        co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

        // idTransfer 用 POST + JSON 体。
        Json::JsonObjectRef trans;
        trans.SetNamedValue(L"domain", winrt::Windows::Data::Json::JsonValue::CreateStringValue(userId));
        Json::JsonObjectRef payload;
        payload.SetNamedValue(L"trans", trans);

        HttpStringContent content(payload.Stringify(), winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8, L"application/json");

        HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_httpClient.PostAsync(winrt::Windows::Foundation::Uri(std::wstring(kIdTransferUrl)), content);
        }
        catch (hresult_error const& ex)
        {
            throw ApiError(
                ApiError::Kind::Network,
                L"请求 idTransfer 失败：" + std::wstring(ex.message()));
        }

        const int32_t status = static_cast<int32_t>(response.StatusCode());
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();
        if (status < 200 || status >= 300)
        {
            throw ApiError(ApiError::Kind::Http, L"idTransfer 返回 HTTP " + std::to_wstring(status), status);
        }

        const Json::JsonObjectRef root = ParseRootObject(body);
        if (auto code = ExtractBizCode(root); code && *code != 0)
        {
            throw ApiError(
                ApiError::Kind::Business,
                L"idTransfer 业务失败：" + ExtractBizMessage(root),
                status,
                *code);
        }

        Json::JsonValueRef data = Json::Get(root, L"data");
        if (Json::IsNull(data) || data.ValueType() != Json::JsonValueKind::Object)
        {
            throw ApiError(ApiError::Kind::Parse, L"idTransfer 响应缺少 data 对象", status);
        }
        co_return winrt::hstring(Parse::UuidFromJson(data.GetObject()));
    }

    IAsyncAction FiveEClient::GetMatchListAsync(MatchListQuery const& query, std::vector<MatchSummary>& out)
    {
        if (query.uuid.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"uuid 不能为空，请先调用 GetUuidAsync");
        }

        co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

        const int64_t startTime = query.startTime.value_or(DaysAgoUnixSeconds(180));
        const int64_t endTime = query.endTime.value_or(NowUnixSeconds());

        const std::wstring url =
            std::wstring(kDataApiBase) + L"/match/list" +
            L"?match_type=" + std::to_wstring(query.matchType) +
            L"&page=" + std::to_wstring(query.page) +
            L"&date=" + std::to_wstring(query.date) +
            L"&start_time=" + std::to_wstring(startTime) +
            L"&end_time=" + std::to_wstring(endTime) +
            L"&uuid=" + UrlEncode(query.uuid) +
            L"&limit=" + std::to_wstring(query.limit) +
            L"&cs_type=" + std::to_wstring(query.csType);

        HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(url));
        }
        catch (hresult_error const& ex)
        {
            throw ApiError(ApiError::Kind::Network, L"请求 match/list 失败：" + std::wstring(ex.message()));
        }

        const int32_t status = static_cast<int32_t>(response.StatusCode());
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();
        if (status < 200 || status >= 300)
        {
            throw ApiError(ApiError::Kind::Http, L"match/list 返回 HTTP " + std::to_wstring(status), status);
        }

        const Json::JsonObjectRef root = ParseRootObject(body);
        if (auto code = ExtractBizCode(root); code && *code != 0)
        {
            throw ApiError(
                ApiError::Kind::Business,
                L"match/list 业务失败：" + ExtractBizMessage(root),
                status,
                *code);
        }

        out = Parse::MatchListFromJson(Json::Get(root, L"data"));
    }

    IAsyncAction FiveEClient::GetAllMatchesAsync(
        MatchListQuery const& query, std::vector<MatchSummary>& out, int32_t maxPages)
    {
        std::vector<MatchSummary> all;
        MatchListQuery page = query;
        page.page = query.page > 0 ? query.page : 1;

        for (int32_t index = 0; maxPages <= 0 || index < maxPages; ++index)
        {
            std::vector<MatchSummary> batch;
            co_await GetMatchListAsync(page, batch);
            if (batch.empty())
            {
                // 空页说明已经取完。
                break;
            }
            const size_t before = all.size();
            for (auto& match : batch)
            {
                all.push_back(std::move(match));
            }
            // 不足一页也说明取完了。
            if (all.size() - before < static_cast<size_t>(page.limit))
            {
                break;
            }
            ++page.page;
        }

        out = std::move(all);
    }

    IAsyncAction FiveEClient::GetMatchDetailAsync(std::wstring matchId, MatchDetail& out)
    {
        if (matchId.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"matchId 不能为空");
        }

        co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

        const std::wstring url = std::wstring(kDataApiBase) + L"/match/" + UrlEncode(matchId);
        HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(url));
        }
        catch (hresult_error const& ex)
        {
            throw ApiError(ApiError::Kind::Network, L"请求 match 详情失败：" + std::wstring(ex.message()));
        }

        const int32_t status = static_cast<int32_t>(response.StatusCode());
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();
        if (status < 200 || status >= 300)
        {
            throw ApiError(ApiError::Kind::Http, L"match 详情返回 HTTP " + std::to_wstring(status), status);
        }

        const Json::JsonObjectRef root = ParseRootObject(body);
        if (auto code = ExtractBizCode(root); code && *code != 0)
        {
            throw ApiError(
                ApiError::Kind::Business,
                L"match 详情业务失败：" + ExtractBizMessage(root),
                status,
                *code);
        }

        Json::JsonValueRef data = Json::Get(root, L"data");
        if (Json::IsNull(data) || data.ValueType() != Json::JsonValueKind::Object)
        {
            throw ApiError(ApiError::Kind::Parse, L"match 详情响应缺少 data 对象", status);
        }
        out = Parse::MatchDetailFromJson(data.GetObject());
    }

    IAsyncAction FiveEClient::GetMatchDetailWithExtrasAsync(std::wstring matchId, MatchDetail& out)
    {
        MatchDetail detail;
        co_await GetMatchDetailAsync(matchId, detail);
        if (matchId.empty())
        {
            matchId = detail.matchId;
        }
        if (matchId.empty())
        {
            out = std::move(detail);
            co_return;
        }

        // 三个补充接口都是 best-effort：失败只记录，不影响主结果。
        const std::pair<std::wstring_view, std::wstring> endpoints[] = {
            { L"advanced", std::wstring(kDataApiBase) + L"/match/advanced/" + UrlEncode(matchId) },
            { L"leetify_rating", std::wstring(kApiBase) + L"/match/leetify_rating/" + UrlEncode(matchId) },
            { L"vip_plus", std::wstring(kDataApiBase) + L"/vip_plus_match_data/" + UrlEncode(matchId) },
        };

        for (auto const& [name, url] : endpoints)
        {
            try
            {
                co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

                HttpResponseMessage response{ nullptr };
                try
                {
                    response = co_await m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(url));
                }
                catch (hresult_error const&)
                {
                    continue;
                }

                const int32_t status = static_cast<int32_t>(response.StatusCode());
                if (status < 200 || status >= 300)
                {
                    continue;
                }

                const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();
                if (body.empty())
                {
                    continue;
                }

                // 这些接口结构不固定，先原样保留 JSON 文本。
                Json::JsonObjectRef root{ nullptr };
                if (!Json::JsonObjectRef::TryParse(body, root) || !root)
                {
                    continue;
                }
                Json::JsonValueRef data = Json::Get(root, L"data");
                const Json::JsonValueRef payload = Json::IsNull(data) ? root : data;
                detail.extraRaw[std::wstring(name)] = payload.Stringify();
            }
            catch (hresult_error const&)
            {
                // best-effort：忽略单个补充接口的失败。
                continue;
            }
        }

        out = std::move(detail);
    }

    IAsyncAction FiveEClient::GetRecentMatchDetailsAsync(
        std::wstring userId, std::vector<MatchDetail>& out, int32_t limit, int32_t maxPages)
    {
        const std::wstring uuid = std::wstring(co_await GetUuidAsync(userId));
        if (uuid.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"无法获取 uuid，请检查 5E 用户 ID");
        }

        MatchListQuery query;
        query.uuid = uuid;
        query.limit = 30;

        std::vector<MatchSummary> matches;
        co_await GetAllMatchesAsync(query, matches, maxPages);
        if (limit > 0 && matches.size() > static_cast<size_t>(limit))
        {
            matches.resize(static_cast<size_t>(limit));
        }

        std::vector<MatchDetail> details;
        details.reserve(matches.size());
        for (auto const& match : matches)
        {
            if (match.matchId.empty())
            {
                continue;
            }
            try
            {
                MatchDetail detail;
                co_await GetMatchDetailAsync(match.matchId, detail);
                details.push_back(std::move(detail));
            }
            catch (ApiError const&)
            {
                // 单场详情失败不中断整体流程。
                continue;
            }
        }

        out = std::move(details);
    }

    IAsyncOperation<winrt::hstring> FiveEClient::GetDemoUrlAsync(std::wstring matchId)
    {
        MatchDetail detail;
        co_await GetMatchDetailAsync(std::move(matchId), detail);
        // 没有 Demo 时返回空串，调用方用 empty() 判断。
        co_return winrt::hstring(detail.demoUrl);
    }

    IAsyncOperation<winrt::hstring> FiveEClient::FetchRawAsync(std::wstring url)
    {
        if (url.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"url 不能为空");
        }

        co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

        HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(url));
        }
        catch (hresult_error const& ex)
        {
            throw ApiError(ApiError::Kind::Network, L"请求失败：" + std::wstring(ex.message()));
        }

        const int32_t status = static_cast<int32_t>(response.StatusCode());
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();
        if (status < 200 || status >= 300)
        {
            throw ApiError(ApiError::Kind::Http, L"接口返回 HTTP " + std::to_wstring(status), status);
        }
        co_return winrt::hstring(body);
    }

    IAsyncOperation<winrt::hstring> FiveEClient::FetchMatchListRawAsync(MatchListQuery const& query)
    {
        if (query.uuid.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"uuid 不能为空，请先调用 GetUuidAsync");
        }

        const int64_t startTime = query.startTime.value_or(DaysAgoUnixSeconds(180));
        const int64_t endTime = query.endTime.value_or(NowUnixSeconds());

        const std::wstring url =
            std::wstring(kDataApiBase) + L"/match/list" +
            L"?match_type=" + std::to_wstring(query.matchType) +
            L"&page=" + std::to_wstring(query.page) +
            L"&date=" + std::to_wstring(query.date) +
            L"&start_time=" + std::to_wstring(startTime) +
            L"&end_time=" + std::to_wstring(endTime) +
            L"&uuid=" + UrlEncode(query.uuid) +
            L"&limit=" + std::to_wstring(query.limit) +
            L"&cs_type=" + std::to_wstring(query.csType);

        co_return co_await FetchRawAsync(url);
    }

    IAsyncOperation<winrt::hstring> FiveEClient::FetchMatchDetailRawAsync(std::wstring matchId)
    {
        if (matchId.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"matchId 不能为空");
        }
        co_return co_await FetchRawAsync(std::wstring(kDataApiBase) + L"/match/" + UrlEncode(matchId));
    }

    IAsyncAction FiveEClient::GetPlayerProfileAsync(std::wstring uuid, PlayerHome& out)
    {
        if (uuid.empty())
        {
            throw ApiError(ApiError::Kind::Business, L"uuid 不能为空");
        }

        co_await ThrottleAsync(m_lastRequest, m_options.minRequestIntervalMs);

        const std::wstring path = std::wstring(kPlayerHomePath);
        const std::vector<std::pair<std::wstring, std::wstring>> query = {
            { L"uuid", uuid },
        };

        // 用 5E 网关的 HMAC 签名鉴权。
        const auto signedHeaders = Gate::BuildSignedHeaders(L"GET", path, query);

        const std::wstring url =
            std::wstring(kGateBase) + Gate::BuildPathAndParameters(path, query);

        // 关键：必须用 HttpRequestMessage 逐请求设置头。
        // 用 HttpClient::DefaultRequestHeaders 会导致服务器返回
        // 400 Invalid Signature（实测，原因未明）。
        //
        // 另一个坑：Authorization 只参与签名（按空值），但不能真的发送——
        // 不发送时服务器返回 200；WinRT 也会拒绝写入空值。
        HttpRequestMessage request(HttpMethod::Get(), winrt::Windows::Foundation::Uri(url));
        auto headers = request.Headers();
        headers.Append(L"Accept", signedHeaders.accept);
        headers.Append(L"Accept-Language", signedHeaders.acceptLanguage);
        headers.Append(L"Referer", signedHeaders.referer);
        headers.Append(L"Origin", signedHeaders.origin);
        headers.Append(L"x-ca-key", signedHeaders.caKey);
        headers.Append(L"x-ca-signature-method", signedHeaders.signatureMethod);
        headers.Append(L"x-ca-signature-headers", signedHeaders.signatureHeaders);
        headers.Append(L"x-ca-signature", signedHeaders.signature);
        headers.UserAgent().TryParseAdd(kUserAgent);

        HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_httpClient.SendRequestAsync(request);
        }
        catch (hresult_error const& ex)
        {
            throw ApiError(
                ApiError::Kind::Network,
                L"请求 player/home 失败：" + std::wstring(ex.message()));
        }

        const int32_t status = static_cast<int32_t>(response.StatusCode());
        const std::wstring body = (co_await response.Content().ReadAsStringAsync()).c_str();

        if (status == 400 && body.find(L"Invalid Signature") != std::wstring::npos)
        {
            throw ApiError(
                ApiError::Kind::Business,
                L"签名被拒绝：5E 可能已更换网关密钥，需重新提取 HMAC secret",
                status);
        }
        if (status < 200 || status >= 300)
        {
            throw ApiError(
                ApiError::Kind::Http,
                L"player/home 返回 HTTP " + std::to_wstring(status),
                status);
        }

        PlayerHome profile = Parse::PlayerHomeFromJson(body);
        if (!profile.HasData())
        {
            // 业务码非 0 时 data 为 null；把 message 一并带出便于排查。
            std::wstring reason = L"player/home 未返回玩家数据";
            Json::JsonObjectRef root{ nullptr };
            if (Json::JsonObjectRef::TryParse(body, root) && root)
            {
                if (auto message = Json::OptionalString(root, L"message"))
                {
                    if (!message->empty() && *message != L"success")
                    {
                        reason += L"：" + *message;
                    }
                }
            }
            throw ApiError(ApiError::Kind::Business, reason, status);
        }

        out = std::move(profile);
    }

    IAsyncAction FiveEClient::GetPlayerProfilesAsync(
        std::vector<std::wstring> const& uuids, std::vector<PlayerHome>& out)
    {
        std::vector<PlayerHome> profiles;
        profiles.reserve(uuids.size());

        for (auto const& uuid : uuids)
        {
            if (uuid.empty())
            {
                continue;
            }
            PlayerHome profile;
            try
            {
                // 单个玩家失败不影响其余玩家。
                co_await GetPlayerProfileAsync(uuid, profile);
            }
            catch (ApiError const&)
            {
                // 保留一个仅含 uuid 的占位，便于调用方对齐索引。
                profile.identity.uuid = uuid;
            }
            profiles.push_back(std::move(profile));
        }

        out = std::move(profiles);
    }

    std::optional<TeamGroup> FiveEClient::FindPlayerGroup(MatchDetail const& detail, std::wstring_view playerUid)
    {
        if (playerUid.empty())
        {
            return std::nullopt;
        }
        for (auto const& player : detail.players)
        {
            if (player.profile.playerId == playerUid)
            {
                return player.group;
            }
        }
        // 玩家明细里没有时，退回队伍 uid 列表。
        for (auto const& team : detail.teams)
        {
            for (auto const& uid : team.playerUids)
            {
                if (uid == playerUid)
                {
                    return team.group;
                }
            }
        }
        return std::nullopt;
    }

    bool FiveEClient::IsWinForPlayer(MatchSummary const& summary)
    {
        return summary.isWin.value_or(false);
    }
}
