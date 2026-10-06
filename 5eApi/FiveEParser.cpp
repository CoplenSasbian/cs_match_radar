#include "FiveEApiPch.h"
#include "FiveEParser.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>

#include <string>
#include <string_view>

using namespace FiveE::Json;

namespace FiveE::Parse
{
    namespace
    {
        // 5E 的数字字段有时是字符串，统一走 OptionalInt32 / OptionalDouble 的宽进转换。

        SideStats ParseSideStats(JsonObjectRef const& object)
        {
            SideStats stats;
            stats.kills = OptionalInt32(object, L"kill");
            stats.deaths = OptionalInt32(object, L"death");
            stats.assists = OptionalInt32(object, L"assist");
            stats.adr = OptionalDouble(object, L"adr");
            stats.rws = OptionalDouble(object, L"rws");
            stats.rating = OptionalDouble(object, L"rating2");
            if (!stats.rating)
            {
                stats.rating = OptionalDouble(object, L"rating");
            }
            stats.swingScore = OptionalDouble(object, L"rating3");
            stats.firstKills = OptionalInt32(object, L"first_kill");
            stats.firstDeaths = OptionalInt32(object, L"first_death");
            return stats;
        }

        void ParseNumberedStats(
            JsonObjectRef const& object,
            std::wstring_view prefix,
            int32_t start,
            int32_t end,
            std::map<int32_t, int32_t>& out)
        {
            for (int32_t index = start; index <= end; ++index)
            {
                const std::wstring key = std::wstring(prefix) + std::to_wstring(index);
                if (auto value = OptionalInt32(object, key))
                {
                    out[index] = *value;
                }
            }
        }

        LadderStats ParseLadderStats(JsonObjectRef const& levelInfo, JsonObjectRef const& sts)
        {
            LadderStats ladder;
            ladder.score = OptionalInt32(levelInfo, L"score");
            ladder.level = OptionalInt32(levelInfo, L"level");
            if (auto name = OptionalString(levelInfo, L"level_name"))
            {
                ladder.levelName = *name;
            }
            // 真实返回里 level_name 常为空，段位以 level_id 表示，这里做回退。
            if (ladder.levelName.empty())
            {
                if (auto levelId = OptionalInt32(levelInfo, L"level_id"))
                {
                    ladder.levelName = L"段位 " + std::to_wstring(*levelId);
                }
            }
            ladder.rank = OptionalInt32(levelInfo, L"rank");
            if (auto name = OptionalString(levelInfo, L"rank_name"))
            {
                ladder.rankName = *name;
            }
            ladder.changeScore = OptionalInt32(levelInfo, L"change_score");

            // ELO 系列字段在 level_info 中，值是字符串形式的浮点数。
            ladder.elo = OptionalInt32(levelInfo, L"origin_elo");
            ladder.originElo = OptionalInt32(levelInfo, L"origin_elo");
            ladder.changeElo = OptionalInt32(levelInfo, L"change_elo");

            // sts 作为回退来源（部分接口把 elo 放在这里）。
            if (!ladder.elo.has_value())
            {
                ladder.elo = OptionalInt32(sts, L"elo");
            }
            if (!ladder.originElo.has_value())
            {
                ladder.originElo = OptionalInt32(sts, L"origin_elo");
            }
            if (!ladder.changeElo.has_value())
            {
                ladder.changeElo = OptionalInt32(sts, L"change_elo");
            }
            return ladder;
        }

        UtilityStats ParseUtilityStats(JsonObjectRef const& fight)
        {
            UtilityStats utility;
            utility.bombPlants = OptionalInt32(fight, L"planted_bomb");
            utility.bombDefuses = OptionalInt32(fight, L"defused_bomb");
            utility.utilityDamage = OptionalInt32(fight, L"throw_harm");
            utility.enemyUtilityDamage = OptionalInt32(fight, L"throw_harm_enemy");
            utility.enemiesFlashed = OptionalInt32(fight, L"flash_enemy");
            utility.enemyFlashDuration = OptionalDouble(fight, L"flash_enemy_time");
            utility.teammatesFlashed = OptionalInt32(fight, L"flash_team");
            utility.teamFlashDuration = OptionalDouble(fight, L"flash_team_time");
            return utility;
        }

