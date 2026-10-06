#include "pch.h"
#include "PerfectLogWatcher.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Data.Json.h>

#include <filesystem>
#include <fstream>
#include <locale>
#include <codecvt>
#include <sstream>

#include <shlobj.h>

using namespace winrt;
using namespace Windows::Data::Json;

namespace LiveCapture
{
    namespace
    {
        // 日志目录：%APPDATA%\Wmpvp\Log
        std::wstring WmpvpLogDir()
        {
            wchar_t buf[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf)))
            {
                return std::wstring(buf) + L"\\Wmpvp\\Log";
            }
            return L"";
        }

        // 取当天日志文件（参考项目命名 pvpClient.YYYY-MM-DD.log）。
        std::wstring TodayLogPath()
        {
            auto dir = WmpvpLogDir();
            if (dir.empty()) { return L""; }
            // 用本地日期拼文件名。
            time_t now = time(nullptr);
            tm local{};
            localtime_s(&local, &now);
            wchar_t name[64]{};
            wcsftime(name, _countof(name), L"pvpClient.%Y-%m-%d.log", &local);
            return dir + L"\\" + name;
        }

        // 读一行日志里嵌入的 JSON（形如 ... {json} ...）。
        std::wstring ExtractJsonLine(std::wstring const& line)
        {
            auto open = line.find(L'{');
            auto close = line.rfind(L'}');
            if (open == std::wstring::npos || close == std::wstring::npos ||
                close <= open)
            {
                return L"";
            }
            return line.substr(open, close - open + 1);
        }

        // 从一个可能的"匹配成功"日志对象里解析出 10 名玩家。
        // 完美日志结构各异，这里按常见字段尽力解析：
        //   - 玩家列表可能在 players / members / team1 / team2
        //   - 每名玩家至少要有 steamId 或 nickname
        bool ParseLiveMatch(JsonObject const& obj,
                            LiveMatch::LiveMatch& out)
        {
            // 地图（如有）。
            auto mapVal = obj.TryLookup(L"map");
            if (mapVal.ValueType() == JsonValueType::String)
            {
                out.mapName = std::wstring(mapVal.GetString());
            }

            auto pickPlayer = [](JsonObject const& o,
                                 LiveMatch::RadarPlayer& p) -> bool {
                auto steamVal = o.TryLookup(L"steamId");
                auto nameVal = o.TryLookup(L"nickName");
                auto name2 = o.TryLookup(L"nickname");
                auto name3 = o.TryLookup(L"name");
                if (steamVal.ValueType() == JsonValueType::String)
                {
                    p.perfectSteamId = std::wstring(steamVal.GetString());
                }
                for (auto v : { nameVal, name2, name3 })
                {
                    if (v.ValueType() == JsonValueType::String &&
                        p.name.empty())
                    {
                        p.name = std::wstring(v.GetString());
                    }
                }
                return !p.perfectSteamId.empty() || !p.name.empty();
            };

            // 直接 team1 / team2 分组（最常见）。
            auto readTeam = [&](wchar_t const* key, int32_t team,
                                std::vector<LiveMatch::RadarPlayer>& dst) {
                auto tv = obj.TryLookup(key);
                if (tv.ValueType() != JsonValueType::Array) { return; }
                for (auto const& ev : tv.GetArray())
                {
                    if (ev.ValueType() != JsonValueType::Object) { continue; }
                    LiveMatch::RadarPlayer p;
                    p.team = team;
                    if (pickPlayer(ev.GetObject(), p)) { dst.push_back(p); }
                }
            };
            readTeam(L"team1", 1, out.team1);
            readTeam(L"team2", 2, out.team2);

            // 退化：只有一个 players 数组，按前 5 / 后 5 分（不能保证队伍正确）。
            if (out.team1.empty() && out.team2.empty())
            {
                auto pv = obj.TryLookup(L"players");
                if (pv.ValueType() == JsonValueType::Array)
                {
                    int idx = 0;
                    for (auto const& ev : pv.GetArray())
                    {
                        if (ev.ValueType() != JsonValueType::Object) { continue; }
                        LiveMatch::RadarPlayer p;
                        p.team = (idx < 5) ? 1 : 2;
                        if (pickPlayer(ev.GetObject(), p)) { idx < 5 ? out.team1.push_back(p) : out.team2.push_back(p); }
                        ++idx;
                    }
                }
            }

            if (!out.mapName.empty()) { out.startedAt = out.mapName; }
            return out.IsValid();
        }
    }

    struct PerfectLogWatcher::Impl
    {
        PerfectLogWatcher::MatchCallback matchCb;
        PerfectLogWatcher::StatusCallback statusCb;
        PerfectWatcherStatus status;
        std::mutex mutex;

        std::wstring currentPath;
        std::ifstream file;
        std::wstring lastMatchKey;          // 去重：同一局只回调一次

        void Report()
        {
            PerfectWatcherStatus snap;
            {
                std::lock_guard<std::mutex> lock(mutex);
                snap = status;
            }
            if (statusCb) { statusCb(snap); }
        }

        void SetPhase(std::wstring const& phase, std::wstring const& err = L"")
        {
            std::lock_guard<std::mutex> lock(mutex);
            status.phase = phase;
            status.lastError = err;
        }

        bool OpenToday()
        {
            auto path = TodayLogPath();
            std::lock_guard<std::mutex> lock(mutex);
            status.logPath = path;
            if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                status.logExists = false;
                status.fileSize = 0;
                return false;
            }
            // 以二进制打开，定位到末尾。
            file.open(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                status.logExists = false;
                return false;
            }
            status.logExists = true;
            status.fileSize = static_cast<int64_t>(file.tellg());
            file.seekg(0, std::ios::end);
            currentPath = path;
            return true;
        }

        // 轮询一次：读取新增字节，按行解析。
        void PollOnce()
        {
            // 今天日志切换时重新打开。
            auto today = TodayLogPath();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (today != currentPath)
                {
                    if (file.is_open()) { file.close(); }
                }
            }
            if (!file.is_open())
            {
                if (!OpenToday()) { return; }
            }

            // 读增量。
            file.seekg(0, std::ios::end);
            const int64_t end = static_cast<int64_t>(file.tellg());
            int64_t pos = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                pos = status.fileSize;
            }
            if (end < pos) { pos = 0; }     // 文件被截断（重开）
            if (end == pos) { return; }

            file.seekg(pos, std::ios::beg);
            std::vector<char> buf(static_cast<size_t>(end - pos));
            file.read(buf.data(), static_cast<std::streamsize>(end - pos));

            // 转 UTF-8 文本（日志通常为 UTF-8）。
            std::wstring content;
            {
                int need = ::MultiByteToWideChar(
                    CP_UTF8, 0, buf.data(), static_cast<int>(buf.size()),
                    nullptr, 0);
                if (need > 0)
                {
                    content.resize(static_cast<size_t>(need - 1));
                    ::MultiByteToWideChar(
                        CP_UTF8, 0, buf.data(), static_cast<int>(buf.size()),
                        content.data(), need);
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                status.fileSize = end;
            }
            status.linesReceived += static_cast<int>(
                std::count(content.begin(), content.end(), L'\n'));

            // 逐行解析。
            std::wstringstream ss(content);
            std::wstring line;
            while (std::getline(ss, line))
            {
                std::wstring json = ExtractJsonLine(line);
                if (json.empty()) { continue; }
                JsonObject obj{ nullptr };
                if (!JsonObject::TryParse(json, obj) || !obj) { continue; }
                // 只关心含 team1/team2 的对局定锚行。
                if (!obj.HasKey(L"team1") && !obj.HasKey(L"team2") &&
                    !obj.HasKey(L"players"))
                {
                    continue;
                }
                LiveMatch::LiveMatch match;
                match.source = LiveMatch::Platform::Perfect;
                if (ParseLiveMatch(obj, match) && match.IsValid())
                {
                    std::wstring key = match.mapName;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (key == lastMatchKey) { continue; }
                        lastMatchKey = key;
                        status.anchored += 1;
                    }
                    if (matchCb) { matchCb(match); }
                }
            }
        }

        static winrt::fire_and_forget RunAsync(std::shared_ptr<Impl> self)
        {
            co_await winrt::resume_background();
            while (self && self->m_keepRunning)
            {
                try { self->PollOnce(); }
                catch (...) { }
                co_await winrt::resume_after(std::chrono::seconds(2));
            }
        }

        bool m_keepRunning{ false };
    };

    PerfectLogWatcher::PerfectLogWatcher(MatchCallback matchCb, StatusCallback statusCb)
        : m_impl(std::make_shared<Impl>())
    {
        m_impl->matchCb = std::move(matchCb);
        m_impl->statusCb = std::move(statusCb);
    }

    PerfectLogWatcher::~PerfectLogWatcher() { Stop(); }

    void PerfectLogWatcher::Start()
    {
        if (!m_impl) { return; }
        m_impl->m_keepRunning = true;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            m_impl->status.running = true;
            m_impl->status.phase = L"monitoring";
        }
        Impl::RunAsync(m_impl);
    }

    void PerfectLogWatcher::Stop()
    {
        if (!m_impl) { return; }
        m_impl->m_keepRunning = false;
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->status.running = false;
    }
}
