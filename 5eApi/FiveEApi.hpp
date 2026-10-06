#pragma once

// ============================================================================
// 5eApi C++ 轻封装（header-only）
// ============================================================================
//
// 这一层只做三件事：
//   1. RAII 管理客户端句柄
//   2. 把 C 接口的 status + JSON 还原成强类型模型
//   3. 提供与调用方一致的异常语义（FiveE::ApiError）
//
// 它**不重复实现业务逻辑**，只是 C 接口的语法糖。
// 使用前请确保 5eApi.dll 可被加载（与应用同目录，或已随包部署）。

#include "5eApi.h"
#include "FiveEModels.h"
#include "FiveESerialize.h"
#include "FiveEPlayerModels.h"

#include <winrt/Windows.Data.Json.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace FiveE
{
    // 注意：错误类型复用 FiveEModels.h 中的 FiveE::ApiError，
    // 不在这里重复定义，避免 ODR 冲突。

    namespace Detail
    {
        // 由 5eApi 实现（在 FiveEApiResolve.h 中声明），把 C 接口的函数指针
        // 绑定到导入库。这里直接调用导入库符号即可。
        inline void ThrowIfFailed(FiveEStatus status, wchar_t const* context)
        {
            if (status == FiveEStatus_Ok)
            {
                return;
            }
            std::wstring message = FiveEGetLastError();
            if (message.empty())
            {
                message = L"未知错误";
            }
            if (context && *context)
            {
                message = std::wstring(context) + L"：" + message;
            }
            throw ApiError(ApiError::Kind::Business, std::move(message));
        }

        // 接收 DLL 分配的字符串，离开作用域时释放。
        class ScopedString
        {
        public:
            ScopedString() = default;
            ~ScopedString() { if (m_value) { FiveEFreeString(m_value); } }

            ScopedString(ScopedString const&) = delete;
            ScopedString& operator=(ScopedString const&) = delete;

            wchar_t** Receive() { return &m_value; }
            std::wstring Take()
            {
                std::wstring result = m_value ? m_value : L"";
                if (m_value)
                {
                    FiveEFreeString(m_value);
                    m_value = nullptr;
                }
                return result;
            }

        private:
            wchar_t* m_value{ nullptr };
        };
    }

    // ==================== 纯函数辅助 ====================
    //
    // 只依赖模型、不访问网络，因此直接放在头文件里，
    // 无需跨 DLL 边界（也就没有 ABI 问题）。

    // 从详情里找出指定玩家（uid）所在队伍。
    // 先查玩家明细，再退回队伍的 uid 列表。
    inline std::optional<TeamGroup> FindPlayerGroup(
        MatchDetail const& detail, std::wstring_view playerUid)
    {
        if (playerUid.empty())
        {
            return std::nullopt;
        }
        for (auto const& player : detail.players)
        {
            if (player.profile.playerId == playerUid)
            {
                return player.group;
            }
        }
        for (auto const& team : detail.teams)
        {
            for (auto const& uid : team.playerUids)
            {
                if (uid == playerUid)
                {
                    return team.group;
                }
            }
        }
        return std::nullopt;
    }

    // ==================== 客户端 ====================

    class Client
    {
    public:
        // minRequestIntervalMs <= 0 时用 DLL 默认值。
        explicit Client(int32_t minRequestIntervalMs = 0)
        {
            m_handle = FiveEClientCreate(minRequestIntervalMs);
            if (!m_handle)
            {
                std::wstring message = FiveEGetLastError();
                throw ApiError(ApiError::Kind::Business,
                    message.empty() ? L"创建客户端失败" : message);
            }
        }

        ~Client() { if (m_handle) { FiveEClientDestroy(m_handle); } }

        Client(Client const&) = delete;
        Client& operator=(Client const&) = delete;

        void* NativeHandle() const noexcept { return m_handle; }

        // ---- 业务接口（阻塞当前线程）----

        std::wstring GetUuid(std::wstring const& userId)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEGetUuid(m_handle, userId.c_str(), out.Receive()),
                L"获取 uuid 失败");
            return out.Take();
        }

        std::vector<MatchSummary> GetMatchList(
            MatchListQuery const& query, int32_t maxPages = 1)
        {
            // 把查询对象序列化成 C 接口约定的 JSON。
            winrt::Windows::Data::Json::JsonObject json;
            json.SetNamedValue(L"uuid",
                winrt::Windows::Data::Json::JsonValue::CreateStringValue(query.uuid));
            json.SetNamedValue(L"page",
                winrt::Windows::Data::Json::JsonValue::CreateNumberValue(query.page));
            json.SetNamedValue(L"limit",
                winrt::Windows::Data::Json::JsonValue::CreateNumberValue(query.limit));
            json.SetNamedValue(L"matchType",
                winrt::Windows::Data::Json::JsonValue::CreateNumberValue(query.matchType));
            json.SetNamedValue(L"csType",
                winrt::Windows::Data::Json::JsonValue::CreateNumberValue(query.csType));
            json.SetNamedValue(L"maxPages",
                winrt::Windows::Data::Json::JsonValue::CreateNumberValue(maxPages));
            if (query.startTime.has_value())
            {
                json.SetNamedValue(L"startTime",
                    winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                        static_cast<double>(*query.startTime)));
            }
            if (query.endTime.has_value())
            {
                json.SetNamedValue(L"endTime",
                    winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                        static_cast<double>(*query.endTime)));
            }

            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEGetMatchList(m_handle, json.Stringify().c_str(), out.Receive()),
                L"获取对局列表失败");
            return Json::MatchListFromJsonText(out.Take());
        }

        MatchDetail GetMatchDetail(std::wstring const& matchId)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEGetMatchDetail(m_handle, matchId.c_str(), out.Receive()),
                L"获取对局详情失败");
            return Json::MatchDetailFromJsonText(out.Take());
        }

        PlayerHome GetPlayerProfile(std::wstring const& uuid)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEGetPlayerProfile(m_handle, uuid.c_str(), out.Receive()),
                L"获取玩家资料失败");
            return Json::PlayerHomeFromJsonText(out.Take());
        }

        // 玩家资料的原始 JSON 文本（人物卡片直接按 JSON 填，避免二次建模）。
        std::wstring GetPlayerProfileText(std::wstring const& uuid)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEGetPlayerProfile(m_handle, uuid.c_str(), out.Receive()),
                L"获取玩家资料失败");
            return out.Take();
        }

        // 按昵称 / 5E ID 搜索玩家。结果已按"完全匹配优先"排好序。
        void SearchPlayer(std::wstring const& keyword, std::vector<SearchHit>& out)
        {
            out.clear();
            Detail::ScopedString outJson;
            Detail::ThrowIfFailed(
                FiveESearchPlayer(keyword.c_str(), outJson.Receive()),
                L"搜索玩家失败");

            // 返回 {"list":[{username,domain,uuid}, ...]}
            winrt::Windows::Data::Json::JsonObject root{ nullptr };
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(outJson.Take(), root) || !root)
            {
                return;
            }
            auto listValue = root.TryLookup(L"list");
            if (!listValue ||
                listValue.ValueType() != winrt::Windows::Data::Json::JsonValueType::Array)
            {
                return;
            }
            for (auto const& element : listValue.GetArray())
            {
                if (!element ||
                    element.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
                {
                    continue;
                }
                auto item = element.GetObject();
                auto pick = [&](wchar_t const* key) {
                    auto v = item.TryLookup(key);
                    return (v && v.ValueType() ==
                        winrt::Windows::Data::Json::JsonValueType::String)
                        ? std::wstring(v.GetString().c_str()) : std::wstring{};
                };
                SearchHit hit;
                hit.username = pick(L"username");
                hit.domain = pick(L"domain");
                hit.uuid = pick(L"uuid");
                out.push_back(std::move(hit));
            }
        }

        std::wstring FetchRaw(std::wstring const& url)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                FiveEFetchRaw(m_handle, url.c_str(), out.Receive()),
                L"请求失败");
            return out.Take();
        }

    private:
        void* m_handle{ nullptr };
    };
}
