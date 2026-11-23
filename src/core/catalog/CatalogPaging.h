#pragma once

#include <QList>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

// Load-more paging for the catalogue's long lists (charts, artist sections,
// the New Songs and New Albums pages). Pure state, no I/O:
//
// - one request at a time;
// - a missing next offset (or one that does not move forward, or an empty
//   page) ends the list;
// - an item already shown is never added twice (keyed by the caller);
// - an answer to a request the pager no longer waits for is dropped.
//
// A pager can start from a placeholder ("seed"): items shown at once while
// the real list loads. The first page replaces them; if that page fails or is
// empty the placeholder stays, without an error.
namespace lmp::core::catalog {

template <typename T>
struct CatalogPage {
    QList<T> items;
    std::optional<int> nextOffset;
};

enum class PagerStatus {
    Idle,
    Loading,
    Failed,
    Done,
};

struct PageRequest {
    int offset = 0;
    std::uint64_t token = 0;

    friend bool operator==(const PageRequest&, const PageRequest&) = default;
};

template <typename T>
struct PageUpdate {
    // True when the page replaced the items shown rather than adding to them.
    bool replaced = false;
    // Items new to the list, in order; all items when `replaced`.
    QList<T> added;
};

template <typename T>
class CatalogPager {
public:
    using KeyFunction = std::function<QString(const T&)>;

    // `seed` marks `items` as a placeholder the page at `nextOffset`
    // replaces; it only counts when there is a next offset.
    CatalogPager(QList<T> items, std::optional<int> nextOffset, KeyFunction key, bool seed = false)
        : m_key(std::move(key))
        , m_next(validOffset(nextOffset))
    {
        m_list = unique(items);
        for (const T& item : m_list) {
            m_keys.insert(m_key(item));
        }
        m_seeded = seed && m_next.has_value();
    }

    [[nodiscard]] const QList<T>& items() const { return m_list; }
    [[nodiscard]] bool isSeed() const { return m_seeded; }

    [[nodiscard]] PagerStatus status() const
    {
        if (m_inflight) {
            return PagerStatus::Loading;
        }
        if (!m_next) {
            return PagerStatus::Done;
        }
        return m_failedOnce ? PagerStatus::Failed : PagerStatus::Idle;
    }

    // Starts the next request; nothing while one runs or the list is complete.
    [[nodiscard]] std::optional<PageRequest> begin()
    {
        if (m_inflight || !m_next) {
            return std::nullopt;
        }
        ++m_lastToken;
        m_inflight = PageRequest{*m_next, m_lastToken};
        m_failedOnce = false;
        return m_inflight;
    }

    // Applies a page; nothing when `request` is not the one this pager waits for.
    [[nodiscard]] std::optional<PageUpdate<T>> complete(const PageRequest& request, const CatalogPage<T>& page)
    {
        if (!owns(request)) {
            return std::nullopt;
        }
        m_inflight.reset();
        const std::optional<int> offered = validOffset(page.nextOffset);
        // A page must move forward and bring something, so a confused server
        // can never make the list ask forever.
        m_next = offered && *offered > request.offset && !page.items.isEmpty() ? offered : std::nullopt;
        if (m_seeded) {
            m_seeded = false;
            QList<T> fresh = unique(page.items);
            if (fresh.isEmpty()) {
                return PageUpdate<T>{false, {}};
            }
            const qsizetype shown = m_list.size();
            bool extendsShown = shown <= fresh.size();
            for (qsizetype index = 0; extendsShown && index < shown; ++index) {
                extendsShown = m_key(m_list.at(index)) == m_key(fresh.at(index));
            }
            m_list = fresh;
            m_keys.clear();
            for (const T& item : m_list) {
                m_keys.insert(m_key(item));
            }
            if (extendsShown) {
                return PageUpdate<T>{false, fresh.mid(shown)};
            }
            return PageUpdate<T>{true, std::move(fresh)};
        }
        QList<T> added;
        for (const T& item : page.items) {
            const QString id = m_key(item);
            if (m_keys.contains(id)) {
                continue;
            }
            m_keys.insert(id);
            added.append(item);
        }
        m_list.append(added);
        return PageUpdate<T>{false, std::move(added)};
    }

    // Records a failed request; false when it was not the one this pager waits for.
    bool fail(const PageRequest& request)
    {
        if (!owns(request)) {
            return false;
        }
        m_inflight.reset();
        if (m_seeded) {
            // The placeholder is already a fine page: keep it and stop.
            m_seeded = false;
            m_next.reset();
            return true;
        }
        m_failedOnce = true;
        return true;
    }

    // Forgets the running request, so its answer is dropped when it arrives.
    void cancel() { m_inflight.reset(); }

private:
    [[nodiscard]] static std::optional<int> validOffset(std::optional<int> offset)
    {
        return offset && *offset >= 0 ? offset : std::nullopt;
    }

    [[nodiscard]] bool owns(const PageRequest& request) const
    {
        return m_inflight && m_inflight->token == request.token;
    }

    [[nodiscard]] QList<T> unique(const QList<T>& items) const
    {
        QSet<QString> seen;
        QList<T> result;
        for (const T& item : items) {
            const QString id = m_key(item);
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            result.append(item);
        }
        return result;
    }

    KeyFunction m_key;
    QList<T> m_list;
    QSet<QString> m_keys;
    std::optional<int> m_next;
    bool m_seeded = false;
    std::optional<PageRequest> m_inflight;
    std::uint64_t m_lastToken = 0;
    bool m_failedOnce = false;
};

} // namespace lmp::core::catalog
