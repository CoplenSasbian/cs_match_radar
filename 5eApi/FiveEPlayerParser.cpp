#include "FiveEApiPch.h"
#include "FiveEPlayerParser.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>

#include <string>

using namespace FiveE::Json;

namespace FiveE::Parse
{
    namespace
    {
        // 安全取子对象。
        JsonObjectRef GetObject(JsonObjectRef const& parent, std::wstring_view key)
        {
            const auto value = Get(parent, key);
            if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
            {
                return value.GetObject();
            }
            return nullptr;
        }

        void ParseIdentity(JsonObjectRef const& uinfo, PlayerIdentity& out)
        {
            if (!uinfo)
            {
                return;
            }
            if (auto v = OptionalString(uinfo, L"uuid")) out.uuid = *v;
            if (auto v = OptionalString(uinfo, L"domain")) out.domain = *v;
            if (auto v = OptionalString(uinfo, L"username")) out.username = *v;
            if (auto v = OptionalString(uinfo, L"nickname")) out.nickname = *v;
            if (auto v = OptionalString(uinfo, L"avatar_url")) out.avatarUrl = *v;
            if (auto v = OptionalString(uinfo, L"steamid_64")) out.steamId64 = *v;
            if (auto v = OptionalString(uinfo, L"country_id")) out.countryId = *v;
            out.registerDate = OptionalInt64(uinfo, L"reg_date");

            // uid 在不同响应里可能是数字或字符串。
            if (auto v = OptionalString(uinfo, L"uid"))
            {
                out.uid = *v;
            }
            else if (auto n = OptionalInt32(uinfo, L"uid"))
            {
                out.uid = std::to_wstring(*n);
            }
        }

        void ParseCredit(JsonObjectRef const& uinfo, JsonObjectRef const& plusinfo, CreditInfo& out)
        {
            if (uinfo)
            {
                out.credit = OptionalInt32(uinfo, L"credit");
                out.trustedScore = OptionalInt32(uinfo, L"trusted_score");
                out.trustedStatus = OptionalInt32(uinfo, L"trusted_status");
                out.accountStatus = OptionalInt32(uinfo, L"status");
                out.usernameSpamStatus = OptionalInt32(uinfo, L"username_spam_status");
            }
            if (plusinfo)
            {
                out.vipLevel = OptionalInt32(plusinfo, L"vip_level");
            }
        }

        void ParseCareer(JsonObjectRef const& career, CareerStats& out)
        {
            if (!career)
            {
                return;
            }
            out.elo = OptionalDouble(career, L"elo");
            out.levelId = OptionalInt32(career, L"level_id");
            out.rank = OptionalInt32(career, L"rank");
            out.matchTotal = OptionalInt32(career, L"match_total");
            out.matchWin = OptionalInt32(career, L"match_win");
            out.matchLoss = OptionalInt32(career, L"match_loss");
            out.matchTie = OptionalInt32(career, L"match_tie");
            out.matchTimeTotal = OptionalInt32(career, L"match_time_total");
            out.starNum = OptionalInt32(career, L"star_num");
            if (auto v = OptionalString(career, L"best_season")) out.bestSeason = *v;
        }

        // 从 elo_info.modes["9"] 读当前赛季段位。
        void ParseRankFromElo(JsonObjectRef const& eloInfo, RankInfo& out)
        {
            if (!eloInfo)
            {
                return;
            }
            const auto modes = GetObject(eloInfo, L"modes");
            if (!modes)
            {
                return;
            }
            // 优先模式 9（优先排位）；否则取第一个可用模式。
            JsonObjectRef entry = GetObject(modes, L"9");
            if (!entry)
            {
                for (auto const& pair : modes)
                {
                    const auto value = pair.Value();
                    if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
                    {
                        entry = value.GetObject();
                        break;
                    }
                }
            }
            if (!entry)
            {
                return;
            }
            out.elo = OptionalDouble(entry, L"elo");
            out.levelId = OptionalInt32(entry, L"level_id");
            out.rank = OptionalInt32(entry, L"rank");
            out.seasonElo = OptionalDouble(entry, L"season_elo");
            out.starNum = OptionalInt32(entry, L"star_num");
            if (auto v = OptionalString(entry, L"season")) out.season = *v;
        }

