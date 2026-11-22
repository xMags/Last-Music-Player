#include <QByteArray>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <optional>

#include "platform/windows/WindowsAudioPlayer.h"

#include <windows.h>
#include <objbase.h>

using lmp::platform::IAudioPlayer;
using lmp::platform::windows::WindowsAudioPlayer;

namespace {

using State = IAudioPlayer::State;
using ErrorKind = IAudioPlayer::ErrorKind;

// Long enough for a slow audio device to open, short enough to fail quickly.
constexpr int EventTimeoutMs = 10000;

// Everything the player reported, in order.
class EventLog : public QObject {
public:
    enum class Type { State, Position, Duration, Error };
    struct Event {
        quint64 loadId = 0;
        Type type = Type::State;
        State state = State::Idle;
        double seconds = 0.0;
        ErrorKind error = ErrorKind::Unknown;
    };

    explicit EventLog(IAudioPlayer& player)
    {
        connect(&player, &IAudioPlayer::stateChanged, this, [this](quint64 id, State state) {
            events.append({id, Type::State, state, 0.0, ErrorKind::Unknown});
        });
        connect(&player, &IAudioPlayer::positionChanged, this, [this](quint64 id, double seconds) {
            events.append({id, Type::Position, State::Idle, seconds, ErrorKind::Unknown});
        });
        connect(&player, &IAudioPlayer::durationChanged, this, [this](quint64 id, double seconds) {
            events.append({id, Type::Duration, State::Idle, seconds, ErrorKind::Unknown});
        });
        connect(&player, &IAudioPlayer::errorOccurred, this,
                [this](quint64 id, ErrorKind kind, const QString&) {
                    events.append({id, Type::Error, State::Idle, 0.0, kind});
                });
    }

    [[nodiscard]] QList<State> states(quint64 loadId, qsizetype from = 0) const
    {
        QList<State> result;
        for (qsizetype i = from; i < events.size(); ++i) {
            if (events[i].loadId == loadId && events[i].type == Type::State) {
                result.append(events[i].state);
            }
        }
        return result;
    }

    [[nodiscard]] bool reached(quint64 loadId, State state) const
    {
        return states(loadId).contains(state);
    }

    [[nodiscard]] bool finished(quint64 loadId) const
    {
        return reached(loadId, State::Ended) || reached(loadId, State::Error);
    }

    [[nodiscard]] std::optional<ErrorKind> error(quint64 loadId) const
    {
        for (const Event& event : events) {
            if (event.loadId == loadId && event.type == Type::Error) {
                return event.error;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] double lastDuration(quint64 loadId) const
    {
        double duration = 0.0;
        for (const Event& event : events) {
            if (event.loadId == loadId && event.type == Type::Duration) {
                duration = event.seconds;
            }
        }
        return duration;
    }

    // The position reported together with the first Playing state.
    [[nodiscard]] std::optional<double> positionAtPlaying(quint64 loadId) const
    {
        bool playing = false;
        for (const Event& event : events) {
            if (event.loadId != loadId) {
                continue;
            }
            if (event.type == Type::State && event.state == State::Playing) {
                playing = true;
            } else if (playing && event.type == Type::Position) {
                return event.seconds;
            }
        }
        return std::nullopt;
    }

    QList<Event> events;
};

// One second of 16-bit mono silence at 44100 Hz.
[[nodiscard]] QByteArray silentWav()
{
    constexpr quint32 SampleRate = 44100;
    constexpr quint16 Channels = 1;
    constexpr quint16 BitsPerSample = 16;
    constexpr quint16 BlockAlign = Channels * BitsPerSample / 8;
    constexpr quint32 DataSize = SampleRate * BlockAlign;

    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + DataSize};
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32{16} << quint16{1} << Channels << SampleRate << quint32{SampleRate * BlockAlign}
        << BlockAlign << BitsPerSample;
    out.writeRawData("data", 4);
    out << DataSize;
    bytes.append(QByteArray(static_cast<qsizetype>(DataSize), '\0'));
    return bytes;
}

} // namespace

class WindowsAudioPlayerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void autoplayPlaysToTheEnd();
    void seekThenPlayFinishesFromThere();
    void supersededLoadIsNotReported();
    void unsupportedAddressFails();

private:
    QTemporaryDir m_folder;
    QUrl m_silence;
    bool m_apartment = false;
};