        ImpactStats ParseImpactStats(JsonObjectRef const& fight)
        {
            ImpactStats impact;
            impact.awpKills = OptionalInt32(fight, L"awp_kill");
            impact.jumpKills = OptionalInt32(fight, L"jump_kill");
            impact.knifeKills = OptionalInt32(fight, L"knife_kill");
            impact.entryKills = OptionalInt32(fight, L"entry_kill");
            impact.tradeKills = OptionalInt32(fight, L"trade_kill");
            impact.firstKills = OptionalInt32(fight, L"first_kill");
            impact.firstDeaths = OptionalInt32(fight, L"first_death");
            impact.perfectKills = OptionalInt32(fight, L"perfect_kill");
            impact.assistedKills = OptionalInt32(fight, L"assisted_kill");
            impact.revengeKills = OptionalInt32(fight, L"revenge_kill");
            impact.teamKills = OptionalInt32(fight, L"team_kill");
            return impact;
        }

        AwardFlags ParseAwardFlags(JsonObjectRef const& fight)
        {
            AwardFlags flags;
            flags.isMvp = OptionalBool(fight, L"is_mvp");
            flags.isSvp = OptionalBool(fight, L"is_svp");
            flags.isHighlight = OptionalBool(fight, L"is_highlight");
            return flags;
        }

