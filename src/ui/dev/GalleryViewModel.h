#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "ui/catalog/CatalogCardModel.h"
#include "ui/menus/MenuItemListModel.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::ui {

// Sample data for the design gallery: a filled action menu, a toast queue and
// a few sample toasts to show from it. Holds no state the app depends on.
class GalleryViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(lmp::ui::MenuItemListModel* menuItems READ menuItems CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::ToastQueue* toasts READ toasts CONSTANT FINAL)
    Q_PROPERTY(QStringList selectChoices READ selectChoices CONSTANT FINAL)
    Q_PROPERTY(QString lastAction READ lastAction NOTIFY lastActionChanged FINAL)
    // Sample songs for the track rows, tiles, cards and shelves.
    Q_PROPERTY(lmp::ui::TrackListModel* sampleTracks READ sampleTracks CONSTANT FINAL)
    // Chart cards of each kind: the region's chart, recognised and unknown
    // genres, a landmark city and another city.
    Q_PROPERTY(lmp::ui::ChartCardModel* sampleCharts READ sampleCharts CONSTANT FINAL)

public:
    explicit GalleryViewModel(QObject* parent = nullptr);

    [[nodiscard]] MenuItemListModel* menuItems() { return &m_menuItems; }
    [[nodiscard]] ToastQueue* toasts() { return &m_toasts; }
    [[nodiscard]] QStringList selectChoices() const;
    [[nodiscard]] QString lastAction() const { return m_lastAction; }
    [[nodiscard]] TrackListModel* sampleTracks() { return &m_sampleTracks; }
    [[nodiscard]] ChartCardModel* sampleCharts() { return &m_sampleCharts; }

    // Shows the next of three sample toasts: plain, with an action, an error.
    Q_INVOKABLE void showSampleToast();
    // Records what a sample control did, for the gallery to show.
    Q_INVOKABLE void report(const QString& action);

signals:
    void lastActionChanged();

private:
    MenuItemListModel m_menuItems;
    ToastQueue m_toasts;
    TrackListModel m_sampleTracks;
    ChartCardModel m_sampleCharts;
    int m_nextSample = 0;
    QString m_lastAction;
};

} // namespace lmp::ui
