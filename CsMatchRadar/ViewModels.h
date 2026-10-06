#pragma once

#include "MatchRow.g.h"
#include "PlayerRow.g.h"
#include "PerfectMatchRow.g.h"
#include "PerfectMatchPlayerRow.g.h"
#include "RadarPlayer.g.h"

#include "LiveMatchModels.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

// 界面用的只读行视图。
// 这些类型把 FiveE / 完美平台的模型转换成已格式化的展示文本，
// 让 XAML 可以直接用 {Binding} 绑定，避免在 XAML 里做格式化。

namespace winrt::App1::implementation
{
    // 完美平台「最近对局」列表行。
    struct PerfectMatchRow : PerfectMatchRowT<PerfectMatchRow>
    {
        PerfectMatchRow() = default;

        hstring MatchId() const { return m_matchId; }
        hstring MapLabel() const { return m_mapLabel; }
        hstring ResultText() const { return m_resultText; }
        hstring ScoreText() const { return m_scoreText; }
        hstring TimeText() const { return m_timeText; }
        hstring KdaText() const { return m_kdaText; }
        hstring RatingText() const { return m_ratingText; }
        hstring WeText() const { return m_weText; }
        hstring EloText() const { return m_eloText; }
        hstring ModeText() const { return m_modeText; }
        hstring MvpText() const { return m_mvpText; }
        hstring DurationText() const { return m_durationText; }
        hstring ScoreChangeText() const { return m_scoreChangeText; }
        hstring MultiKillText() const { return m_multiKillText; }
        bool IsWin() const { return m_isWin; }
        Microsoft::UI::Xaml::Media::Brush ResultBrush() const { return m_resultBrush; }

        void SetMatchId(hstring const& v) { m_matchId = v; }
        void SetMapLabel(hstring const& v) { m_mapLabel = v; }
        void SetResultText(hstring const& v) { m_resultText = v; }
        void SetScoreText(hstring const& v) { m_scoreText = v; }
        void SetTimeText(hstring const& v) { m_timeText = v; }
        void SetKdaText(hstring const& v) { m_kdaText = v; }
        void SetRatingText(hstring const& v) { m_ratingText = v; }
        void SetWeText(hstring const& v) { m_weText = v; }
        void SetEloText(hstring const& v) { m_eloText = v; }
        void SetModeText(hstring const& v) { m_modeText = v; }
        void SetMvpText(hstring const& v) { m_mvpText = v; }
        void SetDurationText(hstring const& v) { m_durationText = v; }
        void SetScoreChangeText(hstring const& v) { m_scoreChangeText = v; }
        void SetMultiKillText(hstring const& v) { m_multiKillText = v; }
        void SetIsWin(bool v) { m_isWin = v; }
        void SetResultBrush(Microsoft::UI::Xaml::Media::Brush const& v) { m_resultBrush = v; }

    private:
        hstring m_matchId;
        hstring m_mapLabel;
        hstring m_resultText;
        hstring m_scoreText;
        hstring m_timeText;
        hstring m_kdaText;
        hstring m_ratingText;
        hstring m_weText;
        hstring m_eloText;
        hstring m_modeText;
        hstring m_mvpText;
        hstring m_durationText;
        hstring m_scoreChangeText;
        hstring m_multiKillText;
        bool m_isWin{ false };
        Microsoft::UI::Xaml::Media::Brush m_resultBrush{ nullptr };
    };

    // 完美平台对局详情中的玩家行。
    struct PerfectMatchPlayerRow : PerfectMatchPlayerRowT<PerfectMatchPlayerRow>
    {
        PerfectMatchPlayerRow() = default;

        hstring Nickname() const { return m_nickname; }
        hstring KdaText() const { return m_kdaText; }
        hstring RatingText() const { return m_ratingText; }
        hstring AdrText() const { return m_adrText; }
        hstring RwsText() const { return m_rwsText; }
        hstring WeText() const { return m_weText; }
        hstring HeadshotText() const { return m_headshotText; }
        hstring KdText() const { return m_kdText; }
        hstring MvpText() const { return m_mvpText; }
        bool IsMvp() const { return m_isMvp; }
        Microsoft::UI::Xaml::Visibility MvpVisibility() const
        {
            return m_isMvp ? Microsoft::UI::Xaml::Visibility::Visible
                           : Microsoft::UI::Xaml::Visibility::Collapsed;
        }

