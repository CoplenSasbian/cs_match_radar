#include "PerfectApiPch.h"
#include "PerfectAuth.h"
#include "PerfectJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/base.h>

#include <string>

using namespace winrt;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Headers;

namespace Perfect
{
    namespace
    {
        constexpr std::wstring_view kQrApplyUrl =
            L"https://passport.pwesports.cn/qrAuth/applyToken";
        constexpr std::wstring_view kQrCheckUrl =
            L"https://passport.pwesports.cn/qrAuth/check";

        // 扫码登录所属应用标识（来自参考实现实测值）。
        constexpr int32_t kAppId = 7;

        // applyToken 需要一个 steamId 占位；此处用中性 ID，
        // 真实账号在扫码确认后由 accountItem.steamId 返回。
        constexpr std::wstring_view kNeutralSteamId = L"76561197960265728";

        // code 1008：二维码已过期。
        constexpr int32_t kCodeQrExpired = 1008;

        // 校验 token：40 位字母数字。
        bool IsValidToken(std::wstring_view value)
        {
            if (value.size() != 40)
            {
                return false;
            }
            for (const wchar_t ch : value)
            {
                const bool alnum =
                    (ch >= L'0' && ch <= L'9') ||
                    (ch >= L'a' && ch <= L'z') ||
                    (ch >= L'A' && ch <= L'Z');
                if (!alnum)
                {
                    return false;
                }
            }
            return true;
        }

        // 校验 SteamID64：17 位数字。
        bool IsValidSteamId(std::wstring_view value)
        {
            if (value.size() != 17)
            {
                return false;
            }
            for (const wchar_t ch : value)
            {
                if (ch < L'0' || ch > L'9')
                {
                    return false;
                }
            }
            return true;
        }

        // 从 accessUrl 里取出 accessToken 查询参数。
        // 该 URL 同时用于生成二维码，因此这里只做解析。
        std::wstring ExtractAccessToken(std::wstring_view accessUrl)
        {
            const size_t queryPos = accessUrl.find(L'?');
            if (queryPos == std::wstring_view::npos)
            {
                return {};
            }
            size_t pos = queryPos + 1;
            while (pos < accessUrl.size())
            {
                size_t next = accessUrl.find(L'&', pos);
                if (next == std::wstring_view::npos)
                {
                    next = accessUrl.size();
                }
                const std::wstring_view pair = accessUrl.substr(pos, next - pos);
                const size_t eq = pair.find(L'=');
                if (eq != std::wstring_view::npos)
                {
                    if (pair.substr(0, eq) == L"accessToken")
                    {
                        return std::wstring(pair.substr(eq + 1));
                    }
                }
                pos = next + 1;
            }
            return {};
        }

        // POST 一段 JSON 并返回响应文本。
        // 注意：IAsyncOperation<T> 要求 T 是 WinRT 类型，因此这里用 hstring。
        IAsyncOperation<hstring> PostJsonAsync(
            std::wstring_view url, JsonObject const& payload)
        {
            HttpClient client;
            HttpRequestMessage request;
            request.RequestUri(Uri(std::wstring(url)));
            request.Method(HttpMethod::Post());
            request.Content(HttpStringContent(payload.Stringify()));
            // 逐个请求设置内容类型，避免依赖 DefaultRequestHeaders。
            request.Content().Headers().ContentType(
                HttpMediaTypeHeaderValue(L"application/json"));

            auto response = co_await client.SendRequestAsync(request);
            co_return co_await response.Content().ReadAsStringAsync();
        }

        // 取嵌套对象；不存在或类型不符时返回 null。
        JsonObject OptionalObject(JsonObject const& object, std::wstring_view key)
        {
            if (!object)
            {
                return nullptr;
            }
            auto value = Json::Get(object, key);
            if (!value || Json::IsNull(value))
            {
                return nullptr;
            }
            if (value.ValueType() != JsonValueType::Object)
            {
                return nullptr;
            }
            return value.GetObject();
        }

