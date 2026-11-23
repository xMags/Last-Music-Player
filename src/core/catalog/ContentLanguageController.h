#pragma once

#include "core/async/Result.h"
#include "core/catalog/ContentLanguages.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace lmp::core {
class PlaybackClock;
class PlaybackTimer;
} // namespace lmp::core

namespace lmp::core::catalog {

// Saves and detection for the Languages setting.
//
// Saves are optimistic: the change shows at once and is reverted to the last
// value the service confirmed if the newest save fails. Each save takes a
// sequence number and only the newest may write its answer, so an older reply
// never overwrites a newer choice. Chip taps are batched for
// LanguageSaveDelayMs. Detection runs by itself once per session on
// Automatic when the service has never detected the languages.
//
// GUI thread only. The dependencies' futures may finish on any thread; their
// results are handled on this object's thread.
class ContentLanguageController final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        // The setting as the app holds it now; nothing when the service
        // never sent it.
        std::function<std::optional<LanguageSetting>()> read;
        // Puts a value in the app's copy, optimistically or from the service.
        std::function<void(const std::optional<LanguageSetting>&)> write;
        // Sends the whole object; finishes with what the service stored.
        std::function<QFuture<Result<LanguageSetting>>(const LanguageSetting&)> save;
        std::function<QFuture<Result<LanguageSetting>>()> infer;
        // What the service filters by while Automatic has nothing detected:
        // the saved region's languages. Choose starts from them when there
        // is nothing else.
        std::function<QStringList()> fallbackLanguages;
        // A failed save, for the error toast.
        std::function<void(const Failure&)> saveFailed;
        // Something the view draws changed.
        std::function<void()> changed;
        PlaybackClock* clock = nullptr;
    };

    explicit ContentLanguageController(Dependencies dependencies, QObject* parent = nullptr);
    ~ContentLanguageController() override;

    // Forgets everything, for a sign-out or an account switch; late replies
    // are ignored.
    void reset();

    [[nodiscard]] bool detecting() const { return m_detecting; }
    [[nodiscard]] QString detectError() const { return m_detectMessage; }

    // The setting the view draws, including an unsaved empty Choose.
    [[nodiscard]] LanguageSetting current() const;

    void selectMode(LanguageMode mode);
    // A chip in Choose. Refused toggles (the last one, an eleventh) change nothing.
    void toggle(const QString& code);
    // Detection when the listener is on Automatic and the service never
    // detected; at most once per session. True when it started.
    bool autoDetectIfNeeded();
    void detect();

private:
    [[nodiscard]] std::optional<LanguageSetting> stored() const;
    [[nodiscard]] bool busy() const;
    void commit(const LanguageSetting& optimistic, const LanguageSetting& payload, bool debounce);
    void cancelTimer();
    void send(const LanguageSetting& payload);
    void changed();

    Dependencies m_deps;
    std::uint64_t m_generation = 0;
    std::uint64_t m_sequence = 0;
    int m_inFlight = 0;
    std::unique_ptr<PlaybackTimer> m_timer;
    bool m_timerPending = false;
    std::optional<LanguageSetting> m_queued;
    // The last value the service confirmed, to revert a failed save.
    std::optional<LanguageSetting> m_confirmed;
    bool m_autoDetectTried = false;
    // Choose was picked with nothing to preselect; nothing is saved until a pick.
    bool m_choosingEmpty = false;
    bool m_detecting = false;
    QString m_detectMessage;
};

} // namespace lmp::core::catalog
