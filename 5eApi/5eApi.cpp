#include "FiveEApiPch.h"
#include "5eApi.h"
#include "FiveEClient.h"
#include "FiveEModels.h"
#include "FiveEPlayerModels.h"
#include "FiveESerialize.h"
#include "FiveEJson.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <new>
#include <string>

// CoGetApartmentType —— 用于探测当前线程的 apartment，
// 避免在已是 STA 的 UI 线程上强行初始化 MTA 而失败。
#include <objbase.h>

using namespace winrt::Windows::Data::Json;

namespace
{
    constexpr wchar_t kVersion[] = L"1.1.0";

    // ---- 模块级状态 ----

    // 最近一次失败描述。C 接口无法抛异常，因此用模块级缓冲。
    std::wstring& LastErrorBuffer()
    {
        static std::wstring buffer;
        return buffer;
    }

    void SetLastError(std::wstring text)
    {
        LastErrorBuffer() = std::move(text);
    }

    void ClearLastError()
    {
        LastErrorBuffer().clear();
    }

    // ---- 辅助 ----

    // 复制字符串到堆上交给调用方（由 FiveEFreeString 释放）。
    wchar_t* AllocateString(std::wstring const& text)
    {
        if (text.empty())
        {
            // 空结果也返回合法指针，避免调用方把 nullptr 当成失败。
            auto* empty = new (std::nothrow) wchar_t[1];
            if (empty)
            {
                empty[0] = L'\0';
            }
            return empty;
        }
        auto* copy = new (std::nothrow) wchar_t[text.size() + 1];
        if (!copy)
        {
            return nullptr;
        }
        std::wmemcpy(copy, text.c_str(), text.size() + 1);
        return copy;
    }

    // 确保 WinRT apartment 已初始化。C 接口可能从任意线程调用。
    //
    // 注意：不能无条件调用 init_apartment(multi_threaded)。
    // WinUI / WPF 等宿主把 UI 线程初始化为 STA，此时再要求 MTA 会直接抛
    // hresult_error(RPC_E_CHANGED_MODE)，导致 FiveEClientCreate 返回 nullptr，
    // 之后所有调用都失败（表现为“加载对局异常”）。
    //
    // 因此先探测当前线程的 apartment 状态：
    //   - 已初始化（STA 或 MTA）：直接复用现有 apartment，不再调用 init_apartment；
    //   - 未初始化：初始化为 MTA（本组件内部不需要 STA 消息泵）。
    // 无论哪种情况，都额外调用 CoIncrementMTAUsage 以确保进程中有 MTA 可用。
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
            // 当前线程尚未进入 apartment：初始化为 MTA。
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        // 其他情况（S_OK 表示已在 STA/MTA，RPC_E_... 等）都表示线程已有
        // apartment，直接复用；强行再初始化会失败并破坏整个客户端。