        // 解析单个玩家条目。fight 缺失时退回用整个 row 取值。
        MatchPlayer ParsePlayer(JsonObjectRef const& row, TeamGroup group)
        {
            MatchPlayer player;
            player.group = group;

            auto fightValue = Get(row, L"fight");
            JsonObjectRef fight = row;
            if (!IsNull(fightValue) && fightValue.ValueType() == JsonValueKind::Object)
            {
                fight = fightValue.GetObject();
            }

            const auto userInfo = [&]() -> JsonObjectRef {
                auto value = Get(row, L"user_info");
                if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
                {
                    return value.GetObject();
                }
                return nullptr;
            }();

            JsonObjectRef userData = userInfo;
            JsonObjectRef profile = nullptr;
            if (userInfo)
            {
                auto nested = Get(userInfo, L"user_data");
                if (!IsNull(nested) && nested.ValueType() == JsonValueKind::Object)
                {
                    userData = nested.GetObject();
                }
            }
            if (userData)
            {
                auto nested = Get(userData, L"profile");
                if (!IsNull(nested) && nested.ValueType() == JsonValueKind::Object)
                {
                    profile = nested.GetObject();
                }
            }

            // 资料字段在 user_info / user_data / profile 多处出现，逐个回退。
            player.profile.playerId = FirstNonEmpty({
                OptionalString(fight, L"uid"),
                userData ? OptionalString(userData, L"uid") : std::nullopt,
                userInfo ? OptionalString(userInfo, L"uid") : std::nullopt,
            });
            player.profile.username = FirstNonEmpty({
                userData ? OptionalString(userData, L"username") : std::nullopt,
            });
            // 昵称：实测 5E 的 user_data.profile.nickname 恒为空字符串，
            // 真正用于显示的是 user_data.username（如 "盈水展千华"），
            // 因此把它放在首位，其余路径仅作回退。
            player.profile.nickname = FirstNonEmpty({
                userData ? OptionalString(userData, L"username") : std::nullopt,
                profile ? OptionalString(profile, L"nickname") : std::nullopt,
                userInfo ? OptionalString(userInfo, L"nick_name") : std::nullopt,
                userInfo ? OptionalString(userInfo, L"nickname") : std::nullopt,
                userData ? OptionalString(userData, L"nickname") : std::nullopt,
            });
            player.profile.avatarUrl = FirstNonEmpty({
                profile ? OptionalString(profile, L"avatarUrl") : std::nullopt,
                profile ? OptionalString(profile, L"avatar") : std::nullopt,
                profile ? OptionalString(profile, L"avatar_url") : std::nullopt,
                userInfo ? OptionalString(userInfo, L"avatar") : std::nullopt,
            });

            if (userData)
            {
                auto steamValue = Get(userData, L"steam");
                if (!IsNull(steamValue) && steamValue.ValueType() == JsonValueKind::Object)
                {
                    const JsonObjectRef steam = steamValue.GetObject();
                    player.profile.steamId = FirstNonEmpty({
                        OptionalString(steam, L"steamId"),
                        OptionalString(steam, L"steam_id"),
                    });
                    player.profile.steamNickname = FirstNonEmpty({
                        OptionalString(steam, L"personaname"),
                        OptionalString(steam, L"nickname"),
                    });
                }
                // domain 是 5E 的用户 ID（如 11814738gjdwn7），优先作为 playerId。
                if (auto domain = OptionalString(userData, L"domain"))
                {
                    if (!domain->empty())
                    {
                        player.profile.playerId = *domain;
                    }
                }
            }

            if (!player.profile.playerId.empty())
            {
                player.profile.profileUrl = L"https://www.5eplay.com/player/" + player.profile.playerId;
            }
            if (!player.profile.steamId.empty())
            {
                player.profile.steamProfileUrl =
                    L"https://steamcommunity.com/profiles/" + player.profile.steamId;
            }

            if (auto value = Get(row, L"level_info"); !IsNull(value) && value.ValueType() == JsonValueKind::Object)
            {
                JsonObjectRef sts = nullptr;
                if (auto stsValue = Get(row, L"sts"); !IsNull(stsValue) && stsValue.ValueType() == JsonValueKind::Object)
                {
                    sts = stsValue.GetObject();
                }
                player.ladder = ParseLadderStats(value.GetObject(), sts);
            }

            player.kills = OptionalInt32(fight, L"kill");
            player.deaths = OptionalInt32(fight, L"death");
            player.assists = OptionalInt32(fight, L"assist");
            player.rating = OptionalDouble(fight, L"rating2");
            if (!player.rating)
            {
                player.rating = OptionalDouble(fight, L"rating");
            }
            player.swingScore = OptionalDouble(fight, L"rating3");
            player.adr = OptionalDouble(fight, L"adr");
            player.rws = OptionalDouble(fight, L"rws");
            player.kast = OptionalDouble(fight, L"kast");
            player.headshots = OptionalInt32(fight, L"headshot");
            player.headshotRate = OptionalDouble(fight, L"per_headshot");
            player.firstKills = OptionalInt32(fight, L"first_kill");
            player.firstDeaths = OptionalInt32(fight, L"first_death");
            player.awpKills = OptionalInt32(fight, L"awp_kill");
            player.bombPlants = OptionalInt32(fight, L"planted_bomb");
            player.bombDefuses = OptionalInt32(fight, L"defused_bomb");

            ParseNumberedStats(fight, L"kill_", 1, 5, player.multiKills);
            ParseNumberedStats(fight, L"end_1v", 1, 5, player.clutches);

            // 多杀统计按惯例不计入单杀（kill_1）。
            int32_t multiKillCount = 0;
            for (auto const& [count, times] : player.multiKills)
            {
                if (count >= 2)
                {
                    multiKillCount += times;
                }
            }
            player.multiKillCount = multiKillCount;

            int32_t clutchCount = 0;
            for (auto const& [count, times] : player.clutches)
            {
                clutchCount += times;
            }
            player.clutchCount = clutchCount;

            for (auto const& [name, key] : { std::pair{ L"t", L"fight_t" }, { L"ct", L"fight_ct" } })
            {
                if (auto value = Get(row, key); !IsNull(value) && value.ValueType() == JsonValueKind::Object)
                {
                    player.sideStats[name] = ParseSideStats(value.GetObject());
                }
            }

            player.utility = ParseUtilityStats(fight);
            player.impact = ParseImpactStats(fight);
            player.awards = ParseAwardFlags(fight);
            return player;
        }

        MatchAwards ParseMatchAwards(JsonObjectRef const& main)
        {
            MatchAwards awards;
            auto take = [&main](std::wstring_view key) {
                auto value = OptionalString(main, key);
                return value ? *value : std::wstring{};
            };
            awards.mvpPlayerId = take(L"mvp_uid");
            awards.mostAssistsPlayerId = take(L"most_assist_uid");
            awards.mostAwpKillsPlayerId = take(L"most_awp_uid");
            awards.mostClutchesPlayerId = take(L"most_end_uid");
            awards.mostFirstKillsPlayerId = take(L"most_first_kill_uid");
            awards.mostHeadshotsPlayerId = take(L"most_headshot_uid");
            awards.mostJumpKillsPlayerId = take(L"most_jump_uid");
            awards.most1v2ClutchesPlayerId = take(L"most_1v2_uid");
            return awards;
        }
    }

