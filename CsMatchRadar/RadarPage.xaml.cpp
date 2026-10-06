#include "pch.h"
#include "RadarPage.xaml.h"
#if __has_include("RadarPage.g.cpp")
#include "RadarPage.g.cpp"
#endif

#include "ViewModels.h"
#include "Fmt.h"
#include "CredentialStore.h"

#include "FiveEApi.hpp"
#include "PerfectApi.hpp"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <sstream>
#include <string>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

using AppFmt::FormatInt;
using AppFmt::H;

namespace winrt::App1::implementation
{
    namespace
    {
        // 回到 UI 线程的 awaitable（与 MatchWindow 同款实现）。
        struct ResumeUi
        {
            Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) const
            {
                if (!queue)
                {
                    handle.resume();
                    return;
                }
                auto* raw = new std::coroutine_handle<>(handle);
                const bool queued = queue.TryEnqueue(
                    Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                    [raw]() {
                        auto h = *raw;
                        delete raw;
                        h.resume();
                    });
                if (!queued)
                {
                    delete raw;
                    handle.resume();
                }
            }

            void await_resume() const noexcept {}
        };

    }

    RadarPage::RadarPage()
    {
        InitializeComponent();

        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!m_dispatcher)
        {
            m_dispatcher = DispatcherQueue();
        }

        m_team1 = winrt::single_threaded_observable_vector<winrt::App1::RadarPlayer>();
        m_team2 = winrt::single_threaded_observable_vector<winrt::App1::RadarPlayer>();
        if (auto list = Team1List()) { list.ItemsSource(m_team1); }
        if (auto list = Team2List()) { list.ItemsSource(m_team2); }

        // 加载完美凭据（供 enrich 时使用）。
        AppCred::StoredCredentials cred;
        if (AppCred::Load(cred))
        {
            m_perfectToken = cred.accessToken;
            m_perfectSelf = cred.selfSteamId;
            m_hasAuth = !cred.accessToken.empty();
        }

        SetStatusText(L"等待监控。点击“重新扫描”启动实时抓取（需安装 5E / 完美客户端）。");
    }

    void RadarPage::OnLiveMatch(LiveMatch::LiveMatch const& match)
    {
        // 来自后台采集线程：切换到 UI 线程更新状态并启动富集
        if (m_dispatcher)
        {
            m_dispatcher.TryEnqueue([this, match]() {
                SetBusy(true);
                SetStatusText(L"检测到对局，正在获取玩家详细数据…");
                LiveMatch::LiveMatch copy = match;
                EnrichAndShowAsync(std::move(copy));
            });
        }
        else
        {
            LiveMatch::LiveMatch copy = match;
            EnrichAndShowAsync(std::move(copy));
        }
    }

    void RadarPage::OnCaptureStatus(std::wstring const& text, bool /* isError */)
    {
        if (m_dispatcher)
        {
            m_dispatcher.TryEnqueue([this, text]() {
                if (auto t = MonitorStatus())
                {
                    t.Text(winrt::hstring(text));
                }
            });
        }
    }

    winrt::fire_and_forget RadarPage::EnrichAndShowAsync(LiveMatch::LiveMatch match)
    {
        auto strong = get_strong();
        // 切到后台线程做阻塞式 HTTP 查询，避免卡 UI。
        co_await winrt::resume_background();

        for (auto& p : match.team1) { EnrichOne(p); }
        for (auto& p : match.team2) { EnrichOne(p); }

        co_await ResumeUi{ m_dispatcher };
        ShowMatch(match);
        SetBusy(false);
        SetStatusText(L"对局数据已更新。");
    }

    // 单个玩家富集：5E 查 player/home，完美查 player。失败则保持 available=false。
    // 调用方须已在后台线程上（EnrichAndShowAsync 已 resume_background）。
    void RadarPage::EnrichOne(LiveMatch::RadarPlayer& p)
    {
        // ---- 5E ----
        if (!p.fiveEUuid.empty())
        {
            try
            {
                FiveE::Client client;
                auto home = client.GetPlayerProfile(p.fiveEUuid);
                if (home.HasData())
                {
                    p.fiveE.available = true;
                    if (p.name.empty() || p.name == p.fiveEUuid)
                    {
                        p.name = !home.identity.nickname.empty()
                            ? home.identity.nickname : home.identity.username;
                    }
                    double elo = 0; bool hasElo = false;
                    if (home.rank.elo.has_value()) { elo = *home.rank.elo; hasElo = true; }
                    else if (home.career.elo.has_value()) { elo = *home.career.elo; hasElo = true; }
                    if (hasElo) { p.fiveE.rankText = FormatInt(static_cast<int>(elo)); p.fiveE.rankScore = static_cast<int32_t>(elo); }
                    int64_t pt = home.career.matchTimeTotal.value_or(
                        home.season.matchTimeTotal.value_or(0));
                    if (pt > 0) { p.fiveE.playtimeSeconds = pt; }
                    p.fiveE.risk = LiveMatch::FiveECreditToRisk(
                        home.credit.credit, home.credit.trustedStatus);
                    p.fiveE.riskText = LiveMatch::RiskBadgeText(
                        static_cast<LiveMatch::RiskLevel>(static_cast<int>(p.fiveE.risk)));
                }
            }
            catch (...) { /* 查询失败：保留 available=false */ }
        }

        // ---- 完美 ----
        if (!p.perfectSteamId.empty() && m_hasAuth)
        {
            try
            {
                Perfect::Client client;
                Perfect::PerfectCredentials pc{ m_perfectToken, m_perfectSelf };
                auto player = client.GetPlayer(p.perfectSteamId, pc);
                if (player.HasData())
                {
                    p.perfect.available = true;
                    if (p.name.empty() || p.name == p.perfectSteamId)
                    {
                        p.name = !player.identity.name.empty()
                            ? player.identity.name : player.identity.nickname;
                    }
                    p.perfect.rankText = player.currentRank.Display();
                    if (player.currentRank.score.has_value())
                    {
                        p.perfect.rankScore = *player.currentRank.score;
                    }
                    // 完美资料未暴露游玩总时长字段，留空。
                    p.perfect.risk = LiveMatch::PlatformStanding::Risk::Unknown;
                    p.perfect.riskText = L"—";
                }
            }
            catch (...) { /* 查询失败：保留 available=false */ }
        }
    }

    void RadarPage::ShowMatch(LiveMatch::LiveMatch const& match)
    {
        m_team1.Clear();
        m_team2.Clear();
        for (auto const& p : match.team1) { m_team1.Append(ToRadarRow(p)); }
        for (auto const& p : match.team2) { m_team2.Append(ToRadarRow(p)); }

        if (auto h = Team1Header())
        {
            h.Text(H(L"队伍 1（" + std::to_wstring(match.team1.size()) + L" 人）"));
        }
        if (auto h = Team2Header())
        {
            h.Text(H(L"队伍 2（" + std::to_wstring(match.team2.size()) + L" 人）"));
        }

        if (auto src = SourceText())
        {
            std::wstring label = L"未监控";
            if (match.source == LiveMatch::Platform::FiveE) { label = L"5E 实时"; }
            else if (match.source == LiveMatch::Platform::Perfect) { label = L"完美 实时"; }
            else { label = L"演示"; }
            src.Text(winrt::hstring(label));
        }
        if (auto mp = MapText())
        {
            mp.Text(H(L"地图：" + (match.mapName.empty() ? L"—" : match.mapName)));
        }
        SetBusy(false);
    }

    void RadarPage::OnRefreshClick(
        Windows::Foundation::IInspectable const& /* sender */,
        Microsoft::UI::Xaml::RoutedEventArgs const& /* args */)
    {
        StartCapture();
    }

    void RadarPage::StartCapture()
    {
        StopCapture();

        SetBusy(true);
        SetStatusText(L"正在启动实时抓取（5E CDP + 完美日志）…");

        if (!m_fiveECapture)
        {
            m_fiveECapture = std::make_shared<LiveCapture::FiveECdpCapture>(
                [this](LiveMatch::LiveMatch const& m) { OnLiveMatch(m); },
                [this](LiveCapture::FiveECdpStatus const& s) {
                    std::wstring msg = L"5E：" + s.phase;
                    if (!s.lastError.empty()) { msg += L"（" + s.lastError + L"）"; }
                    OnCaptureStatus(msg, !s.lastError.empty());
                });
        }
        if (!m_perfectWatcher)
        {
            m_perfectWatcher = std::make_shared<LiveCapture::PerfectLogWatcher>(
                [this](LiveMatch::LiveMatch const& m) { OnLiveMatch(m); },
                [this](LiveCapture::PerfectWatcherStatus const& s) {
                    std::wstring msg = L"完美：" + s.phase;
                    if (!s.lastError.empty()) { msg += L"（" + s.lastError + L"）"; }
                    OnCaptureStatus(msg, !s.lastError.empty());
                });
        }

        m_fiveECapture->Start();
        m_perfectWatcher->Start();

        // 采集器进入后台被动监听，1 秒缓冲后解除忙碌状态，允许用户正常浏览与操作
        [](winrt::com_ptr<RadarPage> self) -> winrt::fire_and_forget {
            co_await winrt::resume_after(std::chrono::milliseconds(1000));
            co_await ResumeUi{ self->m_dispatcher };
            self->SetBusy(false);
            self->SetStatusText(L"实时监控已启动，检测到对局时将自动显示玩家数据。");
        }(this->get_strong());
    }

    void RadarPage::StopCapture()
    {
        if (m_fiveECapture) { m_fiveECapture->Stop(); }
        if (m_perfectWatcher) { m_perfectWatcher->Stop(); }
        SetBusy(false);
    }

    void RadarPage::SetStatusText(std::wstring const& text)
    {
        if (auto t = StatusText()) { t.Text(winrt::hstring(text)); }
    }

    void RadarPage::SetBusy(bool busy)
    {
        if (auto ring = BusyRing())
        {
            ring.IsActive(busy);
            ring.Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        }
    }
}
