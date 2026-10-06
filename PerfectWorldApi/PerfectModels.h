#pragma once

// 完美世界对战平台玩家资料模型。
//
// 数据来源（均需登录凭据 access_token）：
//   GET  https://pwaweblogin.wmpvp.com/user-info/overview
//   GET  https://pwaweblogin.wmpvp.com/user-info/season-ladder-score-list
//   POST https://pwaweblogin.wmpvp.com/user-info/season-stats
//
// 其中 overview 与 season-stats 的响应是 AES-256-ECB 加密的，
// 需先经 PerfectCrypto 解密（season-ladder-score-list 不加密）。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Perfect
{
    // 完美平台的段位展示规则（来自参考项目，已核实）：
    //   score < 2400          -> 显示分数
    //   score >= 2400, 0-9 星  -> 普通 S
    //   score >= 2400, 10-24 星 -> 黄金 S
    //   score >= 2400, 25-49 星 -> 钻石 S
    //   score >= 2400, 50+ 星   -> 魔王 S
    enum class SStarTier
    {
        None,       // 未到 S 段
        Normal,     // 普通 S
        Gold,       // 黄金 S
        Diamond,    // 钻石 S
        Demon,      // 魔王 S
    };

    // 当前段位 / 历史最高段位。
    struct RankScore
    {
        std::optional<int32_t> score;
        std::optional<int32_t> stars;
        std::wstring season;
        std::wstring seasonShowName;

        // 按段位规则判断 S 段档次。
        // 实现放在头文件（inline）：调用方需要它，且不依赖任何 DLL 内部状态。
        SStarTier Tier() const
        {
            if (!score.has_value() || *score < 2400)
            {
                return SStarTier::None;
            }
            const int32_t starCount = stars.value_or(0);
            if (starCount >= 50) return SStarTier::Demon;
            if (starCount >= 25) return SStarTier::Diamond;
            if (starCount >= 10) return SStarTier::Gold;
            return SStarTier::Normal;
        }

        // 按段位规则生成展示文本，如 "2450" 或 "钻石 S 32★"。
        std::wstring Display() const
        {
            if (!score.has_value())
            {
                return L"—";
            }
            // 2400 分以下直接显示分数。
            if (*score < 2400)
            {
                return std::to_wstring(*score);
            }
            std::wstring prefix;
            switch (Tier())
            {
            case SStarTier::Demon:   prefix = L"魔王 S"; break;
            case SStarTier::Diamond: prefix = L"钻石 S"; break;
            case SStarTier::Gold:    prefix = L"黄金 S"; break;
            case SStarTier::Normal:  prefix = L"S"; break;
            default:                 return std::to_wstring(*score);
            }
            if (stars.has_value())
            {
                prefix += L" " + std::to_wstring(*stars) + L"★";
            }
            return prefix;
        }
    };

    // 生涯/赛季总览中的玩家身份。
    struct PerfectIdentity
    {
        std::wstring steamId;      // SteamID64
        std::wstring uid;
        std::wstring userId;
        std::wstring name;         // 昵称（主显示名）
        std::wstring nickname;
        std::wstring avatar;
        std::wstring zqId;         // 留言板实体 ID
        std::optional<int32_t> perfectPower;
    };

    // 赛季 ladder 统计（部分常用字段）。
    struct PerfectLadderStats
    {
        // 基础
        std::optional<int32_t> score;
        std::optional<int32_t> scoreChange;
        std::optional<int32_t> matchCount;
        std::optional<int32_t> winNum;
        std::optional<int32_t> drawNum;
        std::optional<double> winRate;      // 0..1
        std::optional<int32_t> roundCount;

        // Rating / 输出
        std::optional<double> ratingAvg;
        std::optional<double> ratingCtAvg;
        std::optional<double> ratingTAvg;
        std::optional<double> adpr;
        std::optional<double> rwsAvg;
        std::optional<double> priAvg;
        std::optional<int32_t> killNum;
        std::optional<int32_t> deathNum;
        std::optional<int32_t> assistNum;
        std::optional<double> kastTotal;

        // 爆头 / 首杀
        std::optional<double> hsKillRate;   // 0..1
        std::optional<int32_t> headshotKillNum;
        std::optional<int32_t> firstKillNum;
        std::optional<int32_t> firstDeathNum;
        std::optional<int32_t> firstKillWinRound;

        // 残局
        std::optional<int32_t> c1v1Num, c1v2Num, c1v3Num, c1v4Num, c1v5Num;
        std::optional<int32_t> c1v1Total, c1v2Total, c1v3Total, c1v4Total, c1v5Total;
        std::optional<double> c1vxRate;

        // 多杀 / MVP
        std::optional<int32_t> twoKillNum, threeKillNum, fourKillNum, fiveKillNum;
        std::optional<int32_t> mvpNum;        // 回合 MVP
        std::optional<int32_t> matchMvpNum;   // 场次 MVP

        // 狙击
        std::optional<int32_t> sniperKillNum;
        std::optional<int32_t> awpKillTotal;

        // S 段
        std::optional<int32_t> currSStars;
        std::optional<int32_t> currSLevel;
        std::optional<int32_t> prevSStars;
        std::optional<int32_t> changedStars;

        // ---- 派生指标（分母为 0 或缺失时返回 nullopt）----
        // 实现均为 inline：纯计算，调用方需要直接使用。
        std::optional<double> Kd() const
        {
            if (!killNum.has_value() || !deathNum.has_value() || *deathNum <= 0)
            {
                return std::nullopt;
            }
            return static_cast<double>(*killNum) / static_cast<double>(*deathNum);
        }

        std::optional<double> Kast() const
        {
            if (!kastTotal.has_value() || !roundCount.has_value() || *roundCount <= 0)
            {
                return std::nullopt;
            }
            // kast_total 是回合数累计，除以总回合得到 0..1 比例。
            const double value = *kastTotal / static_cast<double>(*roundCount);
            return value <= 1.0 ? std::optional<double>(value)
                                : std::optional<double>(value / 100.0);
        }

        std::optional<double> OneVOneRate() const
        {
            if (!c1v1Num.has_value() || !c1v1Total.has_value() || *c1v1Total <= 0)
            {
                return std::nullopt;
            }
            return static_cast<double>(*c1v1Num) / static_cast<double>(*c1v1Total);
        }

        std::optional<double> FirstKillRate() const
        {
            if (!firstKillWinRound.has_value() || !firstKillNum.has_value() || *firstKillNum <= 0)
            {
                return std::nullopt;
            }
            return static_cast<double>(*firstKillWinRound) / static_cast<double>(*firstKillNum);
        }

        std::optional<double> EntryRate() const
        {
            if (!firstKillNum.has_value() || !roundCount.has_value() || *roundCount <= 0)
            {
                return std::nullopt;
            }
            return static_cast<double>(*firstKillNum) / static_cast<double>(*roundCount);
        }
    };

    // 雷达图维度（radar_new）。
    struct RadarDimension
    {
        std::wstring key;         // fire_power / marksmanship / ...
        std::optional<double> score;
        std::optional<int32_t> level;
        std::wstring description;
    };

    // 地图统计（map[]）。
    struct PerfectMapStats
    {
        std::wstring map;
        std::wstring nameCn;
        std::wstring nameEn;
        std::optional<int32_t> totalNum;
        std::optional<int32_t> roundCount;
        std::optional<int32_t> winNum;
        std::optional<double> winRate;
        std::optional<double> adpr;
        std::optional<double> ratingAvg;
        std::optional<double> rwsAvg;
        std::optional<int32_t> killNum;
        std::optional<int32_t> headshotKillNum;
    };

    // 武器统计（weapon[]）。
    struct PerfectWeaponStats
    {
        std::wstring name;
        std::wstring nameCn;
        std::wstring nameEn;
        std::optional<int32_t> matchNum;
        std::optional<int32_t> killNum;
        std::optional<double> avgKillNum;
        std::optional<double> headshotRate;
        std::optional<double> firstShotAccuracy;
        std::optional<double> sprayAccuracy;
        std::optional<double> rapidStopSuccessRate;
        std::optional<double> avgTimeToKill;
    };

    // 一名玩家的完整完美平台资料。
    struct PerfectPlayer
    {
        PerfectIdentity identity;

        // 当前段位（ladder.score + curr_s_stars）。
        RankScore currentRank;
        // 历史最高段位（顶层 all_season_max_score + all_season_max_star）。
        RankScore peakRank;

        PerfectLadderStats ladder;
        std::vector<RadarDimension> radar;
        std::vector<PerfectMapStats> maps;
        std::vector<PerfectWeaponStats> weapons;

        // 当前赛季 WE（radar_new.fire_power.detail.we_raw）。
        std::optional<double> seasonWe;

        // 部分加载：overview 成功但 season-stats 失败时为 true。
        bool partialFailure = false;
        std::wstring failureReason;

        // 原始解密后的 JSON（便于排查）。
        std::wstring rawOverviewJson;
        std::wstring rawSeasonStatsJson;

        bool HasData() const
        {
            return !identity.steamId.empty() || !identity.name.empty();
        }
    };
}
