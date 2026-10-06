#pragma once

// StatItem / WeaponItem / MapItem 的生成头文件（由 IDL 生成）。
// 必须在使用对应的 *ItemT 模板基类之前包含。
#include "StatItem.g.h"
#include "WeaponItem.g.h"
#include "MapItem.g.h"

#include "PlayerCard.g.h"

#include <winrt/Windows.Foundation.h>

#include <functional>
#include <string>
#include <vector>

namespace winrt::App1::implementation
{
    // 「标签 - 数值」条目，供卡片里各统计区块共用。
    struct StatItem : StatItemT<StatItem>
    {
        StatItem() = default;

        hstring Label() const { return m_label; }
        hstring Value() const { return m_value; }

        void SetLabel(hstring const& v) { m_label = v; }
        void SetValue(hstring const& v) { m_value = v; }

    private:
        hstring m_label;
        hstring m_value;
    };

    struct WeaponItem : WeaponItemT<WeaponItem>
    {
        WeaponItem() = default;

        hstring NameZh() const { return m_nameZh; }
        hstring Stats() const { return m_stats; }
        hstring Rating() const { return m_rating; }

        void SetNameZh(hstring const& v) { m_nameZh = v; }
        void SetStats(hstring const& v) { m_stats = v; }
        void SetRating(hstring const& v) { m_rating = v; }

    private:
        hstring m_nameZh;
        hstring m_stats;
        hstring m_rating;
    };

    struct MapItem : MapItemT<MapItem>
    {
        MapItem() = default;

        hstring MapName() const { return m_mapName; }
        hstring Stats() const { return m_stats; }
        hstring WinRate() const { return m_winRate; }

        void SetMapName(hstring const& v) { m_mapName = v; }
        void SetStats(hstring const& v) { m_stats = v; }
        void SetWinRate(hstring const& v) { m_winRate = v; }

    private:
        hstring m_mapName;
        hstring m_stats;
        hstring m_winRate;
    };

    // 人物卡片。填充方法全部由调用方（MainWindow）驱动，
    // 卡片本身不关心数据来自哪个平台。
    struct PlayerCard : PlayerCardT<PlayerCard>
    {
        PlayerCard();
        ~PlayerCard();

        // ---- 填充接口（由 MainWindow 调用） ----

        // 身份区。
        void SetIdentity(std::wstring const& nickname, std::wstring const& avatarUrl,
                         std::wstring const& title, std::wstring const& idLine,
                         std::wstring const& rank, std::wstring const& season);

        // 一个统计区块：sectionId 为 1/2/3，items 已格式化好。
        void SetStats(int32_t sectionId, std::wstring const& title,
                      std::vector<std::pair<std::wstring, std::wstring>> const& items);

        // 趋势折线图。points 为按时间正序的数值。
        void SetTrend(std::wstring const& caption,
                      std::vector<double> const& points,
                      std::wstring const& axisLabel);

        void SetWeapons(std::vector<winrt::App1::WeaponItem> const& items);
        void SetMaps(std::vector<winrt::App1::MapItem> const& items);

        // 清空为未加载状态。
        void Reset(std::wstring const& hint);

        // 「查看最近对局」被点击时通知宿主（纯 C++ 回调，不走 IDL 事件）。
        void SetViewMatchesHandler(std::function<void()> const& handler)
        {
            m_viewMatches = handler;
        }

        // XAML Click 处理器必须是 public（生成代码会调用）。
        void OnViewMatchesClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void DrawTrend(std::vector<double> const& points, std::wstring const& axisLabel);

        std::function<void()> m_viewMatches;
    };
}

namespace winrt::App1::factory_implementation
{
    struct PlayerCard : PlayerCardT<PlayerCard, implementation::PlayerCard>
    {
    };

    struct StatItem : StatItemT<StatItem, implementation::StatItem>
    {
    };

    struct WeaponItem : WeaponItemT<WeaponItem, implementation::WeaponItem>
    {
    };

    struct MapItem : MapItemT<MapItem, implementation::MapItem>
    {
    };
}