        void ParseSeason(JsonObjectRef const& seasonData, SeasonStats& out)
        {
            if (!seasonData)
            {
                return;
            }
            if (auto v = OptionalString(seasonData, L"season")) out.season = *v;
            out.matchMode = OptionalInt32(seasonData, L"match_mode");
            out.matchTotal = OptionalInt32(seasonData, L"match_total");
            out.winTotal = OptionalInt32(seasonData, L"win_match_total");
            out.tieTotal = OptionalInt32(seasonData, L"tie_total");

            out.avgRating = OptionalDouble(seasonData, L"avg_rating");
            out.avgAdr = OptionalDouble(seasonData, L"avg_adr");
            out.avgRws = OptionalDouble(seasonData, L"avg_rws");
            out.avgKast = OptionalDouble(seasonData, L"kast");
            out.avgHeadshotRate = OptionalDouble(seasonData, L"per_headshot");
            out.avgKpr = OptionalDouble(seasonData, L"avg_kpr");
            out.avgImpact = OptionalDouble(seasonData, L"avg_impact");

            out.kills = OptionalInt32(seasonData, L"kill");
            out.deaths = OptionalInt32(seasonData, L"death");
            out.assists = OptionalInt32(seasonData, L"assist");
            out.headshots = OptionalInt32(seasonData, L"headshot");
            out.mvpTotal = OptionalInt32(seasonData, L"mvp_total");
            out.matchTimeTotal = OptionalInt32(seasonData, L"match_time_total");
            out.losingStreak = OptionalInt32(seasonData, L"losing_streak");

            out.maxRating = OptionalDouble(seasonData, L"max_rating");
            out.maxAdr = OptionalDouble(seasonData, L"max_adr");
            out.maxRws = OptionalDouble(seasonData, L"max_rws");
        }
    }

    PlayerHome PlayerHomeFromData(JsonObjectRef const& data)
    {
        PlayerHome profile;
        profile.platform = Platform::FiveE;
        if (!data)
        {
            return profile;
        }

        ParseIdentity(GetObject(data, L"uinfo"), profile.identity);
        ParseCredit(GetObject(data, L"uinfo"), GetObject(data, L"plusinfo"), profile.credit);
        ParseCareer(GetObject(data, L"career"), profile.career);
        ParseRankFromElo(GetObject(data, L"elo_info"), profile.rank);
        ParseSeason(GetObject(data, L"season_data"), profile.season);

        // elo 回退：elo_info 没有时用 career 的。
        if (!profile.rank.elo.has_value() && profile.career.elo.has_value())
        {
            profile.rank.elo = profile.career.elo;
        }
        if (!profile.rank.levelId.has_value() && profile.career.levelId.has_value())
        {
            profile.rank.levelId = profile.career.levelId;
        }
        if (!profile.rank.rank.has_value() && profile.career.rank.has_value())
        {
            profile.rank.rank = profile.career.rank;
        }
        if (profile.rank.season.empty())
        {
            profile.rank.season = profile.season.season;
        }
        return profile;
    }

    PlayerHome PlayerHomeFromJson(std::wstring const& responseBody)
    {
        PlayerHome profile;
        profile.platform = Platform::FiveE;
        profile.rawJson = responseBody;

        if (responseBody.empty())
        {
            return profile;
        }

        JsonObjectRef root{ nullptr };
        if (!JsonObjectRef::TryParse(responseBody, root) || !root)
        {
            return profile;
        }

        // 业务码：0 表示成功。非 0 时 data 可能为 null。
        const auto data = Get(root, L"data");
        if (IsNull(data) || data.ValueType() != JsonValueKind::Object)
        {
            return profile;
        }

        auto parsed = PlayerHomeFromData(data.GetObject());
        parsed.rawJson = responseBody;
        return parsed;
    }
}
