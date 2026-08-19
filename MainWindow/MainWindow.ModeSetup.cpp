#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        winrt::Microsoft::UI::Xaml::Controls::TextBlock MakeStatusLine(
            winrt::hstring const& initialText)
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock status;
            status.Text(initialText);
            status.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
            status.FontSize(12.0);
            return status;
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResyncRemoteModeSelectorAsync()
    {
        auto lifetime = get_strong();

        // RadioButtons resolves a selection by checking one child and unchecking
        // the others, and each of those raises SelectionChanged in turn. Writing
        // SelectedIndex while that pass is still running re-enters it, and the
        // events it produces arrive after RefreshAccountSettingsUi has already
        // cleared m_suppressRemoteModeChange -- so the handler runs again on an
        // item the user never picked and commits that mode. Yielding one turn
        // first lets the control settle, which puts the write back inside the
        // guard where it belongs.
        co_await wil::resume_foreground(DispatcherQueue());
        RefreshAccountSettingsUi();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::BeginAccountSignInAsync()
    {
        auto lifetime = get_strong();
        co_await ResyncRemoteModeSelectorAsync();
        co_await ShowAccountSignInDialogAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ShowAccountSignInDialogAsync()
    {
        auto lifetime = get_strong();
        if (!AccountSessionService().IsAccountIntegrationAvailable())
        {
            co_return;
        }

        winrt::Microsoft::UI::Xaml::Controls::StackPanel body;
        body.Spacing(8.0);
        body.Width(340.0);

        auto blurb = MakeStatusLine(
            L"Sign in to use your account library on this PC. Your browser opens to "
            L"complete sign-in.");
        auto status = MakeStatusLine(L"");
        body.Children().Append(blurb);
        body.Children().Append(status);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring{ L"Switch to Account mode" }));
        dialog.Content(body);
        dialog.PrimaryButtonText(L"Sign in");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dialog.XamlRoot(this->Content().XamlRoot());

        // The dialog stays open across the browser round trip so a failed or
        // abandoned sign-in leaves the user somewhere they can retry.
        // The sender is used rather than a captured dialog: the dialog owns this
        // handler, so capturing it would make the pair keep each other alive.
        dialog.PrimaryButtonClick(
            [lifetime, this, status](
                winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& sender,
                winrt::Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const& args)
                -> winrt::fire_and_forget
            {
                // Cancel must be set before the first suspension point, or the
                // dialog closes while the sign-in is still running.
                args.Cancel(true);
                auto deferral = args.GetDeferral();
                auto host = sender;
                host.IsPrimaryButtonEnabled(false);
                auto succeeded = co_await CompleteAccountSignInAsync(status);
                host.IsPrimaryButtonEnabled(true);
                deferral.Complete();
                if (succeeded)
                {
                    host.Hide();
                }
            });

        co_await dialog.ShowAsync();
        RefreshAccountSettingsUi();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::PlayProviderTest_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();

        auto baseUrl = ProviderBaseUrlBox().Text();
        auto apiKey = ProviderApiKeyBox().Password();
        auto status = ProviderTestStatusText();
        if (baseUrl.empty())
        {
            status.Text(L"Missing provider URL");
            co_return;
        }
        if (apiKey.empty())
        {
            status.Text(L"Enter a new API key");
            co_return;
        }

        // Locking the inputs is what makes the credential the test ran against
        // the same one that gets stored, without re-reading the controls after
        // the await.
        ProviderTestButton().IsEnabled(false);
        ProviderBaseUrlBox().IsEnabled(false);
        ProviderApiKeyBox().IsEnabled(false);
        auto connected = co_await ConnectApiKeyProviderAsync(baseUrl, apiKey, status);
        ProviderBaseUrlBox().IsEnabled(true);
        ProviderApiKeyBox().IsEnabled(true);
        ProviderTestButton().IsEnabled(true);
        if (connected)
        {
            ProviderApiKeyBox().Password(L"");
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::ConnectApiKeyProviderAsync(
        winrt::hstring baseUrl,
        winrt::hstring apiKey,
        winrt::Microsoft::UI::Xaml::Controls::TextBlock status)
    {
        auto lifetime = get_strong();
        status.Text(L"Connecting...");

        try
        {
            auto httpStatus = co_await RemoteMusicServiceService().TestApiKeyAsync(baseUrl, apiKey);
            if (httpStatus != 200)
            {
                status.Text(httpStatus == 401 ? L"Unauthorized" : L"Provider unavailable");
                co_return false;
            }

            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                status.Text(L"Cleanup is in progress");
                co_return false;
            }

            if (!CredentialStoreService().WriteProviderApiKey(apiKey)
                || CredentialStoreService().ReadProviderApiKey() != apiKey)
            {
                status.Text(L"Could not store API key");
                co_return false;
            }
            WriteAppSettingString(L"ProviderBaseUrl", baseUrl);
            RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::ApiKey);
            InvalidateRemoteScopeWork();
            DatabaseService().SetRemoteLibraryContext(L"ApiKey");
            m_remoteSearchCache.clear();
            status.Text(L"Configured");
            RefreshAccountSettingsUi();
            operationLease.reset();
            co_await HydrateHomeAsync(true);
            co_return true;
        }
        catch (winrt::hresult_error const&)
        {
            status.Text(L"Provider unavailable");
            co_return false;
        }
        catch (...)
        {
            status.Text(L"Provider unavailable");
            co_return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::CompleteAccountSignInAsync(
        winrt::Microsoft::UI::Xaml::Controls::TextBlock status)
    {
        auto lifetime = get_strong();
        auto operationGeneration = UserDataOperationGateService().Generation();
        status.Text(L"Opening browser sign-in...");

        auto signedIn = co_await AccountSessionService().SignInAsync();
        auto snapshot = AccountSessionService().Snapshot();
        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease || !UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            status.Text(L"Sign-in could not be completed.");
            RefreshAccountSettingsUi();
            co_return false;
        }
        if (!signedIn
            || snapshot.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
            || snapshot.Profile.Id.empty())
        {
            status.Text(snapshot.LastSafeError.empty()
                ? winrt::hstring{ L"Sign-in was not completed." }
                : snapshot.LastSafeError);
            RefreshAccountSettingsUi();
            co_return false;
        }

        try
        {
            // Everything the session hands back is re-checked against the
            // snapshot the sign-in produced: another sign-in or sign-out can
            // land between these awaits, and activating the wrong owner's
            // library would mix two accounts' data.
            auto session = AccountSessionService().CaptureOperation();
            if (session.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
                || session.Generation != snapshot.Generation
                || session.OwnerId != snapshot.Profile.Id)
            {
                throw winrt::hresult_canceled();
            }
            if (!RemoteMusicServiceService().SetMode(
                LastMusicPlayer::Backend::RemoteAccessMode::Account))
            {
                throw winrt::hresult_error(E_NOT_VALID_STATE, L"Account mode is unavailable.");
            }

            InvalidateRemoteScopeWork();
            auto context = RemoteMusicServiceService().CaptureAccountSyncContext();
            auto remoteScope = RemoteMusicServiceService().CaptureScope();
            if (context.OwnerId() != snapshot.Profile.Id
                || remoteScope.AccountGeneration != snapshot.Generation
                || !RemoteMusicServiceService().IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            if (!DatabaseService().SetRemoteLibraryContext(
                L"Account",
                std::wstring(context.OwnerId().c_str())))
            {
                throw winrt::hresult_error(E_FAIL, L"Could not activate the account library.");
            }

            operationLease.reset();
            co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Interactive);
            co_return true;
        }
        catch (winrt::hresult_canceled const&)
        {
            auto current = AccountSessionService().Snapshot();
            if ((current.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
                    && current.Status != LastMusicPlayer::Backend::AccountSessionStatus::Offline)
                || current.Profile.Id.empty())
            {
                RemoteMusicServiceService().SetMode(
                    LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
                InvalidateRemoteScopeWork();
                (void)DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
            }
            status.Text(L"Sign-in could not be completed.");
            RefreshAccountSettingsUi();
            co_return false;
        }
        catch (...)
        {
            RemoteMusicServiceService().SetMode(
                LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
            InvalidateRemoteScopeWork();
            (void)DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
            operationLease.reset();
            status.Text(L"Could not activate the account library.");
            RefreshAccountSettingsUi();
            co_return false;
        }
    }
}
