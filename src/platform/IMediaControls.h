#pragma once

#include <QObject>
#include <QString>

namespace lmp::platform {

// What the system's now-playing display shows for the current song.
struct MediaControlsMetadata {
    QString title;
    QString artist;
    QString album;
    // A local file whose picture is shown: an image file, or an audio file
    // with a cover inside. Empty for no picture.
    QString artworkFilePath;

    friend bool operator==(const MediaControlsMetadata&, const MediaControlsMetadata&) = default;
};

// The system's media controls: the hardware media keys and the system's
// now-playing display (title, artist, cover, play state and progress).
//
// Everything set while disabled is kept and shown again once enabled. Calls
// and signals belong to the object's thread.
class IMediaControls : public QObject {
    Q_OBJECT

public:
    // What the user asked for through a media key or the system display.
    enum class MediaCommand { Toggle, Play, Pause, Stop, Next, Previous };
    Q_ENUM(MediaCommand)

    // The play state the system shows. Closed means nothing is playing and
    // the display has nothing to show.
    enum class PlaybackStatus { Closed, Changing, Stopped, Playing, Paused };
    Q_ENUM(PlaybackStatus)

    ~IMediaControls() override = default;

    IMediaControls(const IMediaControls&) = delete;
    IMediaControls& operator=(const IMediaControls&) = delete;

    // Disabled, the system shows nothing for the app and sends it no commands.
    virtual void setEnabled(bool enabled) = 0;
    // An empty title clears what is shown.
    virtual void setMetadata(const MediaControlsMetadata& metadata) = 0;
    virtual void setPlaybackStatus(PlaybackStatus status) = 0;
    // Seconds; nothing is shown for a duration of 0 (unknown).
    virtual void setTimeline(double positionSeconds, double durationSeconds) = 0;

signals:
    void commandReceived(lmp::platform::IMediaControls::MediaCommand command);

protected:
    explicit IMediaControls(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace lmp::platform
