#include "platform/windows/WindowsNetworkStatus.h"

#include <QMetaObject>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include <memory>
#include <mutex>
#include <utility>

namespace lmp::platform::windows {

namespace {

namespace connectivity = winrt::Windows::Networking::Connectivity;

[[nodiscard]] connectivity::ConnectionProfile internetProfile() noexcept
{
    try {
        return connectivity::NetworkInformation::GetInternetConnectionProfile();
    } catch (...) {
        return nullptr;
    }
}

} // namespace

// The NetworkStatusChanged registration and the link the Windows callback
// uses. The callback may still be running on a worker thread while this
// object is destroyed, so it reaches the object only under the lock, and the
// destructor clears the link under the same lock first.
struct WindowsNetworkStatus::Subscription {
    struct Link {
        std::mutex lock;
        WindowsNetworkStatus* target = nullptr;
    };
    std::shared_ptr<Link> link = std::make_shared<Link>();
    winrt::event_token token{};
};

WindowsNetworkStatus::WindowsNetworkStatus(QObject* parent)
    : QObject(parent)
    , m_subscription(std::make_unique<Subscription>())
{
    m_subscription->link->target = this;
    try {
        m_subscription->token = connectivity::NetworkInformation::NetworkStatusChanged(
            [link = m_subscription->link](const winrt::Windows::Foundation::IInspectable&) {
                // Posting a queued call is safe from any thread while the
                // target exists; Qt drops it if the target is deleted later.
                const std::scoped_lock guard(link->lock);
                if (link->target != nullptr) {
                    QMetaObject::invokeMethod(link->target, &WindowsNetworkStatus::notify, Qt::QueuedConnection);
                }
            });
    } catch (...) {
        m_subscription->token = {};
    }
}

WindowsNetworkStatus::~WindowsNetworkStatus()
{
    {
        const std::scoped_lock guard(m_subscription->link->lock);
        m_subscription->link->target = nullptr;
    }
    if (m_subscription->token.value != 0) {
        try {
            connectivity::NetworkInformation::NetworkStatusChanged(m_subscription->token);
        } catch (...) {
        }
    }
}

bool WindowsNetworkStatus::online() const
{
    const connectivity::ConnectionProfile profile = internetProfile();
    if (!profile) {
        return false;
    }
    try {
        return profile.GetNetworkConnectivityLevel() == connectivity::NetworkConnectivityLevel::InternetAccess;
    } catch (...) {
        return false;
    }
}

bool WindowsNetworkStatus::metered() const
{
    const connectivity::ConnectionProfile profile = internetProfile();
    if (!profile) {
        return false;
    }
    try {
        const connectivity::ConnectionCost cost = profile.GetConnectionCost();
        if (!cost) {
            return false;
        }
        const connectivity::NetworkCostType type = cost.NetworkCostType();
        return type == connectivity::NetworkCostType::Fixed || type == connectivity::NetworkCostType::Variable
            || cost.Roaming() || cost.OverDataLimit();
    } catch (...) {
        return false;
    }
}

void WindowsNetworkStatus::setChangeHandler(ChangeHandler handler)
{
    m_handler = std::move(handler);
}

void WindowsNetworkStatus::notify()
{
    if (m_handler) {
        m_handler(online(), metered());
    }
}

} // namespace lmp::platform::windows
