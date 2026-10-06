#include "PerfectApiPch.h"
#include "PerfectWorldApi.h"
#include "PerfectAuth.h"
#include "PerfectClient.h"
#include "PerfectMatchClient.h"
#include "PerfectCrypto.h"
#include "PerfectModels.h"
#include "PerfectParser.h"
#include "PerfectSerialize.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <new>
#include <string>

// CoGetApartmentType / CreateThread 所需的头。
#include <objbase.h>
#include <exception>

namespace
{
    constexpr wchar_t kVersion[] = L"1.0.0";

    std::wstring& LastErrorBuffer()
    {
        static std::wstring buffer;
        return buffer;
    }

    void SetLastError(std::wstring text) { LastErrorBuffer() = std::move(text); }
    void ClearLastError() { LastErrorBuffer().clear(); }

    wchar_t* AllocateString(std::wstring const& text)
    {
        if (text.empty())
        {
            auto* empty = new (std::nothrow) wchar_t[1];
            if (empty) { empty[0] = L'\0'; }
            return empty;
        }
        auto* copy = new (std::nothrow) wchar_t[text.size() + 1];
        if (!copy) { return nullptr; }
        std::wmemcpy(copy, text.c_str(), text.size() + 1);
        return copy;
    }

    // 确保 WinRT apartment 已初始化。C 接口可能从任意线程调用。
    //
    // 与 5eApi 相同的教训：不能无条件 init_apartment(multi_threaded)。
    // WinUI / WPF 等宿主把 UI 线程初始化为 STA，此时再要求 MTA 会抛
    // hresult_error(RPC_E_CHANGED_MODE，即“无法在设置线程模式后对其加以更改”)，
    // 导致所有接口直接失败。
    // 先探测：已初始化（STA/MTA）就复用，只有确实未初始化才建 MTA。
    void EnsureApartment()
    {
        static thread_local bool initialized = false;
        if (initialized)
        {
            return;
        }

        APTTYPE apartmentType{};
        APTTYPEQUALIFIER qualifier{};
        const HRESULT hr = CoGetApartmentType(&apartmentType, &qualifier);
        if (hr == CO_E_NOTINITIALIZED)
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }

