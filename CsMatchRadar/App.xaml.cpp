#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::App1::implementation
{
    /// <summary>
    /// Initializes the singleton application object.  This is the first line of authored code
    /// executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

        // 未处理异常一律记录下来。
        //
        // 之前这里只在 Debug 下挂断点，Release 版遇到 XAML/协程里的异常
        // 只会让进程直接退出（0xc000027b），排查时拿不到任何信息。
        // 现在把异常消息写入本地文件，便于事后定位。
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            try
            {
                const std::wstring message = e.Message().c_str();
                const std::wstring text =
                    L"[UnhandledException] " + message + L"\r\n";

                // 写到临时目录，无需程序包标识符（支持 unpackaged 模式）。
                wchar_t tempDir[MAX_PATH]{};
                GetTempPathW(MAX_PATH, tempDir);
                std::wstring file = std::wstring(tempDir) + L"CsMatchRadar_crash.log";

                HANDLE handle = CreateFileW(file.c_str(), FILE_APPEND_DATA,
                    FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE)
                {
                    const std::wstring line = text + L"---\r\n";
                    DWORD written = 0;
                    WriteFile(handle, line.c_str(),
                        static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
                    CloseHandle(handle);
                }
            }
            catch (...)
            {
            }

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
            if (IsDebuggerPresent())
            {
                __debugbreak();
            }
#endif
        });
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="e">Details about the launch request and process.</param>
    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
