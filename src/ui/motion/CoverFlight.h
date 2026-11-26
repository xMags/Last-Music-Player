#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace lmp::ui {

// The cover flight (cover-morph.ts): a card notes where its cover was and
// what it showed when it is pressed; when a collection header's cover
// appears soon after (the page that press opened), `flightWanted` asks the
// shell's flight layer to fly a copy from the card to the header. While the
// copy flies, `landing` names the header cover, which hides itself.
//
// GUI thread only.
class CoverFlight : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.coverFlight")

    Q_PROPERTY(QObject* landing READ landing WRITE setLanding NOTIFY landingChanged FINAL)

public:
    // A press older than this did not open the page that follows.
    static constexpr qint64 PressWindowMs = 1500;
    // Covers smaller than this are not worth flying.
    static constexpr qreal MinimumSide = 8.0;

    // `now` gives milliseconds on a steady clock (tests pass their own).
    explicit CoverFlight(std::function<qint64()> now = {}, QObject* parent = nullptr);

    [[nodiscard]] QObject* landing() const { return m_landing; }
    void setLanding(QObject* item);

    // `sceneRect`: the cover in window coordinates.
    Q_INVOKABLE void notePress(const QRectF& sceneRect, const QUrl& source, const QString& artworkId,
                               bool round);
    // A collection header's cover appeared; flies to it after a fresh press.
    Q_INVOKABLE void headerShown(QObject* cover);
    Q_INVOKABLE void forget();

signals:
    void landingChanged();
    void flightWanted(const QRectF& from, const QUrl& source, const QString& artworkId, bool round,
                      QObject* target);

private:
    struct Press {
        qint64 at = 0;
        QRectF rect;
        QUrl source;
        QString artworkId;
        bool round = false;
    };

    std::function<qint64()> m_now;
    std::optional<Press> m_press;
    QPointer<QObject> m_landing;
};

} // namespace lmp::ui
