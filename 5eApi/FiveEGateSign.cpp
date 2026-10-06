#include "FiveEApiPch.h"
#include "FiveEGateSign.h"

#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <cwctype>

using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Windows::Security::Cryptography::Core;
using namespace winrt::Windows::Storage::Streams;

namespace FiveE::Gate
{
    namespace
    {
        // 把宽字符串按 UTF-8 转成 IBuffer。
        IBuffer ToUtf8Buffer(std::wstring_view text)
        {
            const std::wstring owned(text);
            return CryptographicBuffer::ConvertStringToBinary(
                winrt::hstring(owned), BinaryStringEncoding::Utf8);
        }

        // IBuffer -> Base64 字符串。
        std::wstring ToBase64(IBuffer const& buffer)
        {
            return std::wstring(CryptographicBuffer::EncodeToBase64String(buffer).c_str());
        }

        std::wstring ToLowerAscii(std::wstring_view text)
        {
            std::wstring out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
                return static_cast<wchar_t>(std::towlower(c));
            });
            return out;
        }
    }

    std::wstring HmacSha256Base64(std::wstring_view secret, std::wstring_view message)
    {
        // OpenAlgorithm 使用 "HMAC_SHA256"（对应 MacAlgorithmNames::HmacSha256）。
        const MacAlgorithmProvider provider = MacAlgorithmProvider::OpenAlgorithm(L"HMAC_SHA256");

        const IBuffer keyMaterial = ToUtf8Buffer(secret);
        const CryptographicKey key = provider.CreateKey(keyMaterial);

        const IBuffer data = ToUtf8Buffer(message);
        const IBuffer signature = CryptographicEngine::Sign(key, data);

        return ToBase64(signature);
    }

    std::wstring UrlEncodeQueryValue(std::wstring_view value)
    {
        static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
        std::wstring out;
        out.reserve(value.size() * 3);
        for (const wchar_t ch : value)
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

    std::wstring BuildSortedQuery(
        std::vector<std::pair<std::wstring, std::wstring>> const& query)
    {
        if (query.empty())
        {
            return {};
        }
        // 按 key 升序（与参考实现一致：BTreeMap 天然有序）。
        auto sorted = query;
        std::sort(sorted.begin(), sorted.end(),
            [](auto const& a, auto const& b) { return a.first < b.first; });

        std::wstring out;
        for (auto const& [key, value] : sorted)
        {
            if (!out.empty())
            {
                out.push_back(L'&');
            }
            out += key;
            out.push_back(L'=');
            out += value;
        }
        return out;
    }

    std::wstring BuildPathAndParameters(
        std::wstring_view path,
        std::vector<std::pair<std::wstring, std::wstring>> const& query)
    {
        const std::wstring queryString = BuildSortedQuery(query);
        if (queryString.empty())
        {
            return std::wstring(path);
        }
        return std::wstring(path) + L"?" + queryString;
    }

    std::wstring BuildCanonicalString(
        std::wstring_view method,
        std::wstring_view pathAndParameters,
        std::vector<HeaderPair> const& signedHeaders)
    {
        // 签名头块：按名称小写后的字母序排列，每行 "name:value"。
        auto headers = signedHeaders;
        std::sort(headers.begin(), headers.end(),
            [](HeaderPair const& a, HeaderPair const& b) {
                return ToLowerAscii(a.first) < ToLowerAscii(b.first);
            });

        std::wstring block;
        for (auto const& [name, value] : headers)
        {
            if (!block.empty())
            {
                block.push_back(L'\n');
            }
            block += ToLowerAscii(name);
            block.push_back(L':');
            block += value;
        }

        // METHOD \n Accept \n Content-MD5 \n Content-Type \n Date \n [block \n] path
        std::wstring canonical;
        canonical += method;          canonical.push_back(L'\n');
        canonical += L"*/*";          canonical.push_back(L'\n');
        canonical.push_back(L'\n');   // Content-MD5 留空
        canonical.push_back(L'\n');   // Content-Type 留空
        canonical.push_back(L'\n');   // Date 留空
        if (!block.empty())
        {
            canonical += block;
            canonical.push_back(L'\n');
        }
        canonical += pathAndParameters;
        return canonical;
    }

    SignedHeaders BuildSignedHeaders(
        std::wstring_view method,
        std::wstring_view path,
        std::vector<std::pair<std::wstring, std::wstring>> const& query)
    {
        // 只签这两个头，名称与顺序需与 x-ca-signature-headers 一致。
        const std::vector<HeaderPair> signedHeaders = {
            { L"Accept-Language", L"zh-cn" },
            { L"Authorization", L"" },
        };

        const std::wstring pathAndParameters = BuildPathAndParameters(path, query);
        const std::wstring canonical = BuildCanonicalString(method, pathAndParameters, signedHeaders);
        const std::wstring signature = HmacSha256Base64(kGateHmacSecret, canonical);

        SignedHeaders result;
        result.accept = L"*/*";
        result.acceptLanguage = L"zh-cn";
        result.authorization = L"";
        result.contentMd5 = L"";
        result.referer = L"https://view-arena.5eplay.com/";
        result.origin = L"https://view-arena.5eplay.com";
        result.caKey = L"5eplay";
        result.signatureMethod = L"HmacSHA256";
        result.signatureHeaders = L"Accept-Language,Authorization";
        result.signature = signature;
        return result;
    }
}
