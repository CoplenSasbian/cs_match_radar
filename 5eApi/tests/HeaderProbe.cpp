// 定位是哪个请求头导致 E_INVALIDARG。
#include "FiveEApiPch.h"
#include "FiveEGateSign.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/base.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;

int main()
{
    init_apartment(apartment_type::multi_threaded);

    const std::vector<std::pair<std::wstring, std::wstring>> candidates = {
        { L"x-ca-key", L"5eplay" },
        { L"x-ca-signature-method", L"HmacSHA256" },
        { L"x-ca-signature-headers", L"Accept-Language,Authorization" },
        { L"x-ca-signature", L"abc=" },
        { L"Authorization", L" " },
        { L"Accept-Language", L"zh-cn" },
        { L"Referer", L"https://view-arena.5eplay.com/" },
        { L"Origin", L"https://view-arena.5eplay.com" },
        { L"User-Agent", L"Mozilla/5.0" },
    };

    std::printf("=== 逐个测试 header 是否可写入 ===\n");
    for (auto const& [name, value] : candidates)
    {
        HttpClient client;
        auto headers = client.DefaultRequestHeaders();
        try
        {
            headers.Append(name, value);
            std::wprintf(L"  OK    %s\n", name.c_str());
        }
        catch (hresult_error const& ex)
        {
            std::wprintf(L"  FAIL  0x%08X  %s\n", (unsigned)ex.code().value, name.c_str());
        }
    }
    return 0;
}
