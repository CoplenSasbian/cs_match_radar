// 端到端验证：用 C++ 计算签名并实际请求 5E player/home。
// 成功标准：HTTP 200 且返回体含 data.career。
//
// 用法：LiveHomeTests.exe <uuid>

#include "FiveEApiPch.h"
#include "FiveEGateSign.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/base.h>

#include <cstdio>
#include <string>

using namespace winrt;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;

int main(int argc, char** argv)
{
    init_apartment(apartment_type::multi_threaded);

    if (argc < 2)
    {
        std::printf("usage: LiveHomeTests <uuid>\n");
        return 2;
    }
    std::string narrow(argv[1]);
    const std::wstring uuid(narrow.begin(), narrow.end());

    const std::wstring path = L"/cranenew/http/api/data/v3/player/home";

    // 用 C++ 实现计算签名。
    const auto signed_ = FiveE::Gate::BuildSignedHeaders(
        L"GET", path, { { L"uuid", uuid } });

    std::wprintf(L"signature = %s\n", signed_.signature.c_str());
    try {

    const std::wstring fullUrl =
        L"https://gate.5eplay.com" + FiveE::Gate::BuildPathAndParameters(path, { { L"uuid", uuid } });

    HttpClient client;
    auto headers = client.DefaultRequestHeaders();
    headers.Append(L"x-ca-key", signed_.caKey);
    headers.Append(L"x-ca-signature-method", signed_.signatureMethod);
    headers.Append(L"x-ca-signature-headers", signed_.signatureHeaders);
    headers.Append(L"x-ca-signature", signed_.signature);
    // 实测结论：Authorization 只参与签名计算（按空值），
    // 但**不能真的发送**——不发送时服务器返回 200；
    // WinRT 也会拒绝写入空值（E_INVALIDARG），正好一致。
    headers.Append(L"Accept-Language", signed_.acceptLanguage);
    headers.Append(L"Referer", signed_.referer);
    headers.Append(L"Origin", signed_.origin);
    headers.Append(L"User-Agent",
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    std::wprintf(L"URL = %s\n", fullUrl.c_str());
    std::wprintf(L"canonical = [%s]\n",
        FiveE::Gate::BuildCanonicalString(
            L"GET",
            FiveE::Gate::BuildPathAndParameters(path, { { L"uuid", uuid } }),
            { { L"Accept-Language", L"zh-cn" }, { L"Authorization", L"" } }).c_str());
    std::printf("requesting...\n");
    HttpRequestMessage request(HttpMethod::Get(), Uri(fullUrl));
    auto rh = request.Headers();
    rh.Append(L"x-ca-key", signed_.caKey);
    rh.Append(L"x-ca-signature-method", signed_.signatureMethod);
    rh.Append(L"x-ca-signature-headers", signed_.signatureHeaders);
    rh.Append(L"x-ca-signature", signed_.signature);
    rh.Append(L"Accept-Language", signed_.acceptLanguage);
    rh.Append(L"Referer", signed_.referer);
    rh.Append(L"Origin", signed_.origin);
    rh.Append(L"Accept", signed_.accept);
    std::printf("--- outgoing headers ---\n");
    for (auto const& kv : rh) {
        std::wprintf(L"  %s: %s\n", kv.Key().c_str(), kv.Value().c_str());
    }
    const auto response = client.SendRequestAsync(request).get();
    const int status = static_cast<int>(response.StatusCode());
    const std::wstring body = response.Content().ReadAsStringAsync().get().c_str();

    std::printf("HTTP %d, body %zu chars\n", status, body.size());
    if (status != 200)
    {
        std::wprintf(L"--- body ---\n%s\n", body.substr(0, 400).c_str());
        return 1;
    }

    JsonObject root{ nullptr };
    if (!JsonObject::TryParse(body, root) || !root)
    {
        std::printf("FAIL: 响应不是 JSON\n");
        return 1;
    }

    const auto data = FiveE::Json::Get(root, L"data");
    if (FiveE::Json::IsNull(data) || data.ValueType() != FiveE::Json::JsonValueKind::Object)
    {
        std::printf("FAIL: 缺少 data\n");
        return 1;
    }
    const auto dataObj = data.GetObject();

    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    std::printf("[C++ 签名端到端验证]\n");
    check(true, "HTTP 200（签名被服务器接受）");
    check(!FiveE::Json::IsNull(FiveE::Json::Get(dataObj, L"career")), "含 career");
    check(!FiveE::Json::IsNull(FiveE::Json::Get(dataObj, L"uinfo")), "含 uinfo");
    check(!FiveE::Json::IsNull(FiveE::Json::Get(dataObj, L"elo_info")), "含 elo_info");

    // 打印关键字段，便于人工核对。
    const auto career = FiveE::Json::Get(dataObj, L"career").GetObject();
    const auto uinfo = FiveE::Json::Get(dataObj, L"uinfo").GetObject();

    std::printf("\n  玩家概览:\n");
    if (auto v = FiveE::Json::OptionalString(uinfo, L"username"))
        std::wprintf(L"    username = %s\n", v->c_str());
    if (auto v = FiveE::Json::OptionalDouble(career, L"elo"))
        std::printf("    career.elo = %.2f\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(career, L"level_id"))
        std::printf("    level_id = %d\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(career, L"rank"))
        std::printf("    rank = %d\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(career, L"match_total"))
        std::printf("    match_total = %d\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(career, L"match_win"))
        std::printf("    match_win = %d\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(uinfo, L"credit"))
        std::printf("    credit = %d\n", *v);
    if (auto v = FiveE::Json::OptionalInt32(uinfo, L"trusted_score"))
        std::printf("    trusted_score = %d\n", *v);

    std::printf("\n==== %s (failures=%d) ====\n",
        failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
    }
    catch (winrt::hresult_error const& ex) {
        std::wprintf(L"HRESULT ERROR: 0x%08X %s\n", (unsigned)ex.code().value, ex.message().c_str());
        return 3;
    }
    catch (std::exception const& ex) {
        std::printf("STD ERROR: %s\n", ex.what());
        return 3;
    }
}
