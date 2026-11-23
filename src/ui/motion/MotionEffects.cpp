#include "ui/motion/MotionEffects.h"

#include "core/model/Track.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QVariantMap>

#include <algorithm>

namespace lmp::ui {

namespace {

[[nodiscard]] qint64 steadyNow()
{
    static QElapsedTimer clock = []() {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}

} // namespace

MotionEffects::MotionEffects(std::function<qint64()> now, QObject* parent)
    : QObject(parent)
    , m_now(now ? std::move(now) : std::function<qint64()>(&steadyNow))
{
}

void MotionEffects::addQueueTarget(QObject* item, qreal fractionX, int priority)
{
    if (item == nullptr) {
        return;
    }
    m_targets.removeIf([item](const Target& target) { return target.item == nullptr || target.item == item; });
    m_targets.append(Target{item, fractionX, priority});
    std::stable_sort(m_targets.begin(), m_targets.end(),
                     [](const Target& a, const Target& b) { return a.priority > b.priority; });
}

QVariantList MotionEffects::queueTargets() const
{
    QVariantList list;
    for (const Target& target : m_targets) {
        if (target.item != nullptr) {
            list.append(QVariantMap{{QStringLiteral("item"), QVariant::fromValue<QObject*>(target.item.data())},
                                    {QStringLiteral("fractionX"), target.fractionX}});
        }
    }
    return list;
}

void MotionEffects::setWindowBackdrop(QQuickItem* item)
{
    if (m_windowBackdrop != item) {
        m_windowBackdrop = item;
        emit backdropsChanged();
    }
}

void MotionEffects::setFullscreenBackdrop(QQuickItem* item)
{
    if (m_fullscreenBackdrop != item) {
        m_fullscreenBackdrop = item;
        emit backdropsChanged();
    }
}

void MotionEffects::heartLiked(QObject* heart)
{
    if (heart != nullptr) {
        emit heartBurst(heart);
    }
}

void MotionEffects::collectionSaved(QObject* button)
{
    if (button != nullptr) {
        emit saveBurst(button);
    }
}

void MotionEffects::notePress(const QPointF& windowPosition)
{
    m_press = Press{m_now(), windowPosition};
}

void MotionEffects::trackQueued(const core::Track& track)
{
    if (!m_press) {
        return;
    }
    const Press press = *m_press;
    m_press.reset();
    if (m_now() - press.at > PressWindowMs) {
        return;
    }
    const QString title = track.title.trimmed();
    emit queueArc(press.position, QUrl(track.artworkUrl), title,
                  title.isEmpty() ? QString() : QString(title.at(0)).toUpper());
}

bool MotionEffects::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        notePress(static_cast<QMouseEvent*>(event)->scenePosition());
    }
    return QObject::eventFilter(watched, event);
}

} // namespace lmp::ui