    // 依次取第一个非空值。
    std::wstring FirstNonEmpty(std::initializer_list<std::optional<std::wstring>> values)
    {
        for (auto const& value : values)
        {
            if (value && !value->empty())
            {
                return *value;
            }
        }
        return {};
    }

    MatchSummary MatchSummaryFromJson(JsonObjectRef const& object)
    {
        MatchSummary summary;
        if (!object)
        {
            return summary;
        }

        if (auto value = OptionalString(object, L"match_id"))
        {
            summary.matchId = *value;
        }
        if (auto value = OptionalString(object, L"match_code"))
        {
            summary.matchCode = *value;
        }
        if (auto value = OptionalString(object, L"map"))
        {
            summary.mapName = *value;
        }
        summary.mapLabel = FirstNonEmpty({
            OptionalString(object, L"map_desc"),
            OptionalString(object, L"map_name"),
        });

        summary.isWin = OptionalBool(object, L"is_win");
        summary.startTime = OptionalInt64(object, L"start_time");
        summary.endTime = OptionalInt64(object, L"end_time");
        summary.kills = OptionalInt32(object, L"kill");
        summary.deaths = OptionalInt32(object, L"death");
        summary.rws = OptionalDouble(object, L"rws");
        summary.adr = OptionalDouble(object, L"adr");
        summary.rating = OptionalDouble(object, L"rating");
        summary.headshotRate = OptionalDouble(object, L"per_headshot");
        summary.assistCount = OptionalInt32(object, L"assist");
        summary.awpKills = OptionalInt32(object, L"awp_kill");
        summary.mvpCount = OptionalInt32(object, L"is_mvp");
        summary.roundTotal = OptionalInt32(object, L"round_total");
        summary.group1Score = OptionalInt32(object, L"group1_all_score");
        summary.group2Score = OptionalInt32(object, L"group2_all_score");
        summary.changeElo = OptionalInt32(object, L"change_elo");
        summary.location = FirstNonEmpty({
            OptionalString(object, L"location_full"),
            OptionalString(object, L"location"),
        });
        summary.serverIp = FirstNonEmpty({ OptionalString(object, L"server_ip") });
        summary.serverPort = OptionalInt32(object, L"server_port");
        summary.season = OptionalInt32(object, L"season");
        summary.demoUrl = FirstNonEmpty({ OptionalString(object, L"demo_url") });

        if (auto levelValue = Get(object, L"level_info"); !IsNull(levelValue) && levelValue.ValueType() == JsonValueKind::Object)
        {
            if (auto name = OptionalString(levelValue.GetObject(), L"level_name"))
            {
                summary.levelName = *name;
            }
        }
        if (auto status = Get(object, L"status"); !IsNull(status))
        {
            summary.status = ParseStatus(status);
        }

        summary.raw = CollectScalars(object);
        return summary;
    }

    std::vector<MatchSummary> MatchListFromJson(JsonValueRef const& data)
    {
        std::vector<MatchSummary> matches;
        if (IsNull(data) || data.ValueType() != JsonValueKind::Array)
        {
            return matches;
        }
        const JsonArrayRef array = data.GetArray();
        matches.reserve(array.Size());
        for (uint32_t i = 0; i < array.Size(); ++i)
        {
            const JsonValueRef item = array.GetAt(i);
            if (IsNull(item) || item.ValueType() != JsonValueKind::Object)
            {
                continue;
            }
            matches.push_back(MatchSummaryFromJson(item.GetObject()));
        }
        return matches;
    }

