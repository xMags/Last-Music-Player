#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace LastMusicPlayer::Backend
{
    // Minimal Shell_NotifyIcon tray helper backed by a message-only window.
    // Used for the "minimize to tray" close behavior.
    class TrayIcon
    {
    public:
        TrayIcon() = default;
        ~TrayIcon();

        TrayIcon(TrayIcon const&) = delete;
        TrayIcon& operator=(TrayIcon const&) = delete;

        // Creates the hidden window and adds the notification icon.
        bool Create(HICON icon, std::wstring const& tooltip);
        void Remove();
        bool IsActive() const { return m_added; }

        // Invoked on the UI thread (the thread that called Create).
        std::function<void()> OnRestore;
        std::function<void()> OnExit;

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        void ShowContextMenu();

        HWND m_hwnd{ nullptr };
        HICON m_icon{ nullptr };
        bool m_added{ false };
    };
}
