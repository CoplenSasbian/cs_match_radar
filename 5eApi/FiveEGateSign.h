#pragma once

// 5E Gate（阿里云 API 网关）HMAC-SHA256 请求签名。
//
// 5E Arena 前端在打包产物里硬编码了网关密钥，签名规则对齐阿里云 API 网关：
//
//   canonical =
//       METHOD            \n
//       Accept            \n   (固定 "*/*")
//       Content-MD5       \n   (留空)
//       Content-Type      \n   (留空)
//       Date              \n   (留空)
//       <签名头块>        \n   (按小写字母序的 name:value，\n 连接)
//       PathAndParameters      (path + 排序后的 query)
//
//   signature = Base64(HMAC-SHA256(secret, canonical))
//
// 实测：该签名对 GET /cranenew/http/api/data/v3/player/home 有效，
// 返回 HTTP 200（见 docs/5e-auth-findings.md）。

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace FiveE::Gate
{
    // 5E Arena 生产环境网关密钥。
    // 来源：view-arena 前端产物的 production.HMAC_SECRET。
    // 注意：这是第三方平台的内部密钥，5E 可能随时更换；一旦签名开始返回
    // 400 Invalid Signature，需要重新从客户端产物中提取。
    inline constexpr std::wstring_view kGateHmacSecret =
        L"28137ed61f21705e2cddf498e22fae8f";

    // 参与签名的头（名称 + 值）。值可以为空（Authorization 即为空）。
    using HeaderPair = std::pair<std::wstring, std::wstring>;

    // 一次签名需要的全部请求头。
    struct SignedHeaders
    {
        std::wstring accept;
        std::wstring acceptLanguage;
        std::wstring authorization;
        std::wstring contentMd5;
        std::wstring referer;
        std::wstring origin;
        std::wstring caKey;
        std::wstring signatureMethod;
        std::wstring signatureHeaders;   // "Accept-Language,Authorization"
        std::wstring signature;          // Base64
    };

    // 计算 HMAC-SHA256 并做 Base64 编码。
    // 依赖 Windows.Security.Cryptography.Core，无需第三方库。
    std::wstring HmacSha256Base64(std::wstring_view secret, std::wstring_view message);

    // 把 query 参数按 key 升序拼接为 "k1=v1&k2=v2"。
    std::wstring BuildSortedQuery(
        std::vector<std::pair<std::wstring, std::wstring>> const& query);

    // 构造 path + 排序后的 query。
    std::wstring BuildPathAndParameters(
        std::wstring_view path,
        std::vector<std::pair<std::wstring, std::wstring>> const& query);

    // 构造 canonical string（便于测试与排查）。
    std::wstring BuildCanonicalString(
        std::wstring_view method,
        std::wstring_view pathAndParameters,
        std::vector<HeaderPair> const& signedHeaders = {});

    // 生成完整签名头。
    // path 不含 query；query 会自动排序并拼到 path 后参与签名。
    SignedHeaders BuildSignedHeaders(
        std::wstring_view method,
        std::wstring_view path,
        std::vector<std::pair<std::wstring, std::wstring>> const& query = {});

    // 对查询值做 URL 编码（5E 的 uuid 只含 0-9a-f-，但仍做完整处理）。
    std::wstring UrlEncodeQueryValue(std::wstring_view value);
}
