#pragma once

#include "MainWindow.g.h"

// 保存已拉取的详情需要完整的 FiveE 模型定义。
#include "FiveEModels.h"
// 完美平台玩家模型（ShowPerfectResult 需要完整定义）。
#include "PerfectModels.h"
// DLL 轻封装（header-only）提供 FiveE::Client / Perfect::Client。
#include "FiveEApi.hpp"
#include "PerfectApi.hpp"

#include <winrt/Windows.Foundation.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace winrt::App1::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        // 界面事件处理。XAML 生成代码需要访问，因此必须是 public。
        winrt::fire_and_forget OnSearchClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        void OnMatchSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

        void OnUserIdKeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        void OnCopyDemoClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // 导航切换：5E / 完美两个页面。
        void OnNavSelectionChanged(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

        // 完美世界页面。
        void OnPerfectKeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        winrt::fire_and_forget OnPerfectSearchClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // 扫码登录：申请二维码并启动轮询。
        winrt::fire_and_forget OnPerfectQrClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // 清除已保存的登录凭据。
        void OnPerfectLogoutClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // ---- 5E 页：搜索玩家 -> 人物卡片 -> 对局窗口 ----

        winrt::fire_and_forget OnFiveESearchClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFiveESearchKeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);

        // 标题栏统一搜索：按当前选中的页面路由到 5E / 完美搜索。
        void OnHeaderSearchKeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnHeaderSearchClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHeaderPaneClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHeaderPlatformChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        winrt::fire_and_forget FillFiveECardAsync(std::wstring const& uuid,
                                                  std::wstring const& displayName,
                                                  std::wstring const& idLine);
        void ShowFiveECardMessage(std::wstring const& text);
        void OpenMatchWindowFiveE(std::wstring const& uuid, std::wstring const& name,
                                  std::wstring const& idLine);

        // ---- 完美页：搜索玩家 -> 人物卡片 -> 对局窗口 ----

        void OnPerfectSearchKeyDown(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void ShowPerfectSearchResults(
            std::vector<std::wstring> const& steamIds,
            std::vector<std::wstring> const& names,
            std::vector<std::wstring> const& appNames);
        winrt::fire_and_forget FillPerfectCardAsync(std::wstring const& steamId,
                                                   std::wstring const& name);
        void OpenMatchWindowPerfect(std::wstring const& steamId, std::wstring const& name,
                                    std::wstring const& token,
                                    std::wstring const& selfSteamId);
        void SetStatus(winrt::hstring const& text);

    private:
        // ---- 扫码登录辅助 ----

        // 初始化二维码 WebView（只做一次）：加载本地页面并等其就绪。
        winrt::Windows::Foundation::IAsyncAction EnsureQrViewReadyAsync();

        // 申请新二维码并显示，随后开始轮询。
        winrt::Windows::Foundation::IAsyncAction StartQrLoginAsync();

        // 单轮轮询；返回 true 表示应继续轮询。
        // generation 用于识别本轮扫码是否已被取消 / 取代。
        winrt::Windows::Foundation::IAsyncOperation<bool> PollQrOnceAsync(uint64_t generation);

        // 停止轮询并折叠二维码区域。
        void StopQrLogin(hstring const& statusText);

        // 把凭据回填到输入框并更新界面状态。
        void ApplyCredentials(std::wstring const& token, std::wstring const& steamId);

        // UI 线程的 DispatcherQueue（缓存，避免每处 co_await 重复构造，
        // 同时避免误用 Windows::System::DispatcherQueue 类型）。
        Microsoft::UI::Dispatching::DispatcherQueue UiDispatcher();

        // ---- 凭据持久化（DPAPI） ----

        // 保存 / 载入 / 清除本机加密凭据。
        void SaveCredentials(std::wstring const& token, std::wstring const& steamId);
        bool LoadCredentials(std::wstring& token, std::wstring& steamId);
        void ClearCredentials();

        // 启动时尝试用已保存凭据自动登录。
        winrt::fire_and_forget TryAutoLoginAsync();

        // 把一场比赛渲染到详情面板。
        void ShowMatchDetail(size_t index);
        void ClearDetail(hstring const& hint);

        // 状态栏辅助。
        void SetBusy(bool busy);

        // 5E 客户端。首次查询时创建。
        std::unique_ptr<FiveE::Client> m_client;
        std::wstring m_currentUuid;
        std::wstring m_pendingUserId;
        bool m_loading{ false };

        // 5E 页当前查看的玩家（uuid）。
        std::wstring m_currentFiveEUuid;

        // 打开的对局窗口。必须持有，否则窗口对象会被立即回收。
        std::vector<winrt::App1::MatchWindow> m_matchWindows;

        // 完美世界客户端。首次查询时创建。
        std::unique_ptr<Perfect::Client> m_perfectClient;
        bool m_perfectLoading{ false };

        // ---- 最近对局 ----
        // 当前列表对应的比赛 ID（与 PerfectMatchList 的项一一对应）。
        std::vector<std::wstring> m_perfectMatchIds;
        // 当前列表使用的数据来源，查详情时必须与之一致。
        int32_t m_perfectMatchSource{ 3 };
        bool m_perfectMatchLoading{ false };

        // ---- 扫码登录状态 ----
        // 二维码 WebView 是否已初始化完成。
        bool m_qrViewReady{ false };
        // 是否正在轮询（防止重复启动）。
        bool m_qrPolling{ false };
        // 本次二维码的轮询凭据。
        std::wstring m_qrPollToken;
        // 二维码申请时刻，用于判断 5 分钟过期。
        std::chrono::steady_clock::time_point m_qrStartedAt{};
        // 二维码是否已就绪（用于 JS 调用时机判断）。
        bool m_qrRendered{ false };
        // 缓存的 UI DispatcherQueue。
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // 自动登录只执行一次（挂在 Activated 事件上）。
        bool m_autoLoginStarted{ false };
        winrt::event_token m_activatedToken{};
        // 扫码流程代号：取消 / 重新扫码时自增，使旧协程的结果作废。
        uint64_t m_qrGeneration{ 0 };
    };
}

namespace winrt::App1::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
