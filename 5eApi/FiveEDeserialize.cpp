#include "FiveEApiPch.h"
#include "FiveEDeserialize.h"
#include "FiveESerialize.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>

#include <string>
#include <vector>

using namespace winrt::Windows::Data::Json;
using namespace FiveE::Json;

namespace FiveE::Json
{
    namespace
    {
        JsonObjectRef GetObject(JsonObjectRef const& parent, wchar_t const* key)
        {
            const auto value = Get(parent, key);
            if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
            {
                return value.GetObject();
            }
            return nullptr;
        }

        Side ToSide(std::wstring const& text)
        {
            return ParseSide(text);
        }

        MatchStatus ToStatus(std::wstring const& text)
        {
            if (text == L"finished") return MatchStatus::Finished;
            if (text == L"ongoing") return MatchStatus::Ongoing;
            if (text == L"cancelled") return MatchStatus::Cancelled;
            return MatchStatus::Unknown;
        }

        void ReadLadder(JsonObjectRef const& o, LadderStats& out)
        {
            if (!o) return;
            out.score = OptionalInt32(o, L"score");
            out.level = OptionalInt32(o, L"level");
            out.levelName = OptionalString(o, L"levelName").value_or(L"");
            out.rank = OptionalInt32(o, L"rank");
            out.rankName = OptionalString(o, L"rankName").value_or(L"");
            out.changeScore = OptionalInt32(o, L"changeScore");
            out.elo = OptionalInt32(o, L"elo");
            out.originElo = OptionalInt32(o, L"originElo");
            out.changeElo = OptionalInt32(o, L"changeElo");
        }

        void ReadSideStats(JsonObjectRef const& o, SideStats& out)
        {
            if (!o) return;
            out.kills = OptionalInt32(o, L"kills");
            out.deaths = OptionalInt32(o, L"deaths");
            out.assists = OptionalInt32(o, L"assists");
            out.adr = OptionalDouble(o, L"adr");
            out.rws = OptionalDouble(o, L"rws");
            out.rating = OptionalDouble(o, L"rating");
            out.swingScore = OptionalDouble(o, L"swingScore");
            out.firstKills = OptionalInt32(o, L"firstKills");
            out.firstDeaths = OptionalInt32(o, L"firstDeaths");
        }

        void ReadUtility(JsonObjectRef const& o, UtilityStats& out)
        {
            if (!o) return;
            out.bombPlants = OptionalInt32(o, L"bombPlants");
            out.bombDefuses = OptionalInt32(o, L"bombDefuses");
            out.utilityDamage = OptionalInt32(o, L"utilityDamage");
            out.enemyUtilityDamage = OptionalInt32(o, L"enemyUtilityDamage");
            out.enemiesFlashed = OptionalInt32(o, L"enemiesFlashed");
            out.enemyFlashDuration = OptionalDouble(o, L"enemyFlashDuration");
            out.teammatesFlashed = OptionalInt32(o, L"teammatesFlashed");
            out.teamFlashDuration = OptionalDouble(o, L"teamFlashDuration");
        }

        void ReadImpact(JsonObjectRef const& o, ImpactStats& out)
        {
            if (!o) return;
            out.awpKills = OptionalInt32(o, L"awpKills");
            out.jumpKills = OptionalInt32(o, L"jumpKills");
            out.knifeKills = OptionalInt32(o, L"knifeKills");
            out.entryKills = OptionalInt32(o, L"entryKills");
            out.tradeKills = OptionalInt32(o, L"tradeKills");
            out.firstKills = OptionalInt32(o, L"firstKills");
            out.firstDeaths = OptionalInt32(o, L"firstDeaths");
            out.perfectKills = OptionalInt32(o, L"perfectKills");
            out.assistedKills = OptionalInt32(o, L"assistedKills");
            out.revengeKills = OptionalInt32(o, L"revengeKills");
            out.teamKills = OptionalInt32(o, L"teamKills");
        }

        void ReadAwards(JsonObjectRef const& o, AwardFlags& out)
        {
            if (!o) return;
            out.isMvp = OptionalBool(o, L"isMvp");
            out.isSvp = OptionalBool(o, L"isSvp");
            out.isHighlight = OptionalBool(o, L"isHighlight");
        }