        void SetNickname(hstring const& v) { m_nickname = v; }
        void SetKdaText(hstring const& v) { m_kdaText = v; }
        void SetRatingText(hstring const& v) { m_ratingText = v; }
        void SetAdrText(hstring const& v) { m_adrText = v; }
        void SetRwsText(hstring const& v) { m_rwsText = v; }
        void SetWeText(hstring const& v) { m_weText = v; }
        void SetHeadshotText(hstring const& v) { m_headshotText = v; }
        void SetKdText(hstring const& v) { m_kdText = v; }
        void SetMvpText(hstring const& v) { m_mvpText = v; }
        void SetIsMvp(bool v) { m_isMvp = v; }

    private:
        hstring m_nickname;
        hstring m_kdaText;
        hstring m_ratingText;
        hstring m_adrText;
        hstring m_rwsText;
        hstring m_weText;
        hstring m_headshotText;
        hstring m_kdText;
        hstring m_mvpText;
        bool m_isMvp{ false };
    };

    struct MatchRow : MatchRowT<MatchRow>
    {
        MatchRow() = default;

        hstring MatchId() const { return m_matchId; }
        hstring MapLabel() const { return m_mapLabel; }
        hstring ResultText() const { return m_resultText; }
        hstring ScoreText() const { return m_scoreText; }
        hstring TimeText() const { return m_timeText; }
        hstring KdaText() const { return m_kdaText; }
        hstring RatingText() const { return m_ratingText; }
        hstring AdrText() const { return m_adrText; }
        hstring RwsText() const { return m_rwsText; }
        hstring EloText() const { return m_eloText; }
        hstring LevelText() const { return m_levelText; }
        hstring DurationText() const { return m_durationText; }
        bool IsWin() const { return m_isWin; }
        // 供 XAML x:Bind 使用的胜负色条画刷。
        Microsoft::UI::Xaml::Media::Brush ResultBrush() const { return m_resultBrush; }

        // 由实现代码（非 XAML）调用，填充展示字段。
        void SetMatchId(hstring const& value) { m_matchId = value; }
        void SetMapLabel(hstring const& value) { m_mapLabel = value; }
        void SetResultText(hstring const& value) { m_resultText = value; }
        void SetScoreText(hstring const& value) { m_scoreText = value; }
        void SetTimeText(hstring const& value) { m_timeText = value; }
        void SetKdaText(hstring const& value) { m_kdaText = value; }
        void SetRatingText(hstring const& value) { m_ratingText = value; }
        void SetAdrText(hstring const& value) { m_adrText = value; }
        void SetRwsText(hstring const& value) { m_rwsText = value; }
        void SetEloText(hstring const& value) { m_eloText = value; }
        void SetLevelText(hstring const& value) { m_levelText = value; }
        void SetDurationText(hstring const& value) { m_durationText = value; }
        void SetIsWin(bool value) { m_isWin = value; }
        void SetResultBrush(Microsoft::UI::Xaml::Media::Brush const& value) { m_resultBrush = value; }

    private:
        hstring m_matchId;
        hstring m_mapLabel;
        hstring m_resultText;
        hstring m_scoreText;
        hstring m_timeText;
        hstring m_kdaText;
        hstring m_ratingText;
        hstring m_adrText;
        hstring m_rwsText;
        hstring m_eloText;
        hstring m_levelText;
        hstring m_durationText;
        bool m_isWin{ false };
        Microsoft::UI::Xaml::Media::Brush m_resultBrush{ nullptr };
    };

    struct PlayerRow : PlayerRowT<PlayerRow>
    {
        PlayerRow() = default;

