#pragma once

#include <QObject>
#include <QString>

#include "core/playback/PlayerController.h"

namespace lmp::ui {

class ToastQueue;

// Shows the player's notices about remote songs as toasts, in the words
// earlier versions used.
//
// GUI thread only.
class PlaybackNoticeToasts final : public QObject {
    Q_OBJECT

public:
    // Both outlive this object.
    PlaybackNoticeToasts(core::PlayerController& player, ToastQueue& toasts, QObject* parent = nullptr);

    // The toast text for a notice about `title` (may be empty).
    [[nodiscard]] static QString text(core::PlayerController::PlaybackNotice notice, const QString& title);

private:
    ToastQueue& m_toasts;
};

} // namespace lmp::ui
