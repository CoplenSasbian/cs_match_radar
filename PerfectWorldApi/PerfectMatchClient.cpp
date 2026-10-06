#include "PerfectApiPch.h"
#include "PerfectMatchClient.h"
#include "PerfectJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/base.h>

#include <string>

using namespace winrt;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Headers;

namespace Perfect
{
    namespace
    {
        constexpr std::wstring_view kMatchListUrl =
            L"https://api.wmpvp.com/api/csgo/home/match/list";
        constexpr std::wstring_view kMatchDetailUrl =
            L"https://api.wmpvp.com/api/v1/csgo/match";
        constexpr std::wstring_view kPlayerProfileUrl =
            L"https://api.wmpvp.com/api/csgo/home/pvp/detailStats";
        constexpr std::wstring_view kSearchUserUrl =
            L"https://appengine.wmpvp.com/steamcn/app/search/user";

        // token 失效时服务端返回的业务码。
        constexpr int32_t kCodeAuthExpired = 4001;

        // 取嵌套对象。
        JsonObject OptionalObject(JsonObject const& object, std::wstring_view key)
        {
            if (!object)
            {
                return nullptr;
            }
            auto value = Json::Get(object, key);
            if (!value || Json::IsNull(value) ||
                value.ValueType() != JsonValueType::Object)
            {
                return nullptr;
            }
            return value.GetObject();
        }

        // 取数组。
        JsonArray OptionalArray(JsonObject const& object, std::wstring_view key)
        {
            if (!object)
            {
                return nullptr;
            }
            auto value = Json::Get(object, key);
            if (!value || Json::IsNull(value) ||
                value.ValueType() != JsonValueType::Array)
            {
                return nullptr;
            }
            return value.GetArray();
        }

        // 发起 POST 并返回响应文本；同时做统一的错误检查。
        IAsyncOperation<hstring> PostJsonAsync(
            std::wstring_view url,
            JsonObject const& payload,
            PerfectMatchCredentials const& credentials)
        {
            HttpClient client;
            HttpRequestMessage request;
            request.RequestUri(Uri(std::wstring(url)));
            request.Method(HttpMethod::Post());
            request.Content(HttpStringContent(payload.Stringify()));

            auto contentHeaders = request.Content().Headers();
            contentHeaders.ContentType(
                HttpMediaTypeHeaderValue(L"application/json;charset=UTF-8"));

            // 这些头是接口要求的固定组合。
            auto headers = request.Headers();
            headers.Append(L"appversion", std::wstring(PerfectMatchClient::kAppVersion));
            headers.Append(L"platform", L"android");
            headers.Append(L"token", credentials.accessToken);

            auto response = co_await client.SendRequestAsync(request);
            co_return co_await response.Content().ReadAsStringAsync();
        }

        // 校验 statusCode；登录失效映射为 AuthExpired 语义。
        void ThrowIfBusinessError(JsonObject const& root)
        {
            const int32_t statusCode = Json::OptionalInt32(root, L"statusCode").value_or(0);
            if (statusCode == 0)
            {
                return;
            }
            const std::wstring message =
                Json::OptionalString(root, L"errorMessage").value_or(L"未知错误");
            if (statusCode == kCodeAuthExpired)
            {
                throw hresult_error(E_ACCESSDENIED,
                    winrt::hstring(L"登录已失效，请重新扫码登录（" + message + L"）"));
            }
            throw hresult_error(E_FAIL,
                winrt::hstring(L"完美平台接口错误 " + std::to_wstring(statusCode) +
                    L"：" + message));
        }

