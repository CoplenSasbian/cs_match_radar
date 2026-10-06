#pragma once

// ============================================================================
// PerfectWorldApi C++ 轻封装（header-only）
// ============================================================================
//
// 与 FiveEApi.hpp 同样定位：RAII + JSON 还原 + 统一异常语义。
// 不重复实现业务逻辑。
//
// 使用前请确保 PerfectWorldApi.dll 可被加载。

#include "PerfectWorldApi.h"
#include "PerfectModels.h"

// 本头文件直接使用 winrt::Windows::Data::Json 类型。
// 必须显式包含：此前它依赖调用方先包含本头，属于隐式依赖，
// 一旦调用方的 include 顺序变化就会编译失败。
#include <winrt/Windows.Data.Json.h>

#include <memory>
#include <string>
#include <vector>

namespace Perfect
{
    // 登录凭据（公开结构，由调用方提供）。
    // 与 DLL 内部的 Perfect::PerfectCredentials 布局一致，
    // 但这里独立定义，避免把内部头文件暴露给调用方。
    struct PerfectCredentials
    {
        std::wstring accessToken;   // 登录 token
        std::wstring selfSteamId;   // 登录账号自身的 SteamID64
    };

    // 与 DLL 内部一致的错误类型。
    class ClientError
    {
    public:
        ClientError(PerfectStatus status, std::wstring message)
            : m_status(status), m_message(std::move(message))
        {
        }

        PerfectStatus status() const noexcept { return m_status; }
        const std::wstring& message() const noexcept { return m_message; }

        // 登录态失效时调用方应引导用户重新登录。
        bool IsAuthExpired() const noexcept
        {
            return m_status == PerfectStatus_AuthExpired;
        }

    private:
        PerfectStatus m_status;
        std::wstring m_message;
    };

    namespace Detail
    {
        inline void ThrowIfFailed(PerfectStatus status, wchar_t const* context)
        {
            if (status == PerfectStatus_Ok)
            {
                return;
            }
            std::wstring message = PerfectGetLastError();
            if (message.empty())
            {
                message = L"未知错误";
            }
            if (context && *context)
            {
                message = std::wstring(context) + L"：" + message;
            }
            throw ClientError(status, std::move(message));
        }

        class ScopedString
        {
        public:
            ScopedString() = default;
            ~ScopedString() { if (m_value) { PerfectFreeString(m_value); } }

            ScopedString(ScopedString const&) = delete;
            ScopedString& operator=(ScopedString const&) = delete;

            wchar_t** Receive() { return &m_value; }
            std::wstring Take()
            {
                std::wstring result = m_value ? m_value : L"";
                if (m_value)
                {
                    PerfectFreeString(m_value);
                    m_value = nullptr;
                }
                return result;
            }

        private:
            wchar_t* m_value{ nullptr };
        };

        // 把 C 接口返回的 JSON 还原成模型。
        // 只覆盖调用方会用到的字段；DLL 内部有完整解析器，
        // 这一层是为了让 C++ 调用方拿到强类型而不是裸 JSON。
        inline PerfectPlayer ParsePlayerJson(std::wstring const& text)
        {
            using namespace winrt::Windows::Data::Json;

            PerfectPlayer player;
            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(text, root) || !root)
            {
                return player;
            }

            auto getObject = [&](wchar_t const* key) -> JsonObject {
                if (!root.HasKey(key)) { return nullptr; }
                const auto value = root.GetNamedValue(key);
                if (value.ValueType() != JsonValueType::Object) { return nullptr; }
                return value.GetObject();
            };
            auto getString = [](JsonObject const& o, wchar_t const* key) -> std::wstring {
                if (!o || !o.HasKey(key)) { return L""; }
                const auto value = o.GetNamedValue(key);
                if (value.ValueType() != JsonValueType::String) { return L""; }
                return std::wstring(value.GetString());
            };
            auto getInt = [](JsonObject const& o, wchar_t const* key) -> std::optional<int32_t> {
                if (!o || !o.HasKey(key)) { return std::nullopt; }
                const auto value = o.GetNamedValue(key);
                if (value.ValueType() != JsonValueType::Number) { return std::nullopt; }
                return static_cast<int32_t>(value.GetNumber());
            };
            auto getDouble = [](JsonObject const& o, wchar_t const* key) -> std::optional<double> {
                if (!o || !o.HasKey(key)) { return std::nullopt; }
                const auto value = o.GetNamedValue(key);
                if (value.ValueType() != JsonValueType::Number) { return std::nullopt; }
                return value.GetNumber();
            };

            if (const auto o = getObject(L"identity"))
            {
                player.identity.steamId = getString(o, L"steamId");
                player.identity.uid = getString(o, L"uid");
                player.identity.name = getString(o, L"name");
                player.identity.nickname = getString(o, L"nickname");
                player.identity.avatar = getString(o, L"avatar");
                player.identity.zqId = getString(o, L"zqId");
                player.identity.perfectPower = getInt(o, L"perfectPower");
            }