        initialized = true;
    }

    PerfectStatus MapDecryptError(Perfect::DecryptError error)
    {
        using Perfect::DecryptError;
        switch (error)
        {
        case DecryptError::Envelope:  return PerfectStatus_Parse;
        case DecryptError::Base64:    return PerfectStatus_Decrypt;
        case DecryptError::KeyLength: return PerfectStatus_Decrypt;
        case DecryptError::Padding:   return PerfectStatus_Decrypt;
        case DecryptError::Utf8:      return PerfectStatus_Decrypt;
        case DecryptError::Json:      return PerfectStatus_Parse;
        case DecryptError::Crypto:    return PerfectStatus_Decrypt;
        default:                      return PerfectStatus_Internal;
        }
    }

    std::wstring DescribeCurrentException()
    {
        try
        {
            throw;
        }
        catch (winrt::hresult_error const& ex)
        {
            return std::wstring(L"WinRT 调用失败：") + std::wstring(ex.message());
        }
        catch (std::exception const& ex)
        {
            const std::string narrow = ex.what() ? ex.what() : "";
            if (narrow.empty()) { return L"未知错误"; }
            const int size = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
            if (size <= 1) { return L"未知错误"; }
            std::wstring wide(static_cast<size_t>(size - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wide.data(), size);
            return wide;
        }
        catch (...)
        {
            return L"未知错误";
        }
    }

    // 判断 hresult 消息里是否包含登录失效的业务码。
    PerfectStatus MapHresult(winrt::hresult_error const& ex)
    {
        const std::wstring message(ex.message());
        if (message.find(L"1002") != std::wstring::npos ||
            message.find(L"1033") != std::wstring::npos)
        {
            return PerfectStatus_AuthExpired;
        }
        if (message.find(L"解密失败") != std::wstring::npos)
        {
            return PerfectStatus_Decrypt;
        }
        if (message.find(L"不能为空") != std::wstring::npos)
        {
            return PerfectStatus_InvalidArgument;
        }
        return PerfectStatus_Business;
    }

    // 在专用的 MTA 工作线程上执行 func，并等待其完成。
    //
    // 与 5eApi 同样的原因：这些 C 接口内部用 .get() 同步等待 WinRT 异步操作，
    // 而异步完成回调需要在其所属 apartment 上派发。若在宿主 UI 线程（STA）
    // 上直接 .get()，该线程被阻塞、消息泵停转，操作永远完不成（界面卡死）。
    // 因此把实际工作放到独立 MTA 线程执行。
    template <typename TFunc>
    auto RunOnMtaThread(TFunc&& func) -> decltype(func())
    {
        using Result = decltype(func());
        using Fn = std::remove_reference_t<TFunc>;

        struct Context
        {
            Fn* fn{ nullptr };
            std::optional<Result> value;
            std::exception_ptr error;
        };

        Context context;
        context.fn = &func;

        HANDLE thread = CreateThread(
            nullptr, 0,
            [](void* raw) -> DWORD {
                auto* ctx = static_cast<Context*>(raw);
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                try
                {
                    ctx->value.emplace((*ctx->fn)());
                }
                catch (...)
                {
                    ctx->error = std::current_exception();
                }
                return 0;
            },
            &context, 0, nullptr);

        if (!thread)
        {
            throw winrt::hresult_error(E_FAIL, L"创建网络工作线程失败");
        }
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);

        if (context.error)
        {
            std::rethrow_exception(context.error);
        }
        return std::move(*context.value);
    }

    template <typename TFunc>
    PerfectStatus RunAndSerialize(TFunc&& func, wchar_t** outJson)
    {
        if (!outJson)
        {
            SetLastError(L"outJson 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        *outJson = nullptr;
        ClearLastError();
        try
        {
            EnsureApartment();
            // 关键：把阻塞式网络请求放到 MTA 工作线程，避免在 STA UI 线程死锁。
            const std::wstring json = RunOnMtaThread(func);
            *outJson = AllocateString(json);
            if (!*outJson)
            {
                SetLastError(L"内存分配失败");
                return PerfectStatus_Internal;
            }
            return PerfectStatus_Ok;
        }
        catch (winrt::hresult_error const& ex)
        {
            SetLastError(std::wstring(ex.message().c_str()));
            return MapHresult(ex);
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return PerfectStatus_Internal;
        }
    }

    struct ClientHandle
    {
        Perfect::PerfectClient client;
    };

    ClientHandle* AsHandle(void* client)
    {
        return static_cast<ClientHandle*>(client);
    }

    // 校验三个必填参数。
    bool ValidateArgs(void* client, wchar_t const* steamId,
                      wchar_t const* accessToken, wchar_t const* selfSteamId,
                      ClientHandle*& handle)
    {
        handle = AsHandle(client);
        if (!handle) { SetLastError(L"client 句柄为空"); return false; }
        if (!steamId || !*steamId) { SetLastError(L"steamId 不能为空"); return false; }
        if (!accessToken || !*accessToken) { SetLastError(L"accessToken 不能为空"); return false; }
        if (!selfSteamId || !*selfSteamId) { SetLastError(L"selfSteamId 不能为空"); return false; }
        return true;
    }
}

extern "C"
{
    void* __stdcall PerfectClientCreate(void)
    {
        ClearLastError();
        try
        {
            EnsureApartment();
            return new ClientHandle();
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return nullptr;
        }
    }

    void __stdcall PerfectClientDestroy(void* client)
    {
        delete AsHandle(client);
    }

    PerfectStatus __stdcall PerfectGetPlayer(
        void* client,
        wchar_t const* steamId,
        wchar_t const* accessToken,
        wchar_t const* selfSteamId,
        wchar_t** outJson)
    {
        ClientHandle* handle = nullptr;
        if (!ValidateArgs(client, steamId, accessToken, selfSteamId, handle))
        {
            return PerfectStatus_InvalidArgument;
        }

        Perfect::PerfectCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = selfSteamId;
        const std::wstring target = steamId;

        return RunAndSerialize(
            [&] {
                Perfect::PerfectPlayer player;
                handle->client.GetPlayerAsync(target, credentials, player).get();
                return Perfect::Json::ToJson(player);
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectGetOverview(
        void* client,
        wchar_t const* steamId,
        wchar_t const* accessToken,
        wchar_t const* selfSteamId,
        wchar_t** outJson)
    {
        ClientHandle* handle = nullptr;
        if (!ValidateArgs(client, steamId, accessToken, selfSteamId, handle))
        {
            return PerfectStatus_InvalidArgument;
        }

        Perfect::PerfectCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = selfSteamId;
        const std::wstring target = steamId;

        return RunAndSerialize(
            [&] {
                Perfect::PerfectPlayer player;
                handle->client.GetOverviewAsync(target, credentials, player).get();
                return Perfect::Json::ToOverviewJson(player);
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectGetSeasonList(
        void* client,
        wchar_t const* steamId,
        wchar_t const* accessToken,
        wchar_t const* selfSteamId,
        wchar_t** outJson)
    {
        ClientHandle* handle = nullptr;
        if (!ValidateArgs(client, steamId, accessToken, selfSteamId, handle))
        {
            return PerfectStatus_InvalidArgument;
        }

        Perfect::PerfectCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = selfSteamId;
        const std::wstring target = steamId;

        return RunAndSerialize(
            [&] {
                std::vector<std::wstring> seasons;
                handle->client.GetSeasonListAsync(target, credentials, seasons).get();
                return Perfect::Json::ToJson(seasons);
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectApplyQrLogin(wchar_t** outJson)
    {
        return RunAndSerialize(
            [] {
                // 扫码接口不需要登录态；每次新建一个客户端即可。
                Perfect::PerfectAuth auth;
                Perfect::QrLoginResult result;
                auth.ApplyQrAsync(result).get();

                // 用 JsonObject 构造，交给序列化器处理转义。
                winrt::Windows::Data::Json::JsonObject json;
                json.SetNamedValue(L"accessUrl",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(result.accessUrl));
                json.SetNamedValue(L"pollToken",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(result.pollToken));
                return std::wstring(json.Stringify().c_str());
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectPollQrLogin(
        const wchar_t* pollToken, wchar_t** outJson)
    {
        if (!pollToken || !*pollToken)
        {
            SetLastError(L"pollToken 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!outJson)
        {
            SetLastError(L"outJson 不能为空");
            return PerfectStatus_InvalidArgument;
        }

        const std::wstring token = pollToken;
        return RunAndSerialize(
            [&token] {
                Perfect::PerfectAuth auth;
                Perfect::QrLoginResult result;
                auth.PollQrAsync(token, result).get();

                const wchar_t* phase = L"WaitingScan";
                switch (result.phase)
                {
                case Perfect::QrLoginPhase::Scanned:   phase = L"Scanned";   break;
                case Perfect::QrLoginPhase::Confirmed: phase = L"Confirmed"; break;
                case Perfect::QrLoginPhase::Expired:   phase = L"Expired";   break;
                default:                               phase = L"WaitingScan"; break;
                }

                winrt::Windows::Data::Json::JsonObject json;
                json.SetNamedValue(L"phase",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(phase));
                json.SetNamedValue(L"accessToken",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(result.accessToken));
                json.SetNamedValue(L"selfSteamId",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(result.selfSteamId));
                json.SetNamedValue(L"message",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(result.message));
                return std::wstring(json.Stringify().c_str());
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectValidateCredentials(
        const wchar_t* accessToken, const wchar_t* selfSteamId, int32_t* outValid)
    {
        if (!accessToken || !*accessToken || !selfSteamId || !*selfSteamId)
        {
            SetLastError(L"accessToken 与 selfSteamId 都不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!outValid)
        {
            SetLastError(L"outValid 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        *outValid = 0;

        const std::wstring token = accessToken;
        const std::wstring self = selfSteamId;
        try
        {
            EnsureApartment();
            bool valid = false;
            // 走 MTA 工作线程，避免在 STA UI 线程上阻塞。
            RunOnMtaThread([&] {
                Perfect::PerfectAuth auth;
                auth.ValidateAsync(token, self, valid).get();
                return 0;
            });
            *outValid = valid ? 1 : 0;
            return PerfectStatus_Ok;
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return PerfectStatus_Internal;
        }
    }

    PerfectStatus __stdcall PerfectGetMatchList(
        const wchar_t* toSteamId,
        const wchar_t* accessToken,
        const wchar_t* selfSteamId,
        int32_t dataSource,
        int32_t page,
        int32_t pageSize,
        const wchar_t* seasonId,
        wchar_t** outJson)
    {
        if (!toSteamId || !*toSteamId)
        {
            SetLastError(L"toSteamId 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!accessToken || !*accessToken || !selfSteamId || !*selfSteamId)
        {
            SetLastError(L"accessToken 与 selfSteamId 都不能为空");
            return PerfectStatus_InvalidArgument;
        }

        Perfect::PerfectMatchQuery query;
        query.toSteamId = toSteamId;
        query.dataSource = static_cast<Perfect::MatchDataSource>(
            dataSource > 0 ? dataSource : 3);
        query.page = page > 0 ? page : 1;
        query.pageSize = pageSize > 0 ? pageSize : 20;
        if (seasonId && *seasonId)
        {
            query.seasonId = seasonId;
        }

        Perfect::PerfectMatchCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = selfSteamId;

        return RunAndSerialize(
            [&] {
                Perfect::PerfectMatchClient client;
                std::vector<Perfect::PerfectMatchSummary> matches;
                client.GetMatchListAsync(query, credentials, matches).get();

                // 手写紧凑 JSON：字段较多，用 JsonObject 构造保证转义正确。
                using namespace winrt::Windows::Data::Json;
                JsonObject root;
                root.SetNamedValue(L"total",
                    JsonValue::CreateNumberValue(static_cast<double>(matches.size())));

                JsonArray list;
                for (auto const& m : matches)
                {
                    JsonObject item;
                    item.SetNamedValue(L"matchId", JsonValue::CreateStringValue(m.matchId));
                    item.SetNamedValue(L"playerId", JsonValue::CreateStringValue(m.playerId));
                    item.SetNamedValue(L"mapName", JsonValue::CreateStringValue(m.mapName));
                    item.SetNamedValue(L"mapLogo", JsonValue::CreateStringValue(m.mapLogo));
                    item.SetNamedValue(L"mode", JsonValue::CreateStringValue(m.mode));
                    item.SetNamedValue(L"startTime", JsonValue::CreateStringValue(m.startTime));
                    item.SetNamedValue(L"endTime", JsonValue::CreateStringValue(m.endTime));
                    item.SetNamedValue(L"team",
                        JsonValue::CreateNumberValue(m.team.value_or(0)));
                    item.SetNamedValue(L"winTeam",
                        JsonValue::CreateNumberValue(m.winTeam.value_or(0)));
                    item.SetNamedValue(L"score1",
                        JsonValue::CreateNumberValue(m.score1.value_or(0)));
                    item.SetNamedValue(L"score2",
                        JsonValue::CreateNumberValue(m.score2.value_or(0)));
                    item.SetNamedValue(L"kill",
                        JsonValue::CreateNumberValue(m.kill.value_or(0)));
                    item.SetNamedValue(L"death",
                        JsonValue::CreateNumberValue(m.death.value_or(0)));
                    item.SetNamedValue(L"assist",
                        JsonValue::CreateNumberValue(m.assist.value_or(0)));
                    item.SetNamedValue(L"k4", JsonValue::CreateNumberValue(m.k4.value_or(0)));
                    item.SetNamedValue(L"k5", JsonValue::CreateNumberValue(m.k5.value_or(0)));
                    item.SetNamedValue(L"rating",
                        JsonValue::CreateNumberValue(m.rating.value_or(0.0)));
                    item.SetNamedValue(L"pwRating",
                        JsonValue::CreateNumberValue(m.pwRating.value_or(0.0)));
                    item.SetNamedValue(L"we", JsonValue::CreateNumberValue(m.we.value_or(0.0)));
                    item.SetNamedValue(L"pvpScore",
                        JsonValue::CreateNumberValue(m.pvpScore.value_or(0)));
                    item.SetNamedValue(L"pvpScoreChange",
                        JsonValue::CreateNumberValue(m.pvpScoreChange.value_or(0)));
                    item.SetNamedValue(L"duration",
                        JsonValue::CreateNumberValue(m.duration.value_or(0)));
                    item.SetNamedValue(L"pvpMvp",
                        JsonValue::CreateBooleanValue(m.pvpMvp));
                    item.SetNamedValue(L"mvp", JsonValue::CreateBooleanValue(m.mvp));
                    item.SetNamedValue(L"isWin", JsonValue::CreateBooleanValue(m.IsWin()));
                    list.Append(item);
                }
                root.SetNamedValue(L"list", list);
                return std::wstring(root.Stringify().c_str());
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectGetMatchDetail(
        const wchar_t* matchId,
        int32_t dataSource,
        const wchar_t* accessToken,
        const wchar_t* selfSteamId,
        wchar_t** outJson)
    {
        if (!matchId || !*matchId)
        {
            SetLastError(L"matchId 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!accessToken || !*accessToken)
        {
            SetLastError(L"accessToken 不能为空");
            return PerfectStatus_InvalidArgument;
        }

        const std::wstring id = matchId;
        const int32_t source = dataSource > 0 ? dataSource : 3;
        Perfect::PerfectMatchCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = selfSteamId ? selfSteamId : L"";

        return RunAndSerialize(
            [&] {
                Perfect::PerfectMatchClient client;
                Perfect::PerfectMatchDetail detail;
                client.GetMatchDetailAsync(id, source, credentials, detail).get();

                using namespace winrt::Windows::Data::Json;

                auto numOr = [](std::optional<int32_t> const& v) {
                    return JsonValue::CreateNumberValue(v.value_or(0));
                };
                auto dblOr = [](std::optional<double> const& v) {
                    return JsonValue::CreateNumberValue(v.value_or(0.0));
                };

                JsonObject root;
                JsonObject base;
                base.SetNamedValue(L"matchId", JsonValue::CreateStringValue(detail.matchId));
                base.SetNamedValue(L"map", JsonValue::CreateStringValue(detail.mapName));
                base.SetNamedValue(L"mapEn", JsonValue::CreateStringValue(detail.mapEn));
                base.SetNamedValue(L"mapLogo", JsonValue::CreateStringValue(detail.mapLogo));
                base.SetNamedValue(L"mode", JsonValue::CreateStringValue(detail.mode));
                base.SetNamedValue(L"score1", numOr(detail.score1));
                base.SetNamedValue(L"score2", numOr(detail.score2));
                base.SetNamedValue(L"halfScore1", numOr(detail.halfScore1));
                base.SetNamedValue(L"halfScore2", numOr(detail.halfScore2));
                base.SetNamedValue(L"extraScore1", numOr(detail.extraScore1));
                base.SetNamedValue(L"extraScore2", numOr(detail.extraScore2));
                base.SetNamedValue(L"winTeam", numOr(detail.winTeam));
                base.SetNamedValue(L"startTime", JsonValue::CreateStringValue(detail.startTime));
                base.SetNamedValue(L"endTime", JsonValue::CreateStringValue(detail.endTime));
                base.SetNamedValue(L"duration", numOr(detail.duration));
                root.SetNamedValue(L"base", base);

                JsonArray players;
                for (auto const& p : detail.players)
                {
                    JsonObject item;
                    item.SetNamedValue(L"playerId", JsonValue::CreateStringValue(p.playerId));
                    item.SetNamedValue(L"nickName", JsonValue::CreateStringValue(p.nickname));
                    item.SetNamedValue(L"avatar", JsonValue::CreateStringValue(p.avatar));
                    item.SetNamedValue(L"team", numOr(p.team));
                    item.SetNamedValue(L"kill", numOr(p.kill));
                    item.SetNamedValue(L"death", numOr(p.death));
                    item.SetNamedValue(L"assist", numOr(p.assist));
                    item.SetNamedValue(L"headShot", numOr(p.headShot));
                    item.SetNamedValue(L"headShotRatio", dblOr(p.headShotRatio));
                    item.SetNamedValue(L"entryKill", numOr(p.entryKill));
                    item.SetNamedValue(L"entryDeath", numOr(p.entryDeath));
                    item.SetNamedValue(L"awpKill", numOr(p.awpKill));
                    item.SetNamedValue(L"rating", dblOr(p.rating));
                    item.SetNamedValue(L"pwRating", dblOr(p.pwRating));
                    item.SetNamedValue(L"rws", dblOr(p.rws));
                    item.SetNamedValue(L"we", dblOr(p.we));
                    item.SetNamedValue(L"adpr", numOr(p.adpr));
                    item.SetNamedValue(L"kast", numOr(p.kast));
                    item.SetNamedValue(L"twoKill", numOr(p.twoKill));
                    item.SetNamedValue(L"threeKill", numOr(p.threeKill));
                    item.SetNamedValue(L"fourKill", numOr(p.fourKill));
                    item.SetNamedValue(L"fiveKill", numOr(p.fiveKill));
                    item.SetNamedValue(L"vs1", numOr(p.vs1));
                    item.SetNamedValue(L"vs2", numOr(p.vs2));
                    item.SetNamedValue(L"vs3", numOr(p.vs3));
                    item.SetNamedValue(L"vs4", numOr(p.vs4));
                    item.SetNamedValue(L"vs5", numOr(p.vs5));
                    item.SetNamedValue(L"flash", numOr(p.flash));
                    item.SetNamedValue(L"flashTeammate", numOr(p.flashTeammate));
                    item.SetNamedValue(L"throwsCnt", numOr(p.throwsCnt));
                    item.SetNamedValue(L"dmgHealth", numOr(p.dmgHealth));
                    item.SetNamedValue(L"dmgArmor", numOr(p.dmgArmor));
                    item.SetNamedValue(L"grenadeDamage", numOr(p.grenadeDamage));
                    item.SetNamedValue(L"infernoDamage", numOr(p.infernoDamage));
                    item.SetNamedValue(L"tradeFragCount", numOr(p.tradeFragCount));
                    item.SetNamedValue(L"mvpValue", numOr(p.mvpValue));
                    item.SetNamedValue(L"pvpScore", numOr(p.pvpScore));
                    item.SetNamedValue(L"pvpScoreChange", numOr(p.pvpScoreChange));
                    item.SetNamedValue(L"rank", numOr(p.rank));
                    item.SetNamedValue(L"mvp", JsonValue::CreateBooleanValue(p.mvp));
                    item.SetNamedValue(L"vac", JsonValue::CreateBooleanValue(p.vac));
                    players.Append(item);
                }
                root.SetNamedValue(L"players", players);
                return std::wstring(root.Stringify().c_str());
            },
            outJson);
    }

    PerfectStatus __stdcall PerfectGetPlayerProfile(
        const wchar_t* steamId,
        const wchar_t* accessToken,
        const wchar_t* selfSteamId,
        wchar_t** outJson)
    {
        if (!steamId || !*steamId)
        {
            SetLastError(L"steamId 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!accessToken || !*accessToken)
        {
            SetLastError(L"accessToken 不能为空");
            return PerfectStatus_InvalidArgument;
        }

        const std::wstring target = steamId;
        Perfect::PerfectMatchCredentials credentials;
        credentials.accessToken = accessToken;
        credentials.selfSteamId = (selfSteamId && *selfSteamId) ? selfSteamId : L"";

        return RunAndSerialize(
            [&] {
                Perfect::PerfectMatchClient client;
                Perfect::PerfectPlayerProfile profile;
                client.GetPlayerProfileAsync(target, credentials, profile).get();

                using namespace winrt::Windows::Data::Json;
                auto num = [](std::optional<double> const& v) {
                    return JsonValue::CreateNumberValue(v.value_or(0.0));
                };
                auto inum = [](std::optional<int32_t> const& v) {
                    return JsonValue::CreateNumberValue(v.value_or(0));
                };
                auto dbl = [](std::optional<double> const& v) {
                    return JsonValue::CreateNumberValue(v.value_or(0.0));
                };
                auto numArray = [](std::vector<double> const& values) {
                    JsonArray arr;
                    for (double v : values) { arr.Append(JsonValue::CreateNumberValue(v)); }
                    return arr;
                };

                JsonObject p;
                p.SetNamedValue(L"steamId", JsonValue::CreateStringValue(profile.steamId));
                p.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
                p.SetNamedValue(L"avatar", JsonValue::CreateStringValue(profile.avatar));
                p.SetNamedValue(L"summary", JsonValue::CreateStringValue(profile.summary));
                p.SetNamedValue(L"seasonId", JsonValue::CreateStringValue(profile.seasonId));
                p.SetNamedValue(L"pvpScore", inum(profile.pvpScore));
                p.SetNamedValue(L"stars", inum(profile.stars));
                p.SetNamedValue(L"pvpRank", inum(profile.pvpRank));
                p.SetNamedValue(L"cnt", inum(profile.cnt));
                p.SetNamedValue(L"winRate", dbl(profile.winRate));
                p.SetNamedValue(L"kd", dbl(profile.kd));
                p.SetNamedValue(L"rating", dbl(profile.rating));
                p.SetNamedValue(L"pwRating", dbl(profile.pwRating));
                p.SetNamedValue(L"commonRating", dbl(profile.commonRating));
                p.SetNamedValue(L"rws", dbl(profile.rws));
                p.SetNamedValue(L"adr", dbl(profile.adr));
                p.SetNamedValue(L"avgWe", dbl(profile.avgWe));
                p.SetNamedValue(L"headShotRatio", dbl(profile.headShotRatio));
                p.SetNamedValue(L"entryKillRatio", dbl(profile.entryKillRatio));
                p.SetNamedValue(L"kills", inum(profile.kills));
                p.SetNamedValue(L"deaths", inum(profile.deaths));
                p.SetNamedValue(L"assists", inum(profile.assists));
                p.SetNamedValue(L"mvpCount", inum(profile.mvpCount));
                p.SetNamedValue(L"multiKill", inum(profile.multiKill));
                p.SetNamedValue(L"k2", inum(profile.k2));
                p.SetNamedValue(L"k3", inum(profile.k3));
                p.SetNamedValue(L"k4", inum(profile.k4));
                p.SetNamedValue(L"k5", inum(profile.k5));
                p.SetNamedValue(L"vs1", inum(profile.vs1));
                p.SetNamedValue(L"vs2", inum(profile.vs2));
                p.SetNamedValue(L"vs3", inum(profile.vs3));
                p.SetNamedValue(L"vs4", inum(profile.vs4));
                p.SetNamedValue(L"vs5", inum(profile.vs5));
                p.SetNamedValue(L"endingWin", inum(profile.endingWin));
                p.SetNamedValue(L"shot", dbl(profile.shot));
                p.SetNamedValue(L"snipe", dbl(profile.snipe));
                p.SetNamedValue(L"victory", dbl(profile.victory));
                p.SetNamedValue(L"breach", dbl(profile.breach));
                p.SetNamedValue(L"prop", dbl(profile.prop));
                p.SetNamedValue(L"historyRatings", numArray(profile.historyRatings));
                p.SetNamedValue(L"historyPwRatings", numArray(profile.historyPwRatings));
                p.SetNamedValue(L"historyScores", numArray(profile.historyScores));
                p.SetNamedValue(L"historyRws", numArray(profile.historyRws));

                JsonArray dates;
                for (auto const& d2 : profile.historyDates)
                {
                    dates.Append(JsonValue::CreateStringValue(d2));
                }
                p.SetNamedValue(L"historyDates", dates);

                JsonArray weapons;
                for (auto const& w : profile.hotWeapons)
                {
                    JsonObject item;
                    item.SetNamedValue(L"name", JsonValue::CreateStringValue(w.name));
                    item.SetNamedValue(L"nameZh", JsonValue::CreateStringValue(w.nameZh));
                    item.SetNamedValue(L"image", JsonValue::CreateStringValue(w.image));
                    item.SetNamedValue(L"killNum", inum(w.killNum));
                    item.SetNamedValue(L"headshotSum", inum(w.headshotSum));
                    item.SetNamedValue(L"headshotRate", dbl(w.headshotRate));
                    item.SetNamedValue(L"avgDamage", dbl(w.avgDamage));
                    item.SetNamedValue(L"sprayAccuracy", dbl(w.sprayAccuracy));
                    item.SetNamedValue(L"firstShotAccuracy", dbl(w.firstShotAccuracy));
                    item.SetNamedValue(L"matchNum", inum(w.matchNum));
                    item.SetNamedValue(L"levelAccuracy", JsonValue::CreateStringValue(w.levelAccuracy));
                    item.SetNamedValue(L"levelHeadshotRate", JsonValue::CreateStringValue(w.levelHeadshotRate));
                    weapons.Append(item);
                }

                JsonArray maps;
                for (auto const& m : profile.hotMaps)
                {
                    JsonObject item;
                    item.SetNamedValue(L"map", JsonValue::CreateStringValue(m.map));
                    item.SetNamedValue(L"mapName", JsonValue::CreateStringValue(m.mapName));
                    item.SetNamedValue(L"mapImage", JsonValue::CreateStringValue(m.mapImage));
                    item.SetNamedValue(L"mapLogo", JsonValue::CreateStringValue(m.mapLogo));
                    item.SetNamedValue(L"totalMatch", inum(m.totalMatch));
                    item.SetNamedValue(L"totalKill", inum(m.totalKill));
                    item.SetNamedValue(L"deathNum", inum(m.deathNum));
                    item.SetNamedValue(L"totalAdr", dbl(m.totalAdr));
                    item.SetNamedValue(L"ratingSum", dbl(m.ratingSum));
                    item.SetNamedValue(L"winCount", inum(m.winCount));
                    item.SetNamedValue(L"matchMvpNum", inum(m.matchMvpNum));
                    item.SetNamedValue(L"threeKillNum", inum(m.threeKillNum));
                    item.SetNamedValue(L"fourKillNum", inum(m.fourKillNum));
                    maps.Append(item);
                }

                JsonObject root;
                root.SetNamedValue(L"profile", p);
                root.SetNamedValue(L"weapons", weapons);
                root.SetNamedValue(L"maps", maps);
                return std::wstring(root.Stringify().c_str());
            },
            outJson);
    }
    PerfectStatus __stdcall PerfectSearchUser(
        const wchar_t* keyword,
        const wchar_t* accessToken,
        wchar_t** outJson)
    {
        if (!keyword || !*keyword)
        {
            SetLastError(L"keyword 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!accessToken || !*accessToken)
        {
            SetLastError(L"accessToken 不能为空");
            return PerfectStatus_InvalidArgument;
        }

        const std::wstring word = keyword;
        Perfect::PerfectMatchCredentials credentials;
        credentials.accessToken = accessToken;

        return RunAndSerialize(
            [&] {
                Perfect::PerfectMatchClient client;
                std::vector<Perfect::PerfectSearchHit> hits;
                client.SearchUserAsync(word, credentials, hits).get();

                using namespace winrt::Windows::Data::Json;
                JsonObject root;
                JsonArray list;
                for (auto const& hit : hits)
                {
                    JsonObject item;
                    item.SetNamedValue(L"steamId", JsonValue::CreateStringValue(hit.steamId));
                    item.SetNamedValue(L"pvpNickName", JsonValue::CreateStringValue(hit.pvpNickName));
                    item.SetNamedValue(L"pvpAvatar", JsonValue::CreateStringValue(hit.pvpAvatar));
                    item.SetNamedValue(L"appNickName", JsonValue::CreateStringValue(hit.appNickName));
                    list.Append(item);
                }
                root.SetNamedValue(L"list", list);
                return std::wstring(root.Stringify().c_str());
            },
            outJson);
    }
    PerfectStatus __stdcall PerfectDecryptResponse(
        const wchar_t* responseBody, wchar_t** outJson)
    {
        if (!responseBody || !*responseBody)
        {
            SetLastError(L"responseBody 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        if (!outJson)
        {
            SetLastError(L"outJson 不能为空");
            return PerfectStatus_InvalidArgument;
        }
        *outJson = nullptr;
        ClearLastError();

        try
        {
            EnsureApartment();
            const auto result = Perfect::DecryptResponse(responseBody);
            if (!result.ok)
            {
                SetLastError(result.message);
                return MapDecryptError(result.error);
            }
            *outJson = AllocateString(result.json);
            return *outJson ? PerfectStatus_Ok : PerfectStatus_Internal;
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return PerfectStatus_Internal;
        }
    }

    void __stdcall PerfectFreeString(wchar_t* text)
    {
        delete[] text;
    }

    wchar_t const* __stdcall PerfectGetLastError(void)
    {
        return LastErrorBuffer().c_str();
    }

    wchar_t const* __stdcall PerfectGetVersion(void)
    {
        return kVersion;
    }
}
