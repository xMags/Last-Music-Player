#include "ui/share/ShareController.h"

#include "ui/toasts/ToastQueue.h"

#include <QClipboard>
#include <QGuiApplication>

namespace lmp::ui {

namespace share = core::share;

namespace {

bool copyToClipboard(const QString& text)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }
    clipboard->setText(text);
    return true;
}

} // namespace

ShareController::ShareController(Send send, std::function<bool()> available, ToastQueue& toasts, Copy copy,
                                 QObject* parent)
    : QObject(parent)
    , m_send(std::move(send))
    , m_available(std::move(available))
    , m_toasts(toasts)
    , m_copy(copy ? std::move(copy) : Copy(&copyToClipboard))
{
}

bool ShareController::available() const
{
    return m_send && m_available && m_available();
}

void ShareController::share(const share::ShareRequest& request, const QString& failure)
{
    if (m_busy || !available()) {
        return;
    }
    const std::optional<QByteArray> body = share::shareRequestBody(request);
    if (!body) {
        m_toasts.show(failure, true);
        return;
    }
    m_busy = true;
    const QString text = share::shareText(request);
    m_send(*body).then(this, [this, text, failure](const core::Result<QByteArray>& answer) {
        m_busy = false;
        const std::optional<share::ShareLink> link =
            answer ? share::parseShareLink(answer.value()) : std::optional<share::ShareLink>();
        if (!link) {
            m_toasts.show(failure, true);
            return;
        }
        if (!m_copy(text + QLatin1Char('\n') + link->url)) {
            m_toasts.show(failure, true);
            return;
        }
        m_toasts.show(tr("Link copied"));
    });
}

} // namespace lmp::ui
