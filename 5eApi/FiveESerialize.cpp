#include "FiveEApiPch.h"
#include "FiveESerialize.h"

#include <winrt/Windows.Data.Json.h>

#include <string>
#include <vector>

using namespace winrt::Windows::Data::Json;

namespace FiveE::Json
{
    namespace
    {
        // ---- 基础写入 ----

        void Put(JsonObject& object, wchar_t const* key, std::wstring const& value)
        {
            if (!value.empty())
            {
                object.SetNamedValue(key, JsonValue::CreateStringValue(value));
            }
        }

        void PutRaw(JsonObject& object, wchar_t const* key, std::wstring const& value)
        {
            object.SetNamedValue(key, JsonValue::CreateStringValue(value));
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<int32_t> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateNumberValue(*value));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<int64_t> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateNumberValue(static_cast<double>(*value)));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<double> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateNumberValue(*value));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<bool> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateBooleanValue(*value));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, bool value)
        {
            object.SetNamedValue(key, JsonValue::CreateBooleanValue(value));
        }

        wchar_t const* SideText(Side side)
        {
            switch (side)
            {
            case Side::Terrorist: return L"t";
            case Side::CounterTerrorist: return L"ct";
            default: return L"unknown";
            }
        }

        wchar_t const* StatusText(MatchStatus status)
        {
            switch (status)
            {
            case MatchStatus::Finished: return L"finished";
            case MatchStatus::Ongoing: return L"ongoing";
            case MatchStatus::Cancelled: return L"cancelled";
            default: return L"unknown";
            }
        }

        // ---- 子结构 ----

        JsonObject ToObject(LadderStats const& value)
        {
            JsonObject o;
            Put(o, L"score", value.score);
            Put(o, L"level", value.level);
            Put(o, L"levelName", value.levelName);
            Put(o, L"rank", value.rank);
            Put(o, L"rankName", value.rankName);
            Put(o, L"changeScore", value.changeScore);
            Put(o, L"elo", value.elo);
            Put(o, L"originElo", value.originElo);
            Put(o, L"changeElo", value.changeElo);
            return o;
        }

        JsonObject ToObject(SideStats const& value)
        {
            JsonObject o;
            Put(o, L"kills", value.kills);
            Put(o, L"deaths", value.deaths);
            Put(o, L"assists", value.assists);
            Put(o, L"adr", value.adr);
            Put(o, L"rws", value.rws);
            Put(o, L"rating", value.rating);
            Put(o, L"swingScore", value.swingScore);
            Put(o, L"firstKills", value.firstKills);
            Put(o, L"firstDeaths", value.firstDeaths);
            return o;
        }

        JsonObject ToObject(UtilityStats const& value)
        {
            JsonObject o;
            Put(o, L"bombPlants", value.bombPlants);
            Put(o, L"bombDefuses", value.bombDefuses);
            Put(o, L"utilityDamage", value.utilityDamage);
            Put(o, L"enemyUtilityDamage", value.enemyUtilityDamage);
            Put(o, L"enemiesFlashed", value.enemiesFlashed);
            Put(o, L"enemyFlashDuration", value.enemyFlashDuration);
            Put(o, L"teammatesFlashed", value.teammatesFlashed);
            Put(o, L"teamFlashDuration", value.teamFlashDuration);
            return o;
        }

        JsonObject ToObject(ImpactStats const& value)
        {
            JsonObject o;
            Put(o, L"awpKills", value.awpKills);
            Put(o, L"jumpKills", value.jumpKills);
            Put(o, L"knifeKills", value.knifeKills);
            Put(o, L"entryKills", value.entryKills);
            Put(o, L"tradeKills", value.tradeKills);
            Put(o, L"firstKills", value.firstKills);
            Put(o, L"firstDeaths", value.firstDeaths);
            Put(o, L"perfectKills", value.perfectKills);
            Put(o, L"assistedKills", value.assistedKills);
            Put(o, L"revengeKills", value.revengeKills);
            Put(o, L"teamKills", value.teamKills);
            return o;
        }

        JsonObject ToObject(AwardFlags const& value)
        {
            JsonObject o;
            Put(o, L"isMvp", value.isMvp);
            Put(o, L"isSvp", value.isSvp);
            Put(o, L"isHighlight", value.isHighlight);
            return o;
        }

        JsonObject ToObject(PlayerProfile const& value)
        {
            JsonObject o;
            Put(o, L"playerId", value.playerId);
            Put(o, L"nickname", value.nickname);
            Put(o, L"username", value.username);
            Put(o, L"avatarUrl", value.avatarUrl);
            Put(o, L"steamId", value.steamId);
            Put(o, L"steamNickname", value.steamNickname);
            Put(o, L"profileUrl", value.profileUrl);
            Put(o, L"steamProfileUrl", value.steamProfileUrl);
            return o;
        }

        JsonObject ToObject(MatchPlayer const& value)
        {
            JsonObject o;
            o.SetNamedValue(L"profile", ToObject(value.profile));
            o.SetNamedValue(L"group", JsonValue::CreateNumberValue(static_cast<int32_t>(value.group)));
            o.SetNamedValue(L"ladder", ToObject(value.ladder));

            Put(o, L"kills", value.kills);
            Put(o, L"deaths", value.deaths);
            Put(o, L"assists", value.assists);
            Put(o, L"rating", value.rating);
            Put(o, L"swingScore", value.swingScore);
            Put(o, L"adr", value.adr);
            Put(o, L"rws", value.rws);
            Put(o, L"kast", value.kast);
            Put(o, L"headshots", value.headshots);
            Put(o, L"headshotRate", value.headshotRate);
            Put(o, L"firstKills", value.firstKills);
            Put(o, L"firstDeaths", value.firstDeaths);
            Put(o, L"awpKills", value.awpKills);
            Put(o, L"multiKillCount", value.multiKillCount);
            Put(o, L"clutchCount", value.clutchCount);
            Put(o, L"bombPlants", value.bombPlants);
            Put(o, L"bombDefuses", value.bombDefuses);

            // 多杀 / 残局明细（key 是档位数字，这里输出为字符串 key 的对象）。
            if (!value.multiKills.empty())
            {
                JsonObject kills;
                for (auto const& [count, times] : value.multiKills)
                {
                    kills.SetNamedValue(std::to_wstring(count),
                        JsonValue::CreateNumberValue(times));
                }
                o.SetNamedValue(L"multiKills", kills);
            }
            if (!value.clutches.empty())
            {
                JsonObject clutches;
                for (auto const& [count, times] : value.clutches)
                {
                    clutches.SetNamedValue(std::to_wstring(count),
                        JsonValue::CreateNumberValue(times));
                }
                o.SetNamedValue(L"clutches", clutches);
            }

            if (!value.sideStats.empty())
            {
                JsonObject sides;
                for (auto const& [name, stats] : value.sideStats)
                {
                    sides.SetNamedValue(name, ToObject(stats));
                }
                o.SetNamedValue(L"sideStats", sides);
            }

            o.SetNamedValue(L"utility", ToObject(value.utility));
            o.SetNamedValue(L"impact", ToObject(value.impact));
            o.SetNamedValue(L"awards", ToObject(value.awards));
            return o;
        }

        JsonObject ToObject(MatchTeam const& value)
        {
            JsonObject o;
            o.SetNamedValue(L"group", JsonValue::CreateNumberValue(static_cast<int32_t>(value.group)));
            Put(o, L"teamId", value.teamId);

            if (!value.playerUids.empty())
            {
                JsonArray uids;
                for (auto const& uid : value.playerUids)
                {
                    uids.Append(JsonValue::CreateStringValue(uid));
                }
                o.SetNamedValue(L"playerUids", uids);
            }

            o.SetNamedValue(L"firstHalfSide", JsonValue::CreateStringValue(SideText(value.firstHalfSide)));
            o.SetNamedValue(L"secondHalfSide", JsonValue::CreateStringValue(SideText(value.secondHalfSide)));
            Put(o, L"score", value.score);
            Put(o, L"originElo", value.originElo);
            Put(o, L"changeElo", value.changeElo);

            JsonObject half;
            Put(half, L"firstHalf", value.halfScores.firstHalf);
            Put(half, L"secondHalf", value.halfScores.secondHalf);
            o.SetNamedValue(L"halfScores", half);
            return o;
        }

        JsonObject ToObject(MatchAwards const& value)
        {
            JsonObject o;
            Put(o, L"mvpPlayerId", value.mvpPlayerId);
            Put(o, L"mostAssistsPlayerId", value.mostAssistsPlayerId);
            Put(o, L"mostAwpKillsPlayerId", value.mostAwpKillsPlayerId);
            Put(o, L"mostClutchesPlayerId", value.mostClutchesPlayerId);
            Put(o, L"mostFirstKillsPlayerId", value.mostFirstKillsPlayerId);
            Put(o, L"mostHeadshotsPlayerId", value.mostHeadshotsPlayerId);
            Put(o, L"mostJumpKillsPlayerId", value.mostJumpKillsPlayerId);
            Put(o, L"most1v2ClutchesPlayerId", value.most1v2ClutchesPlayerId);
            return o;
        }
    }

    std::wstring ToJson(MatchSummary const& value)
    {
        JsonObject o;
        Put(o, L"matchId", value.matchId);
        Put(o, L"matchCode", value.matchCode);
        Put(o, L"mapName", value.mapName);
        Put(o, L"mapLabel", value.mapLabel);
        Put(o, L"isWin", value.isWin);
        Put(o, L"startTime", value.startTime);
        Put(o, L"endTime", value.endTime);
        Put(o, L"kills", value.kills);
        Put(o, L"deaths", value.deaths);
        Put(o, L"rws", value.rws);
        Put(o, L"adr", value.adr);
        Put(o, L"rating", value.rating);
        Put(o, L"headshotRate", value.headshotRate);
        Put(o, L"assistCount", value.assistCount);
        Put(o, L"awpKills", value.awpKills);
        Put(o, L"mvpCount", value.mvpCount);
        Put(o, L"roundTotal", value.roundTotal);
        Put(o, L"group1Score", value.group1Score);
        Put(o, L"group2Score", value.group2Score);
        Put(o, L"changeElo", value.changeElo);
        Put(o, L"levelName", value.levelName);
        Put(o, L"location", value.location);
        Put(o, L"serverIp", value.serverIp);
        Put(o, L"serverPort", value.serverPort);
        Put(o, L"season", value.season);
        o.SetNamedValue(L"status", JsonValue::CreateStringValue(StatusText(value.status)));
        Put(o, L"demoUrl", value.demoUrl);

        // raw 是原始键值表（值是已转成文本的原始字段），保留给排查用。
        if (!value.raw.empty())
        {
            JsonObject raw;
            for (auto const& [key, text] : value.raw)
            {
                PutRaw(raw, key.c_str(), text);
            }
            o.SetNamedValue(L"raw", raw);
        }

        return std::wstring(o.Stringify());
    }

    std::wstring ToJson(std::vector<MatchSummary> const& values)
    {
        JsonArray array;
        for (auto const& item : values)
        {
            JsonObject o{ nullptr };
            if (JsonObject::TryParse(ToJson(item), o) && o)
            {
                array.Append(o);
            }
        }
        return std::wstring(array.Stringify());
    }

    std::wstring ToJson(MatchDetail const& value)
    {
        JsonObject o;
        Put(o, L"matchId", value.matchId);
        Put(o, L"mapName", value.mapName);
        Put(o, L"mapLabel", value.mapLabel);
        Put(o, L"matchWinner", value.matchWinner);
        Put(o, L"location", value.location);
        Put(o, L"demoUrl", value.demoUrl);
        Put(o, L"serverIp", value.serverIp);
        Put(o, L"serverPort", value.serverPort);
        Put(o, L"roundTotal", value.roundTotal);
        Put(o, L"season", value.season);
        Put(o, L"year", value.year);
        Put(o, L"startTime", value.startTime);
        Put(o, L"endTime", value.endTime);
        o.SetNamedValue(L"status", JsonValue::CreateStringValue(StatusText(value.status)));
        Put(o, L"group1Score", value.group1Score);
        Put(o, L"group2Score", value.group2Score);
        Put(o, L"group1ChangeElo", value.group1ChangeElo);
        Put(o, L"group2ChangeElo", value.group2ChangeElo);

        if (!value.teams.empty())
        {
            JsonArray teams;
            for (auto const& team : value.teams)
            {
                teams.Append(ToObject(team));
            }
            o.SetNamedValue(L"teams", teams);
        }

        if (!value.players.empty())
        {
            JsonArray players;
            for (auto const& player : value.players)
            {
                players.Append(ToObject(player));
            }
            o.SetNamedValue(L"players", players);
        }

        o.SetNamedValue(L"awards", ToObject(value.awards));

        // 可选补充接口的原始 JSON 文本。
        if (!value.extraRaw.empty())
        {
            JsonObject extra;
            for (auto const& [key, text] : value.extraRaw)
            {
                PutRaw(extra, key.c_str(), text);
            }
            o.SetNamedValue(L"extraRaw", extra);
        }

        return std::wstring(o.Stringify());
    }

    std::wstring ToJson(PlayerHome const& value)
    {
        JsonObject root;
        root.SetNamedValue(L"platform", JsonValue::CreateStringValue(L"5e"));

        // identity
        {
            JsonObject o;
            Put(o, L"uuid", value.identity.uuid);
            Put(o, L"uid", value.identity.uid);
            Put(o, L"username", value.identity.username);
            Put(o, L"nickname", value.identity.nickname);
            Put(o, L"domain", value.identity.domain);
            Put(o, L"avatarUrl", value.identity.avatarUrl);
            Put(o, L"steamId64", value.identity.steamId64);
            Put(o, L"registerDate", value.identity.registerDate);
            Put(o, L"countryId", value.identity.countryId);
            root.SetNamedValue(L"identity", o);
        }

        // credit
        {
            JsonObject o;
            Put(o, L"credit", value.credit.credit);
            Put(o, L"trustedScore", value.credit.trustedScore);
            Put(o, L"trustedStatus", value.credit.trustedStatus);
            Put(o, L"accountStatus", value.credit.accountStatus);
            Put(o, L"usernameSpamStatus", value.credit.usernameSpamStatus);
            Put(o, L"vipLevel", value.credit.vipLevel);
            Put(o, L"creditLow", value.credit.IsCreditLow());
            root.SetNamedValue(L"credit", o);
        }

        // rank
        {
            JsonObject o;
            Put(o, L"levelId", value.rank.levelId);
            Put(o, L"levelName", value.rank.levelName);
            Put(o, L"rank", value.rank.rank);
            Put(o, L"elo", value.rank.elo);
            Put(o, L"season", value.rank.season);
            Put(o, L"seasonElo", value.rank.seasonElo);
            Put(o, L"starNum", value.rank.starNum);
            Put(o, L"maxElo", value.rank.maxElo);
            root.SetNamedValue(L"rank", o);
        }

        // career
        {
            JsonObject o;
            Put(o, L"elo", value.career.elo);
            Put(o, L"levelId", value.career.levelId);
            Put(o, L"rank", value.career.rank);
            Put(o, L"matchTotal", value.career.matchTotal);
            Put(o, L"matchWin", value.career.matchWin);
            Put(o, L"matchLoss", value.career.matchLoss);
            Put(o, L"matchTie", value.career.matchTie);
            Put(o, L"matchTimeTotal", value.career.matchTimeTotal);
            Put(o, L"starNum", value.career.starNum);
            Put(o, L"bestSeason", value.career.bestSeason);
            root.SetNamedValue(L"career", o);
        }

        // season
        {
            JsonObject o;
            Put(o, L"season", value.season.season);
            Put(o, L"matchMode", value.season.matchMode);
            Put(o, L"matchTotal", value.season.matchTotal);
            Put(o, L"winTotal", value.season.winTotal);
            Put(o, L"tieTotal", value.season.tieTotal);
            Put(o, L"winRate", value.season.WinRate());
            Put(o, L"avgRating", value.season.avgRating);
            Put(o, L"avgAdr", value.season.avgAdr);
            Put(o, L"avgRws", value.season.avgRws);
            Put(o, L"avgKast", value.season.avgKast);
            Put(o, L"avgHeadshotRate", value.season.avgHeadshotRate);
            Put(o, L"avgKpr", value.season.avgKpr);
            Put(o, L"avgImpact", value.season.avgImpact);
            Put(o, L"kills", value.season.kills);
            Put(o, L"deaths", value.season.deaths);
            Put(o, L"assists", value.season.assists);
            Put(o, L"headshots", value.season.headshots);
            Put(o, L"mvpTotal", value.season.mvpTotal);
            Put(o, L"matchTimeTotal", value.season.matchTimeTotal);
            Put(o, L"losingStreak", value.season.losingStreak);
            Put(o, L"maxRating", value.season.maxRating);
            Put(o, L"maxAdr", value.season.maxAdr);
            Put(o, L"maxRws", value.season.maxRws);
            root.SetNamedValue(L"season", o);
        }

        return std::wstring(root.Stringify());
    }
}
