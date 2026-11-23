#include "core/catalog/ContentLanguageController.h"

#include "core/playback/PlaybackClock.h"

#include <chrono>
#include <utility>

namespace lmp::core::catalog {

ContentLanguageController::ContentLanguageController(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    m_timer = m_deps.clock->createTimer([this]() {
        m_timerPending = false;
        std::optional<LanguageSetting> next = std::move(m_queued);
        m_queued.reset();
        if (next) {
            send(*next);
        }
    });
}

ContentLanguageController::~ContentLanguageController() = default;

void ContentLanguageController::reset()
{
    ++m_generation;
    cancelTimer();
    m_queued.reset();
    m_inFlight = 0;
    m_confirmed.reset();
    m_autoDetectTried = false;
    m_choosingEmpty = false;
    m_detecting = false;
    m_detectMessage.clear();
}

std::optional<LanguageSetting> ContentLanguageController::stored() const
{
    return m_deps.read();
}

LanguageSetting ContentLanguageController::current() const
{
    LanguageSetting value = stored().value_or(LanguageSetting{});
    if (m_choosingEmpty && value.mode != LanguageMode::Custom) {
        value.mode = LanguageMode::Custom;
        value.languages.clear();
    }
    return value;
}

void ContentLanguageController::changed()
{
    if (m_deps.changed) {
        m_deps.changed();
    }
}

void ContentLanguageController::selectMode(LanguageMode mode)
{
    const LanguageSetting value = current();
    if (mode == value.mode && !(mode == LanguageMode::Custom && m_choosingEmpty)) {
        return;
    }
    if (mode == LanguageMode::Custom) {
        QStringList preselected = effectiveLanguages(value);
        if (preselected.isEmpty() && m_deps.fallbackLanguages) {
            preselected = m_deps.fallbackLanguages();
        }
        preselected = preselected.mid(0, account::MaxContentLanguages);
        if (preselected.isEmpty()) {
            m_choosingEmpty = true;
            changed();
            return;
        }
        m_choosingEmpty = false;
        LanguageSetting optimistic = value;
        optimistic.mode = LanguageMode::Custom;
        optimistic.languages = preselected;
        commit(optimistic, customPayload(preselected), false);
        return;
    }
    const bool wasChoosingEmpty = m_choosingEmpty;
    m_choosingEmpty = false;
    const LanguageSetting saved = stored().value_or(LanguageSetting{});
    if (wasChoosingEmpty && saved.mode == mode) {
        // Leaving an unsaved Choose for the mode that is already stored.
        changed();
        return;
    }
    LanguageSetting optimistic = saved;
    if (mode == LanguageMode::Auto) {
        optimistic.mode = LanguageMode::Auto;
        optimistic.languages = saved.inferred.value_or(QStringList());
        commit(optimistic, autoPayload(), false);
        return;
    }
    optimistic.mode = LanguageMode::Off;
    commit(optimistic, offPayload(saved), false);
}

void ContentLanguageController::toggle(const QString& code)
{
    const LanguageSetting value = current();
    if (value.mode != LanguageMode::Custom) {
        return;
    }
    const ToggleResult result = toggleLanguage(value.languages, code);
    if (result.refusal != ToggleRefusal::None) {
        return;
    }
    m_choosingEmpty = false;
    LanguageSetting optimistic = value;
    optimistic.languages = result.languages;
    commit(optimistic, customPayload(result.languages), true);
}

bool ContentLanguageController::autoDetectIfNeeded()
{
    if (m_autoDetectTried || m_detecting || !needsAutoDetect(stored())) {
        return false;
    }
    m_autoDetectTried = true;
    detect();
    return true;
}

void ContentLanguageController::detect()
{
    if (m_detecting) {
        return;
    }
    const std::uint64_t generation = m_generation;
    m_autoDetectTried = true;
    m_detecting = true;
    m_detectMessage.clear();
    changed();
    m_deps.infer().then(this, [this, generation](const Result<LanguageSetting>& answer) {
        if (generation != m_generation) {
            return;
        }
        m_detecting = false;
        if (!answer) {
            m_detectMessage = inferErrorMessage(answer.failure().httpStatus);
            changed();
            return;
        }
        const LanguageSetting& detected = answer.value();
        const auto withDetected = [&detected](LanguageSetting value) {
            value.inferred = detected.inferred.value_or(QStringList());
            if (!detected.inferredAt.isEmpty()) {
                value.inferredAt = detected.inferredAt;
            }
            return value;
        };
        if (busy()) {
            // A save is on its way: keep the listener's newer choice and add
            // only what the service detected.
            m_confirmed = withDetected(m_confirmed.value_or(LanguageSetting{}));
            m_deps.write(withDetected(stored().value_or(LanguageSetting{})));
        } else {
            m_deps.write(detected);
        }
        changed();
    });
}

bool ContentLanguageController::busy() const
{
    return m_inFlight > 0 || m_timerPending;
}

void ContentLanguageController::commit(const LanguageSetting& optimistic, const LanguageSetting& payload,
                                       bool debounce)
{
    if (!busy()) {
        m_confirmed = stored();
    }
    m_deps.write(optimistic);
    changed();
    cancelTimer();
    if (!debounce) {
        m_queued.reset();
        send(payload);
        return;
    }
    m_queued = payload;
    m_timerPending = true;
    m_timer->start(std::chrono::milliseconds(LanguageSaveDelayMs));
}

void ContentLanguageController::cancelTimer()
{
    if (!m_timerPending) {
        return;
    }
    m_timer->stop();
    m_timerPending = false;
}

void ContentLanguageController::send(const LanguageSetting& payload)
{
    const std::uint64_t generation = m_generation;
    const std::uint64_t sequence = ++m_sequence;
    ++m_inFlight;
    m_deps.save(payload).then(this, [this, generation, sequence](const Result<LanguageSetting>& answer) {
        if (generation != m_generation) {
            return;
        }
        --m_inFlight;
        const bool newest = sequence == m_sequence && !m_timerPending;
        if (answer) {
            m_confirmed = answer.value();
            if (newest) {
                m_deps.write(answer.value());
                changed();
            }
            return;
        }
        // A newer save (sent or waiting) settles the value; only the newest
        // failure reverts.
        if (!newest) {
            return;
        }
        m_deps.write(m_confirmed);
        if (m_deps.saveFailed) {
            m_deps.saveFailed(answer.failure());
        }
        changed();
    });
}

} // namespace lmp::core::catalog