        initialized = true;
    }

    // 把 ApiError 映射为状态码。
    FiveEStatus MapError(FiveE::ApiError const& error)
    {
        switch (error.kind())
        {
        case FiveE::ApiError::Kind::Network:  return FiveEStatus_Network;
        case FiveE::ApiError::Kind::Http:     return FiveEStatus_Http;
        case FiveE::ApiError::Kind::Parse:    return FiveEStatus_Parse;
        case FiveE::ApiError::Kind::Business: return FiveEStatus_Business;
        default:                              return FiveEStatus_Internal;
        }
    }

    // 把当前异常转成可读文本。
    std::wstring DescribeCurrentException()
    {
        try
        {
            throw;
        }
        catch (FiveE::ApiError const& ex)
        {
            return ex.message();
        }
        catch (winrt::hresult_error const& ex)
        {
            return std::wstring(L"WinRT 调用失败：") + std::wstring(ex.message());
        }
        catch (std::exception const& ex)
        {
            const std::string narrow = ex.what() ? ex.what() : "";
            if (narrow.empty())
            {
                return L"未知错误";
            }
            const int size = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
            if (size <= 1)
            {
                return L"未知错误";
            }
            std::wstring wide(static_cast<size_t>(size - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wide.data(), size);
            return wide;
        }
        catch (...)
        {
            return L"未知错误";
        }
    }

    // 在专用的 MTA 工作线程上执行 func，并等待其完成。
    //
    // 为什么必须这样：本组件内部用 .get() 同步等待 WinRT 异步操作，
    // 而 WinRT 的 HttpClient 完成回调需要在其所属 apartment 上派发。
    // 若在宿主 UI 线程（STA）上直接 .get()，该线程被阻塞、消息泵无法运转，
    // 异步操作永远无法完成 —— 表现为“加载对局”时界面卡死。
    // 因此把实际工作放到一个独立的 MTA 线程执行，既保证有可用 apartment，
    // 又完全不受调用方 STA 是否泵消息的影响。
    //
    // 返回值：func() 的结果；func 抛出的异常会在调用线程上重新抛出。
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

        // 手动创建线程，避免依赖 std::async 的线程池行为。
        HANDLE thread = CreateThread(
            nullptr, 0,
            [](void* raw) -> DWORD {
                auto* ctx = static_cast<Context*>(raw);
                // 工作线程自行初始化 MTA。
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
            throw FiveE::ApiError(FiveE::ApiError::Kind::Network,
                L"创建网络工作线程失败");
        }
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);

        if (context.error)
        {
            std::rethrow_exception(context.error);
        }
        return std::move(*context.value);
    }

    // 统一的“执行 -> 序列化 -> 分配字符串”包装。
    template <typename TFunc>
    FiveEStatus RunAndSerialize(TFunc&& func, wchar_t** outJson)
    {
        if (!outJson)
        {
            SetLastError(L"outJson 不能为空");
            return FiveEStatus_InvalidArgument;
        }
        *outJson = nullptr;
        ClearLastError();
        try
        {
            EnsureApartment();
            // 关键：把阻塞式的网络请求放到 MTA 工作线程，
            // 避免在宿主 STA UI 线程上等待导致死锁。
            const std::wstring json = RunOnMtaThread(func);
            *outJson = AllocateString(json);
            if (!*outJson)
            {
                SetLastError(L"内存分配失败");
                return FiveEStatus_Internal;
            }
            return FiveEStatus_Ok;
        }
        catch (FiveE::ApiError const& ex)
        {
            SetLastError(ex.message());
            return MapError(ex);
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return FiveEStatus_Internal;
        }
    }

    // 从 JSON 查询参数里取字段（缺省时用 fallback）。
    FiveE::MatchListQuery ParseQueryJson(wchar_t const* queryJson)
    {
        FiveE::MatchListQuery query;
        if (!queryJson || !*queryJson)
        {
            return query;
        }

        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(queryJson, root) || !root)
        {
            return query;
        }

        auto getString = [&](wchar_t const* key) -> std::wstring {
            return FiveE::Json::OptionalString(root, key).value_or(L"");
        };
        auto getInt = [&](wchar_t const* key, int32_t fallback) -> int32_t {
            return FiveE::Json::OptionalInt32(root, key).value_or(fallback);
        };

        query.uuid = getString(L"uuid");
        query.page = getInt(L"page", 1);
        query.limit = getInt(L"limit", 30);
        query.matchType = getInt(L"matchType", -1);
        query.csType = getInt(L"csType", 0);

        if (auto startTime = FiveE::Json::OptionalInt64(root, L"startTime"); startTime && *startTime > 0)
        {
            query.startTime = *startTime;
        }
        if (auto endTime = FiveE::Json::OptionalInt64(root, L"endTime"); endTime && *endTime > 0)
        {
            query.endTime = *endTime;
        }
        return query;
    }

    // 客户端句柄的包装类型。
    struct ClientHandle
    {
        FiveE::FiveEClient client;

        explicit ClientHandle(int32_t intervalMs)
            : client(MakeOptions(intervalMs))
        {
        }

    private:
        static FiveE::FiveEClientOptions MakeOptions(int32_t intervalMs)
        {
            FiveE::FiveEClientOptions options;
            if (intervalMs > 0)
            {
                options.minRequestIntervalMs = intervalMs;
            }
            return options;
        }
    };

    ClientHandle* AsHandle(void* client)
    {
        return static_cast<ClientHandle*>(client);
    }
}

