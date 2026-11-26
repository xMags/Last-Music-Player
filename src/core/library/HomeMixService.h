#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "core/async/Result.h"
#include "core/home/HomeModel.h"

#include <QFuture>
#include <QStringList>
#include "core/library/HomeMixBuilder.h"

namespace lmp::core::db {
class GroupQueries;
class LibraryQueries;
} // namespace lmp::core::db

namespace lmp::core {

// Today's generated mixes, shared by Home, Library and the mix pages. They
// are made from the library (HomeMixBuilder) and kept until the day changes
// or a library without music gets some, so they do not reshuffle after
// every song (home-model.ts homeMixKey). `today` is injected for tests.
//
// GUI thread only (the database's owner).
class HomeMixService : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDate()>;

    // Both queries outlive this object.
    HomeMixService(db::LibraryQueries& library, db::GroupQueries& groups, Clock today, QObject* parent = nullptr);

    // Where Account mode's mixes come from: the account library as the
    // reference client's home model reads it, or nothing outside Account
    // mode (then the mixes come from the library queries).
    using AccountSource = std::function<std::optional<home::HomeLibrary>()>;
    void setAccountSource(AccountSource source);

    // API key mode's provider mixes, as the earlier versions filled them:
    // each local mix gets songs from a provider search (Daily Mix 1 to 5 by
    // the mix's genre, else the listener's top artists in turn or a theme;
    // On Repeat by the top artist; Discover Weekly by "<top artist> new
    // songs", put first), at most 12 songs per mix, no song twice. Asked
    // after every rebuild while `active` says so; nothing otherwise.
    using RemoteSearch = std::function<QFuture<Result<QList<Track>>>(const QString& query)>;
    void setRemoteFill(RemoteSearch search, std::function<bool()> active);

    // The search each mix is filled from (pure; for tests).
    struct FillRequest {
        QString id;
        QString query;
        bool prepend = false;
    };
    [[nodiscard]] static QList<FillRequest> fillRequests(const QList<HomeMix>& mixes, const QStringList& topArtists);
    // A mix's songs with `found` joined (pure; for tests).
    [[nodiscard]] static QList<Track> joinFill(const QList<Track>& mix, const QList<Track>& found, bool prepend);
    static constexpr qsizetype FilledMixSize = 12;

    // Makes the mixes again when their day is over, their source changed (or
    // `force`), reading the library; emits mixesChanged when it did.
    void refresh(bool force = false);

    [[nodiscard]] const QList<HomeMix>& mixes() const { return m_mixes; }
    [[nodiscard]] std::optional<HomeMix> mix(const QString& id) const;

signals:
    void mixesChanged();

private:
    [[nodiscard]] HomeMixInputs readInputs();
    void fillFromRemote(const QStringList& topArtists);

    db::LibraryQueries& m_library;
    db::GroupQueries& m_groups;
    Clock m_today;
    AccountSource m_accountSource;
    RemoteSearch m_remoteSearch;
    std::function<bool()> m_remoteActive;
    // Bumped by every rebuild, so a late provider answer fills nothing.
    quint64 m_fillGeneration = 0;
    QString m_key;
    // The account songs the current mixes were derived from (none for the
    // library's own mixes).
    std::optional<home::HomeLibrary> m_derivedFrom;
    QList<HomeMix> m_mixes;
};

} // namespace lmp::core
