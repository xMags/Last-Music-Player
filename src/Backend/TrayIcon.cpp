#include "pch.h"
#include "Backend/TrayIcon.h"
#include <shellapi.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

namespace
{
    constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    constexpr UINT kTrayIconId = 1;
    constexpr UINT kMenuRestore = 101;
    constexpr UINT kMenuExit = 102;
    constexpr wchar_t kWindowClassName[] = L"LastMusicPlayerTrayWindow";
}

namespace LastMusicPlayer::Backend
{
    TrayIcon::~TrayIcon()
    {
        Remove();
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    bool TrayIcon::Create(HICON icon, std::wstring const& tooltip)
    {
        if (m_added)
        {
            return true;
        }

        m_icon = icon;

        HINSTANCE instance = GetModuleHandleW(nullptr);

        static bool s_classRegistered = false;
        if (!s_classRegistered)
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &TrayIcon::WndProc;
            wc.hInstance = instance;
            wc.lpszClassName = kWindowClassName;
            if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return false;
            }
            s_classRegistered = true;
        }

        m_hwnd = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, instance, nullptr);
        if (!m_hwnd)
        {
            return false;
        }
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kTrayIconId;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayCallbackMessage;
        nid.hIcon = m_icon;
        wcsncpy_s(nid.szTip, tooltip.c_str(), _TRUNCATE);

        if (!Shell_NotifyIconW(NIM_ADD, &nid))
        {
            return false;
        }
        m_added = true;
        return true;
    }

    void TrayIcon::Remove()
    {
        if (!m_added)
        {
            return;
        }
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }

    void TrayIcon::ShowContextMenu()
    {
        POINT pt{};
        GetCursorPos(&pt);

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, kMenuRestore, L"Restore");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

        // Required so the menu dismisses correctly when clicking elsewhere.
        SetForegroundWindow(m_hwnd);
        UINT cmd = TrackPopupMenu(menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
            pt.x, pt.y, 0, m_hwnd, nullptr);
        DestroyMenu(menu);

        if (cmd == kMenuRestore)
        {
            if (OnRestore) OnRestore();
        }
        else if (cmd == kMenuExit)
        {
            if (OnExit) OnExit();
        }
    }

    LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && msg == kTrayCallbackMessage)
        {
            switch (LOWORD(lParam))
            {
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                if (self->OnRestore) self->OnRestore();
                return 0;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                self->ShowContextMenu();
                return 0;
            default:
                break;
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
