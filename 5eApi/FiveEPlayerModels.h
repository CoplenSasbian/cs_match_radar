#pragma once

// 玩家资料模型（跨平台共用）。
//
// 对局雷达的核心是"这个队友/对手是谁、什么水平、有没有问题"，
// 因此这里按"玩家"而不是"对局"来组织数据。
//
// 数据来源：
//   - FiveEPlayerHome：GET /cranenew/http/api/data/v3/player/home（HMAC 签名）
//   - 完美世界：待实现（本地日志 + 登录态接口）

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace FiveE
{
    // 平台标识。
    enum class Platform
    {
        Unknown,
        FiveE,      // 5E 对战平台
        PerfectWorld, // 完美世界对战平台
    };

    // 信用/信誉状态。用于"这个玩家是否可信"的判断。
    //
    // 注意：这不等于"封禁记录"。5E 的 player/home 只暴露信用分与信任分，
    // 完整的封禁历史需要另外的数据源。
    struct CreditInfo
    {
        // 5E 信用分，满分 100000（样本 3926 / 100000）。
        std::optional<int32_t> credit;
        // 信任分（trusted_score），样本 100000。
        std::optional<int32_t> trustedScore;
        // 信任状态（trusted_status），1 通常表示正常。
        std::optional<int32_t> trustedStatus;
        // 账号状态（uinfo.status），0 通常表示正常。
        std::optional<int32_t> accountStatus;
        // 用户名垃圾信息标记（username_spam_status）。
        std::optional<int32_t> usernameSpamStatus;
        // Plus 会员等级。
        std::optional<int32_t> vipLevel;

        // 信用分是否偏低（阈值可调）。用于在界面上给出提示。
        bool IsCreditLow(int32_t threshold = 80000) const
        {
            return credit.has_value() && *credit < threshold;
        }
    };

    // 段位信息。
    struct RankInfo
    {
        // 平台段位 ID（5E 的 level_id，样本 51）。
        std::optional<int32_t> levelId;
        // 段位显示名（部分接口不提供，需要按 levelId 映射）。
        std::wstring levelName;
        // 全服排名（样本 10489）。
        std::optional<int32_t> rank;
        // 当前 ELO（样本 2473.1）。
        std::optional<double> elo;
        // 赛季标识（样本 "2026s4"）。
        std::wstring season;
        // 赛季初始 ELO。
        std::optional<double> seasonElo;
        // 星级。
        std::optional<int32_t> starNum;
        // 赛季最高 ELO。
        std::optional<double> maxElo;
    };

    // 赛季统计（season_data，约 70 个字段，这里取常用部分）。
    struct SeasonStats
    {
        std::wstring season;
        std::optional<int32_t> matchMode;      // 9 = 优先排位
        std::optional<int32_t> matchTotal;
        std::optional<int32_t> winTotal;
        std::optional<int32_t> tieTotal;

        // 场均表现。
        std::optional<double> avgRating;
        std::optional<double> avgAdr;
        std::optional<double> avgRws;
        std::optional<double> avgKast;
        std::optional<double> avgHeadshotRate;   // per_headshot（0..1 比例）
        std::optional<double> avgKpr;            // 每回合击杀
        std::optional<double> avgImpact;

        // 累计表现。
        std::optional<int32_t> kills;
        std::optional<int32_t> deaths;
        std::optional<int32_t> assists;
        std::optional<int32_t> headshots;
        std::optional<int32_t> mvpTotal;
        std::optional<int32_t> matchTimeTotal;   // 秒

        // 当前连败场次。
        std::optional<int32_t> losingStreak;

        // 单场最高。
        std::optional<double> maxRating;
        std::optional<double> maxAdr;
        std::optional<double> maxRws;

        // 胜率（由 winTotal / matchTotal 计算）。
        std::optional<double> WinRate() const
        {
            if (!winTotal.has_value() || !matchTotal.has_value() || *matchTotal <= 0)
            {
                return std::nullopt;
            }
            return static_cast<double>(*winTotal) / static_cast<double>(*matchTotal);
        }
    };

    // 生涯总览（career）。
    struct CareerStats
    {
        std::optional<double> elo;
        std::optional<int32_t> levelId;
        std::optional<int32_t> rank;
        std::optional<int32_t> matchTotal;
        std::optional<int32_t> matchWin;
        std::optional<int32_t> matchLoss;
        std::optional<int32_t> matchTie;
        std::optional<int32_t> matchTimeTotal;   // 秒
        std::optional<int32_t> starNum;
        std::wstring bestSeason;
    };

    // 玩家基础资料（uinfo）。
    struct PlayerIdentity
    {
        std::wstring uuid;        // 5E UUID，跨接口主键
        std::wstring uid;         // 5E 数字 uid
        std::wstring username;    // 昵称（主显示名）
        std::wstring nickname;    // 可能为空
        std::wstring domain;      // 个人域名 slug
        std::wstring avatarUrl;   // 相对路径，需拼 CDN
        std::wstring steamId64;   // 可能为空字符串（实测部分用户为空）
        std::optional<int64_t> registerDate;
        std::wstring countryId;
    };

    // 一名玩家的完整资料（player/home 的全部内容）。
    struct PlayerHome
    {
        Platform platform = Platform::FiveE;
        PlayerIdentity identity;
        CreditInfo credit;
        RankInfo rank;
        CareerStats career;
        SeasonStats season;

        // 原始 JSON 文本，便于排查字段变更。
        std::wstring rawJson;

        // 是否有可用数据。
        bool HasData() const
        {
            return !identity.uuid.empty() || !identity.username.empty();
        }
    };
}
