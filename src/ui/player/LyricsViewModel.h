#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <QFuture>

#include <cstdint>
#include <functional>
#include <optional>

#include "core/lyrics/LyricsService.h"

namespace lmp::core {
class PlayerController;
} // namespace lmp::core

namespace lmp::ui {

// The lyrics of the playing song (`App.lyrics`), shown in the queue rail's
// Lyrics tab and in the full-screen player. A song is asked for once it has
// played for 350 ms, so skipping through songs sends one request. Synced
// lines follow playback with a 400 ms lead, so a line lights as it starts;
// clicking one seeks there. A failed request is remembered for the song
// until Retry; another song asks afresh.
//
// Lives on the GUI thread; answers for a song no longer playing are dropped.
class LyricsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.lyrics")

    // "idle" (nothing plays), "loading", "synced", "plain", "instrumental",
    // "missing" (no lyrics found), "failed", "unavailable" (no account or
    // key to ask with).
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(QStringList lines READ lines NOTIFY changed FINAL)
    Q_PROPERTY(QString plain READ plain NOTIFY changed FINAL)
    // The synced line playing now; -1 before the first.
    Q_PROPERTY(int activeLine READ activeLine NOTIFY activeLineChanged FINAL)
    // The playing song's title, for the "missing" notice.
    Q_PROPERTY(QString songTitle READ songTitle NOTIFY changed FINAL)

public:
    static constexpr int DebounceMs = 350;
    static constexpr qint64 LeadMs = 400;

    // The lyrics saved with a song on this PC (a download's), read off the
    // GUI thread; empty when it has none.
    using SavedLyrics = std::function<QFuture<std::optional<core::lyrics::LyricsResult>>(const core::Track&)>;

    // Both outlive the view model. Saved lyrics, when given, come first:
    // they need no network and show in Offline mode too.
    LyricsViewModel(core::PlayerController& player, core::lyrics::LyricsService& lyrics, SavedLyrics saved = {},
                    QObject* parent = nullptr);

    [[nodiscard]] QString state() const { return m_state; }
    [[nodiscard]] QStringList lines() const;
    [[nodiscard]] QString plain() const { return m_result.plain; }
    [[nodiscard]] int activeLine() const { return m_activeLine; }
    [[nodiscard]] QString songTitle() const { return m_songTitle; }

    Q_INVOKABLE void seekToLine(int index);
    Q_INVOKABLE void retry();
    // Asks again for the playing song: the account or key changed.
    void reload();

signals:
    void changed();
    void activeLineChanged();

private:
    void onTrackChanged();
    void load();
    void loadOnline(const core::Track& track, std::uint64_t request, const QString& key);
    void show(const core::lyrics::LyricsResult& result);
    void followPosition();
    void setState(const QString& state);

    core::PlayerController& m_player;
    core::lyrics::LyricsService& m_lyrics;
    SavedLyrics m_saved;
    QTimer m_debounce;
    std::uint64_t m_request = 0;
    // The song shown, by its lyrics key; and the one whose request failed.
    QString m_songKey;
    QString m_failedKey;
    QString m_songTitle;
    QString m_state = QStringLiteral("idle");
    core::lyrics::LyricsResult m_result;
    int m_activeLine = -1;
};

} // namespace lmp::ui
