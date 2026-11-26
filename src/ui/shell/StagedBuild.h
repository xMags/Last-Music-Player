#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// Spreads the building of a page's parts over event-loop turns, so a page
// with many parts does not build them all in one long turn and freeze the
// window. Each part takes a ticket when it is made (page order); the part is
// due once `served` reaches its ticket. One ticket is served per turn, the
// first on the turn after the first ticket was taken, so the window draws
// between parts. A part on screen does not wait for its turn (the page
// decides that); serving its ticket later costs nothing.
//
// QML: `StagedBuild { id: stage }` on the page, `readonly property int
// ticket: stage.take()` on a part, then the part is due while
// `stage.served >= ticket`. GUI thread only; the timer stops with the object.
class StagedBuild : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int served READ served NOTIFY servedChanged FINAL)

public:
    explicit StagedBuild(QObject* parent = nullptr);

    // The next ticket, 1 for the first part. Starts serving if it was idle.
    Q_INVOKABLE int take();
    // Every ticket up to this one is due.
    [[nodiscard]] int served() const { return m_served; }

signals:
    void servedChanged();

private:
    void serveNext();

    QTimer m_step;
    int m_issued = 0;
    int m_served = 0;
};

} // namespace lmp::ui
