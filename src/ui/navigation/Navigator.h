#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/navigation/Route.h"

namespace lmp::ui {

// The page history (`App.navigator`). Each entry keeps its route, how far its
// page was scrolled and, optionally, the page's view model, so Back shows the
// page as it was left. A fresh page starts at the top; a page returned to
// gets its scroll position back (`returning`).
class Navigator : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.navigator")

    Q_PROPERTY(lmp::ui::Route current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentKind READ currentKind NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool returning READ returning NOTIFY currentChanged FINAL)
    Q_PROPERTY(qreal currentScrollOffset READ currentScrollOffset NOTIFY currentChanged FINAL)
    // How far the entry returned to with Back was scrolled when it was left,
    // read at that moment: a page reloading for it (its list reset to the
    // top) cannot overwrite it before the page scrolls there again.
    Q_PROPERTY(qreal returnScrollOffset READ returnScrollOffset NOTIFY currentChanged FINAL)
    // The control that had keyboard focus on the current page, by its
    // accessible name and its place among controls of that name; Back gives
    // it focus again (view-place.ts). Empty when none.
    Q_PROPERTY(QString currentFocusName READ currentFocusName NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentFocusIndex READ currentFocusIndex NOTIFY currentChanged FINAL)
    Q_PROPERTY(QObject* currentPage READ currentPage NOTIFY currentPageChanged FINAL)

public:
    explicit Navigator(QObject* parent = nullptr);
    ~Navigator() override;

    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;

    [[nodiscard]] Route current() const;
    [[nodiscard]] int currentKind() const { return static_cast<int>(current().kind()); }
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] bool canGoBack() const { return m_entries.size() > 1; }
    [[nodiscard]] bool returning() const { return m_returning; }
    [[nodiscard]] qreal currentScrollOffset() const;
    [[nodiscard]] qreal returnScrollOffset() const { return m_returning ? m_returnOffset : 0; }
    [[nodiscard]] QString currentFocusName() const;
    [[nodiscard]] int currentFocusIndex() const;
    [[nodiscard]] QObject* currentPage() const;
    [[nodiscard]] qsizetype depth() const { return m_entries.size(); }

    // Opens a page on top of the history.
    Q_INVOKABLE void push(const lmp::ui::Route& route);
    // Shows a page in place of the current one.
    Q_INVOKABLE void replace(const lmp::ui::Route& route);
    // Returns to the previous page. Does nothing on the first page.
    Q_INVOKABLE void back();
    // Opens a main view (the sidebar's items).
    Q_INVOKABLE void openView(int kind);
    // Changes a parameter of the current entry in place (a page's tab), so
    // Back to this entry brings it back; the page is not shown again.
    Q_INVOKABLE void setCurrentParam(const QString& name, const QString& value);

    // The current page reports how far it is scrolled, so the entry can keep it.
    Q_INVOKABLE void setCurrentScrollOffset(qreal offset);
    Q_INVOKABLE void setCurrentFocus(const QString& name, int index);
    // Hands the current page's view model to its entry, which owns it from
    // now on and deletes it when the entry leaves the history.
    void setCurrentPage(QObject* page);

    // A display title for a route kind (placeholder pages use it).
    [[nodiscard]] static QString titleFor(Route::Kind kind);

signals:
    void currentChanged();
    void currentPageChanged();

private:
    struct Entry {
        Route route;
        qreal scrollOffset = 0;
        QString focusName;
        int focusIndex = 0;
        QPointer<QObject> page; // owned: a child of the navigator
    };

    static void releasePage(Entry& entry);

    QList<Entry> m_entries;
    bool m_returning = false;
    qreal m_returnOffset = 0;
};

} // namespace lmp::ui
