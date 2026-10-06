#pragma once

// 完美世界对战平台扫码登录。
//
// 为什么要走扫码：完美平台的业务接口都要求登录凭据，而凭据只能通过
// 官方登录流程获得。参考实现（cs-match-helper）走的是二维码流程，这里
// 按同一协议实现：
//
//   1. POST passport.pwesports.cn/qrAuth/applyToken
//        请求：{ appId, qrType, steamId, website, redirect }
//        响应：result.accessUrl —— 用它生成二维码供用户扫描；
//              其中查询串里的 accessToken 是后续轮询用的凭据。
//   2. 每 2 秒 POST passport.pwesports.cn/qrAuth/check
//        请求：{ accessToken, appId }
//        响应：code 1008 表示二维码已过期；
//              result.status: 0=待扫描 1=已扫描待确认 2=已确认
//              状态 2 时 result.token（40 位字母数字）才是业务接口要用的
//              access_token，result.accountItem.steamId（17 位数字）是
//              登录账号自身的 SteamID。
//
// 二维码 5 分钟过期，过期后需重新 applyToken。

#include "PerfectModels.h"

#include <winrt/Windows.Foundation.h>

#include <string>

namespace Perfect
{
    // 扫码登录阶段。
    enum class QrLoginPhase
    {
        WaitingScan,    // 等待扫码
        Scanned,        // 已扫描，等待手机确认
        Confirmed,      // 已确认，凭据可用
        Expired,        // 二维码过期，需重新申请
    };

    // 扫码登录结果。
    struct QrLoginResult
    {
        QrLoginPhase phase = QrLoginPhase::WaitingScan;

        // 申请二维码时返回：供生成二维码图片的 URL。
        std::wstring accessUrl;

        // 轮询用的凭据（来自 accessUrl 的 accessToken 查询参数）。
        std::wstring pollToken;

        // 仅 phase == Confirmed 时有效：业务接口要用的登录凭据。
        std::wstring accessToken;   // 40 位字母数字
        std::wstring selfSteamId;   // 17 位数字

        // 失败时的描述（phase != Confirmed 也可能为空，属正常轮询状态）。
        std::wstring message;
    };

    class PerfectAuth
    {
    public:
        PerfectAuth();

        // 申请二维码。成功后 out.result.accessUrl / pollToken 可用，
        // phase 为 WaitingScan。失败时抛 ClientError。
        winrt::Windows::Foundation::IAsyncAction ApplyQrAsync(QrLoginResult& out);

        // 轮询一次扫码状态。
        //   pollToken 来自 ApplyQrAsync。
        // 返回 Confirmed 时，out 携带可用的 accessToken 与 selfSteamId。
        // 网络抖动等异常不应打断整个流程，调用方按返回值决定是否继续轮询。
        winrt::Windows::Foundation::IAsyncAction PollQrAsync(
            std::wstring pollToken,
            QrLoginResult& out);

        // 校验一组既有凭据是否仍然有效（用于启动时自动登录）。
        // 有效返回 true；凭据失效返回 false（此时应引导重新扫码）。
        winrt::Windows::Foundation::IAsyncAction ValidateAsync(
            std::wstring accessToken,
            std::wstring selfSteamId,
            bool& out);
    };
}
