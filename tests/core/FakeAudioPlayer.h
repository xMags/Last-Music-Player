#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include "platform/IAudioPlayer.h"

// An audio player for the player controller tests: it records the commands
// it is given and emits events only when the test tells it to, in the order
// IAudioPlayer documents.
class FakeAudioPlayer final : public lmp::platform::IAudioPlayer {
public:
    struct Open {
        QUrl source;
        bool autoplay = false;
        double startAtSeconds = 0.0;
    };

    quint64 open(const QUrl& source, bool autoplay, double startAtSeconds) override
    {
        opens.append(Open{source, autoplay, startAtSeconds});
        m_state = State::Loading;
        return ++m_loadId;
    }
    void play() override { ++playCalls; }
    void pause() override { ++pauseCalls; }
    void seek(double seconds) override { seeks.append(seconds); }
    void stop() override
    {
        ++stopCalls;
        m_state = State::Idle;
    }
    void setVolume(double volume) override { volumes.append(volume); }
    [[nodiscard]] quint64 currentLoadId() const override { return m_loadId; }
    [[nodiscard]] State state() const override { return m_state; }

    // The documented order for an open with autoplay, up to Playing.
    void start(double durationSeconds = 200.0)
    {
        ready(durationSeconds);
        playing();
    }
    // The documented order for an open, up to "metadata loaded".
    void ready(double durationSeconds = 200.0)
    {
        m_duration = durationSeconds;
        report(State::Loading);
        emit durationChanged(m_loadId, durationSeconds);
        emit positionChanged(m_loadId, 0.0);
        report(State::Paused);
    }
    void playing() { report(State::Playing); }
    void paused() { report(State::Paused); }
    void position(double seconds) { emit positionChanged(m_loadId, seconds); }
    void end()
    {
        emit positionChanged(m_loadId, m_duration);
        report(State::Ended);
    }
    void fail()
    {
        emit errorOccurred(m_loadId, ErrorKind::Decode, QStringLiteral("unreadable"));
        report(State::Error);
    }

    [[nodiscard]] QString lastOpenedFile() const
    {
        return opens.isEmpty() ? QString() : opens.last().source.toLocalFile();
    }

    QList<Open> opens;
    QList<double> seeks;
    QList<double> volumes;
    int playCalls = 0;
    int pauseCalls = 0;
    int stopCalls = 0;

private:
    void report(State state)
    {
        m_state = state;
        emit stateChanged(m_loadId, state);
    }

    quint64 m_loadId = 0;
    State m_state = State::Idle;
    double m_duration = 0.0;
};
