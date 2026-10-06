#pragma once

// ============================================================================
// 5E 实时对局采集：CDP 抓 comet WebSocket
// ============================================================================
//
// 复刻参考项目 qianjiachun/cs-match-helper 的 5E 链路：
//   1. 检测 5EClient.exe 是否运行；没有则用 runas 提权启动
//      （追加 --remote-debugging-port=9222）
//   2. 通过 CDP（HTTP /json 取 target，WebSocket 连 debugger）订阅
//      Network.webSocketFrameReceived
//   3. 只采集 wss://comet-client-arena.5eplay.com/ 的帧
//   4. 从 game_ctx 帧定锚 gameId + 10 个 UUID（两队各 5 人）
//   5. 定锚成功后回调 LiveMatch；之后继续监听 game_ctx_update 更新地图
//
// 注意：本机未安装 5E 客户端，无法实机验证；模块在"端口不可达/无客户端"
// 时只通过状态回调上报，不会崩溃。

#include "LiveMatchModels.h"

#include <functional>
#include <mutex>
#include <string>

namespace LiveCapture
{
    struct FiveECdpStatus
    {
        bool running = false;            // 采集器已启动（不论是否连上）
        bool clientRunning = false;       // 5EClient.exe 进程存在
        bool cdpReachable = false;        // 9222 端口可达
        std::wstring phase;               // idle / launching / connecting / monitoring / lost
        std::wstring lastError;
        int32_t eventsEmitted = 0;        // 收到的 comet 帧数
        int32_t anchored = 0;             // 已定锚的局数
    };

    class FiveECdpCapture
    {
    public:
        using MatchCallback = std::function<void(LiveMatch::LiveMatch const&)>;
        using StatusCallback = std::function<void(FiveECdpStatus const&)>;

        FiveECdpCapture(MatchCallback matchCb, StatusCallback statusCb);
        ~FiveECdpCapture();

        FiveECdpCapture(FiveECdpCapture const&) = delete;
        FiveECdpCapture& operator=(FiveECdpCapture const&) = delete;

        // 启动采集（在后台线程执行，不阻塞调用方）。
        void Start();
        // 停止并释放资源。
        void Stop();
        bool IsRunning() const;

    private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };
}
