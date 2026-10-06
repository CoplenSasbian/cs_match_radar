#include "PerfectApiPch.h"
#include "PerfectSerialize.h"

#include <winrt/Windows.Data.Json.h>

#include <string>
#include <vector>

using namespace winrt::Windows::Data::Json;

namespace Perfect::Json
{
    namespace
    {
        void Put(JsonObject& object, wchar_t const* key, std::wstring const& value)
        {
            if (!value.empty())
            {
                object.SetNamedValue(key, JsonValue::CreateStringValue(value));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<int32_t> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateNumberValue(*value));
            }
        }

        void Put(JsonObject& object, wchar_t const* key, std::optional<double> const& value)
        {
            if (value.has_value())
            {
                object.SetNamedValue(key, JsonValue::CreateNumberValue(*value));
            }
        }

        wchar_t const* TierName(SStarTier tier)
        {
            switch (tier)
            {
            case SStarTier::Normal:  return L"normal";
            case SStarTier::Gold:    return L"gold";
            case SStarTier::Diamond: return L"diamond";
            case SStarTier::Demon:   return L"demon";
            default:                return L"none";
            }
        }

        JsonObject ToObject(RankScore const& value)
        {
            JsonObject o;
            Put(o, L"score", value.score);
            Put(o, L"stars", value.stars);
            Put(o, L"season", value.season);
            Put(o, L"seasonShowName", value.seasonShowName);
            Put(o, L"display", value.Display());
            o.SetNamedValue(L"tier", JsonValue::CreateStringValue(TierName(value.Tier())));
            return o;
        }

        JsonObject ToObject(PerfectIdentity const& value)
        {
            JsonObject o;
            Put(o, L"steamId", value.steamId);
            Put(o, L"uid", value.uid);
            Put(o, L"userId", value.userId);
            Put(o, L"name", value.name);
            Put(o, L"nickname", value.nickname);
            Put(o, L"avatar", value.avatar);
            Put(o, L"zqId", value.zqId);
            Put(o, L"perfectPower", value.perfectPower);
            return o;
        }

