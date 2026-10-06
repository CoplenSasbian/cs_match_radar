#pragma once

// 完美世界对战平台 —— 比赛记录模型。
//
// 数据来源（均需登录 token，接口见 PerfectMatchClient）：
//   POST https://api.wmpvp.com/api/csgo/home/match/list   比赛列表
//   POST https://api.wmpvp.com/api/v1/csgo/match          单场详情（含全部 10 人）
//
// 说明：
//   - 这些接口用 token 请求头鉴权，且要求 mySteamId 与 token 属于同一账号；
//     toSteamId 才是"要查谁"，因此可以用自己的登录态查询任意玩家。
//   - 响应为明文 JSON（不像 overview/season-stats 需要 AES 解密）。
//   - dataSource 决定数据来源：1=国服竞技 2=国际服竞技 3=完美天梯。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Perfect
{
    // 数据来源（对应接口的 dataSource 参数）。
    enum class MatchDataSource
    {
        OfficialCn = 1,     // 国服竞技（官匹）
        OfficialIntl = 2,   // 国际服竞技（官匹）
        PvpLadder = 3,      // 完美天梯
    };

    // 比赛列表中的一条记录（该玩家在这场比赛里的个人数据）。
    struct PerfectMatchSummary
    {
        std::wstring matchId;       // 形如 PVP@9210321939064225676
        std::wstring playerId;      // 该玩家的 SteamID64

        std::wstring mapName;       // 中文地图名，如 炙热沙城Ⅱ
        std::wstring mapLogo;       // 地图图标 URL
        std::wstring mode;          // 如 天梯组排对局

        std::optional<int32_t> team;        // 该玩家所在队伍（1 / 2）
        std::optional<int32_t> winTeam;     // 获胜队伍
        std::optional<int32_t> score1;
        std::optional<int32_t> score2;

        std::optional<int32_t> kill;
        std::optional<int32_t> death;
        std::optional<int32_t> assist;
        std::optional<int32_t> k4;
        std::optional<int32_t> k5;

        std::optional<double> rating;       // CS Rating
        std::optional<double> pwRating;     // 完美 Rating
        std::optional<double> we;           // WE

        std::optional<int32_t> pvpScore;            // 赛后分数
        std::optional<int32_t> pvpScoreChange;      // 本场分数变化

        std::wstring startTime;     // "2026-10-03 23:19:31"
        std::wstring endTime;
        std::optional<int32_t> duration;    // 分钟

        bool pvpMvp{ false };
        bool mvp{ false };

        // 该玩家是否获胜（由 team / winTeam 推导）。
        bool IsWin() const
        {
            if (!team.has_value() || !winTeam.has_value())
            {
                return false;
            }
            return *team == *winTeam;
        }

        // K/D（death 为 0 时按 kill 计）。
        double Kd() const
        {
            const int32_t k = kill.value_or(0);
            const int32_t d = death.value_or(0);
            return d == 0 ? static_cast<double>(k) : static_cast<double>(k) / d;
        }
    };

    // 详情中的一名玩家。
    struct PerfectMatchPlayer
    {
        std::wstring playerId;      // SteamID64
        std::wstring nickname;
        std::wstring avatar;
        std::optional<int32_t> team;

        std::optional<int32_t> kill;
        std::optional<int32_t> death;
        std::optional<int32_t> assist;
        std::optional<int32_t> headShot;
        std::optional<double> headShotRatio;    // 0..1

        std::optional<int32_t> entryKill;       // 首杀
        std::optional<int32_t> entryDeath;      // 首死
        std::optional<int32_t> awpKill;
        std::optional<int32_t> snipeNum;

        std::optional<double> rating;
        std::optional<double> pwRating;
        std::optional<double> rws;
        std::optional<double> we;
        std::optional<int32_t> adpr;
        std::optional<int32_t> kast;

        // 多杀 / 残局。
        std::optional<int32_t> twoKill, threeKill, fourKill, fiveKill;
        std::optional<int32_t> vs1, vs2, vs3, vs4, vs5;

        // 道具 / 伤害。
        std::optional<int32_t> flash;
        std::optional<int32_t> flashTeammate;
        std::optional<int32_t> throwsCnt;
        std::optional<int32_t> dmgHealth;
        std::optional<int32_t> dmgArmor;
        std::optional<int32_t> grenadeDamage;
        std::optional<int32_t> infernoDamage;
        std::optional<int32_t> tradeFragCount;  // 补枪

        std::optional<int32_t> mvpValue;
        std::optional<int32_t> pvpScore;
        std::optional<int32_t> pvpScoreChange;
        std::optional<int32_t> rank;

        bool mvp{ false };
        bool vac{ false };

        // 击杀矩阵：敌人 SteamID -> 击杀次数。
        std::vector<std::pair<std::wstring, int32_t>> killMap;

        double Kd() const
        {
            const int32_t k = kill.value_or(0);
            const int32_t d = death.value_or(0);
            return d == 0 ? static_cast<double>(k) : static_cast<double>(k) / d;
        }
    };

    // 单场比赛详情。
    struct PerfectMatchDetail
    {
        std::wstring matchId;
        std::wstring mapName;
        std::wstring mapEn;         // de_dust2
        std::wstring mapLogo;
        std::wstring mode;

        std::optional<int32_t> score1;
        std::optional<int32_t> score2;
        std::optional<int32_t> halfScore1;
        std::optional<int32_t> halfScore2;
        std::optional<int32_t> extraScore1;     // 加时
        std::optional<int32_t> extraScore2;
        std::optional<int32_t> winTeam;

        std::wstring startTime;
        std::wstring endTime;
        std::optional<int32_t> duration;

        bool pvpLadder{ false };

        // 全部 10 名玩家。
        std::vector<PerfectMatchPlayer> players;

        // 按队伍分组（便于界面直接渲染左右两列）。
        std::vector<PerfectMatchPlayer> TeamPlayers(int32_t team) const
        {
            std::vector<PerfectMatchPlayer> result;
            for (auto const& player : players)
            {
                if (player.team.value_or(0) == team)
                {
                    result.push_back(player);
                }
            }
            return result;
        }
    };

    // 常用武器。
    struct PerfectWeaponStat
    {
        std::wstring name;        // ak47
        std::wstring nameZh;      // AK47
        std::wstring image;       // 图标 URL
        std::optional<int32_t> killNum;
        std::optional<int32_t> headshotSum;
        std::optional<double> headshotRate;
        std::optional<double> avgDamage;
        std::optional<double> sprayAccuracy;      // 喷洒精度
        std::optional<double> firstShotAccuracy;  // 首枪命中
        std::optional<int32_t> matchNum;
        std::wstring levelAccuracy;   // 评级 A/B/C
        std::wstring levelHeadshotRate;
    };

    // 常用地图。
    struct PerfectMapStat
    {
        std::wstring map;          // de_mirage
        std::wstring mapName;      // 荒漠迷城
        std::wstring mapImage;
        std::wstring mapLogo;
        std::optional<int32_t> totalMatch;
        std::optional<int32_t> totalKill;
        std::optional<int32_t> deathNum;
        std::optional<double> totalAdr;
        std::optional<double> ratingSum;
        std::optional<int32_t> winCount;
        std::optional<int32_t> matchMvpNum;
        std::optional<int32_t> threeKillNum;
        std::optional<int32_t> fourKillNum;
    };

    // 玩家资料（来自 pvp/detailStats）。
    //
    // 该接口用自己的一套字段名（cnt/kd/rws/adr...），
    // 与 overview / season-stats 不同，所以单独建模。
    struct PerfectPlayerProfile
    {
        // 身份
        std::wstring steamId;
        std::wstring name;         // 完美昵称
        std::wstring avatar;
        std::wstring summary;      // 平台称号，如「神枪不朽」
        std::wstring seasonId;     // S25

        // 段位
        std::optional<int32_t> pvpScore;   // 当前天梯分
        std::optional<int32_t> stars;
        std::optional<int32_t> pvpRank;

        // 本赛季
        std::optional<int32_t> cnt;           // 场次
        std::optional<double> winRate;        // 0..1
        std::optional<double> kd;
        std::optional<double> rating;
        std::optional<double> pwRating;
        std::optional<double> commonRating;
        std::optional<double> rws;
        std::optional<double> adr;
        std::optional<double> avgWe;
        std::optional<double> headShotRatio;  // 0..1
        std::optional<double> entryKillRatio;

        // 累计
        std::optional<int32_t> kills;
        std::optional<int32_t> deaths;
        std::optional<int32_t> assists;
        std::optional<int32_t> mvpCount;
        std::optional<int32_t> multiKill;
        std::optional<int32_t> k2, k3, k4, k5;
        std::optional<int32_t> vs1, vs2, vs3, vs4, vs5;
        std::optional<int32_t> endingWin;

        // 五角星（平台能力雷达，均为 0..~10 的分值）
        std::optional<double> shot;     // 枪法
        std::optional<double> snipe;    // 狙击
        std::optional<double> victory;  // 致胜
        std::optional<double> breach;   // 突破
        std::optional<double> prop;     // 道具

        // 趋势（按时间正序）
        std::vector<double> historyRatings;
        std::vector<double> historyPwRatings;
        std::vector<double> historyScores;
        std::vector<double> historyRws;
        std::vector<std::wstring> historyDates;

        std::vector<PerfectWeaponStat> hotWeapons;
        std::vector<PerfectMapStat> hotMaps;
    };
    // 搜索玩家命中的一条结果。
    struct PerfectSearchHit
    {
        std::wstring steamId;        // SteamID64（后续查对局用这个）
        std::wstring pvpNickName;    // 完美平台昵称
        std::wstring pvpAvatar;      // 完美平台头像 URL
        std::wstring appNickName;    // 完美平台编号，如 AotoDie / PW7136946260
    };

    // 比赛列表查询参数。
    struct PerfectMatchQuery
    {
        std::wstring toSteamId;             // 要查的玩家（必填）
        MatchDataSource dataSource{ MatchDataSource::PvpLadder };
        int32_t page{ 1 };
        int32_t pageSize{ 20 };
        std::wstring seasonId{ L"recent" }; // "recent" 或 S25 等
        int32_t pvpType{ -1 };
    };
}