        PerfectMatchSummary ParseSummary(JsonObject const& item)
        {
            PerfectMatchSummary out;
            out.matchId = Json::OptionalString(item, L"matchId").value_or(L"");
            out.playerId = Json::OptionalString(item, L"playerId").value_or(L"");
            out.mapName = Json::OptionalString(item, L"mapName").value_or(L"");
            out.mapLogo = Json::OptionalString(item, L"mapLogo").value_or(L"");
            out.mode = Json::OptionalString(item, L"mode").value_or(L"");
            out.team = Json::OptionalInt32(item, L"team");
            out.winTeam = Json::OptionalInt32(item, L"winTeam");
            out.score1 = Json::OptionalInt32(item, L"score1");
            out.score2 = Json::OptionalInt32(item, L"score2");
            out.kill = Json::OptionalInt32(item, L"kill");
            out.death = Json::OptionalInt32(item, L"death");
            out.assist = Json::OptionalInt32(item, L"assist");
            out.k4 = Json::OptionalInt32(item, L"k4");
            out.k5 = Json::OptionalInt32(item, L"k5");
            out.rating = Json::OptionalDouble(item, L"rating");
            out.pwRating = Json::OptionalDouble(item, L"pwRating");
            out.we = Json::OptionalDouble(item, L"we");
            out.pvpScore = Json::OptionalInt32(item, L"pvpScore");
            out.pvpScoreChange = Json::OptionalInt32(item, L"pvpScoreChange");
            out.startTime = Json::OptionalString(item, L"startTime").value_or(L"");
            out.endTime = Json::OptionalString(item, L"endTime").value_or(L"");
            out.duration = Json::OptionalInt32(item, L"duration");
            out.pvpMvp = Json::OptionalBool(item, L"pvpMvp").value_or(false);
            out.mvp = Json::OptionalBool(item, L"mvp").value_or(false);
            return out;
        }

        PerfectMatchPlayer ParsePlayer(JsonObject const& item)
        {
            PerfectMatchPlayer p;
            p.playerId = Json::OptionalString(item, L"playerId").value_or(L"");
            p.nickname = Json::OptionalString(item, L"nickName").value_or(L"");
            p.avatar = Json::OptionalString(item, L"avatar").value_or(L"");
            p.team = Json::OptionalInt32(item, L"team");

            p.kill = Json::OptionalInt32(item, L"kill");
            p.death = Json::OptionalInt32(item, L"death");
            p.assist = Json::OptionalInt32(item, L"assist");
            p.headShot = Json::OptionalInt32(item, L"headShot");
            p.headShotRatio = Json::OptionalDouble(item, L"headShotRatio");

            p.entryKill = Json::OptionalInt32(item, L"entryKill");
            p.entryDeath = Json::OptionalInt32(item, L"entryDeath");
            p.awpKill = Json::OptionalInt32(item, L"awpKill");
            p.snipeNum = Json::OptionalInt32(item, L"snipeNum");

            p.rating = Json::OptionalDouble(item, L"rating");
            p.pwRating = Json::OptionalDouble(item, L"pwRating");
            p.rws = Json::OptionalDouble(item, L"rws");
            p.we = Json::OptionalDouble(item, L"we");
            p.adpr = Json::OptionalInt32(item, L"adpr");
            p.kast = Json::OptionalInt32(item, L"kast");

            p.twoKill = Json::OptionalInt32(item, L"twoKill");
            p.threeKill = Json::OptionalInt32(item, L"threeKill");
            p.fourKill = Json::OptionalInt32(item, L"fourKill");
            p.fiveKill = Json::OptionalInt32(item, L"fiveKill");
            p.vs1 = Json::OptionalInt32(item, L"vs1");
            p.vs2 = Json::OptionalInt32(item, L"vs2");
            p.vs3 = Json::OptionalInt32(item, L"vs3");
            p.vs4 = Json::OptionalInt32(item, L"vs4");
            p.vs5 = Json::OptionalInt32(item, L"vs5");

            p.flash = Json::OptionalInt32(item, L"flash");
            p.flashTeammate = Json::OptionalInt32(item, L"flashTeammate");
            p.throwsCnt = Json::OptionalInt32(item, L"throwsCnt");
            p.dmgHealth = Json::OptionalInt32(item, L"dmgHealth");
            p.dmgArmor = Json::OptionalInt32(item, L"dmgArmor");
            p.grenadeDamage = Json::OptionalInt32(item, L"grenadeDamage");
            p.infernoDamage = Json::OptionalInt32(item, L"infernoDamage");
            p.tradeFragCount = Json::OptionalInt32(item, L"tradeFragCount");

            p.mvpValue = Json::OptionalInt32(item, L"mvpValue");
            p.pvpScore = Json::OptionalInt32(item, L"pvpScore");
            p.pvpScoreChange = Json::OptionalInt32(item, L"pvpScoreChange");
            p.rank = Json::OptionalInt32(item, L"rank");

            p.mvp = Json::OptionalBool(item, L"mvp").value_or(false);
            p.vac = Json::OptionalBool(item, L"vac").value_or(false);

            // killMap：{ 敌人SteamID: 击杀数 }
            if (auto killMap = OptionalObject(item, L"killMap"); killMap)
            {
                for (auto const& pair : killMap)
                {
                    if (pair.Value() && pair.Value().ValueType() == JsonValueType::Number)
                    {
                        p.killMap.emplace_back(
                            pair.Key().c_str(),
                            static_cast<int32_t>(pair.Value().GetNumber()));
                    }
                }
            }
            return p;
        }
    }

