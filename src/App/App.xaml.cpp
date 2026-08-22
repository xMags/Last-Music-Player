#include "pch.h"
#include "App.xaml.h"
#include "MainWindow/MainWindow.xaml.h"

#include <windows.h>
#include <shobjidl.h>       // IShellLink, IPropertyStore, SetCurrentProcessExplicitAppUserModelID
#include <shlobj.h>         // SHGetKnownFolderPath, FOLDERID_Programs
#include <propkey.h>        // PKEY_AppUserModel_ID
#include <propvarutil.h>    // InitPropVariantFromString
#include <WindowsAppSDK-VersionInfo.h>
#include <MddBootstrap.h>
#include <cstdio>
#include <exception>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::Last_Music_Player::implementation
{
    static wchar_t const* SingleInstanceMutexName()
    {
        return L"Local\\LastMusicPlayer.SingleInstance";
    }

    static wchar_t const* MainWindowTitle()
    {
        return L"Last Music Player";
    }

    static HANDLE g_singleInstanceMutex{};

    // The Start Menu shortcut (with its AppUserModelID property, so Windows
    // resolves "Last Music Player" + icon in the SMTC / Win+V media card) is
    // created by the Inno Setup installer's [Icons] section, which owns and
    // cleans it up. The process AUMID set in wWinMain
    // (SetCurrentProcessExplicitAppUserModelID) must match that shortcut.

    static bool TryAcquireSingleInstance()
    {
        g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, SingleInstanceMutexName());
        if (!g_singleInstanceMutex)
        {
            return true;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(g_singleInstanceMutex);
            g_singleInstanceMutex = nullptr;
            return false;
        }

        return true;
    }

    static void ReleaseSingleInstance()
    {
        if (g_singleInstanceMutex)
        {
            ReleaseMutex(g_singleInstanceMutex);
            CloseHandle(g_singleInstanceMutex);
            g_singleInstanceMutex = nullptr;
        }
    }

    static void BringExistingInstanceToForeground()
    {
        HWND hwnd = FindWindowW(nullptr, MainWindowTitle());
        if (!hwnd)
        {
            return;
        }

        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
    }

    /// <summary>
    /// Initializes the singleton application object.  This is the first line of authored code
    /// executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
#endif

            e.Handled(true);
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

#ifdef DISABLE_XAML_GENERATED_MAIN
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    using winrt::Last_Music_Player::implementation::BringExistingInstanceToForeground;
    using winrt::Last_Music_Player::implementation::ReleaseSingleInstance;
    using winrt::Last_Music_Player::implementation::TryAcquireSingleInstance;

    if (!TryAcquireSingleInstance())
    {
        BringExistingInstanceToForeground();
        return 0;
    }

    // Register an explicit AppUserModelID so the OS can label our SMTC
    // session (Win+V quick-settings) with the app name instead of
    // "Unknown app" + the .exe path. Unpackaged Win32 apps have no
    // package identity, and Windows uses AUMID as the fallback key for
    // resolving display name + icon. Must run before any window is
    // created, hence early in wWinMain.
    SetCurrentProcessExplicitAppUserModelID(L"LastMusicPlayer.App");

    try
    {
#ifndef MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED
        // Framework-dependent builds must locate an installed Windows App SDK
        // runtime via the bootstrapper. Self-contained builds ship the runtime
        // DLLs alongside the .exe, so the bootstrapper is unnecessary (and would
        // fail on a clean PC that has no installed framework) — skip it.
        PACKAGE_VERSION minVersion{};
        minVersion.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
        winrt::check_hresult(MddBootstrapInitialize2(
            WINDOWSAPPSDK_RELEASE_MAJORMINOR,
            WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
            minVersion,
            MddBootstrapInitializeOptions_OnPackageIdentity_NOOP));
#endif

        winrt::init_apartment(winrt::apartment_type::single_threaded);

        ::winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&)
            {
                ::winrt::make<::winrt::Last_Music_Player::implementation::App>();
            });

        ReleaseSingleInstance();
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        ReleaseSingleInstance();
        return error.code().value;
    }
    catch (std::exception const&)
    {
        ReleaseSingleInstance();
        return -1;
    }
    catch (...)
    {
        ReleaseSingleInstance();
        return -1;
    }
}
#endif
