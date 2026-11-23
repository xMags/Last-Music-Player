#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace lmp::ui {

// The page cascade (page-cascade.ts): when a page opens, or its loading
// skeleton gives way to content, the cards and rows created in the next
// moments arrive one after another. Each asks for its place in the line;
// only the first few get one, so a long page still opens at once.
//
// GUI thread only.
class PageCascade : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.cascade")

public:
    // Only the first few: a long cascade would make a page feel slow.
    static constexpr int Limit = 18;
    // Items created later than this after the start do not cascade (rows
    // made while scrolling).
    static constexpr qint64 WindowMs = 700;

    // `now` gives milliseconds on a steady clock (tests pass their own).
    explicit PageCascade(std::function<qint64()> now = {}, QObject* parent = nullptr);

    // A page opened, or its content replaced a skeleton.
    Q_INVOKABLE void restart();
    // The item's place in the line (0 first), or -1 when it does not cascade.
    Q_INVOKABLE int nextIndex();

private:
    std::function<qint64()> m_now;
    qint64 m_startedAt = -1;
    int m_next = 0;
};

} // namespace lmp::ui