    PerfectMatchClient::PerfectMatchClient() = default;

    IAsyncAction PerfectMatchClient::GetMatchListAsync(
        PerfectMatchQuery const& query,
        PerfectMatchCredentials const& credentials,
        std::vector<PerfectMatchSummary>& out)
    {
        out.clear();

        if (query.toSteamId.empty())
        {
            throw hresult_invalid_argument(L"toSteamId 不能为空");
        }
        if (credentials.accessToken.empty() || credentials.selfSteamId.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先扫码登录完美平台");
        }

        JsonObject payload;
        payload.SetNamedValue(L"toSteamId",
            JsonValue::CreateStringValue(query.toSteamId));
        payload.SetNamedValue(L"mySteamId",
            JsonValue::CreateStringValue(credentials.selfSteamId));
        payload.SetNamedValue(L"dataSource",
            JsonValue::CreateNumberValue(static_cast<double>(
                static_cast<int32_t>(query.dataSource))));
        payload.SetNamedValue(L"page", JsonValue::CreateNumberValue(query.page));
        payload.SetNamedValue(L"pageSize", JsonValue::CreateNumberValue(query.pageSize));
        payload.SetNamedValue(L"csgoSeasonId",
            JsonValue::CreateStringValue(query.seasonId));
        payload.SetNamedValue(L"pvpType", JsonValue::CreateNumberValue(query.pvpType));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kMatchListUrl, payload, credentials));

        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(body, root) || !root)
        {
            throw hresult_error(E_FAIL, L"比赛列表返回的不是合法 JSON");
        }
        ThrowIfBusinessError(root);

        auto data = OptionalObject(root, L"data");
        if (!data)
        {
            co_return;
        }
        auto list = OptionalArray(data, L"matchList");
        if (!list)
        {
            co_return;
        }

        out.reserve(list.Size());
        for (auto const& element : list)
        {
            if (!element || element.ValueType() != JsonValueType::Object)
            {
                continue;
            }
            out.push_back(ParseSummary(element.GetObject()));
        }
        co_return;
    }

    IAsyncAction PerfectMatchClient::GetMatchDetailAsync(
        std::wstring matchId,
        int32_t dataSource,
        PerfectMatchCredentials const& credentials,
        PerfectMatchDetail& out)
    {
        out = PerfectMatchDetail{};

        if (matchId.empty())
        {
            throw hresult_invalid_argument(L"matchId 不能为空");
        }
        if (credentials.accessToken.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先扫码登录完美平台");
        }

        JsonObject payload;
        payload.SetNamedValue(L"matchId", JsonValue::CreateStringValue(matchId));
        payload.SetNamedValue(L"platform", JsonValue::CreateStringValue(L"admin"));
        payload.SetNamedValue(L"dataSource",
            JsonValue::CreateStringValue(std::to_wstring(dataSource)));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kMatchDetailUrl, payload, credentials));

        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(body, root) || !root)
        {
            throw hresult_error(E_FAIL, L"比赛详情返回的不是合法 JSON");
        }
        ThrowIfBusinessError(root);

        auto data = OptionalObject(root, L"data");
        if (!data)
        {
            co_return;
        }

        if (auto base = OptionalObject(data, L"base"); base)
        {
            out.matchId = Json::OptionalString(base, L"matchId").value_or(matchId);
            out.mapName = Json::OptionalString(base, L"map").value_or(L"");
            out.mapEn = Json::OptionalString(base, L"mapEn").value_or(L"");
            out.mapLogo = Json::OptionalString(base, L"mapLogo").value_or(L"");
            out.mode = Json::OptionalString(base, L"mode").value_or(L"");
            out.score1 = Json::OptionalInt32(base, L"score1");
            out.score2 = Json::OptionalInt32(base, L"score2");
            out.halfScore1 = Json::OptionalInt32(base, L"halfScore1");
            out.halfScore2 = Json::OptionalInt32(base, L"halfScore2");
            out.extraScore1 = Json::OptionalInt32(base, L"extraScore1");
            out.extraScore2 = Json::OptionalInt32(base, L"extraScore2");
            out.winTeam = Json::OptionalInt32(base, L"winTeam");
            out.startTime = Json::OptionalString(base, L"startTime").value_or(L"");
            out.endTime = Json::OptionalString(base, L"endTime").value_or(L"");
            out.duration = Json::OptionalInt32(base, L"duration");
            out.pvpLadder = Json::OptionalBool(base, L"pvpLadder").value_or(false);
        }

        if (auto players = OptionalArray(data, L"players"); players)
        {
            out.players.reserve(players.Size());
            for (auto const& element : players)
            {
                if (!element || element.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                out.players.push_back(ParsePlayer(element.GetObject()));
            }
        }
        co_return;
    }

    IAsyncAction PerfectMatchClient::SearchUserAsync(
        std::wstring keyword,
        PerfectMatchCredentials const& credentials,
        std::vector<PerfectSearchHit>& out)
    {
        out.clear();

        // 去掉首尾空白。
        {
            const size_t begin = keyword.find_first_not_of(L" \t\r\n");
            const size_t end = keyword.find_last_not_of(L" \t\r\n");
            keyword = (begin == std::wstring::npos)
                ? std::wstring{}
                : keyword.substr(begin, end - begin + 1);
        }
        if (keyword.empty())
        {
            co_return;
        }
        if (credentials.accessToken.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先扫码登录完美平台");
        }

        JsonObject payload;
        payload.SetNamedValue(L"keyword", JsonValue::CreateStringValue(keyword));
        payload.SetNamedValue(L"page", JsonValue::CreateNumberValue(1));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kSearchUserUrl, payload, credentials));

        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(body, root) || !root)
        {
            throw hresult_error(E_FAIL, L"搜索接口返回的不是合法 JSON");
        }

        // 注意：该接口用 code==1 表示成功（与其他接口相反），实测确认。
        const int32_t code = Json::OptionalInt32(root, L"code").value_or(0);
        if (code == kCodeAuthExpired)
        {
            throw hresult_error(E_ACCESSDENIED, L"登录已失效，请重新扫码登录");
        }
        if (code != 1)
        {
            const std::wstring message =
                Json::OptionalString(root, L"message").value_or(L"未知错误");
            throw hresult_error(E_FAIL, winrt::hstring(L"搜索失败：" + message));
        }

        auto results = OptionalArray(root, L"result");
        if (!results)
        {
            co_return;
        }

        out.reserve(results.Size());
        for (auto const& element : results)
        {
            if (!element || element.ValueType() != JsonValueType::Object)
            {
                continue;
            }
            auto item = element.GetObject();
            PerfectSearchHit hit;
            // steamId 在该接口里是数字，但可能以字符串返回，两种都要处理。
            auto steam = Json::Get(item, L"steamId");
            if (steam && !Json::IsNull(steam))
            {
                if (steam.ValueType() == JsonValueType::String)
                {
                    hit.steamId = std::wstring(steam.GetString().c_str());
                }
                else if (steam.ValueType() == JsonValueType::Number)
                {
                    hit.steamId = std::to_wstring(
                        static_cast<unsigned long long>(steam.GetNumber()));
                }
            }
            hit.pvpNickName = Json::OptionalString(item, L"pvpNickName").value_or(L"");
            hit.pvpAvatar = Json::OptionalString(item, L"pvpAvatar").value_or(L"");
            hit.appNickName = Json::OptionalString(item, L"appNickName").value_or(L"");

            if (hit.steamId.empty())
            {
                continue;
            }
            out.push_back(std::move(hit));
        }
        co_return;
    }    IAsyncAction PerfectMatchClient::GetPlayerProfileAsync(
        std::wstring steamId,
        PerfectMatchCredentials const& credentials,
        PerfectPlayerProfile& out)
    {
        out = PerfectPlayerProfile{};

        {
            const size_t begin = steamId.find_first_not_of(L" \t\r\n");
            const size_t end = steamId.find_last_not_of(L" \t\r\n");
            steamId = (begin == std::wstring::npos)
                ? std::wstring{}
                : steamId.substr(begin, end - begin + 1);
        }
        if (steamId.empty())
        {
            throw hresult_invalid_argument(L"steamId 不能为空");
        }
        if (credentials.accessToken.empty())
        {
            throw hresult_invalid_argument(L"缺少登录凭据，请先扫码登录完美平台");
        }

        // 文档标注 accessToken 为可选；带上它可与 header 的 token 互为校验。
        JsonObject payload;
        payload.SetNamedValue(L"mySteamId", JsonValue::CreateStringValue(credentials.selfSteamId));
        payload.SetNamedValue(L"toSteamId", JsonValue::CreateStringValue(steamId));
        payload.SetNamedValue(L"accessToken", JsonValue::CreateStringValue(credentials.accessToken));

        const std::wstring body =
            std::wstring(co_await PostJsonAsync(kPlayerProfileUrl, payload, credentials));

        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(body, root) || !root)
        {
            throw hresult_error(E_FAIL, L"玩家资料返回的不是合法 JSON");
        }
        const int32_t code = Json::OptionalInt32(root, L"statusCode").value_or(0);
        if (code == kCodeAuthExpired)
        {
            throw hresult_error(E_ACCESSDENIED, L"登录已失效，请重新扫码登录");
        }
        if (code != 0)
        {
            const std::wstring message =
                Json::OptionalString(root, L"errorMessage").value_or(L"未知错误");
            throw hresult_error(E_FAIL, winrt::hstring(L"查询玩家资料失败：" + message));
        }

        auto d = OptionalObject(root, L"data");
        if (!d)
        {
            co_return;
        }

        auto num = [&](wchar_t const* key) { return Json::OptionalDouble(d, key); };
        auto inum = [&](wchar_t const* key) { return Json::OptionalInt32(d, key); };
        auto str = [&](wchar_t const* key) {
            return Json::OptionalString(d, key).value_or(L"");
        };

        out.steamId = str(L"steamId");
        out.name = str(L"name");
        out.avatar = str(L"avatar");
        out.summary = str(L"summary");
        out.seasonId = str(L"seasonId");

        out.pvpScore = inum(L"pvpScore");
        out.stars = inum(L"stars");
        out.pvpRank = inum(L"pvpRank");

        out.cnt = inum(L"cnt");
        out.winRate = num(L"winRate");
        out.kd = num(L"kd");
        out.rating = num(L"rating");
        out.pwRating = num(L"pwRating");
        out.commonRating = num(L"commonRating");
        out.rws = num(L"rws");
        out.adr = num(L"adr");
        out.avgWe = num(L"avgWe");
        out.headShotRatio = num(L"headShotRatio");
        out.entryKillRatio = num(L"entryKillRatio");

        out.kills = inum(L"kills");
        out.deaths = inum(L"deaths");
        out.assists = inum(L"assists");
        out.mvpCount = inum(L"mvpCount");
        out.multiKill = inum(L"multiKill");
        out.k2 = inum(L"k2");
        out.k3 = inum(L"k3");
        out.k4 = inum(L"k4");
        out.k5 = inum(L"k5");
        out.vs1 = inum(L"vs1");
        out.vs2 = inum(L"vs2");
        out.vs3 = inum(L"vs3");
        out.vs4 = inum(L"vs4");
        out.vs5 = inum(L"vs5");
        out.endingWin = inum(L"endingWin");

        out.shot = num(L"shot");
        out.snipe = num(L"snipe");
        out.victory = num(L"victory");
        out.breach = num(L"breach");
        out.prop = num(L"prop");

        // 趋势数组。
        auto readNumbers = [&](wchar_t const* key, std::vector<double>& target) {
            if (auto arr = OptionalArray(d, key))
            {
                for (auto const& v : arr)
                {
                    if (v && v.ValueType() == JsonValueType::Number)
                    {
                        target.push_back(v.GetNumber());
                    }
                }
            }
        };
        readNumbers(L"historyRatings", out.historyRatings);
        readNumbers(L"historyPwRatings", out.historyPwRatings);
        readNumbers(L"historyScores", out.historyScores);
        readNumbers(L"historyRws", out.historyRws);

        if (auto dates = OptionalArray(d, L"historyDates"))
        {
            for (auto const& v : dates)
            {
                if (v && v.ValueType() == JsonValueType::String)
                {
                    out.historyDates.push_back(std::wstring(v.GetString().c_str()));
                }
            }
        }

        // 常用武器（取字段更全的 hotWeapons2；它带命中率与评级）。
        if (auto weapons = OptionalArray(d, L"hotWeapons2"))
        {
            for (auto const& element : weapons)
            {
                if (!element || element.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                auto w = element.GetObject();
                PerfectWeaponStat stat;
                auto s = [&](wchar_t const* k) {
                    return Json::OptionalString(w, k).value_or(L"");
                };
                stat.name = s(L"name");
                stat.nameZh = s(L"nameZh");
                stat.image = s(L"image");
                stat.killNum = Json::OptionalInt32(w, L"killNum");
                stat.headshotSum = Json::OptionalInt32(w, L"headshotSum");
                stat.headshotRate = Json::OptionalDouble(w, L"headshotRate");
                stat.avgDamage = Json::OptionalDouble(w, L"avgDamage");
                stat.sprayAccuracy = Json::OptionalDouble(w, L"sprayAccuracy");
                stat.firstShotAccuracy = Json::OptionalDouble(w, L"firstShotAccuracy");
                stat.matchNum = Json::OptionalInt32(w, L"matchNum");
                stat.levelAccuracy = s(L"levelAccuracy");
                stat.levelHeadshotRate = s(L"levelHeadshotRate");
                if (stat.name.empty() && stat.nameZh.empty())
                {
                    continue;
                }
                out.hotWeapons.push_back(std::move(stat));
            }
        }

        // 常用地图。
        if (auto maps = OptionalArray(d, L"hotMaps"))
        {
            for (auto const& element : maps)
            {
                if (!element || element.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                auto m = element.GetObject();
                PerfectMapStat stat;
                auto s = [&](wchar_t const* k) {
                    return Json::OptionalString(m, k).value_or(L"");
                };
                stat.map = s(L"map");
                stat.mapName = s(L"mapName");
                stat.mapImage = s(L"mapImage");
                stat.mapLogo = s(L"mapLogo");
                stat.totalMatch = Json::OptionalInt32(m, L"totalMatch");
                stat.totalKill = Json::OptionalInt32(m, L"totalKill");
                stat.deathNum = Json::OptionalInt32(m, L"deathNum");
                stat.totalAdr = Json::OptionalDouble(m, L"totalAdr");
                stat.ratingSum = Json::OptionalDouble(m, L"ratingSum");
                stat.winCount = Json::OptionalInt32(m, L"winCount");
                stat.matchMvpNum = Json::OptionalInt32(m, L"matchMvpNum");
                stat.threeKillNum = Json::OptionalInt32(m, L"threeKillNum");
                stat.fourKillNum = Json::OptionalInt32(m, L"fourKillNum");
                if (stat.map.empty() && stat.mapName.empty())
                {
                    continue;
                }
                out.hotMaps.push_back(std::move(stat));
            }
        }
        co_return;
    }
}