extern "C"
{
    void* __stdcall FiveEClientCreate(int32_t minRequestIntervalMs)
    {
        ClearLastError();
        try
        {
            EnsureApartment();
            return new ClientHandle(minRequestIntervalMs);
        }
        catch (...)
        {
            SetLastError(DescribeCurrentException());
            return nullptr;
        }
    }

    void __stdcall FiveEClientDestroy(void* client)
    {
        delete AsHandle(client);
    }

    void __stdcall FiveEClientSetBearerToken(void* client, wchar_t const* token)
    {
        // 客户端选项在构造时固定；保留此接口以便未来扩展。
        (void)client;
        (void)token;
    }

    FiveEStatus __stdcall FiveEGetUuid(
        void* client, wchar_t const* userId, wchar_t** outUuid)
    {
        auto* handle = AsHandle(client);
        if (!handle)
        {
            SetLastError(L"client 句柄为空");
            return FiveEStatus_InvalidArgument;
        }
        if (!userId || !*userId)
        {
            SetLastError(L"userId 不能为空");
            return FiveEStatus_InvalidArgument;
        }

        return RunAndSerialize(
            [&] {
                const std::wstring uuid =
                    std::wstring(handle->client.GetUuidAsync(userId).get().c_str());
                if (uuid.empty())
                {
                    throw FiveE::ApiError(FiveE::ApiError::Kind::Business, L"接口未返回 uuid");
                }
                return uuid;
            },
            outUuid);
    }

    FiveEStatus __stdcall FiveEGetMatchList(
        void* client, wchar_t const* queryJson, wchar_t** outJson)
    {
        auto* handle = AsHandle(client);
        if (!handle)
        {
            SetLastError(L"client 句柄为空");
            return FiveEStatus_InvalidArgument;
        }

        const FiveE::MatchListQuery query = ParseQueryJson(queryJson);
        if (query.uuid.empty())
        {
            SetLastError(L"查询参数缺少 uuid");
            return FiveEStatus_InvalidArgument;
        }

        // maxPages 只出现在 JSON 里，不进入 MatchListQuery。
        int32_t maxPages = 1;
        if (queryJson && *queryJson)
        {
            JsonObject root{ nullptr };
            if (JsonObject::TryParse(queryJson, root) && root)
            {
                maxPages = FiveE::Json::OptionalInt32(root, L"maxPages").value_or(1);
            }
        }

        return RunAndSerialize(
            [&] {
                std::vector<FiveE::MatchSummary> matches;
                if (maxPages > 1)
                {
                    handle->client.GetAllMatchesAsync(query, matches, maxPages).get();
                }
                else
                {
                    handle->client.GetMatchListAsync(query, matches).get();
                }
                if (matches.empty())
                {
                    throw FiveE::ApiError(
                        FiveE::ApiError::Kind::Business, L"该区间内没有对局记录");
                }
                return FiveE::Json::ToJson(matches);
            },
            outJson);
    }

    FiveEStatus __stdcall FiveEGetMatchDetail(
        void* client, wchar_t const* matchId, wchar_t** outJson)
    {
        auto* handle = AsHandle(client);
        if (!handle)
        {
            SetLastError(L"client 句柄为空");
            return FiveEStatus_InvalidArgument;
        }
        if (!matchId || !*matchId)
        {
            SetLastError(L"matchId 不能为空");
            return FiveEStatus_InvalidArgument;
        }

        return RunAndSerialize(
            [&] {
                FiveE::MatchDetail detail;
                handle->client.GetMatchDetailAsync(matchId, detail).get();
                if (detail.matchId.empty() && detail.players.empty())
                {
                    throw FiveE::ApiError(
                        FiveE::ApiError::Kind::Business, L"该场对局没有数据");
                }
                return FiveE::Json::ToJson(detail);
            },
            outJson);
    }

    FiveEStatus __stdcall FiveEGetPlayerProfile(
        void* client, wchar_t const* uuid, wchar_t** outJson)
    {
        auto* handle = AsHandle(client);
        if (!handle)
        {
            SetLastError(L"client 句柄为空");
            return FiveEStatus_InvalidArgument;
        }
        if (!uuid || !*uuid)
        {
            SetLastError(L"uuid 不能为空");
            return FiveEStatus_InvalidArgument;
        }

        return RunAndSerialize(
            [&] {
                FiveE::PlayerHome profile;
                handle->client.GetPlayerProfileAsync(uuid, profile).get();
                return FiveE::Json::ToJson(profile);
            },
            outJson);
    }

    FiveEStatus __stdcall FiveEFetchRaw(
        void* client, wchar_t const* url, wchar_t** outJson)
    {
        auto* handle = AsHandle(client);
        if (!handle)
        {
            SetLastError(L"client 句柄为空");
            return FiveEStatus_InvalidArgument;
        }
        if (!url || !*url)
        {
            SetLastError(L"url 不能为空");
            return FiveEStatus_InvalidArgument;
        }

        return RunAndSerialize(
            [&] {
                return std::wstring(handle->client.FetchRawAsync(url).get().c_str());
            },
            outJson);
    }

    FiveEStatus __stdcall FiveESearchPlayer(const wchar_t* keyword, wchar_t** outJson)
    {
        if (!keyword || !*keyword)
        {
            SetLastError(L"keyword 不能为空");
            return FiveEStatus_InvalidArgument;
        }
        if (!outJson)
        {
            SetLastError(L"outJson 不能为空");
            return FiveEStatus_InvalidArgument;
        }

        const std::wstring word = keyword;
        return RunAndSerialize(
            [&] {
                FiveE::FiveEClient client;
                std::vector<FiveE::SearchHit> hits;
                client.SearchPlayerAsync(word, hits).get();

                winrt::Windows::Data::Json::JsonObject root;
                winrt::Windows::Data::Json::JsonArray list;
                for (auto const& hit : hits)
                {
                    winrt::Windows::Data::Json::JsonObject item;
                    item.SetNamedValue(L"username",
                        winrt::Windows::Data::Json::JsonValue::CreateStringValue(hit.username));
                    item.SetNamedValue(L"domain",
                        winrt::Windows::Data::Json::JsonValue::CreateStringValue(hit.domain));
                    item.SetNamedValue(L"uuid",
                        winrt::Windows::Data::Json::JsonValue::CreateStringValue(hit.uuid));
                    list.Append(item);
                }
                root.SetNamedValue(L"list", list);
                return std::wstring(root.Stringify().c_str());
            },
            outJson);
    }
    void __stdcall FiveEFreeString(wchar_t* text)
    {
        delete[] text;
    }

    wchar_t const* __stdcall FiveEGetLastError(void)
    {
        return LastErrorBuffer().c_str();
    }

    wchar_t const* __stdcall FiveEGetVersion(void)
    {
        return kVersion;
    }
}
