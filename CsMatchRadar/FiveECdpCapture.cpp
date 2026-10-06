#include "pch.h"
#include "FiveECdpCapture.h"

#include <shellapi.h>
#include <tlhelp32.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <sstream>
#include <vector>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Data::Json;
using namespace Windows::Security::Cryptography;

namespace LiveCapture
{
    namespace
    {
        constexpr int32_t kDefaultCdpPort = 9222;
        constexpr wchar_t kCometHost[] = L"comet-client-arena.5eplay.com";

        // 是否像 comet 域名（只采集这个）。
        bool IsCometUrl(std::wstring const& url)
        {
            return url.find(kCometHost) != std::wstring::npos;
        }

        // 从字节序列里找到首个 '{' 或 '[' 起的 JSON 片段（参考项目的 find_json_slice）。
        // 有效 JSON 不一定从偏移 0 开始（opcode=2 的二进制帧带协议头）。
        std::wstring ExtractJsonSlice(std::vector<uint8_t> const& bytes)
        {
            const char* begin = reinterpret_cast<const char*>(bytes.data());
            size_t size = bytes.size();
            size_t start = 0;
            for (size_t i = 0; i < size; ++i)
            {
                if (begin[i] == '{' || begin[i] == '[')
                {
                    start = i;
                    break;
                }
            }
            if (start == size) { return L""; }
            // 末尾截断检查：找一个完整的对象/数组边界比较难，这里直接把
            // 剩余字节当文本，JSON 解析器会容忍尾部垃圾（TryParse 失败则返回空）。
            std::string fragment(begin + start, begin + size);
            // 去掉可能的尾部 NUL。
            while (!fragment.empty() && fragment.back() == '\0')
            {
                fragment.pop_back();
            }
            int sizeNeeded = ::MultiByteToWideChar(
                CP_UTF8, 0, fragment.c_str(), -1, nullptr, 0);
            if (sizeNeeded <= 1) { return L""; }
            std::wstring wide(static_cast<size_t>(sizeNeeded - 1), L'\0');
            ::MultiByteToWideChar(
                CP_UTF8, 0, fragment.c_str(), -1, wide.data(), sizeNeeded);
            return wide;
        }

        // Base64 解码为字节（CDP 的 payloadData 是 base64 文本）。
        std::vector<uint8_t> Base64Decode(std::wstring const& text)
        {
            try
            {
                auto buffer = CryptographicBuffer::DecodeFromBase64String(
                    hstring(text));
                auto reader = DataReader::FromBuffer(buffer);
                std::vector<uint8_t> out(buffer.Length());
                if (!out.empty())
                {
                    reader.ReadBytes(out);
                }
                return out;
            }
            catch (...)
            {
                return {};
            }
        }

        // 读 JSON 字符串字段。
        std::wstring JsonStr(JsonObject const& o, wchar_t const* key)
        {
            if (!o || !o.HasKey(key)) { return L""; }
            auto v = o.GetNamedValue(key);
            if (v.ValueType() != JsonValueType::String) { return L""; }
            return std::wstring(v.GetString());
        }

        // 从 game_ctx 里展平出 t1 / t2 各自的 UUID 列表。
        bool CollectTeamUuids(JsonObject const& gmi,
                             std::vector<std::wstring>& t1,
                             std::vector<std::wstring>& t2)
        {
            auto collectFrom = [&](JsonObject const& teamObj, std::vector<std::wstring>& out) {
                auto roomsVal = teamObj.TryLookup(L"rooms");
                if (!roomsVal ||
                    roomsVal.ValueType() != JsonValueType::Array) { return; }
                for (auto const& roomVal : roomsVal.GetArray())
                {
                    if (roomVal.ValueType() != JsonValueType::Object) { continue; }
                    auto membersVal =
                        roomVal.GetObject().TryLookup(L"members");
                    if (!membersVal ||
                        membersVal.ValueType() != JsonValueType::Array) { continue; }
                    for (auto const& m : membersVal.GetArray())
                    {
                        if (m.ValueType() != JsonValueType::String) { continue; }
                        std::wstring uuid = std::wstring(m.GetString());
                        if (!uuid.empty()) { out.push_back(uuid); }
                    }
                }
            };

            auto t1Val = gmi.TryLookup(L"t1");
            auto t2Val = gmi.TryLookup(L"t2");
            if (t1Val.ValueType() == JsonValueType::Object)
            {
                collectFrom(t1Val.GetObject(), t1);
            }
            if (t2Val.ValueType() == JsonValueType::Object)
            {
                collectFrom(t2Val.GetObject(), t2);
            }
            return t1.size() == 5 && t2.size() == 5;
        }

