// 解析层自测：用贴近 5E 真实返回的 JSON 样本验证字段映射。
// 不发起网络请求，因此可在无网络 / 无能力声明的环境下运行。

#include "pch.h"
#include "FiveEJson.h"
#include "FiveEParser.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <cstdio>
#include <string>

using namespace winrt::Windows::Data::Json;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* name)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", name);
        }
        else
        {
            std::printf("  FAIL  %s\n", name);
            ++g_failures;
        }
    }

    void CheckW(std::wstring const& actual, std::wstring const& expected, const char* name)
    {
        const bool ok = actual == expected;
        if (!ok)
        {
            std::wprintf(L"  FAIL  %hs\n        actual   = [%s]\n        expected = [%s]\n",
                name, actual.c_str(), expected.c_str());
            ++g_failures;
        }
        else
        {
            std::printf("  PASS  %s\n", name);
        }
    }

    void CheckI(std::optional<int32_t> const& actual, int32_t expected, const char* name)
    {
        const bool ok = actual.has_value() && *actual == expected;
        if (!ok)
        {
            std::printf("  FAIL  %s (expected %d)\n", name, expected);
            ++g_failures;
        }
        else
        {
            std::printf("  PASS  %s\n", name);
        }
    }

    void CheckD(std::optional<double> const& actual, double expected, const char* name)
    {
        const bool ok = actual.has_value() && std::abs(*actual - expected) < 0.0001;
        if (!ok)
        {
            std::printf("  FAIL  %s (expected %f)\n", name, expected);
            ++g_failures;
        }
        else
        {
            std::printf("  PASS  %s\n", name);
        }
    }

    JsonObject ParseObject(std::wstring const& text)
    {
        JsonObject object{ nullptr };
        if (!JsonObject::TryParse(text, object))
        {
            std::printf("  FAIL  JSON 样本无法解析\n");
            ++g_failures;
        }
        return object;
    }
}

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ---- 样本 1：/match/list 的单条记录 ----
    // 注意 5E 会把部分数字以字符串形式返回（adr / rws / rating）。
    std::printf("[1] MatchSummary 解析\n");
    {
        const std::wstring sample = LR"({
            "match_id": "5e-abc123",
            "match_code": "5e-abc123",
            "map": "de_mirage",
            "map_desc": "荒漠迷城",
            "is_win": true,
            "start_time": 1730000000,
            "end_time": 1730003600,
            "kill": 24,
            "death": 15,
            "assist": 5,
            "adr": "85.3",
            "rws": "12.5",
            "rating": "1.25",
            "per_headshot": "0.45",
            "awp_kill": 3,
            "is_mvp": 4,
            "round_total": 30,
            "group1_all_score": 16,
            "group2_all_score": 14,
            "change_elo": 21,
            "location": "华东",
            "server_ip": "1.2.3.4",
            "server_port": 27015,
            "season": 12,
            "status": 2,
            "demo_url": "https://demo.5eplay.com/x.dem",
            "level_info": { "level_name": "黄金" }
        })";

        const auto summary = FiveE::Parse::MatchSummaryFromJson(ParseObject(sample));
        CheckW(summary.matchId, L"5e-abc123", "match_id");
        CheckW(summary.mapName, L"de_mirage", "map");
        CheckW(summary.mapLabel, L"荒漠迷城", "map_desc");
        Check(summary.isWin.value_or(false), "is_win == true");
        CheckI(summary.kills, 24, "kill");
        // 字符串形式的数字应当被解析出来。
        CheckD(summary.adr, 85.3, "adr (string -> double)");
        CheckD(summary.rws, 12.5, "rws (string -> double)");
        CheckD(summary.rating, 1.25, "rating (string -> double)");
        CheckI(summary.group1Score, 16, "group1_all_score");
        CheckI(summary.changeElo, 21, "change_elo");
        CheckW(summary.levelName, L"黄金", "level_info.level_name");
        CheckW(summary.demoUrl, L"https://demo.5eplay.com/x.dem", "demo_url");
        Check(summary.status == FiveE::MatchStatus::Finished, "status 2 == Finished");
        Check(summary.startTime.has_value() && *summary.startTime == 1730000000, "start_time");
    }

    // ---- 样本 2：/match/{id} 的 data ----
    std::printf("[2] MatchDetail 解析\n");
    {
        const std::wstring sample = LR"({
            "main": {
                "match_code": "5e-xyz789",
                "map": "de_inferno",
                "map_desc": "炼狱小镇",
                "match_winner": "group_1",
                "demo_url": "https://demo.5eplay.com/y.dem",
                "round_total": 24,
                "season": 12,
                "year": 2025,
                "start_time": 1730000000,
                "end_time": 1730003000,
                "status": 2,
                "group1_all_score": 13,
                "group2_all_score": 11,
                "group1_change_elo": 18,
                "group2_change_elo": -18,
                "group1_tid": "t1",
                "group1_uids": ["u1", "u2"],
                "group2_uids": "u3,u4",
                "group1_fh_role": "t",
                "group1_sh_role": "ct",
                "group1_fh_score": 8,
                "group1_sh_score": 5,
                "group1_origin_elo": 2000,
                "mvp_uid": "u1",
                "most_awp_uid": "u2",
                "location_full": "中国 华东"
            },
            "group_1": [
                {
                    "uid": "u1",
                    "level_info": { "level": 18, "level_name": "大师", "score": 1500 },
                    "sts": { "elo": 2018, "origin_elo": 2000, "change_elo": 18 },
                    "user_info": {
                        "nick_name": "玩家一",
                        "user_data": {
                            "username": "player1",
                            "domain": "11814738gjdwn7",
                            "steam": { "steamId": "76561198000000001", "personaname": "P1" }
                        }
                    },
                    "fight": {
                        "uid": "u1",
                        "kill": 28, "death": 14, "assist": 6,
                        "adr": "98.7", "rws": "15.2", "rating2": "1.42", "rating3": "1.55",
                        "kast": "78.5", "per_headshot": "0.55", "headshot": 15,
                        "first_kill": 5, "first_death": 2, "awp_kill": 9,
                        "planted_bomb": 3, "defused_bomb": 1,
                        "kill_2": 4, "kill_3": 2, "kill_4": 1, "kill_5": 0,
                        "end_1v1": 2, "end_1v2": 1, "end_1v3": 0, "end_1v4": 0, "end_1v5": 0,
                        "throw_harm": 120, "flash_enemy": 8,
                        "is_mvp": 1, "is_highlight": 1
                    },
                    "fight_t": { "kill": 15, "death": 7, "adr": "100.1" },
                    "fight_ct": { "kill": 13, "death": 7, "adr": "97.2" }
                }
            ],
            "group_2": [
                {
                    "uid": "u3",
                    "level_info": { "level": 17, "level_name": "钻石" },
                    "user_info": { "nick_name": "对手一", "user_data": { "domain": "999" } },
                    "fight": { "uid": "u3", "kill": 20, "death": 18, "assist": 4, "adr": "80.0", "rating": "1.05" }
                }
            ]
        })";

        const auto detail = FiveE::Parse::MatchDetailFromJson(ParseObject(sample));
        CheckW(detail.matchId, L"5e-xyz789", "main.match_code");
        CheckW(detail.mapName, L"de_inferno", "main.map");
        CheckW(detail.matchWinner, L"group_1", "match_winner");
        CheckW(detail.demoUrl, L"https://demo.5eplay.com/y.dem", "demo_url");
        CheckW(detail.location, L"中国 华东", "location_full 优先");
        CheckI(detail.roundTotal, 24, "round_total");
        CheckI(detail.season, 12, "season");
        CheckI(detail.year, 2025, "year");
        CheckI(detail.group1Score, 13, "group1_all_score");
        CheckI(detail.group1ChangeElo, 18, "group1_change_elo");
        CheckI(detail.group2ChangeElo, -18, "group2_change_elo");

        Check(detail.teams.size() == 2, "teams 数量 == 2");
        if (detail.teams.size() == 2)
        {
            const auto& t1 = detail.teams[0];
            CheckW(t1.teamId, L"t1", "group1_tid");
            Check(t1.playerUids.size() == 2, "group1_uids 数组解析");
            Check(t1.firstHalfSide == FiveE::Side::Terrorist, "group1_fh_role == t");
            Check(t1.secondHalfSide == FiveE::Side::CounterTerrorist, "group1_sh_role == ct");
            CheckI(t1.halfScores.firstHalf, 8, "group1_fh_score");
            CheckI(t1.halfScores.secondHalf, 5, "group1_sh_score");
            CheckI(t1.originElo, 2000, "group1_origin_elo");

            const auto& t2 = detail.teams[1];
            // 逗号分隔字符串形式的 uids 也应解析。
            Check(t2.playerUids.size() == 2, "group2_uids 逗号分隔解析");
        }

        Check(detail.players.size() == 2, "players 数量 == 2");
        if (!detail.players.empty())
        {
            const auto& p = detail.players[0];
            CheckW(p.profile.playerId, L"11814738gjdwn7", "playerId 取 domain");
            CheckW(p.profile.nickname, L"玩家一", "nick_name");
            CheckW(p.profile.steamId, L"76561198000000001", "steam.steamId");
            Check(p.profile.profileUrl == L"https://www.5eplay.com/player/11814738gjdwn7",
                "profileUrl 拼接");
            Check(p.group == FiveE::TeamGroup::Group1, "group_1 归属");
            CheckI(p.kills, 28, "fight.kill");
            CheckI(p.assists, 6, "fight.assist");
            CheckD(p.adr, 98.7, "fight.adr (string)");
            CheckD(p.rating, 1.42, "rating2 优先于 rating");
            CheckD(p.swingScore, 1.55, "rating3 -> swingScore");
            CheckD(p.kast, 78.5, "kast");
            CheckI(p.headshots, 15, "headshot");
            CheckD(p.headshotRate, 0.55, "per_headshot");
            CheckI(p.awpKills, 9, "awp_kill");
            CheckI(p.bombPlants, 3, "planted_bomb");
            CheckI(p.bombDefuses, 1, "defused_bomb");

            // 多杀：kill_2 + kill_3 + kill_4 + kill_5 = 4+2+1+0 = 7（不含 kill_1）
            CheckI(p.multiKillCount, 7, "multiKillCount 不含单杀");
            Check(p.multiKills.size() == 4, "multiKills 记录 4 个档位");
            // 残局：1v1 + 1v2 = 2 + 1 = 3
            CheckI(p.clutchCount, 3, "clutchCount");
            Check(p.clutches.size() == 5, "clutches 记录 5 个档位");

            CheckI(p.ladder.level, 18, "level_info.level");
            CheckW(p.ladder.levelName, L"大师", "level_info.level_name");
            CheckI(p.ladder.elo, 2018, "sts.elo");
            CheckI(p.ladder.changeElo, 18, "sts.change_elo");

            Check(p.sideStats.size() == 2, "sideStats 含 t/ct");
            if (auto it = p.sideStats.find(L"t"); it != p.sideStats.end())
            {
                CheckI(it->second.kills, 15, "fight_t.kill");
                CheckD(it->second.adr, 100.1, "fight_t.adr");
            }

            CheckI(p.utility.utilityDamage, 120, "throw_harm");
            CheckI(p.utility.enemiesFlashed, 8, "flash_enemy");
            Check(p.awards.isMvp.value_or(false), "is_mvp");
            Check(p.awards.isHighlight.value_or(false), "is_highlight");
        }

        CheckW(detail.awards.mvpPlayerId, L"u1", "mvp_uid");
        CheckW(detail.awards.mostAwpKillsPlayerId, L"u2", "most_awp_uid");

        // group_2 玩家应回退到 rating（因为无 rating2）。
        if (detail.players.size() == 2)
        {
            const auto& q = detail.players[1];
            Check(q.group == FiveE::TeamGroup::Group2, "group_2 归属");
            CheckD(q.rating, 1.05, "rating 回退（无 rating2）");
        }
    }

    // ---- 样本 3：缺失字段 / 空对象应安全降级 ----
    std::printf("[3] 容错性\n");
    {
        const auto summary = FiveE::Parse::MatchSummaryFromJson(ParseObject(L"{}"));
        Check(summary.matchId.empty(), "空对象 matchId 为空");
        Check(!summary.adr.has_value(), "空对象 adr 无值");
        Check(summary.status == FiveE::MatchStatus::Unknown, "空对象 status == Unknown");

        const auto detail = FiveE::Parse::MatchDetailFromJson(ParseObject(L"{\"main\":{}}"));
        Check(detail.players.empty(), "无 group 数组时 players 为空");
        Check(detail.teams.size() == 2, "teams 仍构建两个分组");

        // 非数字字符串不应被当成数字。
        const auto bad = FiveE::Parse::MatchSummaryFromJson(
            ParseObject(L"{\"adr\":\"N/A\",\"kill\":\"abc\"}"));
        Check(!bad.adr.has_value(), "adr=\"N/A\" 解析为无值");
        Check(!bad.kills.has_value(), "kill=\"abc\" 解析为无值");
    }

    // ---- 样本 4：idTransfer 的 uuid ----
    std::printf("[4] Uuid 解析\n");
    {
        const auto data = ParseObject(L"{\"uuid\":\"abc-def-123\"}");
        CheckW(FiveE::Parse::UuidFromJson(data), L"abc-def-123", "uuid 提取");
        CheckW(FiveE::Parse::UuidFromJson(ParseObject(L"{}")), L"", "缺失 uuid 返回空");
    }

    // ---- 样本 5：JSON 辅助函数 ----
    std::printf("[5] Json 辅助函数\n");
    {
        using namespace FiveE::Json;
        const auto obj = ParseObject(L"{\"a\":1,\"b\":\"x\",\"c\":true,\"d\":null}");

        Check(OptionalInt32(obj, L"a").value_or(0) == 1, "OptionalInt32");
        CheckW(OptionalString(obj, L"b").value_or(L""), L"x", "OptionalString");
        Check(OptionalBool(obj, L"c").value_or(false), "OptionalBool");
        Check(!OptionalString(obj, L"d").has_value(), "null -> nullopt");
        Check(!OptionalString(obj, L"missing").has_value(), "缺失键 -> nullopt");
        CheckW(FormatUnixSeconds(0).substr(0, 4), L"1970", "FormatUnixSeconds(0) 依赖时区");
    }

    std::printf("\n==== %s (failures=%d) ====\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
