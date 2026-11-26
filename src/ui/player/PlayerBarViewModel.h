#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace lmp::core {
class PlayerController;
class TrackActions;
struct Track;
}

namespace lmp::core::connect {
class ConnectSync;
}

namespace lmp::ui {

// The player bar (`App.playerBar`), and the song and transport the
// full-screen player shows: a view of the PlayerController. Every command
// goes to the controller; every property follows its change signals. The
// volume is 0 to 100 here and 0 to 1 in the controller.
//
// The seek bar keeps its own value while it is held (AppSlider shows
// `progress` only while not pressed), so position updates never move it
// under the pointer; seekToRatio() follows the drag, as the design does.
//
// While another of the account's devices plays (`remote`), everything shown
// is that device's song and state, and every playback control goes to it
// instead; `playingOn` names it. The heart and the download mark stay this
// app's: the account's like and this PC's download of that song.
class PlayerBarViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.playerBar")

    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged FINAL)
    // What the bar shows: the song, or the prompt while nothing is loaded.
    Q_PROPERTY(QString displayTitle READ displayTitle NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString displayArtist READ displayArtist NOTIFY trackChanged FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString artworkId READ artworkId NOTIFY trackChanged FINAL)
    // What the bar showed before the latest song change (songChanged).
    Q_PROPERTY(QString previousTitle READ previousTitle NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString previousArtist READ previousArtist NOTIFY trackChanged FINAL)
    Q_PROPERTY(bool liked READ liked NOTIFY stateChanged FINAL)
    // The address the song's download is listed under (its source address),
    // empty for a file of this PC: the player bar's download mark.
    Q_PROPERTY(QString downloadAddress READ downloadAddress NOTIFY trackChanged FINAL)
    // Playing, or starting to play.
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY stateChanged FINAL)
    // 0: off, 1: repeat all, 2: repeat one.
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal elapsedSeconds READ elapsedSeconds NOTIFY positionChanged FINAL)
    Q_PROPERTY(qreal totalSeconds READ totalSeconds NOTIFY positionChanged FINAL)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY positionChanged FINAL)
    Q_PROPERTY(QString totalText READ totalText NOTIFY positionChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY positionChanged FINAL)
    // 0 to 100: the level, kept while muted.
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged FINAL)
    // What the volume slider shows: 0 while muted, otherwise the level (0 to 1).
    Q_PROPERTY(qreal volumeRatio READ volumeRatio NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool remote READ remote NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString playingOn READ playingOn NOTIFY trackChanged FINAL)

public:
    // `player`, and `link` when given, must outlive this view model.
    // `actions`, when given, says whether the song is liked and likes it;
    // without it the heart only follows the song's own field.
    explicit PlayerBarViewModel(core::PlayerController& player, core::connect::ConnectSync* link = nullptr,
                                core::TrackActions* actions = nullptr,
                                QObject* parent = nullptr);

    [[nodiscard]] bool hasTrack() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] QString displayArtist() const;
    [[nodiscard]] QUrl artworkUrl() const { return m_artworkUrl; }
    [[nodiscard]] QString artworkId() const { return m_artworkId; }
    [[nodiscard]] QString previousTitle() const { return m_previousTitle; }
    [[nodiscard]] QString previousArtist() const { return m_previousArtist; }
    [[nodiscard]] bool liked() const { return m_liked; }
    [[nodiscard]] QString downloadAddress() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] bool shuffle() const;
    [[nodiscard]] int repeatMode() const;
    [[nodiscard]] qreal elapsedSeconds() const;
    [[nodiscard]] qreal totalSeconds() const;
    [[nodiscard]] QString elapsedText() const { return formatTime(elapsedSeconds()); }
    [[nodiscard]] QString totalText() const { return formatTime(totalSeconds()); }
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] int volume() const;
    [[nodiscard]] bool muted() const;
    [[nodiscard]] qreal volumeRatio() const;
    [[nodiscard]] bool remote() const;
    [[nodiscard]] QString playingOn() const;

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void next();
    Q_INVOKABLE void toggleShuffle();
    // Off, then repeat all, then repeat one, then off again.
    Q_INVOKABLE void cycleRepeat();
    // Only the shown state for now: liking is stored once the library exists.
    Q_INVOKABLE void toggleLike();
    // Reads the heart again: the account (and its likes) became known or
    // changed after the song was shown, as at launch while the session is
    // still being checked.
    void refreshLiked();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void share();
    // Seeks to a point along the song, 0 to 1.
    Q_INVOKABLE void seekToRatio(qreal ratio);
    // Sets the level, 0 to 1 (rounded to whole percent); ends mute.
    Q_INVOKABLE void setVolumeRatio(qreal ratio);
    // What the seek bubble shows at a point along the bar: the time there,
    // or nothing when no song with a known length is loaded.
    Q_INVOKABLE QString seekLabelAt(qreal ratio) const;
    // What the volume bubble shows at a point along the bar: "62%".
    Q_INVOKABLE QString volumeLabelAt(qreal ratio) const;

    // m:ss, as the design writes song times; negative and unknown are 0:00.
    [[nodiscard]] static QString formatTime(qreal seconds);

signals:
    void trackChanged();
    void stateChanged();
    void positionChanged();
    void volumeChanged();
    void shareRequested();
    // Another song replaced the one shown (not the launch restore, which
    // shows a song without opening it); previousTitle and previousArtist
    // hold what was shown before, for the song-change motion.
    void songChanged();

private:
    void onTrackChanged();
    // The heart for `track`: the library's answer, or the song's own field.
    [[nodiscard]] bool likedNow(const core::Track* track) const;
    // The song shown: the other device's while remote, else the player's.
    [[nodiscard]] const core::Track* shownTrack() const;

    core::PlayerController& m_player;
    core::TrackActions* m_actions = nullptr;
    core::connect::ConnectSync* m_connect = nullptr;
    // The identity of the song shown; empty while idle.
    QString m_shownIdentity;
    QString m_shownTitle;
    QString m_shownArtist;
    QUrl m_artworkUrl;
    QString m_artworkId;
    QString m_previousTitle;
    QString m_previousArtist;
    bool m_liked = false;
};

} // namespace lmp::ui
