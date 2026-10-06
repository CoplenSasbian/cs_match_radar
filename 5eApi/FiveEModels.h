#pragma once

// 5E 平台对局信息数据模型。
// 字段命名对应 5E 原始 JSON 键名，便于与接口文档交叉比对。

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace FiveE
{
    // 搜索玩家命中的一条结果。
    struct SearchHit
    {
        std::wstring username;   // 昵称（同时也是玩家“名称”）
        std::wstring domain;     // 5E ID（domain），查询对局用
        std::wstring uuid;       // 可能为空，需再调 idTransfer 换取
        std::wstring avatarUrl;
    };
    // 比赛阵营。5E 用 group_1 / group_2 表示双方，不保证哪方是“我方”。
    enum class TeamGroup
    {
        Group1 = 1,
        Group2 = 2,
    };

    // 玩家在单张地图某一半场的阵营。
    enum class Side
    {
        Unknown,
        Terrorist,   // T
        CounterTerrorist, // CT
    };

    // 比赛状态。/match/list 与 /match/{id} 的 status 字段取值。
    enum class MatchStatus
    {
        Unknown,
        Finished,   // 已结束
        Ongoing,    // 进行中
        Cancelled,  // 已取消 / 弃赛
    };

    // 简单的“有值才写入”的标量容器。
    // 5E 的字段随比赛类型（天梯 / PUG / 积分赛）变化很大，
    // 因此这里保留一个通用键值表，避免为每个变体都定义结构体。
    using ScalarMap = std::map<std::wstring, std::wstring>;

    // 一场比赛在某半场的比分。
    struct HalfScores
    {
        std::optional<int32_t> firstHalf;   // 上半场得分
        std::optional<int32_t> secondHalf;  // 下半场得分
    };

    // 队伍信息，来自 detail.main 的 groupN_* 字段。
    struct MatchTeam
    {
        TeamGroup group = TeamGroup::Group1;
        std::wstring teamId;                    // groupN_tid
        std::vector<std::wstring> playerUids;   // groupN_uids
        Side firstHalfSide = Side::Unknown;     // groupN_fh_role
        Side secondHalfSide = Side::Unknown;    // groupN_sh_role
        std::optional<int32_t> score;           // groupN_all_score
        std::optional<int32_t> originElo;       // groupN_origin_elo
        std::optional<int32_t> changeElo;       // groupN_change_elo
        HalfScores halfScores;
    };

    // 玩家段位 / 天梯信息，来自 row.level_info 与 row.sts。
    struct LadderStats
    {
        std::optional<int32_t> score;         // level_info.score
        std::optional<int32_t> level;         // level_info.level
        std::wstring levelName;               // level_info.level_name
        std::optional<int32_t> rank;          // level_info.rank
        std::wstring rankName;                // level_info.rank_name
        std::optional<int32_t> changeScore;   // level_info.change_score
        std::optional<int32_t> elo;           // sts.elo
        std::optional<int32_t> originElo;     // sts.origin_elo
        std::optional<int32_t> changeElo;     // sts.change_elo
    };

    // 玩家资料。
    struct PlayerProfile
    {
        std::wstring playerId;
        std::wstring nickname;
        std::wstring username;
        std::wstring avatarUrl;
        std::wstring steamId;
        std::wstring steamNickname;
        std::wstring profileUrl;
        std::wstring steamProfileUrl;
    };

    // 某一半场的个人数据（fight_t / fight_ct）。
    struct SideStats
    {
        std::optional<int32_t> kills;
        std::optional<int32_t> deaths;
        std::optional<int32_t> assists;
        std::optional<double> adr;
        std::optional<double> rws;
        std::optional<double> rating;
        std::optional<double> swingScore;
        std::optional<int32_t> firstKills;
        std::optional<int32_t> firstDeaths;
    };

    // 投掷物 / 道具数据。
    struct UtilityStats
    {
        std::optional<int32_t> bombPlants;
        std::optional<int32_t> bombDefuses;
        std::optional<int32_t> utilityDamage;
        std::optional<int32_t> enemyUtilityDamage;
        std::optional<int32_t> enemiesFlashed;
        std::optional<double> enemyFlashDuration;
        std::optional<int32_t> teammatesFlashed;
        std::optional<double> teamFlashDuration;
    };

    // 个人高光 / 特殊击杀数据。
    struct ImpactStats
    {
        std::optional<int32_t> awpKills;
        std::optional<int32_t> jumpKills;
        std::optional<int32_t> knifeKills;
        std::optional<int32_t> entryKills;
        std::optional<int32_t> tradeKills;
        std::optional<int32_t> firstKills;
        std::optional<int32_t> firstDeaths;
        std::optional<int32_t> perfectKills;
        std::optional<int32_t> assistedKills;
        std::optional<int32_t> revengeKills;
        std::optional<int32_t> teamKills;
    };

    // 个人荣誉标记。
    struct AwardFlags
    {
        std::optional<bool> isMvp;
        std::optional<bool> isSvp;
        std::optional<bool> isHighlight;
    };

    // 单场比赛中的一名玩家。
    struct MatchPlayer
    {
        PlayerProfile profile;
        TeamGroup group = TeamGroup::Group1;
        LadderStats ladder;

        std::optional<int32_t> kills;
        std::optional<int32_t> deaths;
        std::optional<int32_t> assists;
        std::optional<double> rating;         // rating2 优先，回退 rating
        std::optional<double> swingScore;     // rating3
        std::optional<double> adr;
        std::optional<double> rws;
        std::optional<double> kast;
        std::optional<int32_t> headshots;
        std::optional<double> headshotRate;   // per_headshot
        std::optional<int32_t> firstKills;
        std::optional<int32_t> firstDeaths;
        std::optional<int32_t> awpKills;
        std::optional<int32_t> multiKillCount;
        std::map<int32_t, int32_t> multiKills;   // 1..5 -> 次数
        std::optional<int32_t> clutchCount;
        std::map<int32_t, int32_t> clutches;     // 1..5 -> 1vN 残局次数
        std::optional<int32_t> bombPlants;
        std::optional<int32_t> bombDefuses;

        std::map<std::wstring, SideStats> sideStats;   // "t" / "ct"
        UtilityStats utility;
        ImpactStats impact;
        AwardFlags awards;
    };

    // 比赛级荣誉（detail.main 的 most_*_uid）。
    struct MatchAwards
    {
        std::wstring mvpPlayerId;
        std::wstring mostAssistsPlayerId;
        std::wstring mostAwpKillsPlayerId;
        std::wstring mostClutchesPlayerId;
        std::wstring mostFirstKillsPlayerId;
        std::wstring mostHeadshotsPlayerId;
        std::wstring mostJumpKillsPlayerId;
        std::wstring most1v2ClutchesPlayerId;
    };

    // 列表接口 /match/list 的原始条目。
    // 该接口返回的字段集合不固定，因此除常用字段外保留 raw 键值表。
    struct MatchSummary
    {
        std::wstring matchId;      // match_id
        std::wstring matchCode;    // match_code
        std::wstring mapName;      // map
        std::wstring mapLabel;     // map_desc / map_name
        std::optional<bool> isWin; // is_win
        std::optional<int64_t> startTime;
        std::optional<int64_t> endTime;
        std::optional<int32_t> kills;
        std::optional<int32_t> deaths;
        std::optional<double> rws;
        std::optional<double> adr;
        std::optional<double> rating;
        std::optional<double> headshotRate;
        std::optional<int32_t> assistCount;
        std::optional<int32_t> awpKills;
        std::optional<int32_t> mvpCount;
        std::optional<int32_t> roundTotal;
        std::optional<int32_t> group1Score;
        std::optional<int32_t> group2Score;
        std::optional<int32_t> changeElo;
        std::wstring levelName;
        std::wstring location;
        std::wstring serverIp;
        std::optional<int32_t> serverPort;
        std::optional<int32_t> season;
        MatchStatus status = MatchStatus::Unknown;
        std::wstring demoUrl;
        ScalarMap raw;
    };

    // 详情接口 /match/{id} 的完整结果。
    struct MatchDetail
    {
        // detail.main：比赛主信息。
        std::wstring matchId;          // match_code 优先，回退 match_id
        std::wstring mapName;          // map
        std::wstring mapLabel;         // map_desc
        std::wstring matchWinner;      // match_winner
        std::wstring location;         // location_full 优先，回退 location
        std::wstring demoUrl;          // demo_url
        std::wstring serverIp;
        std::optional<int32_t> serverPort;
        std::optional<int32_t> roundTotal;
        std::optional<int32_t> season;
        std::optional<int32_t> year;
        std::optional<int64_t> startTime;
        std::optional<int64_t> endTime;
        MatchStatus status = MatchStatus::Unknown;
        std::optional<int32_t> group1Score;
        std::optional<int32_t> group2Score;
        std::optional<int32_t> group1ChangeElo;
        std::optional<int32_t> group2ChangeElo;

        std::vector<MatchTeam> teams;      // group_1 / group_2
        std::vector<MatchPlayer> players;  // group_1 / group_2 展开
        MatchAwards awards;

        // 可选补充接口的原始结果，键为 "advanced" / "leetify_rating" / "vip_plus"。
        // 这些接口的返回结构不固定，先按原始 JSON 文本保留。
        std::map<std::wstring, std::wstring> extraRaw;
    };

    // 分页查询参数，对应 /match/list。
    struct MatchListQuery
    {
        std::wstring uuid;                 // 用户唯一标识
        std::optional<int64_t> startTime;  // 默认 180 天前
        std::optional<int64_t> endTime;    // 默认当天结束
        int32_t page = 1;
        int32_t limit = 30;
        int32_t matchType = -1;
        int32_t date = 0;
        int32_t csType = 0;
    };

    // 5E 接口错误。what() 为 UTF-8，message 为宽字符便于界面显示。
    class ApiError
    {
    public:
        enum class Kind
        {
            Network,      // 连接失败 / 超时
            Http,         // 非 2xx 响应
            Parse,        // JSON 解析失败
            Business,     // HTTP 200 但业务返回码非成功
        };

        // 构造函数 inline：调用方（宿主应用）需要构造它来抛异常，
        // 因此实现必须可见，不能只留在 DLL 里。
        ApiError(Kind kind, std::wstring message, int32_t httpStatus = 0, int32_t bizCode = 0)
            : m_kind(kind), m_message(std::move(message)),
              m_httpStatus(httpStatus), m_bizCode(bizCode)
        {
        }

        Kind kind() const noexcept { return m_kind; }
        const std::wstring& message() const noexcept { return m_message; }
        int32_t httpStatus() const noexcept { return m_httpStatus; }
        int32_t bizCode() const noexcept { return m_bizCode; }

    private:
        Kind m_kind;
        std::wstring m_message;
        int32_t m_httpStatus;
        int32_t m_bizCode;
    };
}
