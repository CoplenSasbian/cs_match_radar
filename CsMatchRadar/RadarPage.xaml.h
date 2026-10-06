#pragma once

#include "RadarPage.g.h"

#include "LiveMatchModels.h"
#include "FiveECdpCapture.h"
#include "PerfectLogWatcher.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <memory>
#include <vector>

namespace winrt::App1::implementation
{
    struct RadarPage : RadarPageT<RadarPage>
    {
        RadarPage();

        winrt::hstring Title() const { return m_title; }

        // 实时采集回调。
        void OnLiveMatch(LiveMatch::LiveMatch const& match);
        void OnCaptureStatus(std::wstring const& text, bool isError);

        void OnRefreshClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };

        std::shared_ptr<LiveCapture::FiveECdpCapture> m_fiveECapture;
        std::shared_ptr<LiveCapture::PerfectLogWatcher> m_perfectWatcher;

        Windows::Foundation::Collections::IObservableVector<winrt::App1::RadarPlayer> m_team1;
        Windows::Foundation::Collections::IObservableVector<winrt::App1::RadarPlayer> m_team2;

        bool m_hasAuth = false;
        winrt::hstring m_title{ L"对局雷达" };
        std::wstring m_perfectToken;
        std::wstring m_perfectSelf;

        void StartCapture();
        void StopCapture();
        winrt::fire_and_forget EnrichAndShowAsync(LiveMatch::LiveMatch match);
        void EnrichOne(LiveMatch::RadarPlayer& p);
        void SetStatusText(std::wstring const& text);
        void SetBusy(bool busy);
        void ShowMatch(LiveMatch::LiveMatch const& match);
    };
}

namespace winrt::App1::factory_implementation
{
    struct RadarPage : RadarPageT<RadarPage, implementation::RadarPage> {};
}
