#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

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

    // Makes the mixes again when their day is over (or `force`), reading the
    // library; emits mixesChanged when it did.
    void refresh(bool force = false);

    [[nodiscard]] const QList<HomeMix>& mixes() const { return m_mixes; }
    [[nodiscard]] std::optional<HomeMix> mix(const QString& id) const;

signals:
    void mixesChanged();

private:
    [[nodiscard]] HomeMixInputs readInputs();

    db::LibraryQueries& m_library;
    db::GroupQueries& m_groups;
    Clock m_today;
    QString m_key;
    QList<HomeMix> m_mixes;
};

} // namespace lmp::core