void WindowsAudioPlayerTest::initTestCase()
{
    // The application's GUI thread is a single-threaded apartment (Qt sets it
    // up for the window); run the player in the same kind of apartment.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QVERIFY2(SUCCEEDED(apartment), "the test thread must be able to enter an apartment");
    m_apartment = true;

    QVERIFY(m_folder.isValid());
    QFile file(m_folder.filePath(QStringLiteral("silence.wav")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray wav = silentWav();
    QCOMPARE(file.write(wav), wav.size());
    file.close();
    m_silence = QUrl::fromLocalFile(file.fileName());
}

void WindowsAudioPlayerTest::cleanupTestCase()
{
    if (m_apartment) {
        CoUninitialize();
    }
}

void WindowsAudioPlayerTest::autoplayPlaysToTheEnd()
{
    WindowsAudioPlayer player;
    player.setVolume(0.0);
    EventLog log(player);

    const quint64 id = player.open(m_silence, true, 0.0);
    QVERIFY(id != 0);
    QCOMPARE(player.currentLoadId(), id);
    QVERIFY2(log.events.isEmpty(), "nothing is reported from inside open()");

    QTRY_VERIFY_WITH_TIMEOUT(log.finished(id), EventTimeoutMs);
    if (log.error(id) == ErrorKind::OutputDevice) {
        QSKIP("This machine has no usable audio output device.");
    }
    QVERIFY2(!log.error(id), "the silent file must play");

    const QList<State> expected{State::Loading, State::Paused, State::Playing, State::Ended};
    QCOMPARE(log.states(id), expected);
    const double duration = log.lastDuration(id);
    QVERIFY2(duration >= 0.9 && duration <= 1.1, qPrintable(QString::number(duration)));
    QCOMPARE(player.state(), State::Ended);
}

void WindowsAudioPlayerTest::seekThenPlayFinishesFromThere()
{
    WindowsAudioPlayer player;
    player.setVolume(0.0);
    EventLog log(player);

    const quint64 id = player.open(m_silence, false, 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(log.reached(id, State::Paused) || log.finished(id), EventTimeoutMs);
    if (log.error(id) == ErrorKind::OutputDevice) {
        QSKIP("This machine has no usable audio output device.");
    }
    QVERIFY(!log.error(id));
    // Without autoplay it stays paused.
    QTest::qWait(300);
    QCOMPARE(log.states(id), (QList<State>{State::Loading, State::Paused}));

    player.seek(0.5);
    QVERIFY(!log.events.isEmpty());
    QCOMPARE(log.events.last().type, EventLog::Type::Position);
    QCOMPARE(log.events.last().seconds, 0.5);

    QElapsedTimer clock;
    clock.start();
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(log.finished(id), EventTimeoutMs);
    if (log.error(id) == ErrorKind::OutputDevice) {
        QSKIP("This machine has no usable audio output device.");
    }
    QVERIFY(!log.error(id));
    QCOMPARE(log.states(id),
             (QList<State>{State::Loading, State::Paused, State::Playing, State::Ended}));

    // Playing resumed from the sought position, not from the start.
    const std::optional<double> resumedAt = log.positionAtPlaying(id);
    QVERIFY(resumedAt.has_value());
    QVERIFY2(*resumedAt >= 0.4, qPrintable(QString::number(*resumedAt)));
    QCOMPARE(log.events.last().type, EventLog::Type::State);
    // Ended comes right after the position at the full duration.
    const EventLog::Event& atEnd = log.events[log.events.size() - 2];
    QCOMPARE(atEnd.type, EventLog::Type::Position);
    QVERIFY(atEnd.seconds >= 0.9);
}

void WindowsAudioPlayerTest::supersededLoadIsNotReported()
{
    WindowsAudioPlayer player;
    player.setVolume(0.0);
    EventLog log(player);

    const quint64 first = player.open(m_silence, true, 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(log.reached(first, State::Playing) || log.finished(first),
                             EventTimeoutMs);
    if (log.error(first) == ErrorKind::OutputDevice) {
        QSKIP("This machine has no usable audio output device.");
    }
    QVERIFY(!log.error(first));

    const qsizetype replacedAt = log.events.size();
    const quint64 second = player.open(m_silence, false, 0.0);
    QVERIFY(second > first);
    QTRY_VERIFY_WITH_TIMEOUT(log.reached(second, State::Paused) || log.finished(second),
                             EventTimeoutMs);
    // Long enough for the first load to have reached its end had it lived on.
    QTest::qWait(1500);

    for (qsizetype i = replacedAt; i < log.events.size(); ++i) {
        QCOMPARE(log.events[i].loadId, second);
    }
    QCOMPARE(log.states(second), (QList<State>{State::Loading, State::Paused}));
    QCOMPARE(player.state(), State::Paused);
}

void WindowsAudioPlayerTest::unsupportedAddressFails()
{
    WindowsAudioPlayer player;
    EventLog log(player);

    const quint64 id = player.open(QUrl(QStringLiteral("ftp://example.invalid/a.wav")), true, 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(log.finished(id), EventTimeoutMs);
    QCOMPARE(log.error(id), std::optional<ErrorKind>(ErrorKind::SourceNotSupported));
    QCOMPARE(log.states(id), (QList<State>{State::Loading, State::Error}));

    // Commands after a failure do nothing.
    const qsizetype before = log.events.size();
    player.play();
    player.seek(0.2);
    QCOMPARE(log.events.size(), before);
}

QTEST_GUILESS_MAIN(WindowsAudioPlayerTest)
#include "tst_windows_audio_player.moc"