        JsonObject ToObject(PerfectLadderStats const& value)
        {
            JsonObject o;
            Put(o, L"score", value.score);
            Put(o, L"scoreChange", value.scoreChange);
            Put(o, L"matchCount", value.matchCount);
            Put(o, L"winNum", value.winNum);
            Put(o, L"drawNum", value.drawNum);
            Put(o, L"winRate", value.winRate);
            Put(o, L"roundCount", value.roundCount);

            Put(o, L"ratingAvg", value.ratingAvg);
            Put(o, L"ratingCtAvg", value.ratingCtAvg);
            Put(o, L"ratingTAvg", value.ratingTAvg);
            Put(o, L"adpr", value.adpr);
            Put(o, L"rwsAvg", value.rwsAvg);
            Put(o, L"priAvg", value.priAvg);
            Put(o, L"killNum", value.killNum);
            Put(o, L"deathNum", value.deathNum);
            Put(o, L"assistNum", value.assistNum);
            Put(o, L"kastTotal", value.kastTotal);

            Put(o, L"hsKillRate", value.hsKillRate);
            Put(o, L"headshotKillNum", value.headshotKillNum);
            Put(o, L"firstKillNum", value.firstKillNum);
            Put(o, L"firstDeathNum", value.firstDeathNum);
            Put(o, L"firstKillWinRound", value.firstKillWinRound);

            Put(o, L"1v1Num", value.c1v1Num);
            Put(o, L"1v2Num", value.c1v2Num);
            Put(o, L"1v3Num", value.c1v3Num);
            Put(o, L"1v4Num", value.c1v4Num);
            Put(o, L"1v5Num", value.c1v5Num);
            Put(o, L"1v1Total", value.c1v1Total);
            Put(o, L"1v2Total", value.c1v2Total);
            Put(o, L"1v3Total", value.c1v3Total);
            Put(o, L"1v4Total", value.c1v4Total);
            Put(o, L"1v5Total", value.c1v5Total);
            Put(o, L"1vxRate", value.c1vxRate);

            Put(o, L"twoKillNum", value.twoKillNum);
            Put(o, L"threeKillNum", value.threeKillNum);
            Put(o, L"fourKillNum", value.fourKillNum);
            Put(o, L"fiveKillNum", value.fiveKillNum);
            Put(o, L"mvpNum", value.mvpNum);
            Put(o, L"matchMvpNum", value.matchMvpNum);

            Put(o, L"sniperKillNum", value.sniperKillNum);
            Put(o, L"awpKillTotal", value.awpKillTotal);

            Put(o, L"currSStars", value.currSStars);
            Put(o, L"currSLevel", value.currSLevel);
            Put(o, L"prevSStars", value.prevSStars);
            Put(o, L"changedStars", value.changedStars);

            // 派生指标（分母为 0 时不会出现）。
            Put(o, L"kd", value.Kd());
            Put(o, L"kast", value.Kast());
            Put(o, L"oneVOneRate", value.OneVOneRate());
            Put(o, L"firstKillRate", value.FirstKillRate());
            Put(o, L"entryRate", value.EntryRate());
            return o;
        }
    }

    std::wstring ToJson(PerfectPlayer const& value)
    {
        JsonObject root;
        root.SetNamedValue(L"platform", JsonValue::CreateStringValue(L"perfect"));
        root.SetNamedValue(L"identity", ToObject(value.identity));
        root.SetNamedValue(L"currentRank", ToObject(value.currentRank));
        root.SetNamedValue(L"peakRank", ToObject(value.peakRank));
        root.SetNamedValue(L"ladder", ToObject(value.ladder));

        if (!value.radar.empty())
        {
            JsonArray radar;
            for (auto const& dim : value.radar)
            {
                JsonObject o;
                Put(o, L"key", dim.key);
                Put(o, L"score", dim.score);
                Put(o, L"level", dim.level);
                Put(o, L"description", dim.description);
                radar.Append(o);
            }
            root.SetNamedValue(L"radar", radar);
        }

        if (!value.maps.empty())
        {
            JsonArray maps;
            for (auto const& m : value.maps)
            {
                JsonObject o;
                Put(o, L"map", m.map);
                Put(o, L"nameCn", m.nameCn);
                Put(o, L"nameEn", m.nameEn);
                Put(o, L"totalNum", m.totalNum);
                Put(o, L"roundCount", m.roundCount);
                Put(o, L"winNum", m.winNum);
                Put(o, L"winRate", m.winRate);
                Put(o, L"adpr", m.adpr);
                Put(o, L"ratingAvg", m.ratingAvg);
                Put(o, L"rwsAvg", m.rwsAvg);
                Put(o, L"killNum", m.killNum);
                Put(o, L"headshotKillNum", m.headshotKillNum);
                maps.Append(o);
            }
            root.SetNamedValue(L"maps", maps);
        }

        if (!value.weapons.empty())
        {
            JsonArray weapons;
            for (auto const& w : value.weapons)
            {
                JsonObject o;
                Put(o, L"name", w.name);
                Put(o, L"nameCn", w.nameCn);
                Put(o, L"nameEn", w.nameEn);
                Put(o, L"matchNum", w.matchNum);
                Put(o, L"killNum", w.killNum);
                Put(o, L"avgKillNum", w.avgKillNum);
                Put(o, L"headshotRate", w.headshotRate);
                Put(o, L"firstShotAccuracy", w.firstShotAccuracy);
                Put(o, L"sprayAccuracy", w.sprayAccuracy);
                Put(o, L"rapidStopSuccessRate", w.rapidStopSuccessRate);
                Put(o, L"avgTimeToKill", w.avgTimeToKill);
                weapons.Append(o);
            }
            root.SetNamedValue(L"weapons", weapons);
        }

        Put(root, L"seasonWe", value.seasonWe);
        root.SetNamedValue(L"partialFailure", JsonValue::CreateBooleanValue(value.partialFailure));
        Put(root, L"failureReason", value.failureReason);

        return std::wstring(root.Stringify());
    }

    std::wstring ToOverviewJson(PerfectPlayer const& value)
    {
        JsonObject root;
        root.SetNamedValue(L"platform", JsonValue::CreateStringValue(L"perfect"));
        root.SetNamedValue(L"identity", ToObject(value.identity));
        root.SetNamedValue(L"currentRank", ToObject(value.currentRank));
        return std::wstring(root.Stringify());
    }

    std::wstring ToJson(std::vector<std::wstring> const& values)
    {
        JsonArray array;
        for (auto const& item : values)
        {
            array.Append(JsonValue::CreateStringValue(item));
        }
        return std::wstring(array.Stringify());
    }
}
