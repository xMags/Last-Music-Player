#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// The player bar (`App.playerBar`). Until playback exists it only keeps what
// is set on it: the buttons flip their states and the sliders store their
// values. Playback drives it later through the same properties.
class PlayerBarViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.playerBar")

    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString artist READ artist WRITE setArtist NOTIFY trackChanged FINAL)
    // What the bar shows: the song, or the prompt while nothing is loaded.
    Q_PROPERTY(QString displayTitle READ displayTitle NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString displayArtist READ displayArtist NOTIFY trackChanged FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl WRITE setArtworkUrl NOTIFY trackChanged FINAL)
    Q_PROPERTY(QString artworkId READ artworkId WRITE setArtworkId NOTIFY trackChanged FINAL)
    Q_PROPERTY(bool liked READ liked WRITE setLiked NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY stateChanged FINAL)
    // 0: off, 1: repeat all, 2: repeat one.
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal elapsedSeconds READ elapsedSeconds WRITE setElapsedSeconds NOTIFY positionChanged FINAL)
    Q_PROPERTY(qreal totalSeconds READ totalSeconds WRITE setTotalSeconds NOTIFY positionChanged FINAL)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY positionChanged FINAL)
    Q_PROPERTY(QString totalText READ totalText NOTIFY positionChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY positionChanged FINAL)
    // 0 to 100.
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged FINAL)

public:
    static constexpr int kDefaultVolume = 62;

    explicit PlayerBarViewModel(QObject* parent = nullptr);

    [[nodiscard]] bool hasTrack() const { return !m_title.isEmpty(); }
    [[nodiscard]] QString title() const { return m_title; }
    void setTitle(const QString& title);
    [[nodiscard]] QString artist() const { return m_artist; }
    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] QString displayArtist() const;
    void setArtist(const QString& artist);
    [[nodiscard]] QUrl artworkUrl() const { return m_artworkUrl; }
    void setArtworkUrl(const QUrl& url);
    [[nodiscard]] QString artworkId() const { return m_artworkId; }
    void setArtworkId(const QString& id);
    [[nodiscard]] bool liked() const { return m_liked; }
    void setLiked(bool liked);
    [[nodiscard]] bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    [[nodiscard]] bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    [[nodiscard]] int repeatMode() const { return m_repeatMode; }
    void setRepeatMode(int mode);
    [[nodiscard]] qreal elapsedSeconds() const { return m_elapsed; }
    void setElapsedSeconds(qreal seconds);
    [[nodiscard]] qreal totalSeconds() const { return m_total; }
    void setTotalSeconds(qreal seconds);
    [[nodiscard]] QString elapsedText() const { return formatTime(m_elapsed); }
    [[nodiscard]] QString totalText() const { return formatTime(m_total); }
    [[nodiscard]] qreal progress() const;
    [[nodiscard]] int volume() const { return m_volume; }
    void setVolume(int volume);
    [[nodiscard]] bool muted() const { return m_muted; }
    void setMuted(bool muted);

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void next();
    Q_INVOKABLE void toggleShuffle();
    // Off, then repeat all, then repeat one, then off again.
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void toggleLike();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void share();
    // Seeks to a point along the song, 0 to 1.
    Q_INVOKABLE void seekToRatio(qreal ratio);
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
    void previousRequested();
    void nextRequested();
    void shareRequested();

private:
    QString m_title;
    QString m_artist;
    QUrl m_artworkUrl;
    QString m_artworkId;
    bool m_liked = false;
    bool m_playing = false;
    bool m_shuffle = false;
    int m_repeatMode = 0;
    qreal m_elapsed = 0;
    qreal m_total = 0;
    int m_volume = kDefaultVolume;
    bool m_muted = false;
};

} // namespace lmp::ui
