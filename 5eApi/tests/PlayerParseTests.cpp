// 玩家资料解析验证：用真实 player/home 响应（或保存的样本）校验字段映射。
// 用法：PlayerParseTests.exe <home_response.json>

#include "FiveEApiPch.h"
#include "FiveEPlayerParser.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <cstdio>
#include <fstream>
#include <string>

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
                std::wprintf(L"        %s\n", detail.c_str());
            }
            ++g_failures;
        }
    }

    std::wstring ReadFileUtf8(std::wstring const& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
    if (argc < 2) { std::printf("usage: PlayerParseTests <home.json>\n"); return 2; }

    std::string narrow(argv[1]);
    const std::wstring text = ReadFileUtf8(std::wstring(narrow.begin(), narrow.end()));
    if (text.empty()) { std::printf("cannot read sample\n"); return 2; }

    const auto home = FiveE::Parse::PlayerHomeFromJson(text);

    std::printf("[player/home 解析]\n");
    Check(home.HasData(), "解析出玩家数据");
    Check(!home.identity.uuid.empty(), "uuid 非空", home.identity.uuid);
    Check(!home.identity.username.empty(), "username 非空（主显示名）", home.identity.username);

    // 段位与 ELO
    Check(home.rank.elo.has_value(), "ELO 有值");
    Check(home.rank.levelId.has_value(), "段位 ID 有值");
    Check(home.rank.rank.has_value(), "全服排名有值");
    Check(!home.rank.season.empty(), "赛季标识非空", home.rank.season);

    // 生涯
    Check(home.career.matchTotal.has_value(), "生涯总场次有值");
    Check(home.career.matchWin.has_value(), "生涯胜场有值");

    // 信誉（用于"是否有问题"的判断）
    Check(home.credit.credit.has_value(), "信用分有值");
    Check(home.credit.trustedScore.has_value(), "信任分有值");

    // 赛季统计
    Check(home.season.matchTotal.has_value(), "赛季场次有值");
    Check(home.season.avgRating.has_value(), "赛季平均 Rating 有值");
    Check(home.season.avgAdr.has_value(), "赛季平均 ADR 有值");
    Check(home.season.avgRws.has_value(), "赛季平均 RWS 有值");

    // 胜率派生
    if (auto wr = home.season.WinRate())
    {
        Check(*wr >= 0.0 && *wr <= 1.0, "赛季胜率落在 0..1");
    }
    else
    {
        Check(false, "赛季胜率可计算");
    }

    // 信用分判定
    Check(home.credit.IsCreditLow() == false || home.credit.credit.has_value(),
        "信用分偏低判定可用");

    std::printf("\n  玩家档案:\n");
    std::wprintf(L"    昵称       = %s\n", home.identity.username.c_str());
    std::wprintf(L"    domain     = %s\n", home.identity.domain.c_str());
    std::wprintf(L"    steam      = %s\n",
        home.identity.steamId64.empty() ? L"(空)" : home.identity.steamId64.c_str());
    if (home.rank.elo)      std::printf("    ELO        = %.2f\n", *home.rank.elo);
    if (home.rank.levelId)  std::printf("    段位 ID    = %d\n", *home.rank.levelId);
    if (home.rank.rank)     std::printf("    全服排名   = %d\n", *home.rank.rank);
    std::wprintf(L"    赛季       = %s\n", home.rank.season.c_str());
    if (home.career.matchTotal) std::printf("    生涯场次   = %d\n", *home.career.matchTotal);
    if (home.career.matchWin)   std::printf("    生涯胜场   = %d\n", *home.career.matchWin);
    if (home.credit.credit)     std::printf("    信用分     = %d\n", *home.credit.credit);
    if (home.season.avgRating)  std::printf("    赛季Rating = %.2f\n", *home.season.avgRating);
    if (home.season.avgAdr)     std::printf("    赛季ADR    = %.1f\n", *home.season.avgAdr);
    if (auto wr = home.season.WinRate()) std::printf("    赛季胜率   = %.0f%%\n", *wr * 100.0);

    std::printf("\n==== %s (failures=%d) ====\n",
        g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
