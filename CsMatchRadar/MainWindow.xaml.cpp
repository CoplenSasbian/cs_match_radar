#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// MatchRow / PlayerRow 的生成文件由 ViewModels.cpp 负责编译。
#include "ViewModels.h"
#include "Fmt.h"
#include "PlayerCard.xaml.h"
#include "MatchWindow.xaml.h"

// 5eApi / PerfectWorldApi 通过 DLL 引入：
// 公开 C 接口 + header-only 轻封装（不重复实现业务逻辑）。
#include "5eApi.h"
#include "FiveEApi.hpp"
#include "FiveEModels.h"
#include "FiveEJson.h"

#include "PerfectWorldApi.h"
#include "PerfectApi.hpp"
#include "PerfectMatchModels.h"
#include "PerfectModels.h"

#include "CredentialStore.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;

namespace winrt::App1::implementation
{
    // 格式化辅助已提取到 Fmt.h，供 MainWindow 与 MatchWindow 共用。
    using AppFmt::AccentBrush;
    using AppFmt::FormatDouble;
    using AppFmt::FormatDuration;
    using AppFmt::FormatInt;
    using AppFmt::FormatTime;
    using AppFmt::H;
    using AppFmt::Widen;

    // PlayerCard / MatchWindow 在 x:Name 上得到的是 ABI 类型，
    // 业务方法只存在于实现类型上，这里统一转换一次。
    winrt::App1::implementation::PlayerCard* CardOf(winrt::App1::PlayerCard const& c)
    {
        return winrt::get_self<winrt::App1::implementation::PlayerCard>(c);
    }
    winrt::App1::implementation::MatchWindow* MatchWinOf(winrt::App1::MatchWindow const& w)
    {
        return winrt::get_self<winrt::App1::implementation::MatchWindow>(w);
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();

        // No title bar: extend title-bar content into the client area.
        ExtendsContentIntoTitleBar(true);

        Microsoft::UI::Xaml::UIElement content{ this->Content() };
        if (auto titleBar = content.try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (auto found = titleBar.FindName(L"AppTitleBar").try_as<Grid>())
            {
                SetTitleBar(found);
            }
        }

        // 默认选中 5E 页面。
        if (auto nav = MainNav())
        {
            nav.SelectedItem(NavSearch());
        }
        SetStatus(L"输入 5E 用户 ID 后点击“查询对局”。");

        // 在 UI 线程上缓存 DispatcherQueue。
        // 必须在构造期取：后台线程调用 Window::DispatcherQueue() 会拿到
        // 属于后台线程的队列，之后无法用它回到 UI 线程。
        m_dispatcher = DispatcherQueue();

        // 自动登录必须等窗口/DispatcherQueue 就绪后再做：
        // 在构造函数里直接启动协程时，窗口尚未完成初始化，
        // 任何 UI 访问（以及取 DispatcherQueue）都可能触发
        // 未处理异常导致进程崩溃（0xc000027b）。
        // 这里挂到 Activated 事件上，第一次激活时执行一次。
        m_activatedToken = Activated([this](auto&&, WindowActivatedEventArgs const&) {
            if (m_autoLoginStarted)
            {
                return;
            }
            m_autoLoginStarted = true;
            TryAutoLoginAsync();
        });
    }

    MainWindow::~MainWindow() = default;

    int32_t MainWindow::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    void MainWindow::SetStatus(hstring const& text)
    {
        if (auto block = StatusText())
        {
            block.Text(text);
        }
    }

    void MainWindow::SetBusy(bool busy)
    {
        if (auto ring = BusyRing())
        {
            ring.IsActive(busy);
            ring.Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        }
        if (auto box = HeaderSearchBox())
        {
            box.IsEnabled(!busy);
        }
    }

