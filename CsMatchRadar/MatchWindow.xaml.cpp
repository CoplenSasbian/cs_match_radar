#include "pch.h"
#include "MatchWindow.xaml.h"
#if __has_include("MatchWindow.g.cpp")
#include "MatchWindow.g.cpp"
#endif

#include "ViewModels.h"
#include "Fmt.h"

#include "PerfectWorldApi.h"
#include "PerfectMatchModels.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

using AppFmt::AccentBrush;
using AppFmt::FormatDouble;
using AppFmt::FormatInt;
using AppFmt::FormatTime;
using AppFmt::H;
using AppFmt::Widen;

namespace winrt::App1::implementation
{
    namespace
    {
        // 回到 UI 线程的 awaitable。
        // WinUI3 的 DispatcherQueue 没有内置 co_await，这里用 TryEnqueue 实现。
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

        constexpr int32_t kPlatformFiveE = 0;
        constexpr int32_t kPlatformPerfect = 1;
    }

    MatchWindow::MatchWindow()
    {
        InitializeComponent();

        // 与主窗口一致：把标题栏区域交给系统绘制拖拽按钮。
        ExtendsContentIntoTitleBar(true);
        if (auto content = Content().try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (auto found = content.FindName(L"MatchTitleBar"))
            {
                // SetTitleBar 不接受 nullptr，传空会触发断言。
                if (auto bar = found.try_as<Microsoft::UI::Xaml::UIElement>())
                {
                    SetTitleBar(bar);
                }
            }
        }
    }

    MatchWindow::~MatchWindow() = default;

    winrt::App1::MatchWindow MatchWindow::Create()
    {
        return make<MatchWindow>();
    }

    void MatchWindow::Init(
        int32_t platform, std::wstring playerId, std::wstring displayName,
        std::wstring token, std::wstring selfSteamId)
    {
        m_platform = platform;
        m_playerId = std::move(playerId);
        m_displayName = std::move(displayName);
        m_token = std::move(token);
        m_selfSteamId = std::move(selfSteamId);

        if (m_platform == kPlatformFiveE)
        {
            PlayerTitle().Text(H(L"5E　" + m_displayName));
            PlayerSubtitle().Text(H(m_playerId));
            MatchList().Visibility(Visibility::Visible);
            PerfectMatchList().Visibility(Visibility::Collapsed);
            SourceBox().Visibility(Visibility::Collapsed);
            m_dataSource = 3;
        }
        else
        {
            PlayerTitle().Text(H(L"完美　" + m_displayName));
            PlayerSubtitle().Text(H(m_playerId));
            MatchList().Visibility(Visibility::Collapsed);
            PerfectMatchList().Visibility(Visibility::Visible);
            SourceBox().Visibility(Visibility::Visible);
        }

        if (m_dispatcher == nullptr)
        {
            m_dispatcher = DispatcherQueue();
        }
    }

    void MatchWindow::SetStatus(hstring const& text)
    {
        if (auto block = StatusText())
        {
            block.Text(text);
        }
    }

    void MatchWindow::SetBusy(bool busy)
    {
        if (auto ring = BusyRing())
        {
            ring.IsActive(busy);
            ring.Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    std::wstring MatchWindow::SelectedCount()
    {
        switch (CountBox().SelectedIndex())
        {
        case 0: return L"10";
        case 2: return L"50";
        default: return L"20";
        }
    }

    int32_t MatchWindow::SelectedDataSource()
    {
        switch (SourceBox().SelectedIndex())
        {
        case 1: return 1;
        case 2: return 2;
        default: return 3;
        }
    }

    void MatchWindow::ClearDetail()
    {
        MatchTitle().Text(L"对局详情");
        MatchSubtitle().Text(L"从左侧选择一场对局");
        MatchStats().Visibility(Visibility::Collapsed);
        DemoButton().Visibility(Visibility::Collapsed);
        TeamAList().ItemsSource(nullptr);
        TeamBList().ItemsSource(nullptr);
    }

    void MatchWindow::OnRefreshClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        LoadAsync();
    }

    void MatchWindow::OnSourceChanged(
        IInspectable const& /* sender */, SelectionChangedEventArgs const& /* args */)
    {
        if (m_platform == kPlatformPerfect && !m_loading)
        {
            LoadAsync();
        }
    }

    void MatchWindow::OnCopyDemoClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        if (m_currentMatchId.empty())
        {
            return;
        }
        try
        {
            auto data = winrt::Windows::ApplicationModel::DataTransfer::DataPackage();
            data.SetText(H(L"https://arena.5eplay.com/match/" + m_currentMatchId));
            winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
            SetStatus(L"Demo 链接已复制。");
        }
        catch (...)
        {
            SetStatus(L"复制失败。");
        }
    }

