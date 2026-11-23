#pragma once

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTime>
#include <QTimeZone>

#include <atomic>

#include "core/async/Result.h"
#include "platform/ITagReader.h"

namespace lmp::test {

// Tags from the file name: "Artist - Title.mp3"; an "untagged" file has none.
// `slow` makes each read take 20 ms, so a cancel can land mid-scan.
class FakeTagReader final : public lmp::platform::ITagReader {
public:
    lmp::core::Result<lmp::platform::TrackTags> read(const QString& filePath) override
    {
        ++reads;
        if (slow) {
            QThread::msleep(20);
        }
        const QString name = QFileInfo(filePath).completeBaseName();
        if (name.startsWith(QStringLiteral("unreadable"))) {
            return lmp::core::Failure::of(lmp::core::FailureKind::Storage);
        }
        lmp::platform::TrackTags tags;
        tags.durationSeconds = 125.4;
        tags.created = QDateTime(QDate(2024, 1, 2), QTime(10, 0), QTimeZone::UTC);
        tags.modified = QDateTime(QDate(2025, 3, 4), QTime(10, 0), QTimeZone::UTC);
        if (!name.startsWith(QStringLiteral("untagged"))) {
            const QStringList parts = name.split(QStringLiteral(" - "));
            tags.artist = parts.value(0);
            tags.title = parts.value(1);
            tags.album = QStringLiteral("Album");
            tags.genre = QStringLiteral("Rock");
        }
        return tags;
    }

    std::atomic<int> reads{0};
    std::atomic<bool> slow{false};
};

} // namespace lmp::test