            auto readRank = [&](wchar_t const* key, RankScore& out) {
                if (const auto o = getObject(key))
                {
                    out.score = getInt(o, L"score");
                    out.stars = getInt(o, L"stars");
                    out.season = getString(o, L"season");
                    out.seasonShowName = getString(o, L"seasonShowName");
                }
            };
            readRank(L"currentRank", player.currentRank);
            readRank(L"peakRank", player.peakRank);

            if (const auto o = getObject(L"ladder"))
            {
                player.ladder.score = getInt(o, L"score");
                player.ladder.matchCount = getInt(o, L"matchCount");
                player.ladder.winNum = getInt(o, L"winNum");
                player.ladder.winRate = getDouble(o, L"winRate");
                player.ladder.roundCount = getInt(o, L"roundCount");
                player.ladder.ratingAvg = getDouble(o, L"ratingAvg");
                player.ladder.adpr = getDouble(o, L"adpr");
                player.ladder.rwsAvg = getDouble(o, L"rwsAvg");
                player.ladder.killNum = getInt(o, L"killNum");
                player.ladder.deathNum = getInt(o, L"deathNum");
                player.ladder.assistNum = getInt(o, L"assistNum");
                player.ladder.kastTotal = getDouble(o, L"kastTotal");
                player.ladder.hsKillRate = getDouble(o, L"hsKillRate");
                player.ladder.firstKillNum = getInt(o, L"firstKillNum");
                player.ladder.firstKillWinRound = getInt(o, L"firstKillWinRound");
                player.ladder.mvpNum = getInt(o, L"mvpNum");
                player.ladder.matchMvpNum = getInt(o, L"matchMvpNum");
                player.ladder.currSStars = getInt(o, L"currSStars");
                player.ladder.c1vxRate = getDouble(o, L"1vxRate");
            }

            player.seasonWe = getDouble(root, L"seasonWe");

            if (root.HasKey(L"partialFailure"))
            {
                const auto value = root.GetNamedValue(L"partialFailure");
                if (value.ValueType() == JsonValueType::Boolean)
                {
                    player.partialFailure = value.GetBoolean();
                }
            }
            player.failureReason = getString(root, L"failureReason");

            return player;
        }
    }

    // ==================== 客户端 ====================

    class Client
    {
    public:
        Client()
        {
            m_handle = PerfectClientCreate();
            if (!m_handle)
            {
                std::wstring message = PerfectGetLastError();
                throw ClientError(PerfectStatus_Internal,
                    message.empty() ? L"创建客户端失败" : message);
            }
        }

        ~Client() { if (m_handle) { PerfectClientDestroy(m_handle); } }

        Client(Client const&) = delete;
        Client& operator=(Client const&) = delete;

        // 查询玩家完整资料（overview + season-stats）。
        // 注意：season-stats 失败时不会抛异常，而是返回带
        // partialFailure=true 的结果。
        PerfectPlayer GetPlayer(
            std::wstring const& steamId,
            PerfectCredentials const& credentials)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                PerfectGetPlayer(m_handle, steamId.c_str(),
                    credentials.accessToken.c_str(), credentials.selfSteamId.c_str(),
                    out.Receive()),
                L"查询玩家失败");
            return Detail::ParsePlayerJson(out.Take());
        }

        // 只查 overview（轻量）。
        PerfectPlayer GetOverview(
            std::wstring const& steamId,
            PerfectCredentials const& credentials)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                PerfectGetOverview(m_handle, steamId.c_str(),
                    credentials.accessToken.c_str(), credentials.selfSteamId.c_str(),
                    out.Receive()),
                L"查询玩家概览失败");
            return Detail::ParsePlayerJson(out.Take());
        }

        // 赛季号列表。
        std::vector<std::wstring> GetSeasonList(
            std::wstring const& steamId,
            PerfectCredentials const& credentials)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                PerfectGetSeasonList(m_handle, steamId.c_str(),
                    credentials.accessToken.c_str(), credentials.selfSteamId.c_str(),
                    out.Receive()),
                L"查询赛季列表失败");

            std::vector<std::wstring> seasons;
            const std::wstring json = out.Take();
            winrt::Windows::Data::Json::JsonArray array{ nullptr };
            if (winrt::Windows::Data::Json::JsonArray::TryParse(json, array) && array)
            {
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    const auto item = array.GetAt(i);
                    if (item.ValueType() ==
                        winrt::Windows::Data::Json::JsonValueType::String)
                    {
                        seasons.push_back(std::wstring(item.GetString()));
                    }
                }
            }
            return seasons;
        }

        // 离线解密（便于排查）。
        static std::wstring DecryptResponse(std::wstring const& responseBody)
        {
            Detail::ScopedString out;
            Detail::ThrowIfFailed(
                PerfectDecryptResponse(responseBody.c_str(), out.Receive()),
                L"解密失败");
            return out.Take();
        }

    private:
        void* m_handle{ nullptr };
    };
}