    // ==================== 加载列表 ====================

    IAsyncAction MatchWindow::LoadAsync()
    {
        if (m_loading)
        {
            co_return;
        }
        m_loading = true;
        SetBusy(true);
        SetStatus(L"正在加载对局列表…");
        ClearDetail();

        std::wstring failure;
        if (m_platform == kPlatformFiveE)
        {
            try
            {
                if (!m_client5e)
                {
                    m_client5e = std::make_unique<FiveE::Client>();
                }
                FiveE::MatchListQuery query;
                query.uuid = m_playerId;
                query.limit = 30;

                std::vector<FiveE::MatchSummary> matches =
                    m_client5e->GetMatchList(query, 5);
                if (matches.size() > static_cast<size_t>(std::stoi(SelectedCount())))
                {
                    matches.resize(static_cast<size_t>(std::stoi(SelectedCount())));
                }
                m_details5e.clear();

                auto items = single_threaded_observable_vector<IInspectable>();
                for (auto const& match : matches)
                {
                    auto row = make<winrt::App1::implementation::MatchRow>();
                    auto* impl = get_self<winrt::App1::implementation::MatchRow>(row);

                    const bool isWin = match.isWin.value_or(false);
                    impl->SetMatchId(H(match.matchId));
                    std::wstring mapLabel = match.mapLabel.empty()
                        ? match.mapName : match.mapLabel;
                    if (mapLabel.empty()) { mapLabel = L"未知地图"; }
                    impl->SetMapLabel(H(mapLabel));
                    impl->SetResultText(isWin ? L"胜利" : L"失败");
                    impl->SetIsWin(isWin);
                    impl->SetResultBrush(AccentBrush(isWin));
                    impl->SetScoreText(H(
                        FormatInt(match.group1Score) + L" : " + FormatInt(match.group2Score)));
                    impl->SetTimeText(H(FormatTime(match.startTime)));
                    impl->SetKdaText(H(
                        FormatInt(match.kills) + L"/" + FormatInt(match.deaths) + L"/" +
                        FormatInt(match.assistCount)));
                    impl->SetRatingText(H(L"Rating " + FormatDouble(match.rating, 2)));
                    impl->SetAdrText(H(L"ADR " + FormatDouble(match.adr, 1)));
                    impl->SetEloText(H(
                        std::wstring(match.changeElo.value_or(0) >= 0 ? L"+" : L"") +
                        std::to_wstring(match.changeElo.value_or(0))));
                    items.Append(row);
                    m_details5e.emplace_back();   // 占位，详情加载时填充
                }

                // 列表行只显示摘要；详情按需加载。
                // 为避免一次性拉 20 场详情，这里仅记录 matchId。
                m_details5e.clear();
                m_details5e.reserve(matches.size());
                for (size_t i = 0; i < matches.size(); ++i)
                {
                    (void)i;
                    m_details5e.emplace_back();
                }

                MatchList().ItemsSource(items);
                SetStatus(H(L"已加载 " + std::to_wstring(items.Size()) + L" 场对局。"));
            }
            catch (FiveE::ApiError const& ex)
            {
                failure = ex.message();
            }
            catch (std::exception const& ex)
            {
                failure = Widen(ex.what() ? ex.what() : "");
            }
            catch (...)
            {
                failure = L"未知错误";
            }
        }
        else
        {
            if (m_token.empty())
            {
                SetBusy(false);
                m_loading = false;
                SetStatus(L"请先在主窗口的“设置”里扫码登录完美平台。");
                co_return;
            }
            try
            {
                wchar_t* raw = nullptr;
                const int32_t pageSize = std::stoi(SelectedCount());
                const auto status = PerfectGetMatchList(
                    m_playerId.c_str(), m_token.c_str(), m_selfSteamId.c_str(),
                    SelectedDataSource(), 1, pageSize, L"recent", &raw);
                if (status != PerfectStatus_Ok || !raw)
                {
                    failure = PerfectGetLastError();
                }
                else
                {
                    const std::wstring json = raw;
                    PerfectFreeString(raw);

                    winrt::Windows::Data::Json::JsonObject root{ nullptr };
                    if (winrt::Windows::Data::Json::JsonObject::TryParse(json, root) && root)
                    {
                        using namespace winrt::Windows::Data::Json;
                        auto getNum = [&](JsonObject const& o, wchar_t const* key) {
                            auto v = o.TryLookup(key);
                            return (v && v.ValueType() == JsonValueType::Number)
                                ? v.GetNumber() : 0.0;
                        };
                        auto getStr = [&](JsonObject const& o, wchar_t const* key) {
                            auto v = o.TryLookup(key);
                            return (v && v.ValueType() == JsonValueType::String)
                                ? std::wstring(v.GetString().c_str()) : std::wstring{};
                        };
                        auto getBool = [&](JsonObject const& o, wchar_t const* key) {
                            auto v = o.TryLookup(key);
                            return (v && v.ValueType() == JsonValueType::Boolean)
                                && v.GetBoolean();
                        };

                        m_matchIdsPerfect.clear();
                        auto items = single_threaded_observable_vector<IInspectable>();
                        if (auto listValue = root.TryLookup(L"list");
                            listValue && listValue.ValueType() == JsonValueType::Array)
                        {
                            for (auto const& element : listValue.GetArray())
                            {
                                if (!element || element.ValueType() != JsonValueType::Object)
                                {
                                    continue;
                                }
                                auto m = element.GetObject();
                                auto row = make<winrt::App1::implementation::PerfectMatchRow>();
                                auto* impl =
                                    get_self<winrt::App1::implementation::PerfectMatchRow>(row);

                                const std::wstring matchId = getStr(m, L"matchId");
                                const int32_t team = static_cast<int32_t>(getNum(m, L"team"));
                                const int32_t winTeam = static_cast<int32_t>(getNum(m, L"winTeam"));
                                const int32_t s1 = static_cast<int32_t>(getNum(m, L"score1"));
                                const int32_t s2 = static_cast<int32_t>(getNum(m, L"score2"));
                                const bool isWin = (team != 0 && team == winTeam);
                                const int32_t mine = (team == 1) ? s1 : s2;
                                const int32_t theirs = (team == 1) ? s2 : s1;
                                const int32_t k4 = static_cast<int32_t>(getNum(m, L"k4"));
                                const int32_t k5 = static_cast<int32_t>(getNum(m, L"k5"));
                                const int32_t duration =
                                    static_cast<int32_t>(getNum(m, L"duration"));
                                const int32_t eloChange =
                                    static_cast<int32_t>(getNum(m, L"pvpScoreChange"));

                                impl->SetMatchId(H(matchId));
                                impl->SetMapLabel(H(getStr(m, L"mapName")));
                                impl->SetResultText(isWin ? L"胜利" : L"失败");
                                impl->SetIsWin(isWin);
                                impl->SetResultBrush(AccentBrush(isWin));
                                impl->SetScoreText(H(FormatInt(mine) + L" : " + FormatInt(theirs)));
                                impl->SetTimeText(H(getStr(m, L"startTime")));
                                impl->SetKdaText(H(
                                    FormatInt(static_cast<int32_t>(getNum(m, L"kill"))) + L"/" +
                                    FormatInt(static_cast<int32_t>(getNum(m, L"death"))) + L"/" +
                                    FormatInt(static_cast<int32_t>(getNum(m, L"assist")))));
                                impl->SetRatingText(H(
                                    L"Rating " + FormatDouble(getNum(m, L"rating"), 2)));
                                impl->SetWeText(H(
                                    L"WE " + FormatDouble(getNum(m, L"we"), 1)));
                                impl->SetEloText(H(
                                    std::wstring(eloChange >= 0 ? L"ELO +" : L"ELO ") +
                                    std::to_wstring(eloChange)));
                                impl->SetModeText(H(getStr(m, L"mode")));
                                impl->SetMvpText(
                                    (getBool(m, L"mvp") || getBool(m, L"pvpMvp"))
                                        ? L"MVP" : L"");
                                impl->SetDurationText(H(duration > 0
                                    ? std::to_wstring(duration) + L" 分钟" : L""));
                                impl->SetScoreChangeText(H(FormatInt(theirs)));
                                {
                                    std::wstring text;
                                    if (k5 > 0) { text += L"5杀×" + std::to_wstring(k5) + L"　"; }
                                    if (k4 > 0) { text += L"4杀×" + std::to_wstring(k4); }
                                    impl->SetMultiKillText(H(text));
                                }

                                items.Append(row);
                                m_matchIdsPerfect.push_back(matchId);
                            }
                        }
                        PerfectMatchList().ItemsSource(items);
                        SetStatus(H(L"已加载 " + std::to_wstring(items.Size()) + L" 场对局。"));
                    }
                    else
                    {
                        failure = L"返回数据解析失败";
                    }
                }
            }
            catch (hresult_error const& ex)
            {
                failure = std::wstring(ex.message().c_str());
            }
            catch (...)
            {
                failure = L"未知错误";
            }
        }

        SetBusy(false);
        m_loading = false;
        if (!failure.empty())
        {
            SetStatus(H(L"加载失败：" + failure));
        }
        co_return;
    }

