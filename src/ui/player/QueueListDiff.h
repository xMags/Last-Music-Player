#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace lmp::ui {

// How a list of unique row keys changed, as the fewest model notifications
// that keep each row's identity (so the views can animate it):
// - None: the same keys in the same order;
// - Move: one row left `from` and now stands at `to` (new indices), every
//   other row kept its order;
// - Splice: rows were removed and others inserted, and the rows kept on both
//   sides are in the same order. `removed` holds runs of old indices,
//   `inserted` runs of new indices, both in ascending order. Apply the
//   removals from the last run to the first, then the insertions from the
//   first run to the last;
// - Reset: an empty list filling up, and anything else (several moves, a
//   move together with an insert or a removal, or more than MaxRuns runs),
//   drawn again from scratch.
//
// Keys must be unique within each list; QueueListModel makes them so by
// counting repeats ("identity#0", "identity#1").
struct QueueListDiff {
    enum class Kind { None, Move, Splice, Reset };

    // A run of neighbouring rows, first and last included.
    struct Run {
        int first = 0;
        int last = 0;

        friend bool operator==(const Run&, const Run&) = default;
    };

    // More runs than this redraw the list instead: one change at a time is
    // what animates well, and a big change reads better as a fresh list.
    static constexpr int MaxRuns = 8;

    Kind kind = Kind::None;
    int from = -1;
    int to = -1;
    QList<Run> removed;
    QList<Run> inserted;

    [[nodiscard]] static QueueListDiff between(const QStringList& before, const QStringList& after);
};

} // namespace lmp::ui
