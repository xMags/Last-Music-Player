#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "core/model/Track.h"

namespace lmp::ui {

// Where a long song list comes from, a page at a time. Implemented over the
// library queries; called on the thread that owns the database (the GUI
// thread), with LIMIT and OFFSET, so a page is quick.
class TrackPageSource {
public:
    virtual ~TrackPageSource() = default;

    // How many songs the list has in all.
    [[nodiscard]] virtual int count() = 0;
    [[nodiscard]] virtual QList<core::Track> page(int offset, int limit) = 0;

protected:
    TrackPageSource() = default;
    TrackPageSource(const TrackPageSource&) = default;
    TrackPageSource& operator=(const TrackPageSource&) = default;
};

// A song list for QML: a short list given at once, or a long one read in
// pages as the view scrolls (canFetchMore / fetchMore). Rows carry what a
// row or a tile shows: the display title and album, the artist, the length,
// the cover, liked, playing and unavailable. Playing and liked change one
// row at a time with precise dataChanged signals.
//
// GUI thread only.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by a page's view model")

    // Rows loaded so far.
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // Songs in the whole list, loaded or not.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationTextRole,
        ArtworkUrlRole,
        ArtworkIdRole,
        ArtworkLabelRole,
        LikedRole,
        PlayingRole,
        UnavailableRole,
        UnavailableReasonRole,
        // The song has its own file on this PC (a file of this PC, or a
        // downloaded song): it plays with no connection.
        HasFileRole,
        // The row's number in the list, from 1.
        NumberRole,
        SourceKeyRole,
        // Part of the page's multi-selection.
        SelectedRole,
        // Listen Again's "12 min ago" (when last played, else added).
        PlayedWhenRole,
        // A song from the service's address, for its download mark; empty
        // for a file on this PC.
        RemoteUrlRole,
    };
    Q_ENUM(Role)

    static constexpr int PageSize = 100;

    explicit TrackListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    [[nodiscard]] int count() const { return static_cast<int>(m_tracks.size()); }
    [[nodiscard]] int totalCount() const { return m_total; }

    // Shows exactly these songs.
    void setTracks(QList<core::Track> tracks);
    // Shows the source's songs, first page at once.
    void setSource(std::shared_ptr<TrackPageSource> source);
    // Loads every remaining page (before playing the whole list).
    void fetchAll();

    [[nodiscard]] core::Track trackAt(int row) const;
    [[nodiscard]] const QList<core::Track>& tracks() const { return m_tracks; }

    // The playing song's source key; its row shows as playing.
    void setPlayingKey(const QString& sourceKey);
    // A song was liked or unliked elsewhere.
    void setLiked(const QString& sourceKey, bool liked);
    // The rows of the page's multi-selection (cleared when the list is replaced).
    void setSelectedRows(const QList<int>& rows);
    // Moves a loaded row (a playlist reordered by dragging); false when a
    // row is out of range or the list still has pages to load.
    bool moveRow(int from, int to);

signals:
    void countChanged();

private:
    void appendPage();
    void reindex();

    QList<core::Track> m_tracks;
    // Each loaded row's source key, so a key finds its rows quickly.
    QList<QString> m_keys;
    QHash<QString, QList<int>> m_rowsByKey;
    std::shared_ptr<TrackPageSource> m_source;
    int m_total = 0;
    QString m_playingKey;
    QSet<int> m_selected;
};

} // namespace lmp::ui
