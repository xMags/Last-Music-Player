#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QList>
#include <QPromise>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdint>
#include <memory>
#include <vector>

#include "FakeAudioPlayer.h"
#include "FakePlaybackClock.h"
#include "TempLibrary.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"
#include "core/playback/PlayerController.h"
#include "core/playback/TrackSourceResolver.h"
#include "core/settings/SettingsStore.h"

// The rest of what the player controller tests share: a resolver that
// answers at once or when told to, a seeded shuffle generator, and a fixture
// holding every dependency in temporary folders.

class FakeSourceResolver final : public lmp::core::TrackSourceResolver {
public:
    [[nodiscard]] QFuture<lmp::core::Result<QUrl>> resolve(const lmp::core::Track& track) override
    {
        ++calls;
        if (!deferred) {
            return QtFuture::makeReadyValueFuture(answerFor(track));
        }
        Pending pending{track, std::make_unique<QPromise<lmp::core::Result<QUrl>>>()};
        pending.promise->start();
        QFuture<lmp::core::Result<QUrl>> future = pending.promise->future();
        m_pending.push_back(std::move(pending));
        return future;
    }

    // Answers the deferred request at `index` (in the order they were made).
    void finish(std::size_t index)
    {
        Pending& pending = m_pending.at(index);
        pending.promise->addResult(answerFor(pending.track));
        pending.promise->finish();
        QCoreApplication::processEvents();
    }

    bool deferred = false;
    QSet<QString> broken; // file paths that fail to resolve
    int calls = 0;

private:
    struct Pending {
        lmp::core::Track track;
        std::unique_ptr<QPromise<lmp::core::Result<QUrl>>> promise;
    };

    [[nodiscard]] lmp::core::Result<QUrl> answerFor(const lmp::core::Track& track) const
    {
        if (broken.contains(track.filePath)) {
            return lmp::core::Failure::of(lmp::core::FailureKind::NotFound, QStringLiteral("missing"));
        }
        return QUrl::fromLocalFile(track.filePath);
    }

    std::vector<Pending> m_pending;
};

// A deterministic stand-in for the shuffle generator.
[[nodiscard]] inline lmp::core::QueueRandom seededRandom(std::uint32_t seed)
{
    auto state = std::make_shared<std::uint32_t>(seed);
    return [state]() {
        *state = *state * 1664525U + 1013904223U;
        return static_cast<double>(*state >> 8) / static_cast<double>(1U << 24);
    };
}

// Everything a controller needs, in temporary folders.
class PlayerFixture {
public:
    PlayerFixture()
        : settings(m_dir.filePath(QStringLiteral("Settings.json")))
        , tracks(library.database())
    {
    }

    [[nodiscard]] bool ok() const { return m_dir.isValid() && library.ok(); }

    [[nodiscard]] std::unique_ptr<lmp::core::PlayerController> makeController(std::uint32_t seed = 7)
    {
        return std::make_unique<lmp::core::PlayerController>(
            lmp::core::PlayerControllerServices{audio, settings, tracks, resolver, clock}, lmp::core::QueueEngine(),
            seededRandom(seed));
    }

    // A local song; its file need not exist.
    [[nodiscard]] lmp::core::Track song(const QString& name) const
    {
        lmp::core::Track track;
        track.title = name;
        track.artist = QStringLiteral("Artist");
        track.sourceKind = QStringLiteral("local");
        track.filePath = QDir::toNativeSeparators(m_dir.filePath(name + QStringLiteral(".mp3")));
        track.durationSeconds = 200.0;
        return track;
    }

    [[nodiscard]] QList<lmp::core::Track> songs(const QStringList& names) const
    {
        QList<lmp::core::Track> list;
        for (const QString& name : names) {
            list.append(song(name));
        }
        return list;
    }

    [[nodiscard]] QString settingsPath() const { return settings.filePath(); }

private:
    QTemporaryDir m_dir;

public:
    TempLibrary library;
    lmp::core::SettingsStore settings;
    lmp::core::db::TrackRepository tracks;
    FakeAudioPlayer audio;
    FakeSourceResolver resolver;
    FakePlaybackClock clock;
};

// Lets queued continuations run.
inline void settle()
{
    QCoreApplication::processEvents();
}

// The title of the song the fake player opened last (file names are titles).
[[nodiscard]] inline QString lastOpenedTitle(const FakeAudioPlayer& audio)
{
    const QString file = audio.lastOpenedFile();
    return file.isEmpty() ? QString() : QFileInfo(file).completeBaseName();
}

[[nodiscard]] inline QStringList visibleTitles(const lmp::core::PlayerController& player)
{
    QStringList titles;
    for (const lmp::core::QueueEntry& entry : player.visibleQueue()) {
        titles.append(entry.track.title);
    }
    return titles;
}
