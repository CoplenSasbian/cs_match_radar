// 完美世界响应解密验证。
//
// 关键：用独立实现（.NET AES）生成密文，再用 C++ 解密比对，
// 避免"自己加密自己解密"这种自证循环。
//
// 测试向量由 tools/gen_perfect_vector.ps1 生成。

#include "PerfectApiPch.h"
#include "PerfectCrypto.h"

#include <winrt/base.h>

#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;

    void Check(bool ok, char const* name, std::wstring const& detail = L"")
    {
        if (ok)
        {
            std::printf("  PASS  %s\n", name);
        }
        else
        {
            std::printf("  FAIL  %s\n", name);
            if (!detail.empty())
            {
                std::wprintf(L"        %s\n", detail.c_str());
            }
            ++g_failures;
        }
    }

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

int main(int argc, char** argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ---- 1. 时间戳规范化 ----
    std::printf("[1] 时间戳规范化\n");
    {
        CheckW(Perfect::NormalizeTimestamp(L"569525"), L"569525", "纯数字");
        CheckW(Perfect::NormalizeTimestamp(L"  569525  "), L"569525", "去空白");
        CheckW(Perfect::NormalizeTimestamp(L""), L"", "空 -> 空");
        CheckW(Perfect::NormalizeTimestamp(L"56.9"), L"", "含小数点 -> 拒绝");
        CheckW(Perfect::NormalizeTimestamp(L"abc"), L"", "非数字 -> 拒绝");
    }

    // ---- 2. 密钥派生 ----
    std::printf("[2] 密钥派生\n");
    {
        // 前缀 26 字符 + 6 位 t = 32 字节。
        const std::wstring key = Perfect::DeriveKey(Perfect::kKeyPrefix, L"569525");
        Check(key.size() == 32, "32 字节密钥", std::to_wstring(key.size()));

        // t 位数不足时派生失败。
        const std::wstring shortKey = Perfect::DeriveKey(Perfect::kKeyPrefix, L"56");
        Check(shortKey.empty(), "t 位数不足 -> 派生失败");

        // 验证前缀长度确实是 26。
        Check(Perfect::kKeyPrefix.size() == 26, "前缀为 26 字符");
    }

    // ---- 3. PKCS#7 去填充 ----
    std::printf("[3] PKCS#7 去填充\n");
    {
        // "hello" + 11 个 0x0B
        std::string padded = "hello";
        padded.append(11, static_cast<char>(0x0B));
        Check(Perfect::RemovePkcs7Padding(padded) == "hello", "标准填充");

        // 满块填充：16 个 0x10
        std::string full(16, static_cast<char>(0x10));
        Check(Perfect::RemovePkcs7Padding(full) == "", "满块填充 -> 空明文");

        // 非法：填充字节不一致
        std::string bad = "hello";
        bad.append(10, static_cast<char>(0x0B));
        bad.push_back(static_cast<char>(0x0C));
        Check(Perfect::RemovePkcs7Padding(bad) == "", "填充不一致 -> 拒绝");

        // 非法：填充值 0
        std::string zero = "hello";
        zero.push_back(static_cast<char>(0x00));
        Check(Perfect::RemovePkcs7Padding(zero) == "", "填充 0 -> 拒绝");

        // 非法：填充值 > 16
        std::string big = "hello";
        big.push_back(static_cast<char>(0x11));
        Check(Perfect::RemovePkcs7Padding(big) == "", "填充 > 16 -> 拒绝");
    }

    // ---- 4. 真实向量解密（决定性测试）----
    // 该向量由 tools/gen_perfect_vector.ps1 用 .NET AES 独立生成。
    std::printf("[4] 真实向量解密\n");
    {
        if (argc < 3)
        {
            std::printf("  SKIP  未提供向量文件（usage: %s <envelope.json> <expected.txt>）\n", argv[0]);
        }
        else
        {
            // 参数：envelope.json（含 code/data.e/data.t）与 expected.json
            auto readAll = [](std::string const& path) -> std::string {
                FILE* f = nullptr;
                if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return {};
                std::string out;
                char buf[4096];
                size_t n = 0;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
                fclose(f);
                return out;
            };

            const std::string envelope = readAll(argv[1]);
            const std::string expected = readAll(argv[2]);

            if (envelope.empty() || expected.empty())
            {
                std::printf("  FAIL  无法读取向量文件\n");
                ++g_failures;
            }
            else
            {
                const std::wstring envW(envelope.begin(), envelope.end());
                const auto result = Perfect::DecryptResponse(envW);
                if (!result.ok)
                {
                    Check(false, "解密成功", result.message);
                }
                else
                {
                    // 按 UTF-8 字节比较：明文含中文，绝不能窄字符逐字节比对
                    // （宽字符转 char 会丢信息，导致误报）。
                    auto toUtf8 = [](std::wstring const& w) {
                        if (w.empty()) return std::string{};
                        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                            static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
                        std::string out(static_cast<size_t>(n), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            out.data(), n, nullptr, nullptr);
                        return out;
                    };
                    // 双方都去掉仅用于可读性的空白后按字节比较。
                    auto stripWs = [](std::string const& s) {
                        std::string out;
                        for (char ch : s) {
                            if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') out.push_back(ch);
                        }
                        return out;
                    };
                    const std::string actual = stripWs(toUtf8(result.json));
                    const std::string want = stripWs(expected);
                    if (actual == want)
                    {
                        Check(true, "解密结果与独立实现（.NET AES）逐字节一致");
                    }
                    else
                    {
                        std::wstring a(actual.begin(), actual.end());
                        std::wstring e(want.begin(), want.end());
                        Check(false, "解密结果与独立实现一致",
                            L"长度 actual=" + std::to_wstring(actual.size()) +
                            L" want=" + std::to_wstring(want.size()));
                    }
                }
            }
        }
    }

    // ---- 5. 错误语义 ----
    std::printf("[5] 错误语义\n");
    {
        // 未加密的错误响应应原样返回。
        const auto passthrough = Perfect::DecryptResponse(
            L"{\"code\":1033,\"data\":null,\"msg\":\"expired\"}");
        Check(passthrough.ok && passthrough.json.find(L"1033") != std::wstring::npos,
            "code!=0 原样返回（登录失效语义）");

        // 缺少 data.e
        const auto missing = Perfect::DecryptResponse(L"{\"code\":0,\"data\":{}}");
        Check(!missing.ok && missing.error == Perfect::DecryptError::Envelope,
            "缺少 data.e -> Envelope");

        // 非法 Base64
        const auto badB64 = Perfect::DecryptResponse(
            L"{\"code\":0,\"data\":{\"e\":\"%%%\",\"t\":569525}}");
        Check(!badB64.ok && badB64.error == Perfect::DecryptError::Base64,
            "非法 Base64 -> Base64");

        // 密钥长度错误
        const auto badKey = Perfect::DecryptResponse(
            L"{\"code\":0,\"data\":{\"e\":\"AAAA\",\"t\":1}}");
        Check(!badKey.ok && badKey.error == Perfect::DecryptError::KeyLength,
            "t 位数不足 -> KeyLength");

        // 长度不是 16 的倍数
        const auto badLen = Perfect::DecryptResponse(
            L"{\"code\":0,\"data\":{\"e\":\"AAAA\",\"t\":569525}}");
        Check(!badLen.ok && badLen.error == Perfect::DecryptError::Base64,
            "密文长度非 16 倍数 -> Base64");
    }

    std::printf("\n==== %s (failures=%d) ====\n",
        g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
