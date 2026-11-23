#include "ui/motion/CoverFlight.h"

#include <QElapsedTimer>

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

CoverFlight::CoverFlight(std::function<qint64()> now, QObject* parent)
    : QObject(parent)
    , m_now(now ? std::move(now) : std::function<qint64()>(&steadyNow))
{
}

void CoverFlight::setLanding(QObject* item)
{
    if (m_landing == item) {
        return;
    }
    m_landing = item;
    emit landingChanged();
}

void CoverFlight::notePress(const QRectF& sceneRect, const QUrl& source, const QString& artworkId,
                            const QString& label, bool round)
{
    if (sceneRect.width() < MinimumSide || sceneRect.height() < MinimumSide) {
        m_press.reset();
        return;
    }
    m_press = Press{.at = m_now(), .rect = sceneRect, .source = source, .artworkId = artworkId, .label = label,
                    .round = round};
}

void CoverFlight::headerShown(QObject* cover)
{
    if (cover == nullptr || !m_press) {
        return;
    }
    const Press press = *m_press;
    m_press.reset();
    if (m_now() - press.at > PressWindowMs) {
        return;
    }
    emit flightWanted(press.rect, press.source, press.artworkId, press.label, press.round, cover);
}

void CoverFlight::forget()
{
    m_press.reset();
}

} // namespace lmp::ui
