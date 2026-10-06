#include "PerfectApiPch.h"
#include "PerfectParser.h"
#include "PerfectModels.h"
#include "PerfectJson.h"

#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <string>

using namespace Perfect::Json;

namespace Perfect
{
    // RankScore::Tier / Display 与 PerfectLadderStats 的派生指标
    // 已移入 PerfectModels.h 作为 inline 实现（调用方需要直接使用它们）。

    namespace
    {
        JsonObjectRef GetObject(JsonObjectRef const& parent, std::wstring_view key)
        {
            const auto value = Get(parent, key);
            if (!IsNull(value) && value.ValueType() == JsonValueKind::Object)
            {
                return value.GetObject();
            }
            return nullptr;
        }

        JsonArrayRef GetArray(JsonObjectRef const& parent, std::wstring_view key)
        {
            const auto value = Get(parent, key);
            if (!IsNull(value) && value.ValueType() == JsonValueKind::Array)
            {
                return value.GetArray();
            }
            return nullptr;
        }

        // 完美平台的 id 字段可能是字符串或数字。
        std::wstring ReadIdString(JsonObjectRef const& object, std::wstring_view key)
        {
            if (auto s = OptionalString(object, key))
            {
                return *s;
            }
            return {};
        }

        void ParseIdentity(JsonObjectRef const& user, PerfectIdentity& out)
        {
            if (!user)
            {
                return;
            }
            out.steamId = ReadIdString(user, L"steamId");
            out.uid = ReadIdString(user, L"uid");
            out.userId = ReadIdString(user, L"userId");
            out.zqId = ReadIdString(user, L"zqId");
            if (auto v = OptionalString(user, L"name")) out.name = *v;
            if (auto v = OptionalString(user, L"nickname")) out.nickname = *v;
            if (auto v = OptionalString(user, L"avatar")) out.avatar = *v;

            if (const auto power = GetObject(user, L"perfectPower"))
            {
                out.perfectPower = OptionalInt32(power, L"perfectPower");
            }
        }

        void ParseLadder(JsonObjectRef const& ladder, PerfectLadderStats& out)
        {
            if (!ladder)
            {
                return;
            }
            out.score = OptionalInt32(ladder, L"score");
            out.scoreChange = OptionalInt32(ladder, L"score_change");
            out.matchCount = OptionalInt32(ladder, L"match_count");
            out.winNum = OptionalInt32(ladder, L"win_num");
            out.drawNum = OptionalInt32(ladder, L"draw_num");
            out.winRate = OptionalDouble(ladder, L"win_rate");
            out.roundCount = OptionalInt32(ladder, L"round_count");

            out.ratingAvg = OptionalDouble(ladder, L"rating_avg");
            out.ratingCtAvg = OptionalDouble(ladder, L"pw_rating_ct_avg");
            out.ratingTAvg = OptionalDouble(ladder, L"pw_rating_t_avg");
            out.adpr = OptionalDouble(ladder, L"adpr");
            out.rwsAvg = OptionalDouble(ladder, L"rws_avg");
            out.priAvg = OptionalDouble(ladder, L"pri_avg");
            out.killNum = OptionalInt32(ladder, L"kill_num");
            out.deathNum = OptionalInt32(ladder, L"death_num");
            out.assistNum = OptionalInt32(ladder, L"assist_num");
            out.kastTotal = OptionalDouble(ladder, L"kast_total");

            out.hsKillRate = OptionalDouble(ladder, L"hs_kill_rate");
            out.headshotKillNum = OptionalInt32(ladder, L"headshot_kill_num");
            out.firstKillNum = OptionalInt32(ladder, L"first_kill_num");
            out.firstDeathNum = OptionalInt32(ladder, L"first_death_num");
            out.firstKillWinRound = OptionalInt32(ladder, L"first_kill_win_round");

            out.c1v1Num = OptionalInt32(ladder, L"1v1_num");
            out.c1v2Num = OptionalInt32(ladder, L"1v2_num");
            out.c1v3Num = OptionalInt32(ladder, L"1v3_num");
            out.c1v4Num = OptionalInt32(ladder, L"1v4_num");
            out.c1v5Num = OptionalInt32(ladder, L"1v5_num");
            out.c1v1Total = OptionalInt32(ladder, L"1v1_total");
            out.c1v2Total = OptionalInt32(ladder, L"1v2_total");
            out.c1v3Total = OptionalInt32(ladder, L"1v3_total");
            out.c1v4Total = OptionalInt32(ladder, L"1v4_total");
            out.c1v5Total = OptionalInt32(ladder, L"1v5_total");
            out.c1vxRate = OptionalDouble(ladder, L"1vx_rate");

            out.twoKillNum = OptionalInt32(ladder, L"two_kill_num");
            out.threeKillNum = OptionalInt32(ladder, L"three_kill_num");
            out.fourKillNum = OptionalInt32(ladder, L"four_kill_num");
            out.fiveKillNum = OptionalInt32(ladder, L"five_kill_num");
            out.mvpNum = OptionalInt32(ladder, L"mvp_num");
            out.matchMvpNum = OptionalInt32(ladder, L"match_mvp_num");

            out.sniperKillNum = OptionalInt32(ladder, L"sniper_kill_num");
            out.awpKillTotal = OptionalInt32(ladder, L"awp_kill_total");

            out.currSStars = OptionalInt32(ladder, L"curr_s_stars");
            out.currSLevel = OptionalInt32(ladder, L"curr_s_level");
            out.prevSStars = OptionalInt32(ladder, L"prev_s_stars");
            out.changedStars = OptionalInt32(ladder, L"changed_stars");
        }