        hstring Nickname() const { return m_nickname; }
        hstring GroupText() const { return m_groupText; }
        hstring KdaText() const { return m_kdaText; }
        hstring KdText() const { return m_kdText; }
        hstring AdrText() const { return m_adrText; }
        hstring RatingText() const { return m_ratingText; }
        hstring RwsText() const { return m_rwsText; }
        hstring KastText() const { return m_kastText; }
        hstring HeadshotText() const { return m_headshotText; }
        hstring AwpText() const { return m_awpText; }
        hstring MvpText() const { return m_mvpText; }
        hstring DurationText() const { return m_durationText; }
        hstring LevelText() const { return m_levelText; }
        hstring SteamId() const { return m_steamId; }
        hstring SteamUrl() const { return m_steamUrl; }
        bool HasSteam() const { return m_hasSteam; }
        bool IsMvp() const { return m_isMvp; }
        bool IsMyTeam() const { return m_isMyTeam; }
        // 供 XAML x:Bind 控制 MVP 角标显示。
        Microsoft::UI::Xaml::Visibility MvpVisibility() const
        {
            return m_isMvp ? Microsoft::UI::Xaml::Visibility::Visible
                           : Microsoft::UI::Xaml::Visibility::Collapsed;
        }
        // 没有 Steam 绑定时隐藏整块 Steam 信息。
        Microsoft::UI::Xaml::Visibility SteamVisibility() const
        {
            return m_hasSteam ? Microsoft::UI::Xaml::Visibility::Visible
                              : Microsoft::UI::Xaml::Visibility::Collapsed;
        }

        // ---- 展开后的完整信息 ----
        hstring MultiKillText() const { return m_multiKillText; }
        hstring Kills1Text() const { return m_kills1Text; }
        hstring ClutchText() const { return m_clutchText; }
        hstring ClutchDetailText() const { return m_clutchDetailText; }
        hstring FirstKillText() const { return m_firstKillText; }
        hstring BombText() const { return m_bombText; }
        hstring FlashText() const { return m_flashText; }
        hstring UtilityText() const { return m_utilityText; }
        hstring SwingText() const { return m_swingText; }
        hstring EloText() const { return m_eloText; }
        hstring RankText() const { return m_rankText; }
        hstring HeadshotCountText() const { return m_headshotCountText; }
        hstring SideTText() const { return m_sideTText; }
        hstring SideCtText() const { return m_sideCtText; }

        void SetNickname(hstring const& value) { m_nickname = value; }
        void SetGroupText(hstring const& value) { m_groupText = value; }
        void SetKdaText(hstring const& value) { m_kdaText = value; }
        void SetKdText(hstring const& value) { m_kdText = value; }
        void SetAdrText(hstring const& value) { m_adrText = value; }
        void SetRatingText(hstring const& value) { m_ratingText = value; }
        void SetRwsText(hstring const& value) { m_rwsText = value; }
        void SetKastText(hstring const& value) { m_kastText = value; }
        void SetHeadshotText(hstring const& value) { m_headshotText = value; }
        void SetAwpText(hstring const& value) { m_awpText = value; }
        void SetMvpText(hstring const& value) { m_mvpText = value; }
        void SetLevelText(hstring const& value) { m_levelText = value; }
        void SetSteamId(hstring const& value) { m_steamId = value; }
        void SetSteamUrl(hstring const& value) { m_steamUrl = value; }
        void SetHasSteam(bool value) { m_hasSteam = value; }
        void SetIsMvp(bool value) { m_isMvp = value; }
        void SetIsMyTeam(bool value) { m_isMyTeam = value; }

        void SetMultiKillText(hstring const& v) { m_multiKillText = v; }
        void SetKills1Text(hstring const& v) { m_kills1Text = v; }
        void SetClutchText(hstring const& v) { m_clutchText = v; }
        void SetClutchDetailText(hstring const& v) { m_clutchDetailText = v; }
        void SetFirstKillText(hstring const& v) { m_firstKillText = v; }
        void SetBombText(hstring const& v) { m_bombText = v; }
        void SetFlashText(hstring const& v) { m_flashText = v; }
        void SetUtilityText(hstring const& v) { m_utilityText = v; }
        void SetSwingText(hstring const& v) { m_swingText = v; }
        void SetEloText(hstring const& v) { m_eloText = v; }
        void SetRankText(hstring const& v) { m_rankText = v; }
        void SetHeadshotCountText(hstring const& v) { m_headshotCountText = v; }
        void SetSideTText(hstring const& v) { m_sideTText = v; }
        void SetSideCtText(hstring const& v) { m_sideCtText = v; }

    private:
        hstring m_nickname;
        hstring m_groupText;
        hstring m_kdaText;
        hstring m_kdText;
        hstring m_adrText;
        hstring m_ratingText;
        hstring m_rwsText;
        hstring m_kastText;
        hstring m_headshotText;
        hstring m_awpText;
        hstring m_mvpText;
        hstring m_durationText;
        hstring m_levelText;
        hstring m_steamId;
        hstring m_steamUrl;
        bool m_hasSteam{ false };
        bool m_isMvp{ false };
        bool m_isMyTeam{ false };

