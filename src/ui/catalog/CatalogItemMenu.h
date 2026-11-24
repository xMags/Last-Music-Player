#pragma once

#include <QFuture>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"
#include "ui/menus/MenuItemListModel.h"

namespace lmp::core {
class TrackActions;
namespace catalog {
class CatalogService;
}
} // namespace lmp::core

namespace lmp::ui {

class CatalogCardModel;
class Navigator;
class ShareController;
class ToastQueue;

// The menu of a catalogue card that is not a song (catalog.ts
// openCatalogItemMenu; App.catalogItemMenu): an artist from a provider gets
// Go to artist and Share; an album or playlist gets Play now, Play next,
// Add to queue, Shuffle, Download (where songs can be kept here), Share and
// View details. Its songs load only when an item needs them; a failure says
// so in a toast.
//
// GUI thread only.
class CatalogItemMenu final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.catalogItemMenu")

    Q_PROPERTY(lmp::ui::MenuItemListModel* menu READ menu CONSTANT FINAL)

public:
    using DetailLoader = std::function<QFuture<core::Result<core::catalog::Detail>>(
        const core::account::LibraryArtistIdentity& identity, const QString& artist)>;

    struct Parts {
        core::catalog::CatalogService& catalog;
        core::TrackActions& actions;
        Navigator& navigator;
        ToastQueue& toasts;
        ShareController* share = nullptr;
        // Whether songs can be kept on this PC now, and keeping them.
        std::function<bool()> canDownload = {};
        std::function<bool(const QString&, const QString&, const QString&, const QList<core::Track>&)> download = {};
        // A provider album's songs (API key mode).
        DetailLoader providerAlbum = {};
    };

    // Everything in `parts` outlives this object.
    explicit CatalogItemMenu(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] MenuItemListModel* menu() { return &m_menu; }

    // Fills the menu for the card at `row` of `cards`. False for a song or
    // a card the menu has nothing for (the page opens the song menu then).
    Q_INVOKABLE bool prepare(lmp::ui::CatalogCardModel* cards, int row);
    Q_INVOKABLE void trigger(const QString& itemId);

private:
    void withTracks(const std::function<void(const QList<core::Track>&)>& use);

    core::catalog::CatalogService& m_catalog;
    core::TrackActions& m_actions;
    Navigator& m_navigator;
    ToastQueue& m_toasts;
    ShareController* m_share = nullptr;
    std::function<bool()> m_canDownload;
    std::function<bool(const QString&, const QString&, const QString&, const QList<core::Track>&)> m_download;
    DetailLoader m_providerAlbum;
    MenuItemListModel m_menu;
    std::optional<core::catalog::CatalogItem> m_item;
    // A newer menu drops the songs an older one was loading.
    quint64 m_request = 0;
};

} // namespace lmp::ui
