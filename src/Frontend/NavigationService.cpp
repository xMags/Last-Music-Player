#include "pch.h"
#include "Frontend/NavigationService.h"

namespace LastMusicPlayer::Frontend
{
    void NavigationService::NavigateTo(NavPage page)
    {
        if (m_currentPage != page)
        {
            m_currentPage = page;
            if (m_navigationChangedCallback)
                m_navigationChangedCallback(m_currentPage);
        }
    }

    NavPage NavigationService::GetCurrentPage() const
    {
        return m_currentPage;
    }

    void NavigationService::OnNavigationChanged(NavigationChangedCallback callback)
    {
        m_navigationChangedCallback = std::move(callback);
    }
}
