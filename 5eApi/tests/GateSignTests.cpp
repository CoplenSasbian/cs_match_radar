// Gate 签名验证。
//
// 关键测试：用参考项目提供的已知向量校验 canonical string 与 HMAC 是否正确。
// 该向量来自 5E 前端真实请求，签名值必须逐字符一致。

#include "FiveEApiPch.h"
#include "FiveEGateSign.h"

#include <winrt/base.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void CheckW(std::wstring const& actual, std::wstring const& expected, char const* name)
    {
        if (actual == expected)
        {
            std::printf("  PASS  %s\n", name);
        }
        else
        {
            std::printf("  FAIL  %s\n", name);
            std::wprintf(L"        actual   = %s\n", actual.c_str());
            std::wprintf(L"        expected = %s\n", expected.c_str());
            ++g_failures;
        }
    }
}

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ---- 1. canonical string 构造 ----
    std::printf("[1] Canonical string\n");
    {
        const std::wstring canonical = FiveE::Gate::BuildCanonicalString(
            L"GET",
            L"/cranenew/http/api/data/v3/player/home?uuid=ed954be0-7222-11ee-9ce2-ec0d9a495494",
            { { L"Accept-Language", L"zh-cn" }, { L"Authorization", L"" } });

        const std::wstring expected =
            L"GET\n"
            L"*/*\n"
            L"\n"
            L"\n"
            L"\n"
            L"accept-language:zh-cn\n"
            L"authorization:\n"
            L"/cranenew/http/api/data/v3/player/home?uuid=ed954be0-7222-11ee-9ce2-ec0d9a495494";

        CheckW(canonical, expected, "canonical 与参考实现一致");
    }

    // ---- 2. 已知签名向量（决定性测试） ----
    std::printf("[2] 已知签名向量\n");
    {
        // 参考项目 platform_5e_gate_sign.rs 的测试用例：
        //   期望 V+81D/A51CzX6Azk0HbElYwxZfLiquWykkaT6k+Z8Io=
        const std::wstring canonical = FiveE::Gate::BuildCanonicalString(
            L"GET",
            L"/cranenew/http/api/data/v3/player/home?uuid=ed954be0-7222-11ee-9ce2-ec0d9a495494",
            { { L"Accept-Language", L"zh-cn" }, { L"Authorization", L"" } });

        const std::wstring signature = FiveE::Gate::HmacSha256Base64(
            FiveE::Gate::kGateHmacSecret, canonical);

        CheckW(signature, L"V+81D/A51CzX6Azk0HbElYwxZfLiquWykkaT6k+Z8Io=",
            "HMAC-SHA256 签名与已知向量一致");
    }

    // ---- 3. query 排序 ----
    std::printf("[3] Query 排序\n");
    {
        // 输入顺序打乱，输出必须按 key 升序。
        const std::wstring pathAndParams = FiveE::Gate::BuildPathAndParameters(
            L"/api/test",
            { { L"zeta", L"1" }, { L"alpha", L"2" }, { L"mid", L"3" } });
        CheckW(pathAndParams, L"/api/test?alpha=2&mid=3&zeta=1", "query 按 key 升序拼接");

        const std::wstring noQuery = FiveE::Gate::BuildPathAndParameters(L"/api/test", {});
        CheckW(noQuery, L"/api/test", "无 query 时不加 ?");
    }

    // ---- 4. 完整签名头 ----
    std::printf("[4] 签名头\n");
    {
        const auto headers = FiveE::Gate::BuildSignedHeaders(
            L"GET",
            L"/cranenew/http/api/data/v3/player/home",
            { { L"uuid", L"ed954be0-7222-11ee-9ce2-ec0d9a495494" } });

        CheckW(headers.caKey, L"5eplay", "x-ca-key");
        CheckW(headers.signatureMethod, L"HmacSHA256", "signature-method 大小写必须精确");
        CheckW(headers.signatureHeaders, L"Accept-Language,Authorization", "signature-headers");
        CheckW(headers.signature, L"V+81D/A51CzX6Azk0HbElYwxZfLiquWykkaT6k+Z8Io=",
            "经由 BuildSignedHeaders 得到的签名同样正确");
        CheckW(headers.referer, L"https://view-arena.5eplay.com/", "referer");
    }

    // ---- 5. URL 编码 ----
    std::printf("[5] URL 编码\n");
    {
        // UUID 里的连字符不需转义。
        CheckW(FiveE::Gate::UrlEncodeQueryValue(L"ed954be0-7222-11ee-9ce2-ec0d9a495494"),
            L"ed954be0-7222-11ee-9ce2-ec0d9a495494", "uuid 原样保留");
        CheckW(FiveE::Gate::UrlEncodeQueryValue(L"a b&c=d"), L"a%20b%26c%3Dd", "空格与保留字符转义");
    }

    std::printf("\n==== %s (failures=%d) ====\n",
        g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
