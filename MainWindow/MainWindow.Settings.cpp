#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/ProviderClient.h"
#include "Backend/SettingsManager.h"
#include "Backend/TrayIcon.h"
#include "Backend/DiscordPresence.h"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Text.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    void MainWindow::SettingsSection_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto btn = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
        if (!btn)
        {
            return;
        }
        auto tag = ReadTagString(btn.Tag());
        UpdateSettingsSection(tag.empty() ? winrt::hstring{ L"Profile" } : tag);
    }

    namespace
    {
        winrt::hstring TrimDisplayName(winrt::hstring const& value)
        {
            std::wstring s{ value.c_str() };
            while (!s.empty() && std::iswspace(s.front())) s.erase(s.begin());
            while (!s.empty() && std::iswspace(s.back()))  s.pop_back();
            return winrt::hstring{ s };
        }
    }

    void MainWindow::DisplayNameBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto box = DisplayNameBox();
        auto btn = DisplayNameSaveButton();
        if (!box || !btn) return;
        // Dirty = current text (trimmed) differs from what's persisted.
        // Mirrors the trim semantics in DisplayNameSave_Click so leading
        // or trailing whitespace alone doesn't make the row look dirty.
        auto current = TrimDisplayName(box.Text());
        auto persisted = TrimDisplayName(SettingsManagerService().GetString(L"UserDisplayName", L""));
        btn.IsEnabled(current != persisted);
    }

    void MainWindow::DisplayNameSave_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto box = DisplayNameBox();
        if (!box) return;
        auto trimmed = TrimDisplayName(box.Text());
        // Empty allowed — ApplyUserDisplayName falls back to "Listener".
        SettingsManagerService().SetString(L"UserDisplayName", trimmed);
        // Reflect the trimmed value back into the box so the user sees
        // exactly what was saved.
        if (box.Text() != trimmed)
        {
            box.Text(trimmed);
        }
        if (auto btn = DisplayNameSaveButton())
        {
            btn.IsEnabled(false);
        }
        ApplyUserDisplayName();
    }

    void MainWindow::DisplayNameBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        (void)sender;
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }
        args.Handled(true);
        // Enter commits via the same path as the Save button so there's
        // only one persistence path.
        DisplayNameSave_Click(nullptr, winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
    }

    void MainWindow::MainBodyGrid_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        ApplySettingsResponsiveLayout(args.NewSize().Width);
        ApplyRightRailWidth();
    }

    void MainWindow::UpdateSettingsSection(winrt::hstring const& key)
    {
        m_selectedSettingsSection = key;
        EnsureAccentBrushes();

        using winrt::Microsoft::UI::Xaml::Visibility;
        using winrt::Microsoft::UI::Text::FontWeights;

        struct SecRow
        {
            winrt::Microsoft::UI::Xaml::Controls::Button btn;
            winrt::Microsoft::UI::Xaml::Controls::Border pill;
            winrt::Microsoft::UI::Xaml::Controls::FontIcon glyph;
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label;
            winrt::Microsoft::UI::Xaml::UIElement panel;
            winrt::hstring id;
        };

        SecRow rows[] = {
            { SetNavProfile(), SetPillProfile(), SetGlyphProfile(), SetLabelProfile(), SetSecProfile(), L"Profile" },
            { SetNavLibrary(), SetPillLibrary(), SetGlyphLibrary(), SetLabelLibrary(), SetSecLibrary(), L"Library" },
            { SetNavPlayback(), SetPillPlayback(), SetGlyphPlayback(), SetLabelPlayback(), SetSecPlayback(), L"Playback" },
            { SetNavAudio(), SetPillAudio(), SetGlyphAudio(), SetLabelAudio(), SetSecAudio(), L"Audio" },
            { SetNavAppearance(), SetPillAppearance(), SetGlyphAppearance(), SetLabelAppearance(), SetSecAppearance(), L"Appearance" },
            { SetNavIntegrations(), SetPillIntegrations(), SetGlyphIntegrations(), SetLabelIntegrations(), SetSecIntegrations(), L"Integrations" },
            { SetNavAbout(), SetPillAbout(), SetGlyphAbout(), SetLabelAbout(), SetSecAbout(), L"About" },
        };

        for (auto const& r : rows)
        {
            bool selected = (key == r.id);
            r.pill.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
            r.btn.Background(selected ? m_brushAccentSoft : m_brushTransparent);
            r.glyph.Foreground(selected ? m_brushAccent : m_brushGlyphIdle);
            r.label.Foreground(selected ? m_brushAccent : m_brushLabelIdle);
            r.label.FontWeight(selected ? FontWeights::SemiBold() : FontWeights::Normal());
            r.panel.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
        }

        if (auto mainBody = MainBodyGrid())
        {
            ApplySettingsResponsiveLayout(mainBody.ActualWidth());
        }
    }

    void MainWindow::ApplySettingsResponsiveLayout(double width)
    {
        constexpr double compactBreakpoint = 1500.0;
        bool compact = width < compactBreakpoint;
        m_settingsCompactLayout = compact;

        using winrt::Microsoft::UI::Xaml::FrameworkElement;
        using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
        using winrt::Microsoft::UI::Xaml::Thickness;
        using winrt::Microsoft::UI::Xaml::UIElement;
        using winrt::Microsoft::UI::Xaml::Visibility;
        using winrt::Microsoft::UI::Xaml::Controls::Grid;
        using winrt::Microsoft::UI::Xaml::GridLengthHelper;

        if (auto column = SettingsNavColumn())
        {
            column.Width(GridLengthHelper::FromPixels(compact ? 56.0 : 220.0));
        }
        if (auto panel = SettingsNavPanel())
        {
            panel.Margin(compact ? Thickness{ 0, 0, 12, 0 } : Thickness{ 0, 0, 24, 0 });
        }

        auto setNavVisuals = [&](winrt::Microsoft::UI::Xaml::Controls::TextBlock const& label,
                                 winrt::Microsoft::UI::Xaml::Controls::Border const& pill,
                                 winrt::hstring const& id)
        {
            if (label)
            {
                label.Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
            }
            if (pill)
            {
                pill.Visibility(!compact && m_selectedSettingsSection == id ? Visibility::Visible : Visibility::Collapsed);
            }
        };

        setNavVisuals(SetLabelProfile(), SetPillProfile(), L"Profile");
        setNavVisuals(SetLabelLibrary(), SetPillLibrary(), L"Library");
        setNavVisuals(SetLabelPlayback(), SetPillPlayback(), L"Playback");
        setNavVisuals(SetLabelAudio(), SetPillAudio(), L"Audio");
        setNavVisuals(SetLabelAppearance(), SetPillAppearance(), L"Appearance");
        setNavVisuals(SetLabelIntegrations(), SetPillIntegrations(), L"Integrations");
        setNavVisuals(SetLabelAbout(), SetPillAbout(), L"About");

        auto placeAction = [&](UIElement const& element, int wideColumn, int compactColumn)
        {
            if (!element)
            {
                return;
            }

            if (auto fe = element.try_as<FrameworkElement>())
            {
                Grid::SetColumn(fe, compact ? compactColumn : wideColumn);
                Grid::SetRow(fe, compact ? 1 : 0);
                fe.HorizontalAlignment(compact ? HorizontalAlignment::Left : HorizontalAlignment::Stretch);
                fe.Margin(compact ? Thickness{ 0, 12, 0, 0 } : Thickness{ 0, 0, 0, 0 });
            }
        };

        placeAction(MusicFolderActions(), 2, 1);
        placeAction(ScanMusicButton(), 2, 1);
        placeAction(AutoplaySwitch(), 1, 0);
        placeAction(GaplessSwitch(), 1, 0);
        placeAction(CloseBehaviorCombo(), 1, 0);
        placeAction(OutputDeviceCombo(), 1, 0);
        placeAction(ThemeOptionsPanel(), 1, 0);
        placeAction(AccentSwatchesPanel(), 1, 0);
        placeAction(ShowAlbumArtSwitch(), 1, 0);
        placeAction(DiscordPresenceSwitch(), 1, 0);
        placeAction(WindowsMediaControlsSwitch(), 1, 0);
    }

    void MainWindow::ThemeSegment_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto clicked = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!clicked)
        {
            return;
        }
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton segs[] = {
            ThemeLight(), ThemeDark(), ThemeSystem()
        };
        for (auto const& s : segs)
        {
            if (s && s != clicked)
            {
                s.IsChecked(false);
            }
        }

        auto tag = ReadTagString(clicked.Tag());
        if (tag.empty())
        {
            tag = L"System";
        }
        auto theme = winrt::Microsoft::UI::Xaml::ElementTheme::Default;
        if (tag == L"Light")
        {
            theme = winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        }
        else if (tag == L"Dark")
        {
            theme = winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
        }

        if (auto root = this->Content().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            root.RequestedTheme(theme);
        }
        // Captured brushes are theme-specific; force a re-capture on next use.
        m_accentBrushesCaptured = false;

        // Persist the choice (skipped while LoadSettingsIntoUi is syncing UI).
        if (!m_loadingSettings)
        {
            SettingsManagerService().SetString(L"Theme", tag);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDataFolder_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        try
        {
            std::filesystem::create_directories(AppDataDirectory());
            auto localFolder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
                winrt::hstring{ AppDataDirectory().wstring() });
            co_await winrt::Windows::System::Launcher::LaunchFolderAsync(localFolder);
        }
        catch (...) {}
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ShowLicenses_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::hstring credits =
            L"Last Music Player\n"
            L"© 2026 Debashis\n\n"
            L"Bundled components:\n"
            L"  • Windows App SDK 1.8 — MIT License\n"
            L"  • SQLite 3 — Public Domain\n"
            L"  • C++/WinRT — MIT License\n"
            L"  • Segoe Fluent Icons — Microsoft, bundled with Windows\n\n"
            L"This application is provided as-is, without warranty of any kind.";

        winrt::Microsoft::UI::Xaml::Controls::TextBlock body;
        body.Text(credits);
        body.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        body.FontSize(13.0);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Licenses & credits")));
        dlg.Content(body);
        dlg.CloseButtonText(L"Close");
        dlg.XamlRoot(this->Content().XamlRoot());

        co_await dlg.ShowAsync();
    }

    // Maps a Settings control's x:Name to its persisted settings key.
    static winrt::hstring SettingKeyForControl(winrt::hstring const& name)
    {
        std::wstring n{ name.c_str() };
        if (n == L"ShowAlbumArtSwitch")        return L"ShowAlbumArt";
        if (n == L"WindowsMediaControlsSwitch")return L"WindowsMediaControls";
        if (n == L"GaplessSwitch")             return L"Gapless";
        if (n == L"AutoplaySwitch")            return L"Autoplay";
        if (n == L"DiscordPresenceSwitch")     return L"DiscordPresence";
        if (n == L"CloseBehaviorCombo")        return L"CloseBehavior";
        if (n == L"OutputDeviceCombo")         return L"OutputDeviceIndex";
        return {};
    }

    void MainWindow::SettingsToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto ts = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch>();
        if (!ts)
        {
            return;
        }
        auto name = ts.Name();
        auto key = SettingKeyForControl(name);
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetBool(key, ts.IsOn());
        }
        if (name == L"ShowAlbumArtSwitch")
        {
            ApplyShowAlbumArt();
        }
        else if (name == L"WindowsMediaControlsSwitch")
        {
            ApplyWindowsMediaControls();
        }
        else if (name == L"DiscordPresenceSwitch")
        {
            ApplyDiscordPresence();
        }
        else if (name == L"GaplessSwitch")
        {
            ApplyGaplessSetting();
        }
        else if (name == L"AutoplaySwitch")
        {
            // Turning autoplay on should immediately start filling Up Next;
            // turning it off is honored lazily at the next end-of-queue.
            if (ts.IsOn())
            {
                MaybeExtendAutoplayQueue();
            }
        }
    }

    void MainWindow::SettingsCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)args;
        auto cb = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBox>();
        if (!cb)
        {
            return;
        }
        if (cb.Name() == L"OutputDeviceCombo")
        {
            auto idx = cb.SelectedIndex();
            winrt::hstring deviceId =
                (idx > 0 && static_cast<size_t>(idx) < m_outputDeviceIds.size())
                    ? m_outputDeviceIds[static_cast<size_t>(idx)]
                    : winrt::hstring{};
            if (!m_loadingSettings)
            {
                SettingsManagerService().SetString(L"OutputDeviceId", deviceId);
            }
            ApplyOutputDeviceAsync();
            return;
        }

        auto key = SettingKeyForControl(cb.Name());
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetInt(key, cb.SelectedIndex());
        }
    }

    void MainWindow::SettingsSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        auto sl = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Slider>();
        if (!sl)
        {
            return;
        }
        auto key = SettingKeyForControl(sl.Name());
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetDouble(key, args.NewValue());
        }
    }

    void MainWindow::AccentSwatch_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        (void)args;
        auto el = sender.try_as<winrt::Microsoft::UI::Xaml::Shapes::Ellipse>();
        if (!el)
        {
            return;
        }
        auto hex = ReadTagString(el.Tag());
        if (hex.empty())
        {
            return;
        }
        if (!m_loadingSettings)
        {
            SettingsManagerService().SetString(L"AccentColor", hex);
        }
        ApplyAccentColor(hex);
    }

    void MainWindow::ApplyShowAlbumArt()
    {
        bool show = SettingsManagerService().GetBool(L"ShowAlbumArt", true);
        if (auto c = BottomArtContainer())
        {
            c.Visibility(show ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                              : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    void MainWindow::ApplyWindowsMediaControls()
    {
        bool on = SettingsManagerService().GetBool(L"WindowsMediaControls", true);
        try
        {
            // Toggle the window-bound SMTC primarily — that's the one the
            // OS routes media keys to. The player-internal SMTC is kept
            // in sync too in case any UWP-side consumer reads it.
            auto smtc = m_windowSmtc
                ? m_windowSmtc
                : AudioPlayerService().GetMediaPlayer().SystemMediaTransportControls();
            smtc.IsEnabled(on);
        }
        catch (...)
        {
        }
    }

    void MainWindow::ApplyThemeFromSetting()
    {
        auto theme = SettingsManagerService().GetString(L"Theme", L"System");
        auto pick = (theme == L"Light") ? ThemeLight()
                  : (theme == L"Dark")  ? ThemeDark()
                                        : ThemeSystem();
        if (!pick)
        {
            return;
        }
        if (pick.IsChecked() && pick.IsChecked().Value())
        {
            // Already checked (e.g. the System default) — apply directly since
            // setting IsChecked(true) again would not raise Checked.
            ThemeSegment_Checked(pick, nullptr);
        }
        else
        {
            pick.IsChecked(true); // raises ThemeSegment_Checked
        }
    }

    static winrt::Windows::UI::Color ColorFromHex(winrt::hstring const& hex)
    {
        std::wstring s{ hex.c_str() };
        if (!s.empty() && s.front() == L'#')
        {
            s.erase(s.begin());
        }
        unsigned long v = wcstoul(s.c_str(), nullptr, 16);
        winrt::Windows::UI::Color c{};
        if (s.size() <= 6)
        {
            c.A = 255;
            c.R = static_cast<uint8_t>((v >> 16) & 0xFF);
            c.G = static_cast<uint8_t>((v >> 8) & 0xFF);
            c.B = static_cast<uint8_t>(v & 0xFF);
        }
        else
        {
            c.A = static_cast<uint8_t>((v >> 24) & 0xFF);
            c.R = static_cast<uint8_t>((v >> 16) & 0xFF);
            c.G = static_cast<uint8_t>((v >> 8) & 0xFF);
            c.B = static_cast<uint8_t>(v & 0xFF);
        }
        return c;
    }

    void MainWindow::ApplyAccentColor(winrt::hstring const& hex)
    {
        EnsureAccentBrushes();

        // Move the selection ring to the chosen swatch.
        winrt::Microsoft::UI::Xaml::Shapes::Ellipse sws[] = {
            AccentSw0(), AccentSw1(), AccentSw2(), AccentSw3(), AccentSw4()
        };
        std::wstring selected{ hex.c_str() };
        for (auto const& e : sws)
        {
            if (!e)
            {
                continue;
            }
            bool isSel = std::wstring{ ReadTagString(e.Tag()).c_str() } == selected;
            if (isSel && m_brushStroke)
            {
                e.Stroke(m_brushStroke);
            }
            e.StrokeThickness(isSel ? 2.0 : 0.0);
        }

        // The captured accent brushes ARE the shared theme-dictionary
        // SolidColorBrush instances; mutating their Color in place recolors
        // every {ThemeResource AccentBrush}/{AccentSoftBrush} consumer live
        // (nav highlights, slider foregrounds, accent glyphs/text, etc.).
        auto color = ColorFromHex(hex);
        if (auto ab = m_brushAccent.try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>())
        {
            ab.Color(color);
        }
        if (auto sb = m_brushAccentSoft.try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>())
        {
            auto soft = color;
            soft.A = sb.Color().A; // keep the soft brush's translucency
            sb.Color(soft);
        }
    }

    void MainWindow::EnsureTrayIcon()
    {
        if (m_trayIcon && m_trayIcon->IsActive())
        {
            return;
        }
        if (!m_trayIcon)
        {
            m_trayIcon = std::make_shared<LastMusicPlayer::Backend::TrayIcon>();
        }

        HICON icon = reinterpret_cast<HICON>(LoadImageW(
            nullptr,
            GetAppAssetPath(L"Assets\\AppIcon.ico").c_str(),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE));

        auto weak = get_weak();
        m_trayIcon->OnRestore = [weak]()
        {
            if (auto self = weak.get())
            {
                if (self->m_appWindow)
                {
                    self->m_appWindow.Show();
                }
                if (self->m_hwnd)
                {
                    ShowWindow(self->m_hwnd, SW_SHOW);
                    SetForegroundWindow(self->m_hwnd);
                }
                if (self->m_trayIcon)
                {
                    self->m_trayIcon->Remove();
                }
            }
        };
        m_trayIcon->OnExit = [weak]()
        {
            if (auto self = weak.get())
            {
                self->m_forceExit = true;
                self->m_cast.Disconnect();
                self->ClearCastCallbacks();
                if (self->m_trayIcon)
                {
                    self->m_trayIcon->Remove();
                }
                if (auto app = winrt::Microsoft::UI::Xaml::Application::Current())
                {
                    app.Exit();
                }
            }
        };
        m_trayIcon->Create(icon, L"Last Music Player");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::PopulateOutputDevicesAsync()
    {
        auto lifetime = get_strong();
        auto savedId = SettingsManagerService().GetString(L"OutputDeviceId", L"");

        std::vector<std::pair<winrt::hstring, winrt::hstring>> devices; // id, name
        try
        {
            auto selector = winrt::Windows::Media::Devices::MediaDevice::GetAudioRenderSelector();
            auto found = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(selector);
            for (auto const& d : found)
            {
                devices.emplace_back(d.Id(), d.Name());
            }
        }
        catch (...)
        {
        }

        auto combo = OutputDeviceCombo();
        if (!combo)
        {
            co_return;
        }

        m_loadingSettings = true;
        combo.Items().Clear();
        m_outputDeviceIds.clear();

        combo.Items().Append(winrt::box_value(winrt::hstring{ L"System default" }));
        m_outputDeviceIds.push_back(L""); // index 0 == system default

        int selectedIndex = 0;
        for (auto const& [id, deviceName] : devices)
        {
            combo.Items().Append(winrt::box_value(deviceName));
            m_outputDeviceIds.push_back(id);
            if (!savedId.empty() && id == savedId)
            {
                selectedIndex = static_cast<int>(m_outputDeviceIds.size()) - 1;
            }
        }
        combo.SelectedIndex(selectedIndex);
        m_loadingSettings = false;

        co_await ApplyOutputDeviceAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ApplyOutputDeviceAsync()
    {
        auto lifetime = get_strong();
        auto savedId = SettingsManagerService().GetString(L"OutputDeviceId", L"");
        try
        {
            auto mp = AudioPlayerService().GetMediaPlayer();
            if (savedId.empty())
            {
                mp.AudioDeviceType(winrt::Windows::Media::Playback::MediaPlayerAudioDeviceType::Multimedia);
            }
            else
            {
                auto info = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::CreateFromIdAsync(savedId);
                if (info)
                {
                    mp.AudioDevice(info);
                }
            }
        }
        catch (...)
        {
        }
    }

    void MainWindow::ApplyDiscordPresence()
    {
        bool on = SettingsManagerService().GetBool(L"DiscordPresence", false);
        if (on)
        {
            if (!m_discord)
            {
                m_discord = std::make_shared<LastMusicPlayer::Backend::DiscordPresence>();
            }
            if (!m_discord->IsConnected())
            {
                m_discord->Connect();
            }
            auto current = AudioPlayerService().GetCurrentTrack();
            if (current)
            {
                UpdateDiscordNowPlaying(current);
            }
        }
        else if (m_discord)
        {
            m_discord->Clear();
        }
    }

    bool MainWindow::IsDiscordPlaybackActive(winrt::Windows::Media::Playback::MediaPlaybackState state)
    {
        using winrt::Windows::Media::Playback::MediaPlaybackState;
        return state == MediaPlaybackState::Playing
            || state == MediaPlaybackState::Opening
            || state == MediaPlaybackState::Buffering;
    }

    void MainWindow::SampleDiscordPlaybackSnapshot(bool& isPlaying, double& positionSeconds, double& durationSeconds)
    {
        isPlaying = true;
        positionSeconds = 0.0;
        durationSeconds = 0.0;

        if (m_sink == PlaybackSink::Cast)
        {
            isPlaying = m_castSession.IsPlaying;
            positionSeconds = m_castSession.CurrentSeconds;
            if (m_castSession.IsPlaying && m_castSession.ProgressStampMs > 0)
            {
                auto now = ::GetTickCount64();
                positionSeconds += static_cast<double>(now - m_castSession.ProgressStampMs) / 1000.0;
            }
            durationSeconds = m_castSession.DurationSeconds;
            if (durationSeconds <= 0.5)
            {
                auto current = AudioPlayerService().GetCurrentTrack();
                if (current && current.DurationSeconds() > 0.5)
                {
                    durationSeconds = current.DurationSeconds();
                }
            }
            return;
        }

        try
        {
            auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();
            auto state = session.PlaybackState();
            isPlaying = IsDiscordPlaybackActive(state);
            auto natural = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
            if (natural > 0.5)
            {
                durationSeconds = natural;
            }
            positionSeconds = static_cast<double>(session.Position().count()) / 10000000.0;
        }
        catch (...)
        {
        }
    }

    void MainWindow::UpdateDiscordPlaybackState(bool isPlaying, double positionSeconds, double durationSeconds)
    {
        if (!SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }
        auto current = AudioPlayerService().GetCurrentTrack();
        if (!current)
        {
            if (m_discord)
            {
                m_discord->Clear();
            }
            return;
        }
        if (!m_discord || !m_discord->IsConnected())
        {
            UpdateDiscordNowPlaying(current);
            return;
        }
        m_discord->SetPlaybackState(isPlaying, positionSeconds, durationSeconds);
        m_discordPresenceRefreshMs = ::GetTickCount64();
    }

    void MainWindow::RefreshDiscordPresenceIfNeeded(bool isPlaying, double positionSeconds, double durationSeconds)
    {
        if (!SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }

        auto now = ::GetTickCount64();
        bool connected = m_discord && m_discord->IsConnected();
        if (!connected)
        {
            if (m_discordReconnectAttemptMs != 0 && now - m_discordReconnectAttemptMs < 5000)
            {
                return;
            }
            m_discordReconnectAttemptMs = now;
        }
        else
        {
            if (m_discordPresenceRefreshMs != 0 && now - m_discordPresenceRefreshMs < 60000)
            {
                return;
            }
            m_discordPresenceRefreshMs = now;
        }

        try
        {
            UpdateDiscordPlaybackState(isPlaying, positionSeconds, durationSeconds);
        }
        catch (...)
        {
        }
    }

    void MainWindow::UpdateDiscordNowPlaying(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track || !SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }
        if (!m_discord)
        {
            m_discord = std::make_shared<LastMusicPlayer::Backend::DiscordPresence>();
        }
        if (!m_discord->IsConnected())
        {
            m_discord->Connect();
        }

        LastMusicPlayer::Backend::PresencePayload payload;
        payload.title     = std::wstring{ track.Title().c_str() };
        payload.artist    = std::wstring{ track.Artist().c_str() };
        payload.album     = std::wstring{ track.Album().c_str() };
        payload.artworkUrl = std::wstring{ track.ArtworkUrl().c_str() };
        payload.durationSeconds = track.DurationSeconds();
        // SourceKind is "local" or "remote" — drives the third-line label on
        // the Discord activity card ("Source: Local" vs "Source: Remote").
        payload.isLocal = (std::wstring{ track.SourceKind().c_str() } == L"local");

        // Prefer the active playback sink for duration + position. Local
        // MediaPlayer enters Opening/Buffering during normal playback and is
        // intentionally paused while casting, so Discord must not treat every
        // non-Playing MediaPlayer state as user-paused.
        double position = 0.0;
        double duration = payload.durationSeconds;
        bool playing = true;
        SampleDiscordPlaybackSnapshot(playing, position, duration);
        if (duration > 0.5) payload.durationSeconds = duration;

        payload.positionSeconds = position;
        payload.isPlaying = playing;

        m_discord->SetNowPlaying(payload);
        m_discordPresenceRefreshMs = ::GetTickCount64();

        if (!m_discord->IsConnected())
        {
            return;
        }

        // Discord's RPC IPC `large_image` field accepts URLs only from a small
        // whitelist of CDNs; arbitrary domains are silently rejected. The
        // provider resolves a Discord-renderable cover URL for the track and
        // hands it back asynchronously. Resolution is fire-and-forget; once the
        // URL arrives, SetArtworkProxyUrl re-emits the activity with the banner.
        // The title acts as a race guard so a stale resolution can't paint
        // artwork onto a track the user has since skipped.
        if (!payload.title.empty())
        {
            ResolveDiscordArtworkAsync(
                winrt::hstring{ payload.title },
                winrt::hstring{ payload.artist });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResolveDiscordArtworkAsync(
        winrt::hstring trackTitle, winrt::hstring trackArtist)
    {
        auto lifetime = get_strong();

        auto savedBaseUrl = ReadAppSettingString(L"ProviderBaseUrl");
        auto savedApiKey = ReadAppSettingString(L"ProviderApiKey");
        if (savedBaseUrl.empty())
        {
            co_return;
        }
        if (trackTitle.empty() && trackArtist.empty())
        {
            co_return;
        }

        LastMusicPlayer::Backend::ProviderClient client;
        client.SetBaseUrl(savedBaseUrl);
        client.SetBearerToken(savedApiKey);

        winrt::hstring proxyUrl;
        try
        {
            proxyUrl = co_await client.ResolveDiscordArtworkAsync(trackTitle, trackArtist);
        }
        catch (winrt::hresult_error const&)
        {
            co_return;
        }
        catch (...)
        {
            co_return;
        }

        if (proxyUrl.empty())
        {
            co_return;
        }

        if (!m_discord || !m_discord->IsConnected())
        {
            co_return;
        }
        m_discord->SetArtworkProxyUrl(
            std::wstring{ proxyUrl.c_str() },
            std::wstring{ trackTitle.c_str() },
            std::wstring{ trackArtist.c_str() });
    }

    void MainWindow::UpdateAboutStats()
    {
        if (!AboutDbStats())
        {
            return;
        }
        try
        {
            if (DatabaseService().IsInitialized())
            {
                auto st = DatabaseService().GetLibraryStats();
                std::wstring text = L"SQLite 3 · "
                    + std::to_wstring(st.SongCount) + L" tracks · "
                    + std::to_wstring(st.AlbumCount) + L" albums · "
                    + std::to_wstring(st.ArtistCount) + L" artists";
                AboutDbStats().Text(winrt::hstring{ text });
            }
        }
        catch (...)
        {
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResetDefaults_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog confirm;
        confirm.Title(winrt::box_value(winrt::hstring{ L"Reset settings" }));
        confirm.Content(winrt::box_value(winrt::hstring{
            L"Restore all preferences to their defaults? Your music library and "
            L"data are not affected." }));
        confirm.PrimaryButtonText(L"Reset");
        confirm.CloseButtonText(L"Cancel");
        confirm.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        confirm.XamlRoot(this->Content().XamlRoot());

        auto result = co_await confirm.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto& s = SettingsManagerService();
        s.SetBool(L"ShowAlbumArt", true);
        s.SetBool(L"WindowsMediaControls", true);
        s.SetBool(L"Gapless", true);
        s.SetBool(L"Autoplay", true);
        s.SetBool(L"DiscordPresence", false);
        s.SetInt(L"CloseBehavior", 0);
        s.SetString(L"ScanFormats", L".mp3,.flac");
        s.SetString(L"Theme", L"System");
        s.SetString(L"AccentColor", L"#FF0097B2");
        s.SetString(L"OutputDeviceId", L"");
        for (int i = 0; i < 10; ++i)
        {
            wchar_t key[16];
            std::swprintf(key, 16, L"EqBand%d", i);
            s.SetDouble(winrt::hstring{ key }, 0.0);
        }
        s.SetDouble(L"EqPreamp", 0.0);

        LoadSettingsIntoUi();
        ApplyDiscordPresence();
        co_await ApplyOutputDeviceAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::WipeAllData_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();

        winrt::Microsoft::UI::Xaml::Controls::TextBlock body;
        body.Text(L"This clears the local library database, playback history, liked tracks, playlists, queues, settings, provider config and app caches. Music files on disk are not deleted.");
        body.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        body.FontSize(13.0);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog confirm;
        confirm.Title(winrt::box_value(winrt::hstring{ L"Clean up everything?" }));
        confirm.Content(body);
        confirm.PrimaryButtonText(L"Clean up");
        confirm.CloseButtonText(L"Cancel");
        confirm.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        confirm.XamlRoot(this->Content().XamlRoot());

        auto result = co_await confirm.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto setCleanupStatus = [this](winrt::hstring const& text, bool warning = false)
        {
            (void)warning;
            try
            {
                if (auto status = WipeAllDataStatusText())
                {
                    status.Text(text);
                    status.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                }
            }
            catch (...)
            {
            }
        };
        setCleanupStatus(L"Cleaning up app data...");
        if (auto button = WipeAllDataButton())
        {
            button.IsEnabled(false);
        }

        if (m_libraryScan.InProgress)
        {
            m_libraryScan.CancelRequested = true;
            ++m_libraryScan.Epoch;
            SetLibraryScanUi(false, L"", false);
        }

        ++m_homeHydration.StartupEpoch;
        ++m_homeHydration.HomeEpoch;
        ++m_homeHydration.MixRefreshId;
        ++m_browseHydrationEpoch;
        ++m_libraryHydrationEpoch;
        ++m_libraryDetailHydrationEpoch;
        ++m_searchDebounceId;
        ++m_searchRequestId;
        ++m_nowPlayingArtworkEpoch;
        ++m_lyricsHydrationEpoch;
        ++m_autoplay.Epoch;
        m_homeHydration.InFlight = false;
        m_homeHydration.Pending = false;
        m_homeHydration.PendingRefresh = false;
        m_autoplay.InFlight = false;
        m_autoplay.ResumeWhenReady = false;
        m_autoplay.SeenKeys.clear();
        m_remoteSearchCache.clear();

        if (m_lyricsHydrationTimer)
        {
            m_lyricsHydrationTimer.Stop();
        }
        if (m_volumePersistTimer)
        {
            m_volumePersistTimer.Stop();
        }
        m_volumePersistQueued = false;

        try
        {
            m_cast.Disconnect();
            ClearCastCallbacks();
        }
        catch (...)
        {
        }
        m_sink = PlaybackSink::Local;
        m_castSession = {};
        EnsureAccentBrushes();
        if (auto icon = CastIcon())
        {
            icon.Foreground(m_brushGlyphIdle);
        }

        try
        {
            AudioPlayerService().ClearTrack();
        }
        catch (...)
        {
        }
        if (m_discord)
        {
            m_discord->Clear();
        }
        try
        {
            if (m_windowSmtc)
            {
                m_windowSmtc.PlaybackStatus(winrt::Windows::Media::MediaPlaybackStatus::Stopped);
                auto updater = m_windowSmtc.DisplayUpdater();
                updater.MusicProperties().Title(L"");
                updater.MusicProperties().Artist(L"");
                updater.Thumbnail(nullptr);
                updater.Update();
            }
        }
        catch (...)
        {
        }

        bool dbCleared = true;
        bool cacheCleared = true;
        ClearSavedAppState();

        co_await winrt::resume_background();
        try
        {
            if (!DatabaseService().IsInitialized())
            {
                dbCleared = DatabaseService().Initialize();
            }
            if (dbCleared)
            {
                dbCleared = DatabaseService().ClearAllUserData();
            }
        }
        catch (...)
        {
            dbCleared = false;
        }

        try
        {
            StreamCacheService().Clear();
            std::error_code ec;
            std::filesystem::remove_all(AppDataDirectory() / L"thumbs", ec);
            cacheCleared = !ec;
        }
        catch (...)
        {
            cacheCleared = false;
        }

        co_await wil::resume_foreground(dispatcher);

        bool uiResetSucceeded = true;
        try
        {
            SettingsManagerService().Reset();
            m_pendingResumeTrack = nullptr;
            m_homeRecentHistory.clear();
            m_homePlayCounts.clear();
            m_homeLastPlayedOrder.clear();
            m_homeMixes.clear();
            m_homeRankedGenres.clear();
            m_homeGenrePools.clear();
            m_homeMixGenres.clear();
            m_catalogTracks.clear();
            m_browseAllResults.clear();
            m_librarySongAllResults.clear();
            m_libraryDetailAllResults.clear();
            m_browseMatchedCount = 0;
            m_browseMatchedSeconds = 0.0;
            m_librarySongsMatchedCount = 0;
            m_librarySongsMatchedSeconds = 0.0;
            m_libraryDetailMatchedCount = 0;
            m_browsePageLoading = false;
            m_librarySongsPageLoading = false;
            m_libraryDetailPageLoading = false;
            ++m_browsePageLoadId;
            ++m_librarySongsPageLoadId;
            ++m_libraryDetailPageLoadId;
            m_libraryStats = {};
            m_catalogLoaded = true;
            m_browseResultsValid = true;
            m_homePlaySequence = 0;
            m_queue = {};

            m_homeTracks.Clear();
            m_recentlyAddedTracks.Clear();
            m_homeMostPlayedTracks.Clear();
            m_homeLikedTracks.Clear();
            m_browseTracks.Clear();
            m_searchTracks.Clear();
            m_librarySongs.Clear();
            m_libraryGenres.Clear();
            m_manualPlaylists.Clear();
            m_autoPlaylists.Clear();
            m_sidebarPlaylists.Clear();
            m_libraryDetailTracks.Clear();
            m_upNextQueue.Clear();
            m_albums.Clear();
            m_artists.Clear();
            HideLibraryDetail();
            RefreshAutoPlaylists();
            UpdateShuffleRepeatVisuals();
            UpdateBrowseStats();
            UpdateAboutStats();
            UpdateLibraryActionButtons();

            if (auto box = MusicFolderPathBox()) box.Text(L"");
            if (auto box = ProviderBaseUrlBox()) box.Text(L"");
            if (auto box = ProviderApiKeyBox()) box.Password(L"");
            if (auto text = ProviderTestStatusText()) text.Text(L"Ready");
            if (auto box = DisplayNameBox()) box.Text(L"");
            if (auto btn = DisplayNameSaveButton()) btn.IsEnabled(false);

            auto clearArtwork = [this]()
            {
                if (auto image = BottomPlayerArt()) image.Source(nullptr);
                if (auto image = RightPanelArt()) image.Source(nullptr);
                if (auto image = FsArt()) image.Source(nullptr);
                if (auto art = BottomPlayerArt()) art.Opacity(0.0);
                if (auto art = RightPanelArt()) art.Opacity(0.0);
                if (auto art = FsArt()) art.Opacity(0.0);
                if (auto generated = BottomGeneratedArtwork()) generated.Opacity(1.0);
                if (auto generated = RightPanelGeneratedArtwork()) generated.Opacity(1.0);
                if (auto generated = FsGeneratedArtwork()) generated.Opacity(1.0);
            };
            clearArtwork();
            if (auto title = BottomPlayerTitle()) title.Text(L"Not Playing");
            if (auto artist = BottomPlayerArtist()) artist.Text(L"Select a track");
            if (auto title = NowPlayingTitle()) title.Text(L"Not Playing");
            if (auto artist = NowPlayingArtist()) artist.Text(L"Select a track");
            if (auto title = FsTitle()) title.Text(L"Not Playing");
            if (auto artist = FsArtist()) artist.Text(L"Select a track");
            if (auto title = RightPanelGeneratedTitle()) title.Text(L"MUSIC");
            if (auto caption = RightPanelGeneratedCaption()) caption.Text(L"Select a track");
            if (auto glyph = BottomGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto glyph = RightPanelGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto glyph = FsGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto text = NpMetaAlbum()) text.Text(L"\x2014");
            if (auto text = NpMetaYear()) text.Text(L"\x2014");
            if (auto text = NpMetaFormat()) text.Text(L"\x2014");
            if (auto icon = PlayPauseIcon()) icon.Glyph(L"\xE768");
            if (auto icon = FsPlayPauseIcon()) icon.Glyph(L"\xE768");
            if (auto icon = BottomLikeIcon())
            {
                icon.Glyph(L"\xEB51");
                if (m_brushGlyphIdle) icon.Foreground(m_brushGlyphIdle);
            }
            if (auto icon = FsLikeIcon())
            {
                icon.Glyph(L"\xEB51");
                if (m_brushGlyphIdle) icon.Foreground(m_brushGlyphIdle);
            }
            if (auto button = BottomLikeButton())
            {
                button.Tag(winrt::box_value(winrt::hstring{}));
            }
            ApplyPlaybackProgress(0.0, 0.0);
            if (auto t = LyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = LyricsTrackArtist()) t.Text(L"Select a track");
            if (auto t = FsLyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = FsLyricsTrackArtist()) t.Text(L"Select a track");
            ResetLyricsViewToEmpty(L"Tap Lyrics while a song is playing.");

            if (auto section = HomeRecentlyAddedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto section = HomeMostPlayedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto section = HomeLikedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto grid = HomeRecentGridView())
            {
                grid.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto empty = ListenAgainEmptyText())
            {
                empty.Text(L"Play something to see it here");
                empty.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }

            LoadSettingsIntoUi();
            ApplyUserDisplayName();
            m_loadingSettings = true;
            try
            {
                if (auto combo = OutputDeviceCombo())
                {
                    combo.SelectedIndex(0);
                }
            }
            catch (...)
            {
            }
            m_loadingSettings = false;
            co_await ApplyOutputDeviceAsync();
        }
        catch (...)
        {
            uiResetSucceeded = false;
        }

        if (auto button = WipeAllDataButton())
        {
            button.IsEnabled(true);
        }
        if (dbCleared && cacheCleared && uiResetSucceeded)
        {
            setCleanupStatus(L"Cleanup complete. Music files on disk were left untouched.");
        }
        else if (!dbCleared)
        {
            setCleanupStatus(L"UI reset, but the database could not be cleared. Close the app and try again.", true);
        }
        else if (!cacheCleared)
        {
            setCleanupStatus(L"Cleanup complete, but one cache folder could not be fully removed.", true);
        }
        else
        {
            setCleanupStatus(L"Cleanup finished, but part of the visible UI could not be refreshed.", true);
        }
    }

    // Pushes persisted settings into the Settings UI controls. Runs with
    // m_loadingSettings set so each control's change handler no-ops during
    // the initial sync.
    void MainWindow::LoadSettingsIntoUi()
    {
        m_loadingSettings = true;
        auto& s = SettingsManagerService();

        if (ShowAlbumArtSwitch())         ShowAlbumArtSwitch().IsOn(s.GetBool(L"ShowAlbumArt", true));
        if (WindowsMediaControlsSwitch()) WindowsMediaControlsSwitch().IsOn(s.GetBool(L"WindowsMediaControls", true));
        if (GaplessSwitch())              GaplessSwitch().IsOn(s.GetBool(L"Gapless", true));
        if (AutoplaySwitch())             AutoplaySwitch().IsOn(s.GetBool(L"Autoplay", true));
        if (DiscordPresenceSwitch())      DiscordPresenceSwitch().IsOn(s.GetBool(L"DiscordPresence", false));

        if (CloseBehaviorCombo())         CloseBehaviorCombo().SelectedIndex(s.GetInt(L"CloseBehavior", 0));

        {
            std::unordered_set<std::wstring> exts;
            for (auto const& ext : ScanFileExtensions())
            {
                exts.insert(ToLowerCopy(ext));
            }
            auto has = [&](wchar_t const* e) { return exts.find(e) != exts.end(); };
            if (FmtMp3())  FmtMp3().IsChecked(has(L".mp3"));
            if (FmtFlac()) FmtFlac().IsChecked(has(L".flac"));
            if (FmtWav())  FmtWav().IsChecked(has(L".wav"));
            if (FmtM4a())  FmtM4a().IsChecked(has(L".m4a"));
            if (FmtAac())  FmtAac().IsChecked(has(L".aac"));
            if (FmtOgg())  FmtOgg().IsChecked(has(L".ogg"));
            if (FmtOpus()) FmtOpus().IsChecked(has(L".opus"));
            if (FmtWma())  FmtWma().IsChecked(has(L".wma"));
        }

        ApplyShowAlbumArt();
        ApplyWindowsMediaControls();
        ApplyThemeFromSetting();
        ApplyAccentColor(s.GetString(L"AccentColor", L"#FF0097B2"));

        m_loadingSettings = false;
    }

    void MainWindow::BuildEqualizerBars()
    {
        const wchar_t* freqs[] = { L"32", L"64", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };

        auto host = EqualizerBars();
        host.Children().Clear();
        m_equalizerSliders.clear();

        // Defer the EQ attach until the user actually drags a band. Calling
        // AddAudioEffect at startup added activation overhead and risked
        // perturbing MediaPlayer before its Source was set; lazy-attach via
        // UpdateEqualizerBand keeps the cold path identical to pre-EQ.

        for (int i = 0; i < 10; ++i)
        {
            winrt::Microsoft::UI::Xaml::Controls::StackPanel column;
            column.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Vertical);
            column.Spacing(8.0);
            column.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);

            winrt::Microsoft::UI::Xaml::Controls::Slider bar;
            bar.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Vertical);
            bar.Minimum(-12.0);
            bar.Maximum(12.0);
            bar.Value(0.0);
            bar.StepFrequency(1.0);
            bar.Height(132.0);
            bar.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            bar.IsEnabled(false);

            int bandIndex = i;
            bar.ValueChanged([this, bandIndex](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
            {
                (void)sender;
                double value = args.NewValue();
                AudioPlayerService().UpdateEqualizerBand(bandIndex, value);
                if (!m_loadingSettings)
                {
                    wchar_t key[16];
                    std::swprintf(key, 16, L"EqBand%d", bandIndex);
                    SettingsManagerService().SetDouble(winrt::hstring{ key }, value);
                    // Re-check which preset chip should be highlighted now
                    // that the user has nudged a band. Suppressed while a
                    // preset click is programmatically driving the sliders
                    // (m_skipPresetSync), so partial sequences of band
                    // updates do not flicker the chip selection.
                    SyncActiveEqualizerPreset();
                }
            });

            winrt::Microsoft::UI::Xaml::Controls::TextBlock lbl;
            lbl.Text(winrt::hstring(freqs[i]));
            lbl.FontSize(11.0);
            lbl.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            EnsureAccentBrushes();
            if (m_brushGlyphIdle)
            {
                lbl.Foreground(m_brushGlyphIdle);
            }

            column.Children().Append(bar);
            column.Children().Append(lbl);
            host.Children().Append(column);
            m_equalizerSliders.push_back(bar);
        }
    }

    void MainWindow::ApplyEqualizerFromSettings()
    {
        m_loadingSettings = true;
        double gains[10]{};
        for (int i = 0; i < 10; ++i)
        {
            wchar_t key[16];
            std::swprintf(key, 16, L"EqBand%d", i);
            gains[i] = SettingsManagerService().GetDouble(winrt::hstring{ key }, 0.0);
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                m_equalizerSliders[i].Value(gains[i]);
            }
        }
        AudioPlayerService().ApplyEqualizerBands(gains);

        // Preamp (slider range is [-12, 0]). The slider ValueChanged also
        // pushes the value to AudioPlayer, but call SetEqualizerPreamp
        // explicitly so the effect picks the value up even if the slider
        // didn't move (Slider.Value(x) on a slider already at x is a no-op).
        double preamp = SettingsManagerService().GetDouble(L"EqPreamp", 0.0);
        if (preamp < -12.0) preamp = -12.0;
        if (preamp > 0.0) preamp = 0.0;
        if (auto sl = EqPreampSlider())
        {
            sl.Value(preamp);
        }
        AudioPlayerService().SetEqualizerPreamp(preamp);

        // Attach the effect even when every band is at 0 dB. The effect
        // short-circuits in passthrough mode, so the cost is negligible,
        // but having it in the chain from startup means slider drags never
        // trigger a mid-playback graph reconfigure.
        AudioPlayerService().EnsureEqualizerAttached();
        m_loadingSettings = false;
        SyncActiveEqualizerPreset();
    }

    void MainWindow::ApplyEqualizerPreset(std::array<double, 10> const& gains)
    {
        // Drive the sliders; ValueChanged persists each band and pushes it to
        // the effect's PropertySet. m_skipPresetSync is set by the caller so
        // intermediate band states do not flicker the chip selection.
        for (int i = 0; i < 10; ++i)
        {
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                m_equalizerSliders[i].Value(gains[static_cast<size_t>(i)]);
            }
        }
    }

    namespace
    {
        // Canonical gain shapes for the named presets. The detection routine
        // below picks the matching preset name; mismatches fall through to
        // "Custom". Keep in sync with EqualizerPreset_Click's gain arrays.
        struct PresetShape
        {
            wchar_t const* Tag;
            std::array<double, 10> Gains;
        };

        wchar_t const* DetectActivePresetForGains(std::array<double, 10> const& gains)
        {
            static const PresetShape shapes[] = {
                { L"Flat",       { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
                { L"Bass",       { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 } },
                { L"Treble",     { 0, 0, 0, 0, 0, 1, 3, 5, 6, 6 } },
                { L"Vocal",      { -2, -1, 0, 1, 3, 4, 4, 2, 0, -1 } },
                { L"Acoustic",   { 2, 3, 2, 1, 1, 2, 3, 4, 3, 2 } },
                { L"Electronic", { 4, 4, 2, 0, -2, 0, 1, 3, 4, 5 } },
            };
            auto matches = [&](std::array<double, 10> const& p)
            {
                for (size_t i = 0; i < 10; ++i)
                {
                    // EQ step is 1 dB; an epsilon below 0.5 keeps detection
                    // exact for integer presets while tolerating float drift
                    // from persisted/restored values.
                    if (std::abs(gains[i] - p[i]) > 0.25) return false;
                }
                return true;
            };
            for (auto const& s : shapes)
            {
                if (matches(s.Gains)) return s.Tag;
            }
            return L"Custom";
        }
    }

    void MainWindow::SyncActiveEqualizerPreset()
    {
        // Callers (notably ApplyEqualizerPreset's slider drives) set
        // m_skipPresetSync so intermediate per-band states do not flicker
        // the chip selection through "Custom" while a preset is being
        // applied one band at a time.
        if (m_skipPresetSync) return;

        std::array<double, 10> gains{};
        for (int i = 0; i < 10; ++i)
        {
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                gains[static_cast<size_t>(i)] = m_equalizerSliders[i].Value();
            }
        }
        auto active = DetectActivePresetForGains(gains);

        struct Chip
        {
            winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton Btn;
            wchar_t const* Tag;
        };
        Chip chips[] = {
            { EqPresetFlat(),       L"Flat" },
            { EqPresetBass(),       L"Bass" },
            { EqPresetTreble(),     L"Treble" },
            { EqPresetVocal(),      L"Vocal" },
            { EqPresetAcoustic(),   L"Acoustic" },
            { EqPresetElectronic(), L"Electronic" },
            { EqPresetCustom(),     L"Custom" },
        };
        // Chips fire Click only on user input, not on programmatic
        // IsChecked changes — no recursion guard needed here.
        for (auto const& c : chips)
        {
            if (!c.Btn) continue;
            bool shouldCheck = (std::wcscmp(active, c.Tag) == 0);
            c.Btn.IsChecked(shouldCheck);
        }
    }

    void MainWindow::EqualizerPreset_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto btn = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!btn) return;

        // ToggleButton.Click fires after the framework has flipped IsChecked.
        // Force it back to checked so clicking an already-active chip does
        // not un-highlight it (chips model exclusive selection, not toggle).
        btn.IsChecked(true);

        // Uncheck the other preset chips so only one stays "active".
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton presets[] = {
            EqPresetFlat(), EqPresetBass(), EqPresetTreble(), EqPresetVocal(),
            EqPresetAcoustic(), EqPresetElectronic(), EqPresetCustom()
        };
        for (auto const& p : presets)
        {
            if (p && p != btn)
            {
                p.IsChecked(false);
            }
        }

        auto tag = ReadTagString(btn.Tag());
        if (tag == L"Custom")
        {
            // Custom is IsHitTestVisible=False; user clicks can't reach here.
            // Kept as defensive no-op in case the chip is enabled later.
            return;
        }

        // Bands are 32, 64, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz. Presets
        // are illustrative shapes — feel free to tune later.
        std::array<double, 10> gains{};
        if (tag == L"Bass") gains = { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 };
        else if (tag == L"Treble") gains = { 0, 0, 0, 0, 0, 1, 3, 5, 6, 6 };
        else if (tag == L"Vocal") gains = { -2, -1, 0, 1, 3, 4, 4, 2, 0, -1 };
        else if (tag == L"Acoustic") gains = { 2, 3, 2, 1, 1, 2, 3, 4, 3, 2 };
        else if (tag == L"Electronic") gains = { 4, 4, 2, 0, -2, 0, 1, 3, 4, 5 };
        // "Flat" (and anything else) → all zeros.

        // Drive sliders under skip-sync so partial intermediate band states
        // do not bounce the chip selection mid-apply through "Custom".
        m_skipPresetSync = true;
        ApplyEqualizerPreset(gains);
        m_skipPresetSync = false;
    }

    void MainWindow::EqResetButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        // Zero every band (ValueChanged persists each EqBandN).
        std::array<double, 10> zeros{};
        m_skipPresetSync = true;
        ApplyEqualizerPreset(zeros);
        m_skipPresetSync = false;

        // Zero the preamp (ValueChanged persists EqPreamp).
        if (auto sl = EqPreampSlider())
        {
            sl.Value(0.0);
        }

        // Land on Flat now that everything is zeroed.
        SyncActiveEqualizerPreset();
    }

    void MainWindow::EqPreampSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        (void)sender;
        double value = args.NewValue();
        AudioPlayerService().SetEqualizerPreamp(value);

        if (auto text = EqPreampValueText())
        {
            wchar_t buf[16]{};
            if (std::abs(value) < 0.0001)
            {
                std::swprintf(buf, 16, L"0 dB");
            }
            else
            {
                // Slider clamps to [-12, 0]; format with one decimal if
                // needed but most user values land on integer steps.
                std::swprintf(buf, 16, L"%g dB", value);
            }
            text.Text(winrt::hstring{ buf });
        }

        if (!m_loadingSettings)
        {
            SettingsManagerService().SetDouble(L"EqPreamp", value);
        }
    }

}