    // ==================== 5E 详情 ====================

    void MatchWindow::OnMatchSelectionChanged(
        IInspectable const& /* sender */, SelectionChangedEventArgs const& /* args */)
    {
        const int32_t index = MatchList().SelectedIndex();
        if (index < 0)
        {
            return;
        }
        ShowFiveEDetailAsync(static_cast<size_t>(index));
    }

    IAsyncAction MatchWindow::ShowFiveEDetailAsync(size_t index)
    {
        // 列表行未携带 matchId，这里按索引重新取一次列表以拿到 id。
        // 为保持简单，直接让用户点选的行驱动。
        MatchTitle().Text(L"正在加载对局详情…");
        MatchSubtitle().Text(L"");
        TeamAList().ItemsSource(nullptr);
        TeamBList().ItemsSource(nullptr);

        if (m_client5e == nullptr)
        {
            co_return;
        }

        std::wstring failure;
        try
        {
            // 取该场详情。
            auto item = MatchList().SelectedItem();
            std::wstring matchId;
            if (item)
            {
                if (auto row = item.try_as<winrt::App1::MatchRow>())
                {
                    matchId = std::wstring(row.MatchId());
                }
            }
            if (matchId.empty())
            {
                co_return;
            }
            m_currentMatchId = matchId;

            FiveE::MatchDetail detail = m_client5e->GetMatchDetail(matchId);

            MatchTitle().Text(H(detail.mapLabel.empty() ? L"对局详情" : detail.mapLabel));
            std::wstring subtitle = detail.matchId;
            if (!detail.location.empty()) { subtitle += L"　·　" + detail.location; }
            const std::wstring startText = FormatTime(detail.startTime);
            if (!startText.empty()) { subtitle += L"　·　" + startText; }
            if (!detail.demoUrl.empty())
            {
                subtitle += L"　·　Demo 可用";
                DemoButton().Visibility(Visibility::Visible);
            }
            MatchSubtitle().Text(H(subtitle));
            StatScore().Text(H(
                FormatInt(detail.group1Score) + L" : " + FormatInt(detail.group2Score)));
            StatRounds().Text(H(FormatInt(detail.roundTotal)));
            StatDuration().Text(H(AppFmt::FormatDuration(detail.startTime, detail.endTime)));
            MatchStats().Visibility(Visibility::Visible);

            // 分两队。
            std::optional<FiveE::TeamGroup> myGroup;
            for (auto const& player : detail.players)
            {
                (void)player;
            }

            auto teamA = single_threaded_observable_vector<IInspectable>();
            auto teamB = single_threaded_observable_vector<IInspectable>();
            for (auto const& player : detail.players)
            {
                auto row = make<winrt::App1::implementation::PlayerRow>();
                auto* impl = get_self<winrt::App1::implementation::PlayerRow>(row);

                std::wstring name = player.profile.nickname.empty()
                    ? (player.profile.username.empty() ? L"未知玩家" : player.profile.username)
                    : player.profile.nickname;
                impl->SetNickname(H(name));
                impl->SetIsMvp(player.awards.isMvp.value_or(false));
                impl->SetGroupText(player.group == FiveE::TeamGroup::Group1 ? L"组 1" : L"组 2");
                impl->SetKdaText(H(
                    FormatInt(player.kills) + L"/" + FormatInt(player.deaths) + L"/" +
                    FormatInt(player.assists)));
                impl->SetRatingText(H(L"Rating " + FormatDouble(player.rating, 2)));
                impl->SetAdrText(H(L"ADR " + FormatDouble(player.adr, 1)));
                impl->SetRwsText(H(L"RWS " + FormatDouble(player.rws, 1)));
                if (player.kast.has_value())
                {
                    impl->SetKastText(H(L"KAST " + FormatDouble(*player.kast * 100.0, 0) + L"%"));
                }
                if (player.headshotRate.has_value())
                {
                    impl->SetHeadshotText(H(
                        L"爆头 " + FormatDouble(*player.headshotRate * 100.0, 0) + L"%"));
                }

                if (player.group == FiveE::TeamGroup::Group1)
                {
                    teamA.Append(row);
                }
                else
                {
                    teamB.Append(row);
                }
            }
            TeamAHeader().Text(H(L"队伍 A（" + std::to_wstring(teamA.Size()) + L" 人）"));
            TeamBHeader().Text(H(L"队伍 B（" + std::to_wstring(teamB.Size()) + L" 人）"));
            TeamAList().ItemsSource(teamA);
            TeamBList().ItemsSource(teamB);
            if (auto fe = Content().try_as<Microsoft::UI::Xaml::FrameworkElement>())
            {
                if (auto tpl = fe.Resources().TryLookup(winrt::box_value(L"PlayerRowTemplate")).try_as<Microsoft::UI::Xaml::DataTemplate>())
                {
                    TeamAList().ItemTemplate(tpl);
                    TeamBList().ItemTemplate(tpl);
                }
            }
        }
        catch (FiveE::ApiError const& ex)
        {
            failure = ex.message();
        }
        catch (...)
        {
            failure = L"未知错误";
        }

        if (!failure.empty())
        {
            MatchTitle().Text(H(L"详情加载失败：" + failure));
        }
        co_return;
    }