        // 解析 { code, description, result } 信封。
        JsonObject ParseEnvelope(std::wstring const& body, int32_t& code, std::wstring& description)
        {
            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(body, root) || !root)
            {
                throw hresult_error(E_FAIL, L"扫码接口返回的不是合法 JSON");
            }
            code = Json::OptionalInt32(root, L"code").value_or(-1);
            description = Json::OptionalString(root, L"description").value_or(L"");
            return OptionalObject(root, L"result");
        }
    }

    PerfectAuth::PerfectAuth() = default;

    IAsyncAction PerfectAuth::ApplyQrAsync(QrLoginResult& out)
    {
        out = QrLoginResult{};

        JsonObject payload;
        payload.SetNamedValue(L"appId", JsonValue::CreateNumberValue(kAppId));
        payload.SetNamedValue(L"qrType", JsonValue::CreateNumberValue(1));
        payload.SetNamedValue(L"steamId",
            JsonValue::CreateStringValue(std::wstring(kNeutralSteamId)));
        payload.SetNamedValue(L"website", JsonValue::CreateStringValue(L"pvp"));
        payload.SetNamedValue(L"redirect", JsonValue::CreateBooleanValue(false));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kQrApplyUrl, payload));

        int32_t code = -1;
        std::wstring description;
        JsonObject result = ParseEnvelope(body, code, description);
        if (code != 0)
        {
            throw hresult_error(E_FAIL,
                winrt::hstring(L"申请二维码失败（" + std::to_wstring(code) + L"）：" + description));
        }
        if (!result)
        {
            throw hresult_error(E_FAIL, L"申请二维码失败：响应缺少 result");
        }

        const std::wstring accessUrl =
            Json::OptionalString(result, L"accessUrl").value_or(L"");
        if (accessUrl.empty())
        {
            throw hresult_error(E_FAIL, L"申请二维码失败：响应缺少 accessUrl");
        }
        const std::wstring pollToken = ExtractAccessToken(accessUrl);
        if (pollToken.empty())
        {
            throw hresult_error(E_FAIL, L"申请二维码失败：accessUrl 中没有 accessToken");
        }

        out.phase = QrLoginPhase::WaitingScan;
        out.accessUrl = accessUrl;
        out.pollToken = pollToken;
        co_return;
    }

    IAsyncAction PerfectAuth::PollQrAsync(std::wstring pollToken, QrLoginResult& out)
    {
        out = QrLoginResult{};
        out.pollToken = std::move(pollToken);

        if (out.pollToken.empty())
        {
            throw hresult_invalid_argument(L"pollToken 不能为空");
        }

        JsonObject payload;
        payload.SetNamedValue(L"accessToken", JsonValue::CreateStringValue(out.pollToken));
        payload.SetNamedValue(L"appId", JsonValue::CreateNumberValue(kAppId));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kQrCheckUrl, payload));

        int32_t code = -1;
        std::wstring description;
        JsonObject result = ParseEnvelope(body, code, description);

        if (code != 0)
        {
            if (code == kCodeQrExpired)
            {
                out.phase = QrLoginPhase::Expired;
                out.message = L"二维码已过期，请重新获取";
                co_return;
            }
            throw hresult_error(E_FAIL,
                winrt::hstring(L"轮询扫码状态失败（" + std::to_wstring(code) + L"）：" + description));
        }
        if (!result)
        {
            throw hresult_error(E_FAIL, L"轮询扫码状态失败：响应缺少 result");
        }

        const int32_t status = Json::OptionalInt32(result, L"status").value_or(0);
        switch (status)
        {
        case 0:
            out.phase = QrLoginPhase::WaitingScan;
            break;
        case 1:
            out.phase = QrLoginPhase::Scanned;
            break;
        case 2:
        {
            const std::wstring token =
                Json::OptionalString(result, L"token").value_or(L"");

            std::wstring steamId;
            if (auto account = OptionalObject(result, L"accountItem"); account)
            {
                steamId = Json::OptionalString(account, L"steamId").value_or(L"");
            }

            if (!IsValidToken(token) || !IsValidSteamId(steamId))
            {
                throw hresult_error(E_FAIL,
                    L"登录已确认，但返回的凭据格式不正确");
            }

            out.phase = QrLoginPhase::Confirmed;
            out.accessToken = token;
            out.selfSteamId = steamId;
            break;
        }
        default:
            throw hresult_error(E_FAIL,
                winrt::hstring(L"未知的扫码状态：" + std::to_wstring(status)));
        }
        co_return;
    }

    IAsyncAction PerfectAuth::ValidateAsync(
        std::wstring accessToken, std::wstring selfSteamId, bool& out)
    {
        out = false;
        if (accessToken.empty() || selfSteamId.empty())
        {
            co_return;
        }

        // 用一个最轻的接口验证登录态：
        // season-ladder-score-list 不加密，解析成本最低。
        HttpClient client;
        const std::wstring url =
            L"https://pwaweblogin.wmpvp.com/user-info/season-ladder-score-list?access_token=" +
            accessToken + L"&uid=" + selfSteamId + L"&ignore_season=";

        HttpRequestMessage request;
        request.RequestUri(Uri(url));
        request.Method(HttpMethod::Get());
        auto headers = request.Headers();
        headers.Append(L"Referer", L"https://client.wmpvp.com");
        headers.Append(L"PwaSteamId", selfSteamId);

        const std::wstring body =
            (co_await (co_await client.SendRequestAsync(request))
                .Content().ReadAsStringAsync()).c_str();

        // 登录态失效时接口返回业务错误码（1002 / 1033）。
        JsonObject root{ nullptr };
        if (JsonObject::TryParse(body, root) && root)
        {
            const int32_t code = Json::OptionalInt32(root, L"code").value_or(0);
            if (code == 1002 || code == 1033)
            {
                co_return;
            }
            out = (code == 0);
        }
        co_return;
    }
}