        void ReadProfile(JsonObjectRef const& o, PlayerProfile& out)
        {
            if (!o) return;
            out.playerId = OptionalString(o, L"playerId").value_or(L"");
            out.nickname = OptionalString(o, L"nickname").value_or(L"");
            out.username = OptionalString(o, L"username").value_or(L"");
            out.avatarUrl = OptionalString(o, L"avatarUrl").value_or(L"");
            out.steamId = OptionalString(o, L"steamId").value_or(L"");
            out.steamNickname = OptionalString(o, L"steamNickname").value_or(L"");
            out.profileUrl = OptionalString(o, L"profileUrl").value_or(L"");
            out.steamProfileUrl = OptionalString(o, L"steamProfileUrl").value_or(L"");
        }

        void ReadNumberedObject(JsonObjectRef const& parent, wchar_t const* key,
                                std::map<int32_t, int32_t>& out)
        {
            const auto o = GetObject(parent, key);
            if (!o) return;
            for (auto const& pair : o)
            {
                const std::wstring name(pair.Key());
                if (name.empty()) continue;
                // key 是档位数字的字符串形式。
                const int32_t index = std::stoi(name);
                const auto value = pair.Value();
                if (!IsNull(value) && value.ValueType() == JsonValueKind::Number)
                {
                    out[index] = static_cast<int32_t>(value.GetNumber());
                }
            }
        }

        void ReadPlayer(JsonObjectRef const& o, MatchPlayer& out)
        {
            if (!o) return;
            ReadProfile(GetObject(o, L"profile"), out.profile);
            if (auto group = OptionalInt32(o, L"group"))
            {
                out.group = static_cast<TeamGroup>(*group);
            }
            ReadLadder(GetObject(o, L"ladder"), out.ladder);

            out.kills = OptionalInt32(o, L"kills");
            out.deaths = OptionalInt32(o, L"deaths");
            out.assists = OptionalInt32(o, L"assists");
            out.rating = OptionalDouble(o, L"rating");
            out.swingScore = OptionalDouble(o, L"swingScore");
            out.adr = OptionalDouble(o, L"adr");
            out.rws = OptionalDouble(o, L"rws");
            out.kast = OptionalDouble(o, L"kast");
            out.headshots = OptionalInt32(o, L"headshots");
            out.headshotRate = OptionalDouble(o, L"headshotRate");
            out.firstKills = OptionalInt32(o, L"firstKills");
            out.firstDeaths = OptionalInt32(o, L"firstDeaths");
            out.awpKills = OptionalInt32(o, L"awpKills");
            out.multiKillCount = OptionalInt32(o, L"multiKillCount");
            out.clutchCount = OptionalInt32(o, L"clutchCount");
            out.bombPlants = OptionalInt32(o, L"bombPlants");
            out.bombDefuses = OptionalInt32(o, L"bombDefuses");

            ReadNumberedObject(o, L"multiKills", out.multiKills);
            ReadNumberedObject(o, L"clutches", out.clutches);

            if (const auto sides = GetObject(o, L"sideStats"))
            {
                for (auto const& pair : sides)
                {
                    const auto value = pair.Value();
                    if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
                    {
                        SideStats stats;
                        ReadSideStats(value.GetObject(), stats);
                        out.sideStats[std::wstring(pair.Key())] = stats;
                    }
                }
            }

            ReadUtility(GetObject(o, L"utility"), out.utility);
            ReadImpact(GetObject(o, L"impact"), out.impact);
            ReadAwards(GetObject(o, L"awards"), out.awards);
        }

        void ReadTeam(JsonObjectRef const& o, MatchTeam& out)
        {
            if (!o) return;
            if (auto group = OptionalInt32(o, L"group"))
            {
                out.group = static_cast<TeamGroup>(*group);
            }
            out.teamId = OptionalString(o, L"teamId").value_or(L"");

            const auto uids = Get(o, L"playerUids");
            if (!IsNull(uids) && uids.ValueType() == JsonValueKind::Array)
            {
                const auto array = uids.GetArray();
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    if (auto text = OptionalString(array.GetAt(i)))
                    {
                        out.playerUids.push_back(*text);
                    }
                }
            }

