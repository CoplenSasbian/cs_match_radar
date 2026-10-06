#pragma once

// 完美平台登录凭据的本机持久化。
//
// 用 Windows DPAPI（CryptProtectData）加密后落盘：
//   - 密钥由系统按「当前 Windows 用户」派生，其他用户 / 其他机器无法解密；
//   - 附加 entropy，避免同机其他程序直接解密我们写下的密文；
//   - 明文 token 不落盘。
//
// 存储位置：应用包的 LocalSettings（ApplicationData.Current.LocalSettings）。
// 打包应用无需额外权限即可使用。

#include <string>

namespace AppCred
{
    // 已保存的凭据。
    struct StoredCredentials
    {
        std::wstring accessToken;
        std::wstring selfSteamId;
    };

    // 保存（覆盖）凭据。失败返回 false。
    bool Save(std::wstring const& accessToken, std::wstring const& selfSteamId);

    // 读取凭据。没有保存过或解密失败返回 false。
    bool Load(StoredCredentials& out);

    // 清除已保存的凭据。
    void Clear();
}
