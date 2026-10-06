#pragma once

// 完美世界对战平台响应解密。
//
// 完美平台的 overview / season-stats 接口返回的不是明文 JSON，而是：
//
//   { "code": 0, "data": { "e": "<Base64 密文>", "t": 569525 }, "msg": "" }
//
// 解密规则（来自参考项目 perfect_crypto.rs，已逐条核对）：
//   - 算法：AES-256-ECB
//   - 填充：PKCS#7
//   - 密钥：固定前缀 + data.t 的十进制字符串，必须恰好 32 字节
//     前缀 = "G#r%*VCDYj6P5$mny0838MhH8d"（26 字符）
//     因此 t 必须是 6 位十进制数（26 + 6 = 32），如参考样本 t=569525
//   - 密文：Base64 解码 data.e
//   - 解密结果需通过 UTF-8 与 JSON 双重校验
//
// 注意：错误响应（code != 0）**不加密**，应原样返回。

#include <string>
#include <string_view>

namespace Perfect
{
    // 固定密钥前缀。
    inline constexpr std::wstring_view kKeyPrefix = L"G#r%*VCDYj6P5$mny0838MhH8d";

    // 解密失败的原因，便于区分「登录态失效」与「算法/字段变更」。
    enum class DecryptError
    {
        None,
        Envelope,   // 缺少 data.e / data.t
        Base64,     // data.e 不是合法 Base64，或长度不是 16 的倍数
        KeyLength,  // 派生密钥不是 32 字节
        Padding,    // PKCS#7 填充非法
        Utf8,       // 明文不是合法 UTF-8
        Json,       // 明文不是合法 JSON
        Crypto,     // WinRT 加密调用失败
    };

    struct DecryptResult
    {
        bool ok = false;
        std::wstring json;              // 解密后的 JSON 文本
        DecryptError error = DecryptError::None;
        std::wstring message;           // 可读错误说明

        explicit operator bool() const { return ok; }
    };

    // 把 t 规范化为密钥后缀文本。
    // t 可能是数字或字符串；返回空串表示不合法。
    std::wstring NormalizeTimestamp(std::wstring_view t);

    // 派生 AES-256 密钥（前缀 + t），并校验长度必须为 32。
    // 失败返回空串。
    std::wstring DeriveKey(std::wstring_view prefix, std::wstring_view timestamp);

    // 用给定密钥解密 Base64 密文（AES-256-ECB + PKCS#7）。
    // 返回解密后的明文（原始字节转成的 UTF-8 文本）。
    DecryptResult DecryptBase64(std::wstring_view key, std::wstring_view base64Ciphertext);

    // 从解密得到的明文中去掉 PKCS#7 填充。
    // 输入应为 16 字节对齐的密文解密结果。
    std::string RemovePkcs7Padding(std::string const& decrypted);

    // 上层入口：传入完整响应体，返回解密后的 JSON 文本。
    // 若响应 code != 0（未加密的错误响应），原样返回该响应。
    DecryptResult DecryptResponse(std::wstring_view responseBody);

    // 错误类型名（用于日志）。
    wchar_t const* DecryptErrorName(DecryptError error);
}