        void ParseRadar(JsonObjectRef const& radarNew, PerfectPlayer& out)
        {
            if (!radarNew)
            {
                return;
            }
            static constexpr std::wstring_view kKeys[] = {
                L"fire_power", L"marksmanship", L"follow_up_shot",
                L"first", L"item", L"1vn", L"sniper", L"app",
            };
            for (const auto key : kKeys)
            {
                const auto dim = GetObject(radarNew, key);
                if (!dim)
                {
                    continue;
                }
                RadarDimension entry;
                entry.key = std::wstring(key);
                entry.score = OptionalDouble(dim, L"score");
                entry.level = OptionalInt32(dim, L"level");
                if (auto v = OptionalString(dim, L"description")) entry.description = *v;

                // 当前赛季 WE 在 fire_power.detail.we_raw。
                if (key == L"fire_power")
                {
                    if (const auto detail = GetObject(dim, L"detail"))
                    {
                        out.seasonWe = OptionalDouble(detail, L"we_raw");
                    }
                }
                // 只有带数值 score 的维度才计入。
                if (entry.score.has_value())
                {
                    out.radar.push_back(std::move(entry));
                }
            }
        }

        void ParseMaps(JsonArrayRef const& maps, PerfectPlayer& out)
        {
            if (!maps)
            {
                return;
            }
            for (uint32_t i = 0; i < maps.Size(); ++i)
            {
                const auto item = maps.GetAt(i);
                if (IsNull(item) || item.ValueType() != JsonValueKind::Object)
                {
                    continue;
                }
                const auto object = item.GetObject();

                PerfectMapStats entry;
                if (auto v = OptionalString(object, L"map")) entry.map = *v;
                entry.totalNum = OptionalInt32(object, L"total_num");
                entry.roundCount = OptionalInt32(object, L"round_count");
                entry.winNum = OptionalInt32(object, L"win_num");
                entry.adpr = OptionalDouble(object, L"adpr");
                entry.ratingAvg = OptionalDouble(object, L"pw_rating_avg");
                entry.rwsAvg = OptionalDouble(object, L"rws_avg");
                entry.killNum = OptionalInt32(object, L"kill_num");
                entry.headshotKillNum = OptionalInt32(object, L"headshot_kill_num");

                // 地图显示名在 map_info 里。
                if (const auto info = GetObject(object, L"map_info"))
                {
                    if (auto v = OptionalString(info, L"name_cn")) entry.nameCn = *v;
                    if (auto v = OptionalString(info, L"name_en")) entry.nameEn = *v;
                }
                // 胜率由 win/total 派生（接口未直接给）。
                if (entry.winNum.has_value() && entry.totalNum.has_value() && *entry.totalNum > 0)
                {
                    entry.winRate =
                        static_cast<double>(*entry.winNum) / static_cast<double>(*entry.totalNum);
                }
                out.maps.push_back(std::move(entry));
            }
        }

        void ParseWeapons(JsonArrayRef const& weapons, PerfectPlayer& out)
        {
            if (!weapons)
            {
                return;
            }
            for (uint32_t i = 0; i < weapons.Size(); ++i)
            {
                const auto item = weapons.GetAt(i);
                if (IsNull(item) || item.ValueType() != JsonValueKind::Object)
                {
                    continue;
                }
                const auto object = item.GetObject();

                PerfectWeaponStats entry;
                if (auto v = OptionalString(object, L"name")) entry.name = *v;
                entry.matchNum = OptionalInt32(object, L"match_num");
                entry.killNum = OptionalInt32(object, L"kill_num");
                entry.avgKillNum = OptionalDouble(object, L"avg_kill_num");
                entry.headshotRate = OptionalDouble(object, L"headshot_rate");
                entry.firstShotAccuracy = OptionalDouble(object, L"first_shot_accuracy");
                entry.sprayAccuracy = OptionalDouble(object, L"spray_accuracy");
                entry.rapidStopSuccessRate = OptionalDouble(object, L"rapid_stop_success_rate");
                entry.avgTimeToKill = OptionalDouble(object, L"avg_time_to_kill");

                if (const auto info = GetObject(object, L"weapon_info"))
                {
                    if (auto v = OptionalString(info, L"name_cn")) entry.nameCn = *v;
                    if (auto v = OptionalString(info, L"name_en")) entry.nameEn = *v;
                }
                out.weapons.push_back(std::move(entry));
            }
            // 按击杀数降序，与参考项目一致。
            std::stable_sort(out.weapons.begin(), out.weapons.end(),
                [](PerfectWeaponStats const& a, PerfectWeaponStats const& b) {
                    return a.killNum.value_or(0) > b.killNum.value_or(0);
                });
        }
    }

