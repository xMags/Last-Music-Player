#pragma once
#include <string>
#include <functional>

namespace LastMusicPlayer::Frontend
{
    enum class NavPage
    {
        Home,
        Browse,
        Library,
        Settings
    };

    class NavigationService
    {
    public:
        NavigationService() = default;
        ~NavigationService() = default;

        void NavigateTo(NavPage page);
        NavPage GetCurrentPage() const;

        // Callback when navigation changes
        using NavigationChangedCallback = std::function<void(NavPage)>;
        void OnNavigationChanged(NavigationChangedCallback callback);

    private:
        NavPage m_currentPage{ NavPage::Home };
        NavigationChangedCallback m_navigationChangedCallback;
    };
}
