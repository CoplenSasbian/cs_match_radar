#pragma once

// ============================================================================
// 完美世界实时对局采集：tail 本地日志
// ============================================================================
//
// 复刻参考项目：完美平台对局进行时会向
//   %APPDATA%\Wmpvp\Log\pvpClient.YYYY-MM-DD.log
// 写入结构化日志行（含当前对局的玩家与地图）。我们轮询 tail 这个文件，
// 解析出"正在进行"的对局，归一化成 LiveMatch。
//
// 本机未安装完美客户端时，日志目录不存在，模块只通过状态回调上报，
// 不会崩溃。

#include "LiveMatchModels.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace LiveCapture
{
    struct PerfectWatcherStatus
    {
        bool running = false;
        bool logExists = false;            // 日志目录/文件是否存在
        std::wstring logPath;              // 当前 tail 的文件
        int64_t fileSize = 0;              // 当前文件大小
        int32_t linesReceived = 0;
        std::wstring phase;                // idle / monitoring / lost
        std::wstring lastError;
        int32_t anchored = 0;
    };

    class PerfectLogWatcher
    {
    public:
        using MatchCallback = std::function<void(LiveMatch::LiveMatch const&)>;
        using StatusCallback = std::function<void(PerfectWatcherStatus const&)>;

        PerfectLogWatcher(MatchCallback matchCb, StatusCallback statusCb);
        ~PerfectLogWatcher();

        PerfectLogWatcher(PerfectLogWatcher const&) = delete;
        PerfectLogWatcher& operator=(PerfectLogWatcher const&) = delete;

        void Start();
        void Stop();

    private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };
}
