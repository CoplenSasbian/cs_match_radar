#include "PerfectApiPch.h"
#include "PerfectCrypto.h"
#include "PerfectJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Windows::Security::Cryptography::Core;
using namespace winrt::Windows::Storage::Streams;

namespace Perfect
{
    namespace
    {
        constexpr size_t kAesBlockSize = 16;
        constexpr size_t kKeyLength = 32;

        DecryptResult MakeError(DecryptError kind, std::wstring message)
        {
            DecryptResult result;
            result.ok = false;
            result.error = kind;
            result.message = std::move(message);
            return result;
        }

        // 宽字符 -> UTF-8 字节。
        std::string ToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return {};
            }
            std::string out(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                out.data(), size, nullptr, nullptr);
            return out;
        }

        // UTF-8 字节 -> 宽字符（失败返回空串）。
        // 严格的 UTF-8 校验：非法序列会被 MultiByteToWideChar 拒绝。
        bool FromUtf8(std::string const& bytes, std::wstring& out)
        {
            if (bytes.empty())
            {
                out.clear();
                return true;
            }
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (size <= 0)
            {
                return false;
            }
            out.assign(static_cast<size_t>(size), L'\0');
            const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                bytes.data(), static_cast<int>(bytes.size()), out.data(), size);
            return written == size;
        }

        IBuffer BytesToBuffer(std::vector<uint8_t> const& bytes)
        {
            return CryptographicBuffer::CreateFromByteArray(
                winrt::array_view<uint8_t const>(bytes.data(), bytes.data() + bytes.size()));
        }

        std::vector<uint8_t> BufferToBytes(IBuffer const& buffer)
        {
            winrt::com_array<uint8_t> raw;
            CryptographicBuffer::CopyToByteArray(buffer, raw);
            return std::vector<uint8_t>(raw.begin(), raw.end());
        }
    }

    std::wstring NormalizeTimestamp(std::wstring_view t)
    {
        // 去掉首尾空白。
        size_t begin = 0;
        size_t end = t.size();
        while (begin < end && (t[begin] == L' ' || t[begin] == L'\t' ||
                               t[begin] == L'\r' || t[begin] == L'\n'))
        {
            ++begin;
        }
        while (end > begin && (t[end - 1] == L' ' || t[end - 1] == L'\t' ||
                               t[end - 1] == L'\r' || t[end - 1] == L'\n'))
        {
            --end;
        }
        const std::wstring trimmed(t.substr(begin, end - begin));
        if (trimmed.empty())
        {
            return {};
        }
        // 必须是纯十进制。参考实现中数字型 t 会以 to_string 形式参与拼接，
        // 因此这里只接受 0-9，避免出现 "569525.0" 这类形式。
        for (wchar_t const ch : trimmed)
        {
            if (ch < L'0' || ch > L'9')
            {
                return {};
            }
        }
        return trimmed;
    }

    std::wstring DeriveKey(std::wstring_view prefix, std::wstring_view timestamp)
    {
        std::wstring key(prefix);
        key += timestamp;
        if (key.size() != kKeyLength)
        {
            return {};
        }
        return key;
    }

    std::string RemovePkcs7Padding(std::string const& decrypted)
    {
        if (decrypted.empty())
        {
            return {};
        }
        const auto padding = static_cast<uint8_t>(decrypted.back());
        if (padding == 0 || padding > kAesBlockSize || decrypted.size() < padding)
        {
            return {};
        }
        // 校验末尾 padding 个字节都等于 padding。
        for (size_t i = decrypted.size() - padding; i < decrypted.size(); ++i)
        {
            if (static_cast<uint8_t>(decrypted[i]) != padding)
            {
                return {};
            }
        }
        return decrypted.substr(0, decrypted.size() - padding);
    }

    DecryptResult DecryptBase64(std::wstring_view key, std::wstring_view base64Ciphertext)
    {
        if (key.empty())
        {
            return MakeError(DecryptError::KeyLength, L"密钥为空");
        }

        // Base64 解码。
        std::vector<uint8_t> ciphertext;
        try
        {
            const IBuffer decoded = CryptographicBuffer::DecodeFromBase64String(
                winrt::hstring(std::wstring(base64Ciphertext)));
            if (!decoded || decoded.Length() == 0)
            {
                return MakeError(DecryptError::Base64, L"data.e 不是合法 Base64 或为空");
            }
            ciphertext = BufferToBytes(decoded);
        }
        catch (winrt::hresult_error const&)
        {
            return MakeError(DecryptError::Base64, L"data.e 不是合法 Base64");
        }

        if (ciphertext.empty() || ciphertext.size() % kAesBlockSize != 0)
        {
            return MakeError(DecryptError::Base64,
                L"密文长度必须是非零的 16 字节倍数");
        }

        // AES-256-ECB + PKCS#7。WinRT 的 AesEcbPkcs7 会自动去填充，
        // 但为保持与参考实现一致的校验语义，这里用 AesEcb（无填充）
        // 自行处理 PKCS#7。
        std::wstring plaintext;
        try
        {
            const SymmetricKeyAlgorithmProvider provider =
                SymmetricKeyAlgorithmProvider::OpenAlgorithm(L"AES_ECB");

            const std::string keyBytes = ToUtf8(key);
            if (keyBytes.size() != kKeyLength)
            {
                return MakeError(DecryptError::KeyLength,
                    L"派生密钥不是 32 字节");
            }
            const IBuffer keyBuffer = CryptographicBuffer::CreateFromByteArray(
                winrt::array_view<uint8_t const>(
                    reinterpret_cast<uint8_t const*>(keyBytes.data()),
                    reinterpret_cast<uint8_t const*>(keyBytes.data()) + keyBytes.size()));

            const CryptographicKey cryptoKey = provider.CreateSymmetricKey(keyBuffer);

            // ECB 模式不需要 IV，传空 buffer。
            const IBuffer iv = CryptographicBuffer::CreateFromByteArray(
                winrt::array_view<uint8_t const>());
            const IBuffer encrypted = BytesToBuffer(ciphertext);

            const IBuffer decrypted = CryptographicEngine::Decrypt(cryptoKey, encrypted, iv);
            const std::vector<uint8_t> plainBytes = BufferToBytes(decrypted);

            const std::string raw(reinterpret_cast<char const*>(plainBytes.data()),
                plainBytes.size());
            const std::string unpadded = RemovePkcs7Padding(raw);
            if (unpadded.empty() && !raw.empty() && raw.size() >= kAesBlockSize)
            {
                // 去掉填充后为空可能是非法填充；但明文本身也可能就是空的。
                // 这里按参考实现的语义：填充非法即报错。
                if (raw.back() == 0 ||
                    static_cast<uint8_t>(raw.back()) > kAesBlockSize)
                {
                    return MakeError(DecryptError::Padding, L"PKCS#7 填充非法");
                }
            }

            if (!FromUtf8(unpadded, plaintext))
            {
                return MakeError(DecryptError::Utf8, L"明文不是合法 UTF-8");
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            return MakeError(DecryptError::Crypto,
                L"解密调用失败：" + std::wstring(ex.message()));
        }

        DecryptResult result;
        result.ok = true;
        result.json = std::move(plaintext);
        return result;
    }

    DecryptResult DecryptResponse(std::wstring_view responseBody)
    {
        if (responseBody.empty())
        {
            return MakeError(DecryptError::Envelope, L"响应体为空");
        }

        Json::JsonObjectRef root{ nullptr };
        if (!Json::JsonObjectRef::TryParse(std::wstring(responseBody), root) || !root)
        {
            return MakeError(DecryptError::Envelope, L"响应不是合法 JSON 对象");
        }

        // code != 0 的错误响应不加密，原样返回。
        if (auto code = Json::OptionalInt32(root, L"code"); code && *code != 0)
        {
            DecryptResult passthrough;
            passthrough.ok = true;
            passthrough.json = std::wstring(responseBody);
            return passthrough;
        }

        // code 缺失时也按加密分支处理（部分接口不带 code）。
        const auto dataValue = Json::Get(root, L"data");
        if (Json::IsNull(dataValue) ||
            dataValue.ValueType() != Json::JsonValueKind::Object)
        {
            return MakeError(DecryptError::Envelope, L"缺少 data 对象");
        }
        const auto data = dataValue.GetObject();

        const auto encryptedValue = Json::Get(data, L"e");
        if (Json::IsNull(encryptedValue) ||
            encryptedValue.ValueType() != Json::JsonValueKind::String)
        {
            return MakeError(DecryptError::Envelope, L"缺少 data.e");
        }
        const std::wstring encrypted(encryptedValue.GetString());

        // t 可能是数字或字符串。
        const auto tValue = Json::Get(data, L"t");
        if (Json::IsNull(tValue))
        {
            return MakeError(DecryptError::Envelope, L"缺少 data.t");
        }
        std::wstring timestampText;
        if (tValue.ValueType() == Json::JsonValueKind::Number)
        {
            // 整数形式；避免出现小数点。
            timestampText = Json::String(tValue);
        }
        else if (tValue.ValueType() == Json::JsonValueKind::String)
        {
            timestampText = std::wstring(tValue.GetString());
        }
        else
        {
            return MakeError(DecryptError::Envelope, L"data.t 类型不支持");
        }

        const std::wstring timestamp = NormalizeTimestamp(timestampText);
        if (timestamp.empty())
        {
            return MakeError(DecryptError::Envelope, L"data.t 不是纯十进制数字");
        }

        const std::wstring key = DeriveKey(kKeyPrefix, timestamp);
        if (key.empty())
        {
            return MakeError(DecryptError::KeyLength,
                L"派生密钥长度不是 32（data.t 位数不足 21 位）");
        }

        auto result = DecryptBase64(key, encrypted);
        if (!result.ok)
        {
            return result;
        }

        // 解密结果必须是合法 JSON。
        Json::JsonObjectRef parsed{ nullptr };
        if (!Json::JsonObjectRef::TryParse(result.json, parsed) || !parsed)
        {
            return MakeError(DecryptError::Json, L"明文不是合法 JSON");
        }
        return result;
    }

    wchar_t const* DecryptErrorName(DecryptError error)
    {
        switch (error)
        {
        case DecryptError::None:     return L"None";
        case DecryptError::Envelope: return L"Envelope";
        case DecryptError::Base64:   return L"Base64";
        case DecryptError::KeyLength:return L"KeyLength";
        case DecryptError::Padding:  return L"Padding";
        case DecryptError::Utf8:     return L"Utf8";
        case DecryptError::Json:     return L"Json";
        case DecryptError::Crypto:   return L"Crypto";
        default:                     return L"Unknown";
        }
    }
}
