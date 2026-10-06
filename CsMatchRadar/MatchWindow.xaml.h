#pragma once

#include "MatchWindow.g.h"

#include "FiveEModels.h"
#include "PerfectMatchModels.h"
#include "FiveEApi.hpp"
#include "PerfectApi.hpp"

#include <winrt/Windows.Foundation.h>

#include <memory>
#include <string>
#include <vector>

namespace winrt::App1::implementation
{
    // 对局窗口。
    //
    // 由主窗口通过 Init(...) 传入要查的玩家与凭据后创建：
    //   - platform: 0=5E，1=完美
    //   - 5E 需要 uuid + 5E ID；完美需要 SteamID + token
    struct MatchWindow : MatchWindowT<MatchWindow>
    {
        MatchWindow();
        ~MatchWindow();

        // IDL 声明的静态工厂，供 C++ 侧创建实例。
        static winrt::App1::MatchWindow Create();

        // 由主窗口在 Activate 前调用，传入查询参数。
        void Init(int32_t platform, std::wstring playerId, std::wstring displayName,
                  std::wstring token, std::wstring selfSteamId);

        // 界面事件。
        void OnMatchSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnPerfectMatchSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnSourceChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnRefreshClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCopyDemoClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);


        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        winrt::Windows::Foundation::IAsyncAction ShowFiveEDetailAsync(size_t index);
        winrt::Windows::Foundation::IAsyncAction ShowPerfectDetailAsync(std::wstring matchId);

        void SetStatus(winrt::hstring const& text);
        void SetBusy(bool busy);
        void ClearDetail();
        std::wstring SelectedCount();
        int32_t SelectedDataSource();

        // ---- 查询参数 ----
        int32_t m_platform{ 0 };          // 0=5E, 1=完美
        std::wstring m_playerId;          // 5E: uuid；完美: SteamID64
        std::wstring m_displayName;       // 标题显示用
        std::wstring m_token;             // 完美 token（5E 不需要）
        std::wstring m_selfSteamId;

        // ---- 数据 ----
        std::unique_ptr<FiveE::Client> m_client5e;
        std::unique_ptr<Perfect::Client> m_clientPerfect;
        std::vector<FiveE::MatchDetail> m_details5e;
        std::vector<std::wstring> m_matchIdsPerfect;
        std::wstring m_currentMatchId;    // 完美：当前详情对应的 matchId
        int32_t m_dataSource{ 3 };
        bool m_loading{ false };
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}

namespace winrt::App1::factory_implementation
{
    struct MatchWindow : MatchWindowT<MatchWindow, implementation::MatchWindow>
    {
    };
}