        hstring m_multiKillText;
        hstring m_kills1Text;
        hstring m_clutchText;
        hstring m_clutchDetailText;
        hstring m_firstKillText;
        hstring m_bombText;
        hstring m_flashText;
        hstring m_utilityText;
        hstring m_swingText;
        hstring m_eloText;
        hstring m_rankText;
        hstring m_headshotCountText;
        hstring m_sideTText;
        hstring m_sideCtText;
    };

    // 雷达上的一名玩家：聚合 5E 与完美两平台的展示字段。
    // 由 LiveMatch::RadarPlayer 转换而来（见 RadarPlayer 构造辅助）。
    struct RadarPlayer : RadarPlayerT<RadarPlayer>
    {
        RadarPlayer() = default;

        hstring Name() const { return m_name; }
        int32_t Team() const { return m_team; }
        bool IsMe() const { return m_isMe; }

        hstring FiveERank() const { return m_fiveERank; }
        hstring FiveEPlaytime() const { return m_fiveEPlaytime; }
        hstring FiveEStatus() const { return m_fiveEStatus; }
        Microsoft::UI::Xaml::Media::Brush FiveEStatusBrush() const { return m_fiveEStatusBrush; }
        bool HasFiveE() const { return m_hasFiveE; }

        hstring PerfectRank() const { return m_perfectRank; }
        hstring PerfectPlaytime() const { return m_perfectPlaytime; }
        hstring PerfectStatus() const { return m_perfectStatus; }
        Microsoft::UI::Xaml::Media::Brush PerfectStatusBrush() const { return m_perfectStatusBrush; }
        bool HasPerfect() const { return m_hasPerfect; }

        hstring RiskBadge() const { return m_riskBadge; }
        Microsoft::UI::Xaml::Media::Brush RiskBrush() const { return m_riskBrush; }

        hstring FiveEUuid() const { return m_fiveEUuid; }
        hstring PerfectSteamId() const { return m_perfectSteamId; }

        void SetName(hstring const& v) { m_name = v; }
        void SetTeam(int32_t v) { m_team = v; }
        void SetIsMe(bool v) { m_isMe = v; }
        void SetIds(hstring const& fiveEUuid, hstring const& perfectSteamId)
        {
            m_fiveEUuid = fiveEUuid; m_perfectSteamId = perfectSteamId;
        }
        void SetFiveE(hstring const& rank, hstring const& playtime, hstring const& status,
                      Microsoft::UI::Xaml::Media::Brush const& brush, bool has)
        {
            m_fiveERank = rank; m_fiveEPlaytime = playtime; m_fiveEStatus = status;
            m_fiveEStatusBrush = brush; m_hasFiveE = has;
        }
        void SetPerfect(hstring const& rank, hstring const& playtime, hstring const& status,
                        Microsoft::UI::Xaml::Media::Brush const& brush, bool has)
        {
            m_perfectRank = rank; m_perfectPlaytime = playtime; m_perfectStatus = status;
            m_perfectStatusBrush = brush; m_hasPerfect = has;
        }
        void SetRisk(hstring const& badge, Microsoft::UI::Xaml::Media::Brush const& brush)
        {
            m_riskBadge = badge; m_riskBrush = brush;
        }

    private:
        hstring m_name;
        int32_t m_team{ 1 };
        bool m_isMe{ false };

        hstring m_fiveERank;
        hstring m_fiveEPlaytime;
        hstring m_fiveEStatus;
        Microsoft::UI::Xaml::Media::Brush m_fiveEStatusBrush{ nullptr };
        bool m_hasFiveE{ false };

        hstring m_perfectRank;
        hstring m_perfectPlaytime;
        hstring m_perfectStatus;
        Microsoft::UI::Xaml::Media::Brush m_perfectStatusBrush{ nullptr };
        bool m_hasPerfect{ false };

        hstring m_riskBadge;
        Microsoft::UI::Xaml::Media::Brush m_riskBrush{ nullptr };

        hstring m_fiveEUuid;
        hstring m_perfectSteamId;
    };