            out.firstHalfSide = ToSide(OptionalString(o, L"firstHalfSide").value_or(L""));
            out.secondHalfSide = ToSide(OptionalString(o, L"secondHalfSide").value_or(L""));
            out.score = OptionalInt32(o, L"score");
            out.originElo = OptionalInt32(o, L"originElo");
            out.changeElo = OptionalInt32(o, L"changeElo");

            if (const auto half = GetObject(o, L"halfScores"))
            {
                out.halfScores.firstHalf = OptionalInt32(half, L"firstHalf");
                out.halfScores.secondHalf = OptionalInt32(half, L"secondHalf");
            }
        }

        void ReadMatchAwards(JsonObjectRef const& o, MatchAwards& out)
        {
            if (!o) return;
            out.mvpPlayerId = OptionalString(o, L"mvpPlayerId").value_or(L"");
            out.mostAssistsPlayerId = OptionalString(o, L"mostAssistsPlayerId").value_or(L"");
            out.mostAwpKillsPlayerId = OptionalString(o, L"mostAwpKillsPlayerId").value_or(L"");
            out.mostClutchesPlayerId = OptionalString(o, L"mostClutchesPlayerId").value_or(L"");
            out.mostFirstKillsPlayerId = OptionalString(o, L"mostFirstKillsPlayerId").value_or(L"");
            out.mostHeadshotsPlayerId = OptionalString(o, L"mostHeadshotsPlayerId").value_or(L"");
            out.mostJumpKillsPlayerId = OptionalString(o, L"mostJumpKillsPlayerId").value_or(L"");
            out.most1v2ClutchesPlayerId = OptionalString(o, L"most1v2ClutchesPlayerId").value_or(L"");
        }
    }

    bool FromJson(std::wstring const& text, MatchSummary& out)
    {
        JsonObjectRef root{ nullptr };
        if (!JsonObject::TryParse(text, root) || !root)
        {
            return false;
        }
        out.matchId = OptionalString(root, L"matchId").value_or(L"");
        out.matchCode = OptionalString(root, L"matchCode").value_or(L"");
        out.mapName = OptionalString(root, L"mapName").value_or(L"");
        out.mapLabel = OptionalString(root, L"mapLabel").value_or(L"");
        out.isWin = OptionalBool(root, L"isWin");
        out.startTime = OptionalInt64(root, L"startTime");
        out.endTime = OptionalInt64(root, L"endTime");
        out.kills = OptionalInt32(root, L"kills");
        out.deaths = OptionalInt32(root, L"deaths");
        out.rws = OptionalDouble(root, L"rws");
        out.adr = OptionalDouble(root, L"adr");
        out.rating = OptionalDouble(root, L"rating");
        out.headshotRate = OptionalDouble(root, L"headshotRate");
        out.assistCount = OptionalInt32(root, L"assistCount");
        out.awpKills = OptionalInt32(root, L"awpKills");
        out.mvpCount = OptionalInt32(root, L"mvpCount");
        out.roundTotal = OptionalInt32(root, L"roundTotal");
        out.group1Score = OptionalInt32(root, L"group1Score");
        out.group2Score = OptionalInt32(root, L"group2Score");
        out.changeElo = OptionalInt32(root, L"changeElo");
        out.levelName = OptionalString(root, L"levelName").value_or(L"");
        out.location = OptionalString(root, L"location").value_or(L"");
        out.serverIp = OptionalString(root, L"serverIp").value_or(L"");
        out.serverPort = OptionalInt32(root, L"serverPort");
        out.season = OptionalInt32(root, L"season");
        out.status = ToStatus(OptionalString(root, L"status").value_or(L""));
        out.demoUrl = OptionalString(root, L"demoUrl").value_or(L"");
        return true;
    }

    std::vector<MatchSummary> MatchListFromJsonText(std::wstring const& text)
    {
        std::vector<MatchSummary> result;
        JsonArrayRef array{ nullptr };
        if (!JsonArray::TryParse(text, array) || !array)
        {
            return result;
        }
        result.reserve(array.Size());
        for (uint32_t i = 0; i < array.Size(); ++i)
        {
            const auto item = array.GetAt(i);
            if (IsNull(item) || item.ValueType() != JsonValueKind::Object)
            {
                continue;
            }
            MatchSummary summary;
            if (FromJson(std::wstring(item.GetObject().Stringify()), summary))
            {
                result.push_back(std::move(summary));
            }
        }
        return result;
    }

    bool FromJson(std::wstring const& text, MatchDetail& out)
    {
        JsonObjectRef root{ nullptr };
        if (!JsonObject::TryParse(text, root) || !root)
        {
            return false;
        }
        out.matchId = OptionalString(root, L"matchId").value_or(L"");
        out.mapName = OptionalString(root, L"mapName").value_or(L"");
        out.mapLabel = OptionalString(root, L"mapLabel").value_or(L"");
        out.matchWinner = OptionalString(root, L"matchWinner").value_or(L"");
        out.location = OptionalString(root, L"location").value_or(L"");
        out.demoUrl = OptionalString(root, L"demoUrl").value_or(L"");
        out.serverIp = OptionalString(root, L"serverIp").value_or(L"");
        out.serverPort = OptionalInt32(root, L"serverPort");
        out.roundTotal = OptionalInt32(root, L"roundTotal");
        out.season = OptionalInt32(root, L"season");
        out.year = OptionalInt32(root, L"year");
        out.startTime = OptionalInt64(root, L"startTime");
        out.endTime = OptionalInt64(root, L"endTime");
        out.status = ToStatus(OptionalString(root, L"status").value_or(L""));
        out.group1Score = OptionalInt32(root, L"group1Score");
        out.group2Score = OptionalInt32(root, L"group2Score");
        out.group1ChangeElo = OptionalInt32(root, L"group1ChangeElo");
        out.group2ChangeElo = OptionalInt32(root, L"group2ChangeElo");

        const auto teams = Get(root, L"teams");
        if (!IsNull(teams) && teams.ValueType() == JsonValueKind::Array)
        {
            const auto array = teams.GetArray();
            for (uint32_t i = 0; i < array.Size(); ++i)
            {
                const auto item = array.GetAt(i);
                if (!IsNull(item) && item.ValueType() == JsonValueKind::Object)
                {
                    MatchTeam team;
                    ReadTeam(item.GetObject(), team);
                    out.teams.push_back(std::move(team));
                }
            }
        }

        const auto players = Get(root, L"players");
        if (!IsNull(players) && players.ValueType() == JsonValueKind::Array)
        {
            const auto array = players.GetArray();
            for (uint32_t i = 0; i < array.Size(); ++i)
            {
                const auto item = array.GetAt(i);
                if (!IsNull(item) && item.ValueType() == JsonValueKind::Object)
                {
                    MatchPlayer player;
                    ReadPlayer(item.GetObject(), player);
                    out.players.push_back(std::move(player));
                }
            }
        }

        ReadMatchAwards(GetObject(root, L"awards"), out.awards);

        if (const auto extra = GetObject(root, L"extraRaw"))
        {
            for (auto const& pair : extra)
            {
                if (auto text2 = OptionalString(pair.Value()))
                {
                    out.extraRaw[std::wstring(pair.Key())] = *text2;
                }
            }
        }
        return true;
    }

    MatchDetail MatchDetailFromJsonText(std::wstring const& text)
    {
        MatchDetail detail;
        FromJson(text, detail);
        return detail;
    }

    bool FromJson(std::wstring const& text, PlayerHome& out)
    {
        JsonObjectRef root{ nullptr };
        if (!JsonObject::TryParse(text, root) || !root)
        {
            return false;
        }

        if (const auto o = GetObject(root, L"identity"))
        {
            out.identity.uuid = OptionalString(o, L"uuid").value_or(L"");
            out.identity.uid = OptionalString(o, L"uid").value_or(L"");
            out.identity.username = OptionalString(o, L"username").value_or(L"");
            out.identity.nickname = OptionalString(o, L"nickname").value_or(L"");
            out.identity.domain = OptionalString(o, L"domain").value_or(L"");
            out.identity.avatarUrl = OptionalString(o, L"avatarUrl").value_or(L"");
            out.identity.steamId64 = OptionalString(o, L"steamId64").value_or(L"");
            out.identity.registerDate = OptionalInt64(o, L"registerDate");
            out.identity.countryId = OptionalString(o, L"countryId").value_or(L"");
        }

        if (const auto o = GetObject(root, L"credit"))
        {
            out.credit.credit = OptionalInt32(o, L"credit");
            out.credit.trustedScore = OptionalInt32(o, L"trustedScore");
            out.credit.trustedStatus = OptionalInt32(o, L"trustedStatus");
            out.credit.accountStatus = OptionalInt32(o, L"accountStatus");
            out.credit.usernameSpamStatus = OptionalInt32(o, L"usernameSpamStatus");
            out.credit.vipLevel = OptionalInt32(o, L"vipLevel");
        }

        if (const auto o = GetObject(root, L"rank"))
        {
            out.rank.levelId = OptionalInt32(o, L"levelId");
            out.rank.levelName = OptionalString(o, L"levelName").value_or(L"");
            out.rank.rank = OptionalInt32(o, L"rank");
            out.rank.elo = OptionalDouble(o, L"elo");
            out.rank.season = OptionalString(o, L"season").value_or(L"");
            out.rank.seasonElo = OptionalDouble(o, L"seasonElo");
            out.rank.starNum = OptionalInt32(o, L"starNum");
            out.rank.maxElo = OptionalDouble(o, L"maxElo");
        }

        if (const auto o = GetObject(root, L"career"))
        {
            out.career.elo = OptionalDouble(o, L"elo");
            out.career.levelId = OptionalInt32(o, L"levelId");
            out.career.rank = OptionalInt32(o, L"rank");
            out.career.matchTotal = OptionalInt32(o, L"matchTotal");
            out.career.matchWin = OptionalInt32(o, L"matchWin");
            out.career.matchLoss = OptionalInt32(o, L"matchLoss");
            out.career.matchTie = OptionalInt32(o, L"matchTie");
            out.career.matchTimeTotal = OptionalInt32(o, L"matchTimeTotal");
            out.career.starNum = OptionalInt32(o, L"starNum");
            out.career.bestSeason = OptionalString(o, L"bestSeason").value_or(L"");
        }

        if (const auto o = GetObject(root, L"season"))
        {
            out.season.season = OptionalString(o, L"season").value_or(L"");
            out.season.matchMode = OptionalInt32(o, L"matchMode");
            out.season.matchTotal = OptionalInt32(o, L"matchTotal");
            out.season.winTotal = OptionalInt32(o, L"winTotal");
            out.season.tieTotal = OptionalInt32(o, L"tieTotal");
            out.season.avgRating = OptionalDouble(o, L"avgRating");
            out.season.avgAdr = OptionalDouble(o, L"avgAdr");
            out.season.avgRws = OptionalDouble(o, L"avgRws");
            out.season.avgKast = OptionalDouble(o, L"avgKast");
            out.season.avgHeadshotRate = OptionalDouble(o, L"avgHeadshotRate");
            out.season.avgKpr = OptionalDouble(o, L"avgKpr");
            out.season.avgImpact = OptionalDouble(o, L"avgImpact");
            out.season.kills = OptionalInt32(o, L"kills");
            out.season.deaths = OptionalInt32(o, L"deaths");
            out.season.assists = OptionalInt32(o, L"assists");
            out.season.headshots = OptionalInt32(o, L"headshots");
            out.season.mvpTotal = OptionalInt32(o, L"mvpTotal");
            out.season.matchTimeTotal = OptionalInt32(o, L"matchTimeTotal");
            out.season.losingStreak = OptionalInt32(o, L"losingStreak");
            out.season.maxRating = OptionalDouble(o, L"maxRating");
            out.season.maxAdr = OptionalDouble(o, L"maxAdr");
            out.season.maxRws = OptionalDouble(o, L"maxRws");
        }
        return true;
    }

    PlayerHome PlayerHomeFromJsonText(std::wstring const& text)
    {
        PlayerHome home;
        FromJson(text, home);
        return home;
    }
}
