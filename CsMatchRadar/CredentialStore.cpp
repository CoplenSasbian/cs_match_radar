#include "pch.h"
#include "CredentialStore.h"

#include <winrt/base.h>

#include <windows.h>
#include <wincrypt.h>

#include <string>
#include <vector>

namespace AppCred
{
    namespace
    {
        // LocalSettings 里的键名。
        constexpr wchar_t kKeyToken[] = L"PerfectAccessTokenProtected";
        constexpr wchar_t kKeySteam[] = L"PerfectSelfSteamIdProtected";

        // 附加 entropy：让密文只对本应用有意义。
        constexpr char kEntropy[] = "CSMatchRadar/perfect-auth/v1";

        // base64 编解码（密文是二进制，LocalSettings 要存字符串）。
        std::wstring Base64Encode(std::vector<BYTE> const& data)
        {
            if (data.empty())
            {
                return {};
            }
            DWORD length = 0;
            if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
                    CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length))
            {
                return {};
            }
            std::wstring result(length, L'\0');
            if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()),
                    CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &length))
            {
                return {};
            }
            // 去掉结尾的 NUL。
            while (!result.empty() && result.back() == L'\0')
            {
                result.pop_back();
            }
            return result;
        }

        bool Base64Decode(std::wstring const& text, std::vector<BYTE>& out)
        {
            out.clear();
            if (text.empty())
            {
                return false;
            }
            DWORD length = 0;
            if (!CryptStringToBinaryW(text.c_str(), static_cast<DWORD>(text.size()),
                    CRYPT_STRING_BASE64, nullptr, &length, nullptr, nullptr))
            {
                return false;
            }
            out.resize(length);
            if (!CryptStringToBinaryW(text.c_str(), static_cast<DWORD>(text.size()),
                    CRYPT_STRING_BASE64, out.data(), &length, nullptr, nullptr))
            {
                out.clear();
                return false;
            }
            out.resize(length);
            return true;
        }

        // 用 DPAPI 加密一段文本，返回 base64 密文。
        std::wstring Protect(std::wstring const& plain)
        {
            if (plain.empty())
            {
                return {};
            }
            const DWORD byteLength = static_cast<DWORD>(plain.size() * sizeof(wchar_t));

            DATA_BLOB input{};
            input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(plain.data()));
            input.cbData = byteLength;

            DATA_BLOB entropy{};
            entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
            entropy.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);

            DATA_BLOB output{};
            if (!CryptProtectData(&input, L"CS Match Radar", &entropy, nullptr, nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN, &output))
            {
                return {};
            }

            std::vector<BYTE> bytes(output.pbData, output.pbData + output.cbData);
            LocalFree(output.pbData);
            return Base64Encode(bytes);
        }

        // 解密；失败返回空串。
        std::wstring Unprotect(std::wstring const& cipherText)
        {
            std::vector<BYTE> bytes;
            if (!Base64Decode(cipherText, bytes) || bytes.empty())
            {
                return {};
            }

            DATA_BLOB input{};
            input.pbData = bytes.data();
            input.cbData = static_cast<DWORD>(bytes.size());

            DATA_BLOB entropy{};
            entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy));
            entropy.cbData = static_cast<DWORD>(sizeof(kEntropy) - 1);

            DATA_BLOB output{};
            if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN, &output))
            {
                return {};
            }

            std::wstring result(reinterpret_cast<wchar_t*>(output.pbData),
                                output.cbData / sizeof(wchar_t));
            LocalFree(output.pbData);
            return result;
        }

        // 存储位置：当前用户的注册表 HKCU\Software\CsMatchRadar（免 WinRT 程序包标识）。
        constexpr wchar_t kRegSubKey[] = L"Software\\CsMatchRadar";

        bool ReadSetting(wchar_t const* key, std::wstring& out)
        {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            {
                return false;
            }
            DWORD bytesNeeded = 0;
            DWORD type = 0;
            if (RegQueryValueExW(hKey, key, nullptr, &type, nullptr, &bytesNeeded) != ERROR_SUCCESS ||
                type != REG_SZ || bytesNeeded == 0)
            {
                RegCloseKey(hKey);
                return false;
            }
            std::vector<wchar_t> buffer(bytesNeeded / sizeof(wchar_t) + 1, 0);
            if (RegQueryValueExW(hKey, key, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &bytesNeeded) == ERROR_SUCCESS)
            {
                out = buffer.data();
                RegCloseKey(hKey);
                return !out.empty();
            }
            RegCloseKey(hKey);
            return false;
        }

        bool WriteSetting(wchar_t const* key, std::wstring const& value)
        {
            HKEY hKey = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }
            DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            LONG res = RegSetValueExW(hKey, key, 0, REG_SZ, reinterpret_cast<BYTE const*>(value.c_str()), bytes);
            RegCloseKey(hKey);
            return res == ERROR_SUCCESS;
        }

        void DeleteSetting(wchar_t const* key)
        {
            HKEY hKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
            {
                RegDeleteValueW(hKey, key);
                RegCloseKey(hKey);
            }
        }
    }

    bool Save(std::wstring const& accessToken, std::wstring const& selfSteamId)
    {
        if (accessToken.empty() || selfSteamId.empty())
        {
            return false;
        }
        const std::wstring protectedToken = Protect(accessToken);
        const std::wstring protectedSteam = Protect(selfSteamId);
        if (protectedToken.empty() || protectedSteam.empty())
        {
            return false;
        }
        return WriteSetting(kKeyToken, protectedToken) &&
               WriteSetting(kKeySteam, protectedSteam);
    }

    bool Load(StoredCredentials& out)
    {
        out = StoredCredentials{};

        std::wstring protectedToken;
        std::wstring protectedSteam;
        if (!ReadSetting(kKeyToken, protectedToken) ||
            !ReadSetting(kKeySteam, protectedSteam))
        {
            return false;
        }

        out.accessToken = Unprotect(protectedToken);
        out.selfSteamId = Unprotect(protectedSteam);
        if (out.accessToken.empty() || out.selfSteamId.empty())
        {
            out = StoredCredentials{};
            return false;
        }
        return true;
    }

    void Clear()
    {
        DeleteSetting(kKeyToken);
        DeleteSetting(kKeySteam);
    }
}