    MatchDetail MatchDetailFromJson(JsonObjectRef const& data)
    {
        MatchDetail detail;
        if (!data)
        {
            return detail;
        }

        JsonObjectRef main = nullptr;
        if (auto value = Get(data, L"main"); !IsNull(value) && value.ValueType() == JsonValueKind::Object)
        {
            main = value.GetObject();
        }

        if (main)
        {
            detail.matchId = FirstNonEmpty({
                OptionalString(main, L"match_code"),
                OptionalString(main, L"match_id"),
            });
            if (auto value = OptionalString(main, L"map"))
            {
                detail.mapName = *value;
            }
            if (auto value = OptionalString(main, L"map_desc"))
            {
                detail.mapLabel = *value;
            }
            if (auto value = OptionalString(main, L"match_winner"))
            {
                detail.matchWinner = *value;
            }
            detail.location = FirstNonEmpty({
                OptionalString(main, L"location_full"),
                OptionalString(main, L"location"),
            });
            if (auto value = OptionalString(main, L"demo_url"))
            {
                detail.demoUrl = *value;
            }
            if (auto value = OptionalString(main, L"server_ip"))
            {
                detail.serverIp = *value;
            }
            detail.serverPort = OptionalInt32(main, L"server_port");
            detail.roundTotal = OptionalInt32(main, L"round_total");
            detail.season = OptionalInt32(main, L"season");
            detail.year = OptionalInt32(main, L"year");
            detail.startTime = OptionalInt64(main, L"start_time");
            detail.endTime = OptionalInt64(main, L"end_time");
            detail.group1Score = OptionalInt32(main, L"group1_all_score");
            detail.group2Score = OptionalInt32(main, L"group2_all_score");
            detail.group1ChangeElo = OptionalInt32(main, L"group1_change_elo");
            detail.group2ChangeElo = OptionalInt32(main, L"group2_change_elo");
            if (auto status = Get(main, L"status"); !IsNull(status))
            {
                detail.status = ParseStatus(status);
            }
            detail.awards = ParseMatchAwards(main);
        }

        // match_id 缺失时退回 data 顶层的 id。
        if (detail.matchId.empty())
        {
            if (auto value = OptionalString(data, L"id"))
            {
                detail.matchId = *value;
            }
        }

        // 双方队伍：group_1 / group_2。
        for (auto const& [group, prefix] : {
                 std::pair{ TeamGroup::Group1, std::wstring_view(L"group1") },
                 { TeamGroup::Group2, std::wstring_view(L"group2") } })
        {
            MatchTeam team;
            team.group = group;
            const std::wstring teamKey = std::wstring(prefix) + L"_tid";
            if (auto value = OptionalString(main, teamKey))
            {
                team.teamId = *value;
            }
            const std::wstring uidsKey = std::wstring(prefix) + L"_uids";
            team.playerUids = SplitPlayerIds(Get(main, uidsKey));
            team.firstHalfSide = ParseSide(FirstNonEmpty({ OptionalString(main, std::wstring(prefix) + L"_fh_role") }));
            team.secondHalfSide = ParseSide(FirstNonEmpty({ OptionalString(main, std::wstring(prefix) + L"_sh_role") }));
            team.score = OptionalInt32(main, std::wstring(prefix) + L"_all_score");
            team.originElo = OptionalInt32(main, std::wstring(prefix) + L"_origin_elo");
            team.changeElo = OptionalInt32(main, std::wstring(prefix) + L"_change_elo");
            team.halfScores.firstHalf = OptionalInt32(main, std::wstring(prefix) + L"_fh_score");
            team.halfScores.secondHalf = OptionalInt32(main, std::wstring(prefix) + L"_sh_score");
            detail.teams.push_back(std::move(team));
        }

        // 玩家：group_1 / group_2 两个数组。
        for (auto const& [group, key] : {
                 std::pair{ TeamGroup::Group1, std::wstring_view(L"group_1") },
                 { TeamGroup::Group2, std::wstring_view(L"group_2") } })
        {
            auto value = Get(data, key);
            if (IsNull(value) || value.ValueType() != JsonValueKind::Array)
            {
                continue;
            }
            const JsonArrayRef array = value.GetArray();
            for (uint32_t i = 0; i < array.Size(); ++i)
            {
                const JsonValueRef item = array.GetAt(i);
                if (IsNull(item) || item.ValueType() != JsonValueKind::Object)
                {
                    continue;
                }
                detail.players.push_back(ParsePlayer(item.GetObject(), group));
            }
        }

        return detail;
    }

    std::wstring UuidFromJson(JsonObjectRef const& data)
    {
        if (!data)
        {
            return {};
        }
        if (auto value = OptionalString(data, L"uuid"))
        {
            return *value;
        }
        return {};
    }
}