        // 判断字符串是否像合法 GUID（5E 的玩家 UUID 形如
        // "ed954be0-7222-11ee-9ce2-ec0d9a495494"）。
        bool LooksLikeUuid(std::wstring const& s)
        {
            // 粗略：含 4 个 '-' 且长度在 32~38 之间。
            if (s.size() < 32 || s.size() > 38) { return false; }
            return std::count(s.begin(), s.end(), L'-') == 4;
        }

        // 纯 Win32 快照检测进程是否存在，避免依赖 WinRT 权限或程序包标识。
        bool CheckProcessRunning(wchar_t const* processName)
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) { return false; }
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            bool found = false;
            if (Process32FirstW(snap, &pe))
            {
                do
                {
                    if (_wcsicmp(pe.szExeFile, processName) == 0)
                    {
                        found = true;
                        break;
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            return found;
        }
    }

    struct FiveECdpCapture::Impl : std::enable_shared_from_this<Impl>
    {
        FiveECdpCapture::MatchCallback matchCb;
        FiveECdpCapture::StatusCallback statusCb;
        FiveECdpStatus status;
        std::mutex mutex;

        // 当前已定锚的局（用于 game_ctx_update 增量更新）。
        std::wstring anchoredGameId;
        std::wstring anchoredMap;

        MessageWebSocket ws{ nullptr };
        HttpClient http{ nullptr };

        void Report()
        {
            FiveECdpStatus snap;
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
            if (!err.empty()) { status.cdpReachable = false; }
        }

        static winrt::fire_and_forget RunAsync(std::shared_ptr<Impl> self)
        {
            co_await winrt::resume_background();
            if (!self || !self->m_keepRunning) { co_return; }

            {
                std::lock_guard<std::mutex> lock(self->mutex);
                self->status.running = true;
            }
            self->SetPhase(L"launching");
            self->Report();

            // 1. 检测 / 启动 5EClient.exe。
            self->EnsureClient();
            if (!self->m_keepRunning) { co_return; }

            if (!self->status.clientRunning)
            {
                self->SetPhase(L"idle", L"未检测到 5E 客户端（请先启动 5EClient.exe）");
                {
                    std::lock_guard<std::mutex> lock(self->mutex);
                    self->status.running = false;
                }
                self->Report();
                co_return;
            }

            // 2. 轮询 9222 端口直到可达（自更新重启等情况，最多 5 次约 2.5 秒）。
            bool reachable = false;
            for (int i = 0; i < 5 && !reachable && self->m_keepRunning; ++i)
            {
                reachable = co_await self->IsCdpPortReachableAsync();
                if (!reachable && self->m_keepRunning)
                {
                    co_await winrt::resume_after(std::chrono::milliseconds(500));
                }
            }
            if (!self->m_keepRunning) { co_return; }

            if (!reachable)
            {
                self->SetPhase(L"lost", L"CDP 端口不可达（5E 客户端未开启调试端口 9222）");
                {
                    std::lock_guard<std::mutex> lock(self->mutex);
                    self->status.running = false;
                }
                self->Report();
                co_return;
            }
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                self->status.cdpReachable = true;
            }

            // 3. 取一个 page target 的 debugger URL。
            winrt::hstring targetUrl = co_await self->PickDebuggerUrlAsync();
            if (!self->m_keepRunning) { co_return; }

            if (targetUrl.empty())
            {
                self->SetPhase(L"lost", L"无法获取 CDP target");
                self->Report();
                co_return;
            }

            // 4. 连 CDP 并订阅 Network 事件。
            self->SetPhase(L"connecting");
            self->Report();
            co_await self->ConnectAndCaptureAsync(targetUrl);
        }

        void EnsureClient()
        {
            // 先检测是否在运行（纯 Win32 快照，免 WinRT 程序包权限）。
            bool running = CheckProcessRunning(L"5EClient.exe");

            {
                std::lock_guard<std::mutex> lock(mutex);
                status.clientRunning = running;
            }

            if (running) { return; }

            // 未运行：尝试在常见安装目录里找 5EClient.exe 并提权启动。
            // 本机无客户端时这里会失败，但只上报状态、不崩溃。
            std::vector<std::wstring> candidates;
            candidates.push_back(
                std::wstring(LR"(C:\Program Files\5EClient\5EClient.exe)"));
            candidates.push_back(
                std::wstring(LR"(C:\Program Files (x86)\5EClient\5EClient.exe)"));
            std::wstring exePath;
            for (auto const& c : candidates)
            {
                if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    exePath = c;
                    break;
                }
            }
            if (exePath.empty())
            {
                SetPhase(L"idle",
                    L"未检测到 5E 客户端（请先启动 5EClient.exe）");
                Report();
                return;
            }

            // 用 runas 提权启动，追加远程调试端口参数。
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = exePath.c_str();
            std::wstring params = L"--remote-debugging-port=" +
                std::to_wstring(kDefaultCdpPort);
            sei.lpParameters = params.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&sei) && sei.hProcess)
            {
                std::lock_guard<std::mutex> lock(mutex);
                status.clientRunning = true;
                CloseHandle(sei.hProcess);
            }
        }

        winrt::Windows::Foundation::IAsyncOperation<bool> IsCdpPortReachableAsync()
        {
            // 用 HttpClient 打 /json/version，2xx 即可达。
            try
            {
                HttpClient client;
                auto resp = co_await client.GetAsync(
                    Uri(L"http://127.0.0.1:9222/json/version"));
                co_return resp.IsSuccessStatusCode();
            }
            catch (...) { co_return false; }
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PickDebuggerUrlAsync()
        {
            try
            {
                if (!http) { http = HttpClient(); }
                auto resp = co_await http.GetAsync(
                    Uri(L"http://127.0.0.1:9222/json"));
                auto body = co_await resp.Content().ReadAsStringAsync();
                JsonArray arr{ nullptr };
                if (!JsonArray::TryParse(body, arr) || !arr) { co_return winrt::hstring{}; }
                for (auto const& item : arr)
                {
                    if (item.ValueType() != JsonValueType::Object) { continue; }
                    auto o = item.GetObject();
                    auto type = JsonStr(o, L"type");
                    auto wsUrl = JsonStr(o, L"webSocketDebuggerUrl");
                    if (type == L"page" && !wsUrl.empty()) { co_return winrt::hstring(wsUrl); }
                }
            }
            catch (...) { }
            co_return winrt::hstring{};
        }

        winrt::Windows::Foundation::IAsyncAction ConnectAndCaptureAsync(
            winrt::hstring const& url)
        {
            try
            {
                ws = MessageWebSocket();
                ws.Control().MessageType(winrt::Windows::Networking::Sockets::SocketMessageType::Utf8);

                std::weak_ptr<Impl> weak = shared_from_this();
                ws.MessageReceived([weak](IInspectable const&, auto const& args) {
                    auto self = weak.lock();
                    if (!self || !self->m_keepRunning) { return; }
                    try
                    {
                        auto reader = args.GetDataReader();
                        uint32_t len = reader.UnconsumedBufferLength();
                        std::vector<uint8_t> buf(len);
                        if (len > 0) { reader.ReadBytes(buf); }
                        self->OnCdpMessage(buf);
                    }
                    catch (...) { }
                });

                ws.Closed([weak](IInspectable const&, auto const&) {
                    auto self = weak.lock();
                    if (!self) { return; }
                    self->SetPhase(L"lost", L"CDP 连接已断开（5E 可能自更新重启）");
                    self->Report();
                });

                co_await ws.ConnectAsync(Uri(url));

                // 订阅 Network 事件（含 WebSocket 帧）。
                co_await SendCdpAsync(1, L"Network.enable", L"{}");

                SetPhase(L"monitoring");
                Report();

                // 采集循环：保持协程存活，直到 Stop。
                while (m_keepRunning)
                {
                    co_await winrt::resume_after(std::chrono::seconds(2));
                }
            }
            catch (winrt::hresult_error const& e)
            {
                SetPhase(L"lost",
                    std::wstring(L"CDP 连接异常：") + std::wstring(e.message().c_str()));
                Report();
            }
            catch (...)
            {
                SetPhase(L"lost", L"CDP 连接发生未知异常");
                Report();
            }
        }

        winrt::Windows::Foundation::IAsyncAction SendCdpAsync(
            int32_t id, std::wstring const& method, std::wstring const& paramsJson)
        {
            std::wstring payload = L"{\"id\":" + std::to_wstring(id) +
                L",\"method\":\"" + method + L"\",\"params\":" + paramsJson + L"}";
            try
            {
                DataWriter writer;
                writer.WriteString(hstring(payload));
                co_await ws.OutputStream().WriteAsync(writer.DetachBuffer());
            }
            catch (...) { }
        }

        void OnCdpMessage(std::vector<uint8_t> const& bytes)
        {
            std::wstring text;
            // CDP 文本消息本身是 UTF-8 JSON。
            if (!bytes.empty())
            {
                int sizeNeeded = ::MultiByteToWideChar(
                    CP_UTF8, 0,
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<int>(bytes.size()), nullptr, 0);
                if (sizeNeeded > 0)
                {
                    text.resize(static_cast<size_t>(sizeNeeded - 1));
                    ::MultiByteToWideChar(
                        CP_UTF8, 0,
                        reinterpret_cast<const char*>(bytes.data()),
                        static_cast<int>(bytes.size()), text.data(), sizeNeeded);
                }
            }
            if (text.empty()) { return; }

            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(text, root) || !root) { return; }

            // 只关心事件。
            auto methodVal = root.TryLookup(L"method");
            if (!methodVal || methodVal.ValueType() != JsonValueType::String)
            {
                return;
            }
            std::wstring method = std::wstring(methodVal.GetString());
            if (method != L"Network.webSocketFrameReceived") { return; }

            auto paramsVal = root.TryLookup(L"params");
            if (!paramsVal || paramsVal.ValueType() != JsonValueType::Object)
            {
                return;
            }
            auto params = paramsVal.GetObject();

            // 过滤 comet 域名。
            auto respVal = params.TryLookup(L"response");
            std::wstring url;
            if (respVal.ValueType() == JsonValueType::Object)
            {
                url = JsonStr(respVal.GetObject(), L"url");
            }
            if (!IsCometUrl(url)) { return; }

            auto frameVal = params.TryLookup(L"frame");
            if (!frameVal || frameVal.ValueType() != JsonValueType::Object)
            {
                return;
            }
            auto frame = frameVal.GetObject();
            auto opcode = static_cast<int32_t>(frame.GetNamedNumber(L"opcode", 0));
            auto payloadData = JsonStr(frame, L"payloadData");

            // 解帧：opcode=1 文本（可能整段是 base64 再解一层）；
            //       opcode=2 二进制（约 12 字节协议头 + JSON）。
            std::vector<uint8_t> raw;
            if (opcode == 1)
            {
                // 先当普通文本试，再试 base64。
                JsonObject inner{ nullptr };
                if (JsonObject::TryParse(payloadData, inner) && inner)
                {
                    HandleCometObject(inner);
                    return;
                }
                raw = Base64Decode(payloadData);
            }
            else if (opcode == 2)
            {
                raw = Base64Decode(payloadData);
            }
            else { return; }

            if (raw.empty()) { return; }
            std::wstring slice = ExtractJsonSlice(raw);
            JsonObject inner{ nullptr };
            if (JsonObject::TryParse(slice, inner) && inner)
            {
                HandleCometObject(inner);
            }
        }

        void HandleCometObject(JsonObject const& obj)
        {
            // game_ctx（权威定锚）或 game_ctx_update（增量）。
            bool isAnchor = obj.HasKey(L"game_ctx");
            bool isUpdate = obj.HasKey(L"game_ctx_update");
            if (!isAnchor && !isUpdate) { return; }

            JsonObject ctx = isAnchor
                ? obj.GetNamedObject(L"game_ctx")
                : obj.GetNamedObject(L"game_ctx_update");

            // 已经定锚过且这次不是权威帧：只更新地图。
            if (isUpdate && !anchoredGameId.empty())
            {
                auto maybeMap = ctx.TryLookup(L"selected_map");
                if (maybeMap.ValueType() == JsonValueType::Number)
                {
                    anchoredMap = std::to_wstring(
                        static_cast<int32_t>(maybeMap.GetNumber()));
                    // 地图更新目前仅记录，界面由定锚事件驱动。
                }
                return;
            }

            auto idVal = ctx.TryLookup(L"id");
            std::wstring gameId = (idVal.ValueType() == JsonValueType::String)
                ? std::wstring(idVal.GetString()) : L"";

            auto gmiVal = ctx.TryLookup(L"gmi");
            if (gmiVal.ValueType() != JsonValueType::Object) { return; }
            auto gmi = gmiVal.GetObject();

            std::vector<std::wstring> t1, t2;
            if (!CollectTeamUuids(gmi, t1, t2)) { return; }

            // 定锚条件：gameId 非空、两队各 5 人、UUID 合法且唯一。
            if (gameId.empty()) { return; }
            for (auto const& u : t1) if (!LooksLikeUuid(u)) { return; }
            for (auto const& u : t2) if (!LooksLikeUuid(u)) { return; }

            LiveMatch::LiveMatch match;
            match.source = LiveMatch::Platform::FiveE;
            match.gameId = gameId;
            match.mapName = anchoredMap.empty() ? L"Arena" : anchoredMap;
            match.startedAt = anchoredMap; // 占位，采集阶段无精确开始时间

            auto makePlayer = [](std::wstring const& uuid, int32_t team) {
                LiveMatch::RadarPlayer p;
                p.team = team;
                p.fiveEUuid = uuid;
                p.name = uuid; // 暂以 UUID 占位，enrichment 阶段补昵称
                return p;
            };
            for (auto const& u : t1) match.team1.push_back(makePlayer(u, 1));
            for (auto const& u : t2) match.team2.push_back(makePlayer(u, 2));

            if (!match.IsValid()) { return; }

            {
                std::lock_guard<std::mutex> lock(mutex);
                status.anchored += 1;
                status.eventsEmitted += 1;
                anchoredGameId = gameId;
                if (anchoredMap.empty()) { anchoredMap = match.mapName; }
            }
            Report();

            if (matchCb) { matchCb(match); }
        }

        bool m_keepRunning{ false };
    };

    FiveECdpCapture::FiveECdpCapture(MatchCallback matchCb, StatusCallback statusCb)
        : m_impl(std::make_shared<Impl>())
    {
        m_impl->matchCb = std::move(matchCb);
        m_impl->statusCb = std::move(statusCb);
    }

    FiveECdpCapture::~FiveECdpCapture() { Stop(); }

    void FiveECdpCapture::Start()
    {
        if (!m_impl) { return; }
        m_impl->m_keepRunning = true;
        Impl::RunAsync(m_impl);
    }

    void FiveECdpCapture::Stop()
    {
        if (!m_impl) { return; }
        m_impl->m_keepRunning = false;
        try
        {
            if (m_impl->ws) { m_impl->ws.Close(1000, L"stop"); }
        }
        catch (...) { }
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->status.running = false;
    }

    bool FiveECdpCapture::IsRunning() const
    {
        if (!m_impl) { return false; }
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        return m_impl->status.running;
    }
}