    // 把实时对局的平台 Standing 转换成界面可用的画刷/文本。
    // 放在实现命名空间，雷达页面与转换辅助共用。
    inline Microsoft::UI::Xaml::Media::Brush RiskBrushOf(LiveMatch::PlatformStanding::Risk risk)
    {
        const wchar_t* key = nullptr;
        switch (risk)
        {
        case LiveMatch::PlatformStanding::Risk::Normal:
            key = L"SystemFillColorSuccessBrush"; break;
        case LiveMatch::PlatformStanding::Risk::Caution:
            key = L"SystemFillColorCautionBrush"; break;
        case LiveMatch::PlatformStanding::Risk::Banned:
            key = L"SystemFillColorCriticalBrush"; break;
        default:
            key = L"SystemFillColorNeutralBrush"; break;
        }
        if (auto found = Microsoft::UI::Xaml::Application::Current()
                .Resources().TryLookup(winrt::box_value(key)))
        {
            if (auto brush = found.try_as<Microsoft::UI::Xaml::Media::Brush>())
            {
                return brush;
            }
        }
        return nullptr;
    }

    // 由 LiveMatch::RadarPlayer 构造一个 App1::RadarPlayer 行。
    inline winrt::App1::RadarPlayer ToRadarRow(LiveMatch::RadarPlayer const& src)
    {
        auto row = winrt::make<winrt::App1::implementation::RadarPlayer>();
        auto* impl = winrt::get_self<winrt::App1::implementation::RadarPlayer>(row);
        impl->SetName(winrt::hstring(src.name.empty() ? L"未知玩家" : src.name));
        impl->SetTeam(src.team);
        impl->SetIsMe(src.isMe);
        impl->SetIds(winrt::hstring(src.fiveEUuid), winrt::hstring(src.perfectSteamId));

        impl->SetFiveE(
            winrt::hstring(src.fiveE.available ? (src.fiveE.rankText.empty() ? L"—" : src.fiveE.rankText) : L"—"),
            winrt::hstring(LiveMatch::FormatPlaytime(src.fiveE.playtimeSeconds)),
            winrt::hstring(src.fiveE.riskText.empty() ? L"—" : src.fiveE.riskText),
            RiskBrushOf(src.fiveE.risk),
            src.fiveE.available);
        impl->SetPerfect(
            winrt::hstring(src.perfect.available ? (src.perfect.rankText.empty() ? L"—" : src.perfect.rankText) : L"—"),
            winrt::hstring(LiveMatch::FormatPlaytime(src.perfect.playtimeSeconds)),
            winrt::hstring(src.perfect.riskText.empty() ? L"—" : src.perfect.riskText),
            RiskBrushOf(src.perfect.risk),
            src.perfect.available);

        LiveMatch::RiskLevel combined = LiveMatch::CombineRisk(src.fiveE.risk, src.perfect.risk);
        impl->SetRisk(winrt::hstring(LiveMatch::RiskBadgeText(combined)),
                      RiskBrushOf(src.fiveE.risk == LiveMatch::PlatformStanding::Risk::Banned ||
                                  src.perfect.risk == LiveMatch::PlatformStanding::Risk::Banned
                                  ? LiveMatch::PlatformStanding::Risk::Banned
                                  : (combined == LiveMatch::RiskLevel::Caution
                                     ? LiveMatch::PlatformStanding::Risk::Caution
                                     : (combined == LiveMatch::RiskLevel::Normal
                                        ? LiveMatch::PlatformStanding::Risk::Normal
                                        : LiveMatch::PlatformStanding::Risk::Unknown))));
        return row;
    }
}

namespace winrt::App1::factory_implementation
{
    struct MatchRow : MatchRowT<MatchRow, implementation::MatchRow>
    {
    };

    struct PlayerRow : PlayerRowT<PlayerRow, implementation::PlayerRow>
    {
    };

    struct PerfectMatchRow : PerfectMatchRowT<PerfectMatchRow, implementation::PerfectMatchRow>
    {
    };

    struct PerfectMatchPlayerRow
        : PerfectMatchPlayerRowT<PerfectMatchPlayerRow, implementation::PerfectMatchPlayerRow>
    {
    };

    struct RadarPlayer : RadarPlayerT<RadarPlayer, implementation::RadarPlayer>
    {
    };
}
