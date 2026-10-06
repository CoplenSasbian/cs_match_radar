// 针对真实 5E 响应的解析验证。
// 读取由 tools/fetch_sample.ps1 抓取的真实 JSON，检查关键字段是否被正确映射。
// 用法：RealDataTests.exe <match_detail.json>

#include "FiveEApiPch.h"
#include "FiveEJson.h"
#include "FiveEParser.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace winrt::Windows::Data::Json;

namespace
{
    int g_failures = 0;

    void Check(bool ok, char const* name, std::wstring const& detail = L"")
    {
        if (ok)
        {
            std::printf("  PASS  %s\n", name);
        }
        else
        {
            std::printf("  FAIL  %s\n", name);
            if (!detail.empty())
            {
                std::wprintf(L"        detail: %s\n", detail.c_str());
            }
            ++g_failures;
        }
    }

    std::wstring ReadFile(std::wstring const& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return {};
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // JSON 是 UTF-8；转成宽字符交给 JsonObject::TryParse。
        if (bytes.size() >= 3 &&
            static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
        {
            bytes.erase(0, 3);
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
            static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), static_cast<int>(bytes.size()),
            wide.data(), size);
        return wide;
    }
}

int main(int argc, char** argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    if (argc < 2)
    {
        std::printf("usage: RealDataTests <match_detail.json>\n");
        return 2;
    }

    // argv 是窄字符；这里假定传入的是 ASCII 路径。
    std::string narrowPath(argv[1]);
    std::wstring path(narrowPath.begin(), narrowPath.end());

    const std::wstring text = ReadFile(path);
    if (text.empty())
    {
        std::printf("cannot read %s\n", argv[1]);
        return 2;
    }

    JsonObject root{ nullptr };
    if (!JsonObject::TryParse(text, root) || !root)
    {
        std::printf("sample is not valid JSON\n");
        return 2;
    }

    // 真实响应外层是 { code, message, data: { main, group_1, group_2, ... } }
    const auto dataValue = FiveE::Json::Get(root, L"data");
    if (FiveE::Json::IsNull(dataValue) || dataValue.ValueType() != FiveE::Json::JsonValueKind::Object)
    {
        std::printf("missing data object\n");
        return 2;
    }

    std::printf("[真实数据] MatchDetail 解析\n");
    const auto detail = FiveE::Parse::MatchDetailFromJson(dataValue.GetObject());

    Check(!detail.matchId.empty(), "matchId 非空", detail.matchId);
    Check(!detail.mapName.empty(), "map 非空", detail.mapName);
    Check(!detail.mapLabel.empty(), "map_desc 非空（中文地图名）", detail.mapLabel);
    Check(detail.roundTotal.has_value() || detail.group1Score.has_value(),
        "比分 / 回合数已填充");
    Check(detail.teams.size() == 2, "teams == 2");
    Check(detail.players.size() == 10, "players == 10（两个 5 人队）");

    // 每个玩家都必须有可显示的名字和队伍归属。
    int namedCount = 0;
    int withRating = 0;
    int withAdr = 0;
    int withLevel = 0;
    for (auto const& p : detail.players)
    {
        if (!p.profile.nickname.empty())
        {
            ++namedCount;
        }
        if (p.rating.has_value())
        {
            ++withRating;
        }
        if (p.adr.has_value())
        {
            ++withAdr;
        }
        if (!p.ladder.levelName.empty())
        {
            ++withLevel;
        }
    }
    Check(namedCount == 10, "全部 10 名玩家都有昵称");
    Check(withRating == 10, "全部 10 名玩家都有 rating");
    Check(withAdr == 10, "全部 10 名玩家都有 adr");
    Check(withLevel == 10, "全部 10 名玩家都有段位文本");

    // KAST 必须是 0..1 的比例；若解析成 63 这种百分数说明字段理解错了。
    bool kastInRange = true;
    for (auto const& p : detail.players)
    {
        if (p.kast.has_value() && (*p.kast < 0.0 || *p.kast > 1.0))
        {
            kastInRange = false;
        }
    }
    Check(kastInRange, "kast 落在 0..1（比例，而非百分数）");

    // 爆头率同样是比例。
    bool hsInRange = true;
    for (auto const& p : detail.players)
    {
        if (p.headshotRate.has_value() && (*p.headshotRate < 0.0 || *p.headshotRate > 1.0))
        {
            hsInRange = false;
        }
    }
    Check(hsInRange, "per_headshot 落在 0..1");

    // ELO 应能从 level_info 读出（值是字符串形式的浮点）。
    int withElo = 0;
    for (auto const& p : detail.players)
    {
        if (p.ladder.originElo.has_value())
        {
            ++withElo;
        }
    }
    Check(withElo == 10, "全部 10 名玩家都有 origin_elo");

    // 展开面板用到的详细字段必须齐全（否则展开后是空的）。
    int withMultiKills = 0, withClutchMap = 0, withFirstKills = 0, withSides = 0, withSwing = 0, withUtility = 0;
    for (auto const& p : detail.players)
    {
        if (p.multiKills.size() == 5) ++withMultiKills;   // kill_1..kill_5
        if (p.clutches.size() == 5) ++withClutchMap;      // end_1v1..end_1v5
        if (p.firstKills.has_value() && p.firstDeaths.has_value()) ++withFirstKills;
        if (p.sideStats.count(L"t") && p.sideStats.count(L"ct")) ++withSides;
        if (p.swingScore.has_value()) ++withSwing;
        if (p.utility.utilityDamage.has_value() && p.utility.enemiesFlashed.has_value()) ++withUtility;
    }
    Check(withMultiKills == 10, "全部 10 人有 kill_1..kill_5 多杀明细");
    Check(withClutchMap == 10, "全部 10 人有 1v1..1v5 残局明细");
    Check(withFirstKills == 10, "全部 10 人有首杀/首死");
    Check(withSides == 10, "全部 10 人有 T/CT 半场拆分");
    Check(withSwing == 10, "全部 10 人有影响力评分 rating3");
    Check(withUtility == 10, "全部 10 人有投掷物数据");

    // Steam 绑定：接口在 user_info.user_data.steam.steamId 返回 64 位 ID。
    int withSteam = 0;
    bool steamLooksValid = true;
    for (auto const& p : detail.players)
    {
        if (p.profile.steamId.empty())
        {
            continue;
        }
        ++withSteam;
        // 64 位 SteamID 以 7656 开头且共 17 位。
        if (p.profile.steamId.size() != 17 || p.profile.steamId.rfind(L"7656", 0) != 0)
        {
            steamLooksValid = false;
        }
        if (p.profile.steamProfileUrl !=
            L"https://steamcommunity.com/profiles/" + p.profile.steamId)
        {
            steamLooksValid = false;
        }
    }
    Check(withSteam == 10, "全部 10 名玩家都有 Steam ID");
    Check(steamLooksValid, "Steam ID 为 17 位 7656 开头，且主页链接拼接正确");

    // 打印首名玩家，便于人工核对。
    if (!detail.players.empty())
    {
        const auto& p = detail.players.front();
        std::printf("\n  样例玩家:\n");
        std::wprintf(L"    nickname = %s\n", p.profile.nickname.c_str());
        std::wprintf(L"    playerId = %s\n", p.profile.playerId.c_str());
        std::wprintf(L"    level    = %s\n", p.ladder.levelName.c_str());
        std::wprintf(L"    k/d/a    = %s\n",
            (std::to_wstring(p.kills.value_or(0)) + L"/" +
             std::to_wstring(p.deaths.value_or(0)) + L"/" +
             std::to_wstring(p.assists.value_or(0))).c_str());
        if (p.rating) std::wprintf(L"    rating   = %.2f\n", *p.rating);
        if (p.adr) std::wprintf(L"    adr      = %.1f\n", *p.adr);
        if (p.kast) std::wprintf(L"    kast     = %.2f (=%.0f%%)\n", *p.kast, *p.kast * 100.0);
        if (p.ladder.originElo) std::wprintf(L"    elo      = %d\n", *p.ladder.originElo);
    }

    std::printf("\n==== %s (failures=%d) ====\n",
        g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