    // ==================== 完美详情 ====================

    void MatchWindow::OnPerfectMatchSelectionChanged(
        IInspectable const& /* sender */, SelectionChangedEventArgs const& /* args */)
    {
        const int32_t index = PerfectMatchList().SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= m_matchIdsPerfect.size())
        {
            return;
        }
        ShowPerfectDetailAsync(m_matchIdsPerfect[static_cast<size_t>(index)]);
    }

    IAsyncAction MatchWindow::ShowPerfectDetailAsync(std::wstring matchId)
    {
        if (matchId.empty() || m_token.empty())
        {
            co_return;
        }

        MatchTitle().Text(L"正在加载对局详情…");
        MatchSubtitle().Text(L"");
        TeamAList().ItemsSource(nullptr);
        TeamBList().ItemsSource(nullptr);
        m_currentMatchId = matchId;

        std::wstring failure;
        auto teamA = single_threaded_observable_vector<IInspectable>();
        auto teamB = single_threaded_observable_vector<IInspectable>();

        try
        {
            wchar_t* raw = nullptr;
            const auto status = PerfectGetMatchDetail(
                matchId.c_str(), SelectedDataSource(),
                m_token.c_str(), m_selfSteamId.c_str(), &raw);
            if (status != PerfectStatus_Ok || !raw)
            {
                failure = PerfectGetLastError();
            }
            else
            {
                const std::wstring json = raw;
                PerfectFreeString(raw);

                winrt::Windows::Data::Json::JsonObject root{ nullptr };
                if (winrt::Windows::Data::Json::JsonObject::TryParse(json, root) && root)
                {
                    using namespace winrt::Windows::Data::Json;
                    auto getNum = [&](JsonObject const& o, wchar_t const* key) {
                        auto v = o.TryLookup(key);
                        return (v && v.ValueType() == JsonValueType::Number)
                            ? v.GetNumber() : 0.0;
                    };
                    auto getStr = [&](JsonObject const& o, wchar_t const* key) {
                        auto v = o.TryLookup(key);
                        return (v && v.ValueType() == JsonValueType::String)
                            ? std::wstring(v.GetString().c_str()) : std::wstring{};
                    };
                    auto getBool = [&](JsonObject const& o, wchar_t const* key) {
                        auto v = o.TryLookup(key);
                        return (v && v.ValueType() == JsonValueType::Boolean)
                            && v.GetBoolean();
                    };

                    if (auto baseValue = root.TryLookup(L"base");
                        baseValue && baseValue.ValueType() == JsonValueType::Object)
                    {
                        auto b = baseValue.GetObject();
                        const std::wstring map = getStr(b, L"map");
                        const int32_t s1 = static_cast<int32_t>(getNum(b, L"score1"));
                        const int32_t s2 = static_cast<int32_t>(getNum(b, L"score2"));
                        const int32_t h1 = static_cast<int32_t>(getNum(b, L"halfScore1"));
                        const int32_t h2 = static_cast<int32_t>(getNum(b, L"halfScore2"));
                        const int32_t duration = static_cast<int32_t>(getNum(b, L"duration"));

                        MatchTitle().Text(H(map.empty() ? L"对局详情" : map));
                        std::wstring subtitle = FormatInt(s1) + L" : " + FormatInt(s2);
                        if (h1 > 0 || h2 > 0)
                        {
                            subtitle += L"　半场 " + FormatInt(h1) + L" : " + FormatInt(h2);
                        }
                        const std::wstring mode = getStr(b, L"mode");
                        if (!mode.empty()) { subtitle += L"　·　" + mode; }
                        const std::wstring start = getStr(b, L"startTime");
                        if (!start.empty()) { subtitle += L"　·　" + start; }
                        if (duration > 0)
                        {
                            subtitle += L"　·　" + std::to_wstring(duration) + L" 分钟";
                        }
                        MatchSubtitle().Text(H(subtitle));
                        StatScore().Text(H(FormatInt(s1) + L" : " + FormatInt(s2)));
                        StatDuration().Text(H(duration > 0
                            ? std::to_wstring(duration) + L" 分钟" : L"—"));
                        MatchStats().Visibility(Visibility::Visible);
                    }

                    if (auto playersValue = root.TryLookup(L"players");
                        playersValue && playersValue.ValueType() == JsonValueType::Array)
                    {
                        for (auto const& element : playersValue.GetArray())
                        {
                            if (!element || element.ValueType() != JsonValueType::Object)
                            {
                                continue;
                            }
                            auto p = element.GetObject();
                            const int32_t team = static_cast<int32_t>(getNum(p, L"team"));
                            const std::wstring nickname = getStr(p, L"nickName");
                            const int32_t kill = static_cast<int32_t>(getNum(p, L"kill"));
                            const int32_t death = static_cast<int32_t>(getNum(p, L"death"));
                            const int32_t assist = static_cast<int32_t>(getNum(p, L"assist"));

                            auto row = make<winrt::App1::implementation::PerfectMatchPlayerRow>();
                            auto* impl =
                                get_self<winrt::App1::implementation::PerfectMatchPlayerRow>(row);

                            impl->SetNickname(H(nickname.empty() ? L"未知玩家" : nickname));
                            impl->SetKdaText(H(
                                FormatInt(kill) + L"/" + FormatInt(death) + L"/" +
                                FormatInt(assist)));
                            impl->SetRatingText(H(
                                L"Rating " + FormatDouble(getNum(p, L"rating"), 2)));
                            impl->SetAdrText(H(
                                L"ADR " + FormatInt(static_cast<int32_t>(getNum(p, L"adpr")))));
                            impl->SetRwsText(H(
                                L"RWS " + FormatDouble(getNum(p, L"rws"), 1)));
                            impl->SetWeText(H(
                                L"WE " + FormatDouble(getNum(p, L"we"), 1)));
                            impl->SetHeadshotText(H(
                                L"爆头 " + FormatDouble(getNum(p, L"headShotRatio") * 100.0, 0) + L"%"));
                            const double kd = death == 0
                                ? static_cast<double>(kill)
                                : static_cast<double>(kill) / death;
                            impl->SetKdText(H(L"K/D " + FormatDouble(kd, 2)));
                            impl->SetIsMvp(getBool(p, L"mvp"));
                            impl->SetMvpText(getBool(p, L"mvp") ? L"MVP" : L"");

                            if (team == 1) { teamA.Append(row); }
                            else { teamB.Append(row); }
                        }
                    }

                    TeamAHeader().Text(H(L"队伍 A（" + std::to_wstring(teamA.Size()) + L" 人）"));
                    TeamBHeader().Text(H(L"队伍 B（" + std::to_wstring(teamB.Size()) + L" 人）"));
                    TeamAList().ItemsSource(teamA);
                    TeamBList().ItemsSource(teamB);
                    if (auto fe = Content().try_as<Microsoft::UI::Xaml::FrameworkElement>())
                    {
                        if (auto tpl = fe.Resources().TryLookup(winrt::box_value(L"PerfectPlayerRowTemplate")).try_as<Microsoft::UI::Xaml::DataTemplate>())
                        {
                            TeamAList().ItemTemplate(tpl);
                            TeamBList().ItemTemplate(tpl);
                        }
                    }
                }
                else
                {
                    failure = L"返回数据解析失败";
                }
            }
        }
        catch (hresult_error const& ex)
        {
            failure = std::wstring(ex.message().c_str());
        }
        catch (...)
        {
            failure = L"未知错误";
        }

        if (!failure.empty())
        {
            MatchTitle().Text(H(L"详情加载失败：" + failure));
        }
        co_return;
    }
}