    namespace Parse
    {
    void ApplyOverview(JsonObjectRef const& root, PerfectPlayer& out)
    {
        if (!root)
        {
            return;
        }
        ParseIdentity(GetObject(root, L"user"), out.identity);

        // 基础当前分：matchmaking.score。season-stats 到达后可覆盖。
        if (const auto matchmaking = GetObject(root, L"matchmaking"))
        {
            out.currentRank.score = OptionalInt32(matchmaking, L"score");
        }
    }

    void ApplySeasonStats(JsonObjectRef const& root, PerfectPlayer& out)
    {
        if (!root)
        {
            return;
        }

        // 历史最高：必须读顶层字段。ladder 内的 all_season_max_* 实测可能为 null。
        out.peakRank.score = OptionalInt32(root, L"all_season_max_score");
        out.peakRank.stars = OptionalInt32(root, L"all_season_max_star");
        if (auto v = OptionalString(root, L"all_season_max_score_season"))
        {
            out.peakRank.season = *v;
        }

        // 当前赛季：ladder.score + ladder.curr_s_stars。
        const auto ladder = GetObject(root, L"ladder");
        ParseLadder(ladder, out.ladder);

        if (ladder)
        {
            if (auto v = OptionalString(ladder, L"season")) out.currentRank.season = *v;
            if (auto v = OptionalString(ladder, L"season_show_name"))
            {
                out.currentRank.seasonShowName = *v;
            }
        }
        // 当前段位分数：ladder.score 优先，回退 overview 的 matchmaking.score。
        if (out.ladder.score.has_value())
        {
            out.currentRank.score = out.ladder.score;
        }
        out.currentRank.stars = out.ladder.currSStars;

        ParseRadar(GetObject(root, L"radar_new"), out);
        ParseMaps(GetArray(root, L"map"), out);
        ParseWeapons(GetArray(root, L"weapon"), out);
    }

    std::wstring FindLatestSeason(JsonObjectRef const& root)
    {
        if (!root)
        {
            return {};
        }
        // 该接口的成功响应是普通 JSON，直接读 data。
        // 递归寻找形如 "Sxx" 的最大编号。
        int best = -1;
        std::wstring bestText;

        std::function<void(Json::JsonValueRef const&)> walk =
            [&](Json::JsonValueRef const& node) {
            if (IsNull(node))
            {
                return;
            }
            if (node.ValueType() == JsonValueKind::Object)
            {
                for (auto const& pair : node.GetObject())
                {
                    walk(pair.Value());
                }
            }
            else if (node.ValueType() == JsonValueKind::Array)
            {
                const auto array = node.GetArray();
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    walk(array.GetAt(i));
                }
            }
            else if (node.ValueType() == JsonValueKind::String)
            {
                const std::wstring text(node.GetString());
                // 匹配 S + 数字。
                if (text.size() >= 2 && (text[0] == L'S' || text[0] == L's'))
                {
                    bool allDigits = true;
                    for (size_t i = 1; i < text.size(); ++i)
                    {
                        if (text[i] < L'0' || text[i] > L'9') { allDigits = false; break; }
                    }
                    if (allDigits)
                    {
                        const int number = std::stoi(text.substr(1));
                        if (number > best)
                        {
                            best = number;
                            bestText = text;
                        }
                    }
                }
            }
        };

        walk(Get(root, L"data"));
        if (bestText.empty())
        {
            walk(root);
        }
        return bestText;
    }

    PerfectPlayer PlayerFromOverviewJson(std::wstring const& decryptedJson)
    {
        PerfectPlayer player;
        if (decryptedJson.empty())
        {
            return player;
        }
        JsonObjectRef root{ nullptr };
        if (!JsonObjectRef::TryParse(decryptedJson, root) || !root)
        {
            return player;
        }
        ApplyOverview(root, player);
        player.rawOverviewJson = decryptedJson;
        return player;
    }
    } // namespace Parse
}