    void MainWindow::OnFiveESearchKeyDown(
        IInspectable const& /* sender */,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            OnFiveESearchClick(nullptr, nullptr);
        }
    }

    void MainWindow::ShowFiveECardMessage(std::wstring const& text)
    {
        CardOf(FiveECard())->Reset(text);
        SetStatus(H(text));
    }

    // ---- 标题栏统一搜索：按当前页面路由到 5E / 完美 ----

    void MainWindow::OnHeaderSearchKeyDown(
        IInspectable const& /* sender */,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            OnHeaderSearchClick(nullptr, nullptr);
        }
    }

    void MainWindow::OnHeaderSearchClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        // 按标题栏的平台选择器决定走 5E 还是完美，与导航页无关。
        const bool perfect = HeaderPlatformBox().SelectedIndex() == 1;
        if (perfect)
        {
            OnPerfectSearchClick(nullptr, nullptr);
        }
        else
        {
            OnFiveESearchClick(nullptr, nullptr);
        }
    }

    void MainWindow::OnHeaderPlatformChanged(
        IInspectable const& /* sender */,
        Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& /* args */)
    {
        const bool perfect = HeaderPlatformBox().SelectedIndex() == 1;
        // 两个平台的页面都保留，按选择器切换可见性。
        if (auto page5e = Page5E()) { page5e.Visibility(perfect ? Visibility::Collapsed : Visibility::Visible); }
        if (auto pagePerfect = PagePerfect()) { pagePerfect.Visibility(perfect ? Visibility::Visible : Visibility::Collapsed); }
        if (auto box = HeaderSearchBox())
        {
            box.PlaceholderText(perfect
                ? L"完美昵称 / 编号 / SteamID"
                : L"5E 名称 / ID");
        }
        SetStatus(perfect
            ? L"完美对局查询：输入 SteamID64 后查询（需先在“设置”里登录）。"
            : L"5E 对局查询：输入 5E 用户 ID 或昵称后查询。");
    }

    void MainWindow::OnHeaderPaneClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        if (auto nav = MainNav())
        {
            nav.IsPaneOpen(!nav.IsPaneOpen());
        }
    }

    void MainWindow::OpenMatchWindowFiveE(std::wstring const& uuid,
                                          std::wstring const& name,
                                          std::wstring const& /* idLine */)
    {
        auto win = winrt::App1::implementation::MatchWindow::Create();
        MatchWinOf(win)->Init(0, uuid, name, L"", L"");
        m_matchWindows.push_back(win);
        win.Activate();
        MatchWinOf(m_matchWindows.back())->LoadAsync();
    }
 

    fire_and_forget MainWindow::OnFiveESearchClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /*args*/)
    {
        if (m_loading) { co_return; }

        std::wstring keyword(HeaderSearchBox().Text().c_str());
        {
            const size_t b = keyword.find_first_not_of(L" \t\r\n");
            const size_t e = keyword.find_last_not_of(L" \t\r\n");
            keyword = (b == std::wstring::npos) ? std::wstring{} : keyword.substr(b, e - b + 1);
        }
        if (keyword.empty())
        {
            ShowFiveECardMessage(L"请输入 5E 名称或 ID。");
            co_return;
        }

        m_loading = true;
        SetBusy(true);
        ShowFiveECardMessage(L"正在搜索玩家…");

        std::wstring failure;
        std::vector<FiveE::SearchHit> hits;
        try
        {
            if (!m_client) { m_client = std::make_unique<FiveE::Client>(); }
            m_client->SearchPlayer(keyword, hits);
        }
        catch (FiveE::ApiError const& ex) { failure = ex.message(); }
        catch (std::exception const& ex) { failure = Widen(ex.what() ? ex.what() : ""); }
        catch (...) { failure = L"未知错误"; }

        SetBusy(false);
        m_loading = false;

        if (!failure.empty())
        {
            ShowFiveECardMessage(L"搜索失败：" + failure);
            co_return;
        }
        if (hits.empty())
        {
            ShowFiveECardMessage(L"未找到匹配「" + keyword + L"」的玩家。");
            co_return;
        }

        if (auto panel = FiveESearchResults())
        {
            panel.Children().Clear();
            for (auto const& hit : hits)
            {
                const std::wstring uuid = hit.uuid;
                const std::wstring nm = hit.username;
                const std::wstring dm = hit.domain;

                auto button = Button();
                button.HorizontalAlignment(HorizontalAlignment::Stretch);
                button.HorizontalContentAlignment(HorizontalAlignment::Left);
                button.Padding(Thickness{ 10, 7, 10, 7 });

                auto sp = StackPanel();
                sp.Spacing(2);
                auto t1 = TextBlock();
                t1.Text(nm.empty() ? dm : nm);
                t1.FontSize(14);
                sp.Children().Append(t1);
                auto t2 = TextBlock();
                t2.Text(dm);
                t2.FontSize(11);
                t2.Opacity(0.7);
                sp.Children().Append(t2);
                button.Content(sp);

                button.Click([this, uuid, nm, dm](auto&&, auto&&) {
                    if (auto popup = FiveESearchPopup()) { popup.IsOpen(false); }
                    FillFiveECardAsync(uuid, nm, dm);
                });
                panel.Children().Append(button);
            }
        }
        if (auto popup = FiveESearchPopup()) { popup.IsOpen(true); }

        FillFiveECardAsync(hits.front().uuid, hits.front().username, hits.front().domain);
        co_return;
    }

    fire_and_forget MainWindow::FillFiveECardAsync(
        std::wstring const& uuid, std::wstring const& displayName,
        std::wstring const& idLine)
    {
        m_currentFiveEUuid = uuid;
        SetStatus(L"正在读取玩家资料…");

        std::wstring failure;
        std::wstring json;
        try
        {
            if (!m_client) { m_client = std::make_unique<FiveE::Client>(); }
            json = m_client->GetPlayerProfileText(uuid);
        }
        catch (FiveE::ApiError const& ex) { failure = ex.message(); }
        catch (std::exception const& ex) { failure = Widen(ex.what() ? ex.what() : ""); }
        catch (...) { failure = L"未知错误"; }

        if (!failure.empty())
        {
            ShowFiveECardMessage(L"读取资料失败：" + failure);
            co_return;
        }

        using namespace winrt::Windows::Data::Json;
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(json, root) || !root)
        {
            ShowFiveECardMessage(L"玩家资料解析失败。");
            co_return;
        }

        auto section = [&](wchar_t const* n) {
            auto v = root.TryLookup(n);
            return (v && v.ValueType() == JsonValueType::Object) ? v.GetObject() : JsonObject{ nullptr };
        };
        auto str = [](JsonObject const& o, wchar_t const* k) {
            auto v = o.TryLookup(k);
            return (v && v.ValueType() == JsonValueType::String)
                ? std::wstring(v.GetString().c_str()) : std::wstring{};
        };
        auto num = [](JsonObject const& o, wchar_t const* k) {
            auto v = o.TryLookup(k);
            return (v && v.ValueType() == JsonValueType::Number) ? v.GetNumber() : 0.0;
        };

        auto identity = section(L"identity");
        auto rank = section(L"rank");
        auto season = section(L"season");
        auto career = section(L"career");

        const std::wstring name = identity ? str(identity, L"username") : L"";
        const std::wstring avatar = identity ? str(identity, L"avatarUrl") : L"";
        const std::wstring showName = displayName.empty() ? name : displayName;
        std::wstring rankText = L"";
        if (rank)
        {
            rankText = std::to_wstring(static_cast<int32_t>(num(rank, L"elo")));
            if (num(rank, L"levelId") > 0)
            {
                rankText += L"　LV" + std::to_wstring(static_cast<int32_t>(num(rank, L"levelId")));
            }
        }
        const std::wstring seasonText = season ? str(season, L"season") : L"";
        CardOf(FiveECard())->SetIdentity(showName, avatar, L"",
            idLine.empty() ? name : idLine, rankText, seasonText);

        std::vector<std::pair<std::wstring, std::wstring>> seasonItems;
        if (season)
        {
            seasonItems.emplace_back(L"场次", std::to_wstring(static_cast<int32_t>(num(season, L"matchTotal"))));
            seasonItems.emplace_back(L"胜率", AppFmt::FormatDouble(num(season, L"winRate") * 100.0, 0) + L"%");
            seasonItems.emplace_back(L"Rating", AppFmt::FormatDouble(num(season, L"avgRating"), 2));
            seasonItems.emplace_back(L"ADR", AppFmt::FormatDouble(num(season, L"avgAdr"), 1));
            seasonItems.emplace_back(L"RWS", AppFmt::FormatDouble(num(season, L"avgRws"), 1));
            seasonItems.emplace_back(L"KAST", AppFmt::FormatDouble(num(season, L"avgKast") * 100.0, 0) + L"%");
            seasonItems.emplace_back(L"爆头", AppFmt::FormatDouble(num(season, L"avgHeadshotRate") * 100.0, 0) + L"%");
        }
        CardOf(FiveECard())->SetStats(1, L"本赛季", seasonItems);

        std::vector<std::pair<std::wstring, std::wstring>> careerItems;
        if (career)
        {
            careerItems.emplace_back(L"总场次", std::to_wstring(static_cast<int32_t>(num(career, L"matchTotal"))));
            careerItems.emplace_back(L"胜", std::to_wstring(static_cast<int32_t>(num(career, L"matchWin"))));
            careerItems.emplace_back(L"负", std::to_wstring(static_cast<int32_t>(num(career, L"matchLoss"))));
            careerItems.emplace_back(L"平", std::to_wstring(static_cast<int32_t>(num(career, L"matchTie"))));
            careerItems.emplace_back(L"生涯 ELO", AppFmt::FormatDouble(num(career, L"elo"), 1));
            careerItems.emplace_back(L"最高赛季", str(career, L"bestSeason"));
        }
        CardOf(FiveECard())->SetStats(2, L"生涯累计", careerItems);

        CardOf(FiveECard())->SetViewMatchesHandler([this, uuid, showName, idLine]() {
            OpenMatchWindowFiveE(uuid, showName, idLine);
        });

        SetStatus(H(L"已找到：" + showName));
        co_return;
    }
    // ================= 导航 =================

    void MainWindow::OnNavSelectionChanged(
        NavigationView const& /* sender */,
        NavigationViewSelectionChangedEventArgs const& args)
    {
        // 用 SelectedItem 的 Tag 判断页面，因为自定义 NavSettings / NavRadar
        // 不是 NavigationView 内置项，args.IsSettingsSelected() 恒为 false。
        std::wstring tag;
        if (auto item = args.SelectedItem().try_as<Microsoft::UI::Xaml::Controls::NavigationViewItem>())
        {
            if (auto tagValue = item.Tag())
            {
                tag = std::wstring(tagValue.as<winrt::hstring>());
            }
        }

        // 默认三项都不显示，按 tag 点亮。
        Page5E().Visibility(Visibility::Collapsed);
        PagePerfect().Visibility(Visibility::Collapsed);
        PageSettings().Visibility(Visibility::Collapsed);
        PageRadar().Visibility(Visibility::Collapsed);
        if (auto area = HeaderSearchArea()) { area.Visibility(Visibility::Collapsed); }

        if (tag == L"settings")
        {
            PageSettings().Visibility(Visibility::Visible);
            SetStatus(L"设置：管理完美平台登录凭据。");
        }
        else if (tag == L"radar")
        {
            PageRadar().Visibility(Visibility::Visible);
            SetStatus(L"对局雷达：点击“重新扫描”启动实时抓取。");
        }
        else
        {
            const bool perfect = HeaderPlatformBox().SelectedIndex() == 1;
            Page5E().Visibility(perfect ? Visibility::Collapsed : Visibility::Visible);
            PagePerfect().Visibility(perfect ? Visibility::Visible : Visibility::Collapsed);
            if (auto area = HeaderSearchArea()) { area.Visibility(Visibility::Visible); }
        }
    }

    // ================= 完美世界页面 =================

    void MainWindow::OnPerfectKeyDown(
        IInspectable const& /* sender */, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            OnPerfectSearchClick(nullptr, nullptr);
        }
    }

    // ==================== 扫码登录 ====================

    namespace
    {
        // 二维码有效期（与 5eApi 侧注释一致：5 分钟）。
        constexpr auto kQrLifetime = std::chrono::minutes(5);

        // 轮询间隔。
        constexpr auto kPollInterval = std::chrono::seconds(2);

        // 从二维码接口返回的 JSON 里取字段。
        std::wstring JsonField(std::wstring const& json, wchar_t const* key)
        {
            winrt::Windows::Data::Json::JsonObject root{ nullptr };
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(json, root) || !root)
            {
                return {};
            }
            auto value = root.TryLookup(key);
            if (!value || value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
            {
                return {};
            }
            return value.GetString().c_str();
        }

        // 把字符串转成可安全内联进 JavaScript 的字面量（含引号）。        // 用 JSON 序列化保证转义正确，避免拼接脚本时被注入。
        std::wstring JsonEscapeForScript(std::wstring const& text)
        {
            winrt::Windows::Data::Json::JsonObject holder;
            holder.SetNamedValue(L"v",
                winrt::Windows::Data::Json::JsonValue::CreateStringValue(text));
            std::wstring json = holder.Stringify().c_str();
            // 去掉外层 {"v": 与 }，只保留带引号的字符串字面量。
            const std::wstring prefix = L"{\"v\":";
            if (json.rfind(prefix, 0) == 0 && json.back() == L'}')
            {
                return json.substr(prefix.size(), json.size() - prefix.size() - 1);
            }
            return L"\"\"";
        }
    }

    // 取 UI 线程的 DispatcherQueue（构造时已缓存）。
    //
    // 注意：WinUI3 的 Window::DispatcherQueue() 返回
    // Microsoft::UI::Dispatching::DispatcherQueue，而不是
    // Windows::System::DispatcherQueue —— 两者不可混用。
    //
    // 关键：必须在 UI 线程上取一次并缓存。若在后台线程调用
    // Window::DispatcherQueue()，得到的队列属于那个后台线程，
    // 再用它回切 UI 线程会抛 RPC_E_WRONG_THREAD
    // （“应用程序调用一个已为另一线程整理的接口”）。
    Microsoft::UI::Dispatching::DispatcherQueue MainWindow::UiDispatcher()
    {
        return m_dispatcher;
    }

    IAsyncAction MainWindow::EnsureQrViewReadyAsync()
    {
        if (m_qrViewReady)
        {
            co_return;
        }

        // WebView2 初始化前必须先让控件进入可视树。
        // 该控件所在面板初始为 Collapsed；对未加载的控件调用
        // EnsureCoreWebView2Async 会失败（表现为进程直接退出）。
        PerfectQrPanel().Visibility(Visibility::Visible);

        // 显式指定用户数据目录到应用可写位置。
        // MSIX 打包应用里 WebView2 的默认目录可能不可写，
        // 这是初始化失败的常见原因。
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2Environment environment{ nullptr };
        try
        {
            const std::wstring localFolder =
                winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
            const std::wstring userDataFolder = localFolder + L"\\WebView2";
            environment = co_await winrt::Microsoft::Web::WebView2::Core::CoreWebView2Environment::
                CreateWithOptionsAsync(
                    winrt::hstring{},                               // 使用系统安装的 WebView2 Runtime
                    winrt::hstring(userDataFolder),                 // 应用可写的用户数据目录
                    winrt::Microsoft::Web::WebView2::Core::CoreWebView2EnvironmentOptions{});
        }
        catch (...)
        {
            environment = nullptr;
        }

        if (environment)
        {
            co_await PerfectQrView().EnsureCoreWebView2Async(environment);
        }
        else
        {
            co_await PerfectQrView().EnsureCoreWebView2Async();
        }

        auto settings = PerfectQrView().CoreWebView2().Settings();
        // 页面是纯本地渲染，禁用无关能力。
        settings.IsScriptEnabled(true);
        settings.AreDefaultContextMenusEnabled(false);
        settings.IsStatusBarEnabled(false);
        settings.AreDevToolsEnabled(false);

        // 定位 qr_page.html。
        //
        // unpackaged 模式下没有包身份，Package::Current() 不可用，
        // 因此直接取 exe 所在目录（资源就躺在旁边）。
        const std::wstring installDir = [] {
            // 优先：exe 所在目录（GetModuleFileName 反映实际加载的模块路径，
            // 无论从 VS、脚本还是双击启动都正确）。
            wchar_t buffer[MAX_PATH]{};
            const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                std::wstring path(buffer, len);
                const size_t slash = path.find_last_of(L"\\/");
                if (slash != std::wstring::npos)
                {
                    return path.substr(0, slash);
                }
            }
            // 回退：仍是打包模式时用包安装目录。
            try
            {
                return std::wstring(
                    winrt::Windows::ApplicationModel::Package::Current()
                        .InstalledLocation().Path().c_str());
            }
            catch (...)
            {
                return std::wstring{};
            }
        }();

        if (installDir.empty())
        {
            co_return;
        }

        const std::wstring pagePath = installDir + L"\\Assets\\qr_page.html";
        PerfectQrView().CoreWebView2().Navigate(winrt::hstring(L"file:///" + pagePath));

        // 等导航完成，确保 renderQr 已就绪。
        m_qrViewReady = true;
        co_return;
    }

    void MainWindow::ApplyCredentials(
        std::wstring const& token, std::wstring const& steamId)
    {
        PerfectTokenBox().Text(winrt::hstring(token));
        PerfectSelfSteamBox().Text(winrt::hstring(steamId));
    }

    void MainWindow::SaveCredentials(std::wstring const& token, std::wstring const& steamId)
    {
        AppCred::Save(token, steamId);
    }

    bool MainWindow::LoadCredentials(std::wstring& token, std::wstring& steamId)
    {
        AppCred::StoredCredentials stored;
        if (!AppCred::Load(stored))
        {
            return false;
        }
        token = stored.accessToken;
        steamId = stored.selfSteamId;
        return true;
    }

    void MainWindow::ClearCredentials()
    {
        AppCred::Clear();
    }

    fire_and_forget MainWindow::TryAutoLoginAsync()
    {
        // fire_and_forget 的异常会终止进程，这里整体兜住。
        try
        {
            std::wstring token;
            std::wstring steamId;
            if (!LoadCredentials(token, steamId))
            {
                co_return;
            }

            // 先把凭据回填，用户即使不校验也能直接查询。
            ApplyCredentials(token, steamId);
            PerfectAuthStatus().Text(L"已载入本机保存的登录凭据，正在校验…");

            bool valid = false;
            bool failed = false;
            try
            {
                const auto result = PerfectValidateCredentials(
                    token.c_str(), steamId.c_str(), reinterpret_cast<int32_t*>(&valid));
                (void)result;
            }
            catch (...)
            {
                failed = true;
            }

            if (failed)
            {
                // 校验失败（多半是网络问题）：保留凭据，不做判定。
                PerfectAuthStatus().Text(L"已载入本机保存的凭据（未能校验，稍后可重试）。");
                PerfectLogoutButton().Visibility(Visibility::Visible);
                co_return;
            }

            if (valid)
            {
                PerfectAuthStatus().Text(L"已自动登录（使用本机保存的凭据）。");
                PerfectLogoutButton().Visibility(Visibility::Visible);
            }
            else
            {
                // 凭据失效：清掉，避免每次启动都做无效请求。
                ClearCredentials();
                PerfectAuthStatus().Text(L"本机保存的凭据已失效，请重新扫码登录。");
                PerfectTokenBox().Text(L"");
                PerfectSelfSteamBox().Text(L"");
            }
        }
        catch (...)
        {
            // 自动登录属于可选优化，任何失败都不应影响应用启动。
        }
        co_return;
    }

    fire_and_forget MainWindow::OnPerfectQrClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        if (m_qrPolling)
        {
            // 已在轮询：再次点击视为取消。
            StopQrLogin(L"已取消扫码登录。");
            co_return;
        }

        // fire_and_forget 里抛出的异常无人接管，会直接终止进程
        // （表现为 0xC000027B，且 XAML 的 UnhandledException 不会触发）。
        // 因此这里兜住所有异常并显示到界面上。
        try
        {
            co_await StartQrLoginAsync();
        }
        catch (hresult_error const& ex)
        {
            // 带上 HRESULT，便于判断是哪个调用失败
            // （例如 0x8001010E = RPC_E_WRONG_THREAD）。
            wchar_t buffer[64]{};
            swprintf_s(buffer, L"（HRESULT 0x%08X）",
                static_cast<unsigned>(static_cast<int32_t>(ex.code())));
            StopQrLogin(H(L"扫码登录失败：" + std::wstring(ex.message().c_str()) + buffer));
        }
        catch (std::exception const& ex)
        {
            StopQrLogin(H(L"扫码登录失败：" + Widen(ex.what() ? ex.what() : "")));
        }
        catch (...)
        {
            StopQrLogin(L"扫码登录失败：未知错误。");
        }
    }

    void MainWindow::OnPerfectLogoutClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /* args */)
    {
        ClearCredentials();
        PerfectTokenBox().Text(L"");
        PerfectSelfSteamBox().Text(L"");
        PerfectLogoutButton().Visibility(Visibility::Collapsed);
        PerfectAuthStatus().Text(L"已退出登录，本机保存的凭据已清除。");
    }

    void MainWindow::StopQrLogin(hstring const& statusText)
    {
        // 递增代号：让仍在等待中的旧协程失效，不再改界面。
        ++m_qrGeneration;
        m_qrPolling = false;
        PerfectQrPanel().Visibility(Visibility::Collapsed);
        PerfectQrButton().Content(box_value(winrt::hstring(L"扫码登录")));
        if (statusText.size() > 0)
        {
            PerfectAuthStatus().Text(statusText);
        }
    }

    IAsyncAction MainWindow::StartQrLoginAsync()
    {
        // 本次扫码流程的代号。用户中途取消再重新扫码时，旧协程可能
        // 仍在等待网络返回；它醒来后不能再去改界面状态，否则会把
        // 新一轮的状态覆盖成旧结果（现象：状态在成功与失败之间来回跳）。
        const uint64_t generation = ++m_qrGeneration;

        PerfectAuthStatus().Text(L"正在申请二维码…");
        PerfectQrButton().IsEnabled(false);

        // 申请二维码。
        std::wstring accessUrl;
        std::wstring pollToken;
        // 失败信息（在 try/catch 内记录，回到 UI 线程后再展示）。
        // 注意：C++ 不允许在 catch 块里 co_await，因此这里只记录、不 await。
        std::wstring failure;
        try
        {
            // 直接在当前（UI）线程调用即可：
            // PerfectApplyQrLogin 内部已经把阻塞的等待放到独立的 MTA
            // 工作线程执行（见 PerfectWorldApi 的 RunOnMtaThread），
            // 既不会卡住 UI，也不会把 UI 对象带到别的 apartment 上。
            // 之前这里先 resume_background()，反而使后续的 WebView2 /
            // XAML 调用跨了线程，抛 RPC_E_WRONG_THREAD。
            wchar_t* raw = nullptr;
            const auto status = PerfectApplyQrLogin(&raw);
            if (status != PerfectStatus_Ok || !raw)
            {
                const std::wstring message = PerfectGetLastError();
                failure = L"申请二维码失败：" +
                    (message.empty() ? std::wstring(L"未知错误") : message);
            }
            else
            {
                const std::wstring json = raw;
                PerfectFreeString(raw);
                accessUrl = JsonField(json, L"accessUrl");
                pollToken = JsonField(json, L"pollToken");
            }
        }
        catch (hresult_error const& ex)
        {
            failure = L"申请二维码失败：" + std::wstring(ex.message().c_str());
        }
        catch (std::exception const& ex)
        {
            failure = L"申请二维码失败：" + Widen(ex.what() ? ex.what() : "");
        }

        // 全程留在 UI 线程（接口内部的阻塞等待由 DLL 自行放到 MTA），
        // 因此这里无需再切回前台。
        //
        // 若期间用户已取消或重新发起，本轮结果作废，不再改界面。
        if (generation != m_qrGeneration)
        {
            co_return;
        }

        if (!failure.empty())
        {
            PerfectQrButton().IsEnabled(true);
            PerfectAuthStatus().Text(H(failure));
            co_return;
        }

        if (accessUrl.empty() || pollToken.empty())
        {
            PerfectQrButton().IsEnabled(true);
            PerfectAuthStatus().Text(L"申请二维码失败：响应缺少必要字段。");
            co_return;
        }

        m_qrPollToken = pollToken;
        m_qrStartedAt = std::chrono::steady_clock::now();
        m_qrPolling = true;

        PerfectQrPanel().Visibility(Visibility::Visible);
        PerfectQrButton().Content(box_value(winrt::hstring(L"取消扫码")));
        PerfectQrButton().IsEnabled(true);
        PerfectAuthStatus().Text(L"请用完美世界 App 扫描二维码。");

        // 确保 WebView 就绪后把 URL 交给页面渲染。
        try
        {
            co_await EnsureQrViewReadyAsync();
            // 页面刚导航完成，脚本可能还没执行完；调用失败时重试几次。
            bool rendered = false;
            for (int attempt = 0; attempt < 20 && !rendered; ++attempt)
            {
                auto script = L"window.renderQr(" + JsonEscapeForScript(accessUrl) + L")";
                auto result = co_await PerfectQrView().ExecuteScriptAsync(winrt::hstring(script));
                const std::wstring text = result.c_str();
                if (text.find(L"\"ok\"") != std::wstring::npos)
                {
                    rendered = true;
                    break;
                }
                // resume_after 在线程池线程恢复，必须切回 UI 线程
                // 才能再次调用 WebView2（否则 RPC_E_WRONG_THREAD）。
                co_await winrt::resume_after(std::chrono::milliseconds(150));
                co_await wil::resume_foreground(UiDispatcher());
            }
            if (!rendered)
            {
                PerfectQrHint().Text(L"二维码渲染失败，请重试。");
            }
            else
            {
                PerfectQrHint().Text(L"请用完美世界 App 扫描二维码（5 分钟内有效）");
            }
        }
        catch (hresult_error const& ex)
        {
            PerfectQrHint().Text(H(L"二维码显示失败：" + std::wstring(ex.message().c_str())));
        }
        catch (std::exception const& ex)
        {
            PerfectQrHint().Text(H(L"二维码显示失败：" + Widen(ex.what() ? ex.what() : "")));
        }
        catch (...)
        {
            PerfectQrHint().Text(L"二维码显示失败：未知错误。");
        }

        // 轮询直到确认、过期或取消。
        while (m_qrPolling && generation == m_qrGeneration)
        {
            co_await winrt::resume_after(kPollInterval);

            // resume_after 会在【线程池线程】上恢复，而不是 UI 线程。
            // 之后任何 XAML / WebView2 访问都会抛 RPC_E_WRONG_THREAD
            // （0x8001010E）。因此每次延时后都必须显式切回 UI 线程。
            co_await wil::resume_foreground(UiDispatcher());

            // 被取消或被新一轮取代：立即退出，不要再触碰界面。
            if (!m_qrPolling || generation != m_qrGeneration)
            {
                co_return;
            }

            // 本地超时保护（接口也会返回过期，但网络异常时靠这里兜底）。
            if (std::chrono::steady_clock::now() - m_qrStartedAt > kQrLifetime)
            {
                StopQrLogin(L"二维码已过期，请重新点击“扫码登录”。");
                co_return;
            }

            const bool keepGoing = co_await PollQrOnceAsync(generation);
            if (!keepGoing)
            {
                co_return;
            }
        }
    }

    IAsyncOperation<bool> MainWindow::PollQrOnceAsync(uint64_t generation)
    {
        const std::wstring token = m_qrPollToken;
        if (token.empty())
        {
            co_return false;
        }

        std::wstring phase;
        std::wstring accessToken;
        std::wstring selfSteamId;
        std::wstring message;
        // 同 StartQrLoginAsync：接口内部已放到 MTA 工作线程，
        // 这里保持在 UI 线程调用即可，不再做线程切换。
        bool failed = false;
        try
        {
            wchar_t* raw = nullptr;
            const auto status = PerfectPollQrLogin(token.c_str(), &raw);
            if (status == PerfectStatus_Ok && raw)
            {
                const std::wstring json = raw;
                PerfectFreeString(raw);
                phase = JsonField(json, L"phase");
                accessToken = JsonField(json, L"accessToken");
                selfSteamId = JsonField(json, L"selfSteamId");
                message = JsonField(json, L"message");
            }
            else
            {
                // 单次网络失败不终止轮询，等下一轮重试。
                failed = true;
            }
        }
        catch (...)
        {
            failed = true;
        }

        if (failed)
        {
            // 保留界面状态，继续轮询。
            co_return m_qrPolling;
        }

        if (phase == L"Confirmed")
        {
            if (accessToken.empty() || selfSteamId.empty())
            {
                StopQrLogin(L"登录已确认，但未取得有效凭据。");
                co_return false;
            }

            ApplyCredentials(accessToken, selfSteamId);
            SaveCredentials(accessToken, selfSteamId);
            PerfectLogoutButton().Visibility(Visibility::Visible);
            StopQrLogin(L"登录成功，凭据已保存到本机。");
            co_return false;
        }
        if (phase == L"Expired")
        {
            StopQrLogin(message.empty()
                ? hstring(L"二维码已过期，请重新点击“扫码登录”。")
                : hstring(message));
            co_return false;
        }
        if (phase == L"Scanned")
        {
            PerfectAuthStatus().Text(L"已扫描，请在手机上确认登录。");
        }
        co_return true;   // WaitingScan / Scanned 继续轮询
    }


 

    // ==================== 完美世界：搜人 → 卡片 → 对局窗口 ====================

    void MainWindow::ShowPerfectSearchResults(
        std::vector<std::wstring> const& steamIds,
        std::vector<std::wstring> const& names,
        std::vector<std::wstring> const& appNames)
    {
        if (auto panel = PerfectSearchResults())
        {
            panel.Children().Clear();
            for (size_t i = 0; i < steamIds.size(); ++i)
            {
                const std::wstring sid = steamIds[i];
                const std::wstring nm = names[i];
                const std::wstring an = appNames[i];

                auto button = Button();
                button.HorizontalAlignment(HorizontalAlignment::Stretch);
                button.HorizontalContentAlignment(HorizontalAlignment::Left);
                button.Padding(Thickness{ 10, 7, 10, 7 });

                auto sp = StackPanel();
                sp.Spacing(2);
                auto t1 = TextBlock();
                t1.Text(nm.empty() ? sid : nm);
                t1.FontSize(14);
                sp.Children().Append(t1);
                auto t2 = TextBlock();
                t2.Text(an.empty() ? sid : an);
                t2.FontSize(11);
                t2.Opacity(0.7);
                sp.Children().Append(t2);
                button.Content(sp);

                button.Click([this, sid, nm](auto&&, auto&&) {
                    if (auto popup = PerfectSearchPopup()) { popup.IsOpen(false); }
                    FillPerfectCardAsync(sid, nm);
                });
                panel.Children().Append(button);
            }
        }
    }

    void MainWindow::OnPerfectSearchKeyDown(
        IInspectable const& /* sender */,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            OnPerfectSearchClick(nullptr, nullptr);
        }
    }

    void MainWindow::OpenMatchWindowPerfect(std::wstring const& steamId,
                                            std::wstring const& name,
                                            std::wstring const& token,
                                            std::wstring const& selfSteamId)
    {
        auto win = winrt::App1::implementation::MatchWindow::Create();
        MatchWinOf(win)->Init(1, steamId, name, token, selfSteamId);
        m_matchWindows.push_back(win);
        win.Activate();
        MatchWinOf(m_matchWindows.back())->LoadAsync();
    }

    fire_and_forget MainWindow::OnPerfectSearchClick(
        IInspectable const& /* sender */, RoutedEventArgs const& /*args*/)
    {
        if (m_perfectLoading) { co_return; }

        std::wstring keyword(HeaderSearchBox().Text().c_str());
        {
            const size_t b = keyword.find_first_not_of(L" \t\r\n");
            const size_t e = keyword.find_last_not_of(L" \t\r\n");
            keyword = (b == std::wstring::npos) ? std::wstring{} : keyword.substr(b, e - b + 1);
        }
        if (keyword.empty())
        {
            CardOf(PerfectCard())->Reset(L"请输入昵称、完美编号或 SteamID。");
            co_return;
        }

        const std::wstring token(PerfectTokenBox().Text().c_str());
        if (token.empty())
        {
            CardOf(PerfectCard())->Reset(L"请先到“设置”扫码登录完美平台，再搜索玩家。");
            co_return;
        }

        m_perfectLoading = true;
        SetBusy(true);
        CardOf(PerfectCard())->Reset(L"正在搜索玩家…");

        std::wstring failure;
        std::vector<std::wstring> ids, nms, ans;
        try
        {
            wchar_t* raw = nullptr;
            const auto st = PerfectSearchUser(keyword.c_str(), token.c_str(), &raw);
            if (st != PerfectStatus_Ok || !raw)
            {
                failure = PerfectGetLastError();
            }
            else
            {
                const std::wstring json = raw;
                PerfectFreeString(raw);
                using namespace winrt::Windows::Data::Json;
                JsonObject root{ nullptr };
                if (JsonObject::TryParse(json, root) && root)
                {
                    auto pick = [&](JsonObject const& o, wchar_t const* k) {
                        auto v = o.TryLookup(k);
                        return (v && v.ValueType() == JsonValueType::String)
                            ? std::wstring(v.GetString().c_str()) : std::wstring{};
                    };
                    if (auto lv = root.TryLookup(L"list");
                        lv && lv.ValueType() == JsonValueType::Array)
                    {
                        for (auto const& el : lv.GetArray())
                        {
                            if (!el || el.ValueType() != JsonValueType::Object) { continue; }
                            auto item = el.GetObject();
                            const std::wstring sid = pick(item, L"steamId");
                            if (sid.empty()) { continue; }
                            ids.push_back(sid);
                            nms.push_back(pick(item, L"pvpNickName"));
                            ans.push_back(pick(item, L"appNickName"));
                        }
                    }
                }
            }
        }
        catch (...) { failure = L"未知错误"; }

        SetBusy(false);
        m_perfectLoading = false;

        if (!failure.empty())
        {
            CardOf(PerfectCard())->Reset(L"搜索失败：" + failure);
            co_return;
        }
        if (ids.empty())
        {
            CardOf(PerfectCard())->Reset(L"未找到匹配「" + keyword + L"」的玩家。");
            co_return;
        }

        ShowPerfectSearchResults(ids, nms, ans);
        if (auto popup = PerfectSearchPopup()) { popup.IsOpen(true); }
        FillPerfectCardAsync(ids.front(), nms.front());
        co_return;
    }
    fire_and_forget MainWindow::FillPerfectCardAsync(std::wstring const& steamId,
                                                   std::wstring const& name)
    {
        const std::wstring token(PerfectTokenBox().Text().c_str());
        const std::wstring self(PerfectSelfSteamBox().Text().c_str());
        SetStatus(L"正在读取玩家资料…");

        std::wstring failure;
        std::wstring json;
        try
        {
            wchar_t* raw = nullptr;
            const auto st = PerfectGetPlayerProfile(
                steamId.c_str(), token.c_str(), self.c_str(), &raw);
            if (st != PerfectStatus_Ok || !raw)
            {
                failure = PerfectGetLastError();
            }
            else
            {
                json = raw;
                PerfectFreeString(raw);
            }
        }
        catch (...) { failure = L"未知错误"; }

        if (!failure.empty())
        {
            CardOf(PerfectCard())->Reset(L"读取资料失败：" + failure);
            co_return;
        }

        using namespace winrt::Windows::Data::Json;
        JsonObject root{ nullptr };
        if (!JsonObject::TryParse(json, root) || !root)
        {
            CardOf(PerfectCard())->Reset(L"资料解析失败。");
            co_return;
        }
        auto section = [&](JsonObject const& r, wchar_t const* n) {
            auto v = r.TryLookup(n);
            return (v && v.ValueType() == JsonValueType::Object)
                ? v.GetObject() : JsonObject{ nullptr };
        };
        auto str = [](JsonObject const& o, wchar_t const* k) {
            auto v = o.TryLookup(k);
            return (v && v.ValueType() == JsonValueType::String)
                ? std::wstring(v.GetString().c_str()) : std::wstring{};
        };
        auto num = [](JsonObject const& o, wchar_t const* k) {
            auto v = o.TryLookup(k);
            return (v && v.ValueType() == JsonValueType::Number) ? v.GetNumber() : 0.0;
        };

        auto profile = section(root, L"profile");
        if (!profile) { CardOf(PerfectCard())->Reset(L"资料为空。"); co_return; }

        const std::wstring nm = str(profile, L"name");
        const std::wstring title = str(profile, L"summary");
        const std::wstring avatar = str(profile, L"avatar");
        const std::wstring season = str(profile, L"seasonId");
        std::wstring rank = std::to_wstring(static_cast<int32_t>(num(profile, L"pvpScore")));

        CardOf(PerfectCard())->SetIdentity(
            nm.empty() ? name : nm, avatar, title, steamId, rank, season);

        auto pct = [&](wchar_t const* k, double scale) {
            return AppFmt::FormatDouble(num(profile, k) * scale, 0) + L"%";
        };
        std::vector<std::pair<std::wstring, std::wstring>> seasonItems = {
            { L"场次",    std::to_wstring(static_cast<int32_t>(num(profile, L"cnt"))) },
            { L"胜率",    pct(L"winRate", 100.0) },
            { L"K/D",     AppFmt::FormatDouble(num(profile, L"kd"), 2) },
            { L"Rating",  AppFmt::FormatDouble(num(profile, L"rating"), 2) },
            { L"WE",      AppFmt::FormatDouble(num(profile, L"avgWe"), 1) },
            { L"ADR",     AppFmt::FormatDouble(num(profile, L"adr"), 1) },
            { L"RWS",     AppFmt::FormatDouble(num(profile, L"rws"), 1) },
            { L"爆头率",  pct(L"headShotRatio", 100.0) },
            { L"首杀率",  pct(L"entryKillRatio", 100.0) },
        };
        CardOf(PerfectCard())->SetStats(1, L"本赛季", seasonItems);

        std::vector<std::pair<std::wstring, std::wstring>> careerItems = {
            { L"击杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"kills"))) },
            { L"死亡",  std::to_wstring(static_cast<int32_t>(num(profile, L"deaths"))) },
            { L"助攻",  std::to_wstring(static_cast<int32_t>(num(profile, L"assists"))) },
            { L"场次 MVP", std::to_wstring(static_cast<int32_t>(num(profile, L"mvpCount"))) },
            { L"多杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"multiKill"))) },
            { L"2 杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"k2"))) },
            { L"3 杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"k3"))) },
            { L"4 杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"k4"))) },
            { L"5 杀",  std::to_wstring(static_cast<int32_t>(num(profile, L"k5"))) },
            { L"1v1",   std::to_wstring(static_cast<int32_t>(num(profile, L"vs1"))) },
            { L"1v2",   std::to_wstring(static_cast<int32_t>(num(profile, L"vs2"))) },
            { L"1v3",   std::to_wstring(static_cast<int32_t>(num(profile, L"vs3"))) },
            { L"1v4",   std::to_wstring(static_cast<int32_t>(num(profile, L"vs4"))) },
            { L"1v5",   std::to_wstring(static_cast<int32_t>(num(profile, L"vs5"))) },
        };
        CardOf(PerfectCard())->SetStats(2, L"累计战绩", careerItems);

        std::vector<std::pair<std::wstring, std::wstring>> abilityItems = {
            { L"枪法", AppFmt::FormatDouble(num(profile, L"shot"), 1) },
            { L"狙击", AppFmt::FormatDouble(num(profile, L"snipe"), 1) },
            { L"致胜", AppFmt::FormatDouble(num(profile, L"victory"), 1) },
            { L"突破", AppFmt::FormatDouble(num(profile, L"breach"), 1) },
            { L"道具", AppFmt::FormatDouble(num(profile, L"prop"), 1) },
        };
        CardOf(PerfectCard())->SetStats(3, L"能力评估", abilityItems);

        // 趋势
        std::vector<double> trend;
        if (auto arr = root.TryLookup(L"profile"); arr && arr.ValueType() == JsonValueType::Object)
        {
            if (auto hr = arr.GetObject().TryLookup(L"historyRatings");
                hr && hr.ValueType() == JsonValueType::Array)
            {
                for (auto const& v : hr.GetArray())
                {
                    if (v && v.ValueType() == JsonValueType::Number) { trend.push_back(v.GetNumber()); }
                }
            }
        }
        CardOf(PerfectCard())->SetTrend(L"近期 Rating 走势", trend, L"Rating");

        // 武器
        std::vector<winrt::App1::WeaponItem> weapons;
        if (auto wv = root.TryLookup(L"weapons"); wv && wv.ValueType() == JsonValueType::Array)
        {
            for (auto const& v : wv.GetArray())
            {
                if (!v || v.ValueType() != JsonValueType::Object) { continue; }
                auto o = v.GetObject();
                auto s = [&](wchar_t const* k) { return str(o, k); };
                auto n = [&](wchar_t const* k) { return static_cast<int32_t>(num(o, k)); };
                winrt::App1::WeaponItem item = winrt::make<winrt::App1::implementation::WeaponItem>();
                auto* impl = winrt::get_self<winrt::App1::implementation::WeaponItem>(item);
                impl->SetNameZh(AppFmt::H(s(L"nameZh").empty() ? s(L"name") : s(L"nameZh")));
                impl->SetStats(AppFmt::H(
                    L"击杀 " + std::to_wstring(n(L"killNum")) +
                    L"　爆头 " + AppFmt::FormatDouble(num(o, L"headshotRate") * 100.0, 0) + L"%" +
                    L"　伤害 " + AppFmt::FormatDouble(num(o, L"avgDamage"), 0)));
                impl->SetRating(AppFmt::H(
                    std::wstring(s(L"levelAccuracy").empty() ? L"" : L"精度 " + s(L"levelAccuracy"))));
                weapons.push_back(item);
            }
        }
        CardOf(PerfectCard())->SetWeapons(weapons);

        // 地图
        std::vector<winrt::App1::MapItem> maps;
        if (auto mv = root.TryLookup(L"maps"); mv && mv.ValueType() == JsonValueType::Array)
        {
            for (auto const& v : mv.GetArray())
            {
                if (!v || v.ValueType() != JsonValueType::Object) { continue; }
                auto o = v.GetObject();
                auto s = [&](wchar_t const* k) { return str(o, k); };
                auto n = [&](wchar_t const* k) { return static_cast<int32_t>(num(o, k)); };
                const int32_t total = n(L"totalMatch");
                const int32_t wins = n(L"winCount");
                winrt::App1::MapItem item = winrt::make<winrt::App1::implementation::MapItem>();
                auto* impl = winrt::get_self<winrt::App1::implementation::MapItem>(item);
                impl->SetMapName(AppFmt::H(s(L"mapName").empty() ? s(L"map") : s(L"mapName")));
                impl->SetStats(AppFmt::H(
                    std::to_wstring(total) + L" 场　" + std::to_wstring(wins) + L" 胜" +
                    L"　ADR " + AppFmt::FormatDouble(num(o, L"totalAdr"), 0)));
                impl->SetWinRate(AppFmt::H(total > 0
                    ? std::to_wstring(wins * 100 / total) + L"% 胜率" : L""));
                maps.push_back(item);
            }
        }
        CardOf(PerfectCard())->SetMaps(maps);

        // 打开对局窗口
        const std::wstring tk = token;
        const std::wstring slf = self;
        const std::wstring sid = steamId;
        const std::wstring showNm = nm.empty() ? name : nm;
        CardOf(PerfectCard())->SetViewMatchesHandler([this, sid, showNm, tk, slf]() {
            OpenMatchWindowPerfect(sid, showNm, tk, slf);
        });

        SetStatus(H(L"已找到：" + showNm));
        co_return;
    }
} // namespace winrt::App1::implementation
