// 完美平台解析验证：段位规则、ladder 字段、派生指标、地图/武器/雷达。

#include "PerfectApiPch.h"
#include "PerfectParser.h"
#include "PerfectModels.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <cstdio>
#include <string>

using namespace winrt::Windows::Data::Json;

namespace
{
    int g_failures = 0;

    void Check(bool ok, char const* name, std::wstring const& detail = L"")
    {
        if (ok) { std::printf("  PASS  %s\n", name); }
        else
        {
            std::printf("  FAIL  %s\n", name);
            if (!detail.empty()) std::wprintf(L"        %s\n", detail.c_str());
            ++g_failures;
        }
    }

    void CheckW(std::wstring const& actual, std::wstring const& expected, char const* name)
    {
        if (actual == expected) { std::printf("  PASS  %s\n", name); }
        else
        {
            std::printf("  FAIL  %s\n", name);
            std::wprintf(L"        actual   = %s\n", actual.c_str());
            std::wprintf(L"        expected = %s\n", expected.c_str());
            ++g_failures;
        }
    }

    Perfect::Json::JsonObjectRef ParseJson(std::wstring const& text)
    {
        Perfect::Json::JsonObjectRef o{ nullptr };
        if (!Perfect::Json::JsonObjectRef::TryParse(text, o)) { std::printf("  JSON 解析失败\n"); ++g_failures; }
        return o;
    }
}

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ---- 1. 段位展示规则（这是完美平台最容易被搞错的地方）----
    std::printf("[1] 段位展示规则\n");
    {
        Perfect::RankScore r;

        // score < 2400 显示分数
        r.score = 2380;
        CheckW(r.Display(), L"2380", "2380 -> 显示分数");
        Check(r.Tier() == Perfect::SStarTier::None, "2380 -> 非 S 段");

        // 2400 且 0-9 星 -> 普通 S
        r.score = 2450; r.stars = 5;
        CheckW(r.Display(), L"S 5★", "2450 / 5 星 -> 普通 S");
        Check(r.Tier() == Perfect::SStarTier::Normal, "5 星 -> Normal");

        // 10-24 星 -> 黄金 S
        r.stars = 10;
        CheckW(r.Display(), L"黄金 S 10★", "10 星 -> 黄金 S");
        Check(r.Tier() == Perfect::SStarTier::Gold, "10 星 -> Gold");

        r.stars = 24;
        Check(r.Tier() == Perfect::SStarTier::Gold, "24 星仍为 Gold");

        // 25-49 星 -> 钻石 S
        r.stars = 25;
        CheckW(r.Display(), L"钻石 S 25★", "25 星 -> 钻石 S");
        Check(r.Tier() == Perfect::SStarTier::Diamond, "25 星 -> Diamond");

        r.stars = 49;
        Check(r.Tier() == Perfect::SStarTier::Diamond, "49 星仍为 Diamond");

        // 50+ 星 -> 魔王 S
        r.stars = 50;
        CheckW(r.Display(), L"魔王 S 50★", "50 星 -> 魔王 S");
        Check(r.Tier() == Perfect::SStarTier::Demon, "50 星 -> Demon");

        // 边界：正好 2400
        r.score = 2400; r.stars = 0;
        Check(r.Tier() == Perfect::SStarTier::Normal, "2400 且 0 星 -> Normal（边界）");

        // 无分数
        Perfect::RankScore empty;
        CheckW(empty.Display(), L"—", "无分数 -> —");
    }

    // ---- 2. overview 解析 ----
    std::printf("[2] overview 解析\n");
    {
        const auto root = ParseJson(LR"({
            "user": {
                "steamId": "76561199667272550",
                "uid": "12345",
                "name": "完美玩家",
                "avatar": "https://example.com/a.png",
                "zqId": "zq-abc",
                "perfectPower": { "perfectPower": 88 }
            },
            "matchmaking": { "score": 2380 }
        })");

        Perfect::PerfectPlayer player;
        Perfect::Parse::ApplyOverview(root, player);

        CheckW(player.identity.steamId, L"76561199667272550", "steamId");
        CheckW(player.identity.name, L"完美玩家", "name（主显示名）");
        CheckW(player.identity.zqId, L"zq-abc", "zqId（留言板实体）");
        Check(player.identity.perfectPower.value_or(0) == 88, "perfectPower");
        Check(player.currentRank.score.value_or(0) == 2380, "matchmaking.score -> 当前分");
        Check(player.HasData(), "HasData");
    }

    // ---- 3. season-stats 解析 ----
    std::printf("[3] season-stats 解析\n");
    {
        const auto root = ParseJson(LR"({
            "all_season_max_score": 2650,
            "all_season_max_star": 32,
            "all_season_max_score_season": "S20",
            "ladder": {
                "season": "S24",
                "season_show_name": "第24赛季",
                "score": 2470,
                "score_change": 15,
                "match_count": 40,
                "win_num": 24,
                "draw_num": 2,
                "win_rate": 0.6,
                "round_count": 800,
                "kill_num": 640,
                "death_num": 560,
                "assist_num": 120,
                "adpr": 88.5,
                "rws_avg": 11.2,
                "rating_avg": 1.15,
                "pw_rating_ct_avg": 1.20,
                "pw_rating_t_avg": 1.10,
                "pri_avg": 0.9,
                "kast_total": 560,
                "hs_kill_rate": 0.52,
                "headshot_kill_num": 330,
                "first_kill_num": 160,
                "first_death_num": 110,
                "first_kill_win_round": 96,
                "1v1_num": 12, "1v1_total": 20,
                "1v2_num": 4,  "1v2_total": 9,
                "1v3_num": 1,  "1v3_total": 3,
                "1vx_rate": 0.35,
                "two_kill_num": 80, "three_kill_num": 24,
                "four_kill_num": 5, "five_kill_num": 1,
                "mvp_num": 120, "match_mvp_num": 14,
                "sniper_kill_num": 90, "awp_kill_total": 70,
                "curr_s_stars": 20, "curr_s_level": 3,
                "prev_s_stars": 15, "changed_stars": 5
            },
            "radar_new": {
                "fire_power": { "score": 82, "level": 4, "detail": { "we_raw": 12.7 } },
                "marksmanship": { "score": 75, "level": 3 },
                "item": { "score": 0, "level": 0 }
            },
            "map": [
                {
                    "map": "de_dust2", "total_num": 30, "round_count": 600,
                    "win_num": 18, "adpr": 90.1, "pw_rating_avg": 1.18, "rws_avg": 11.5,
                    "kill_num": 480, "headshot_kill_num": 240,
                    "map_info": { "name_cn": "炙热沙城2", "name_en": "Dust II" }
                }
            ],
            "weapon": [
                { "name": "ak47", "match_num": 30, "kill_num": 300, "avg_kill_num": 10.0,
                  "headshot_rate": 0.55, "weapon_info": { "name_cn": "AK-47" } },
                { "name": "awp", "match_num": 12, "kill_num": 90, "avg_kill_num": 7.5,
                  "weapon_info": { "name_cn": "AWP" } }
            ]
        })");

        Perfect::PerfectPlayer player;
        Perfect::Parse::ApplySeasonStats(root, player);

        // 当前段位来自 ladder；历史最高来自顶层。
        Check(player.currentRank.score.value_or(0) == 2470, "当前分 = ladder.score");
        Check(player.currentRank.stars.value_or(0) == 20, "当前星 = curr_s_stars");
        CheckW(player.currentRank.Display(), L"黄金 S 20★", "当前段位展示");
        CheckW(player.currentRank.season, L"S24", "当前赛季");

        Check(player.peakRank.score.value_or(0) == 2650, "历史最高分（顶层字段）");
        Check(player.peakRank.stars.value_or(0) == 32, "历史最高星");
        CheckW(player.peakRank.Display(), L"钻石 S 32★", "历史最高段位展示");

        // ladder 基础
        Check(player.ladder.matchCount.value_or(0) == 40, "场次");
        Check(player.ladder.winNum.value_or(0) == 24, "胜场");
        Check(player.ladder.roundCount.value_or(0) == 800, "回合数");

        // 派生：K/D = 640/560
        if (auto kd = player.ladder.Kd())
        {
            Check(std::abs(*kd - 1.142857) < 0.001, "派生 K/D");
        }
        else { Check(false, "派生 K/D"); }

        // 派生：KAST = kast_total / round_count = 560/800 = 0.70
        if (auto kast = player.ladder.Kast())
        {
            Check(std::abs(*kast - 0.70) < 0.001, "派生 KAST（比例）");
        }
        else { Check(false, "派生 KAST"); }

        // 派生：1v1 胜率 = 12/20 = 0.60
        if (auto r = player.ladder.OneVOneRate())
        {
            Check(std::abs(*r - 0.60) < 0.001, "派生 1v1 胜率（带分母）");
        }
        else { Check(false, "派生 1v1 胜率"); }

        // 派生：首杀转化率 = 96/160 = 0.60
        if (auto r = player.ladder.FirstKillRate())
        {
            Check(std::abs(*r - 0.60) < 0.001, "派生首杀转化率");
        }
        else { Check(false, "派生首杀转化率"); }

        // 派生：突破率 = 160/800 = 0.20
        if (auto r = player.ladder.EntryRate())
        {
            Check(std::abs(*r - 0.20) < 0.001, "派生突破率");
        }
        else { Check(false, "派生突破率"); }

        // 雷达：参考实现保留任何非 null 的 score（0 也算有值），共 3 个维度。
        Check(player.radar.size() == 3, "雷达保留全部有 score 的维度（含 0）",
            std::to_wstring(player.radar.size()));

        // 当前赛季 WE
        if (player.seasonWe)
        {
            Check(std::abs(*player.seasonWe - 12.7) < 0.001, "赛季 WE（we_raw）");
        }
        else { Check(false, "赛季 WE"); }

        // 地图
        Check(player.maps.size() == 1, "地图数量");
        if (!player.maps.empty())
        {
            CheckW(player.maps[0].nameCn, L"炙热沙城2", "地图中文名");
            if (auto wr = player.maps[0].winRate)
            {
                Check(std::abs(*wr - 0.6) < 0.001, "地图胜率（win/total 派生）");
            }
            else { Check(false, "地图胜率"); }
        }

        // 武器：应按击杀降序（AK 300 > AWP 90）
        Check(player.weapons.size() == 2, "武器数量");
        if (player.weapons.size() == 2)
        {
            CheckW(player.weapons[0].name, L"ak47", "武器按击杀降序（第一名 AK）");
            CheckW(player.weapons[1].name, L"awp", "武器按击杀降序（第二名 AWP）");
        }
    }

    // ---- 4. 分母为 0 / 缺失时不产生派生值 ----
    std::printf("[4] 派生值边界\n");
    {
        Perfect::PerfectLadderStats s;
        Check(!s.Kd().has_value(), "无数据 -> K/D 无值");
        Check(!s.Kast().has_value(), "无数据 -> KAST 无值");
        Check(!s.OneVOneRate().has_value(), "无数据 -> 1v1 胜率无值");

        s.killNum = 10; s.deathNum = 0;
        Check(!s.Kd().has_value(), "死亡为 0 -> K/D 无值（不产生除零）");

        s.roundCount = 0; s.kastTotal = 100;
        Check(!s.Kast().has_value(), "回合为 0 -> KAST 无值");

        s.c1v1Num = 0; s.c1v1Total = 0;
        Check(!s.OneVOneRate().has_value(), "1v1 分母为 0 -> 无值");
    }

    // ---- 5. 赛季发现 ----
    std::printf("[5] 赛季发现\n");
    {
        const auto root = ParseJson(LR"({
            "code": 0,
            "data": [ { "season": "S22" }, { "season": "S24" }, { "season": "S23" } ]
        })");
        const std::wstring latest = Perfect::Parse::FindLatestSeason(root);
        CheckW(latest, L"S24", "递归选出最大赛季号");
    }

    std::printf("\n==== %s (failures=%d) ====\n",
        g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
