#pragma once

#include "core/async/Result.h"
#include "core/share/ShareLinks.h"

#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace lmp::ui {

class ToastQueue;

// Share links (`App.share`): makes the app's own link for a song, a list of
// songs or an artist through the account, copies the text and the link to
// the clipboard and says "Link copied". The link opens the app for members
// and a preview page for anyone else, and never names where the music comes
// from. One share at a time; a second press while one is on its way is
// ignored.
//
// GUI thread only.
class ShareController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.share")

    // Whether links can be made now (Account mode, signed in).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)

public:
    // Sends POST /v1/music/shares with `body` in the account's name.
    using Send = std::function<QFuture<core::Result<QByteArray>>(const QByteArray& body)>;
    // Puts text on the clipboard; replaced in tests.
    using Copy = std::function<bool(const QString& text)>;

    // `toasts` outlives the controller.
    ShareController(Send send, std::function<bool()> available, ToastQueue& toasts, Copy copy = {},
                    QObject* parent = nullptr);

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool busy() const { return m_busy; }

    // `failure` is the toast when no link comes back ("Could not share this song.").
    void share(const core::share::ShareRequest& request, const QString& failure);

    // The availability may have changed (mode, sign-in).
    void refresh() { emit availableChanged(); }

signals:
    void availableChanged();

private:
    Send m_send;
    std::function<bool()> m_available;
    ToastQueue& m_toasts;
    Copy m_copy;
    bool m_busy = false;
};

} // namespace lmp::ui
