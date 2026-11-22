#include "ui/player/QueueListDiff.h"

#include <QSet>

namespace lmp::ui {

namespace {

using Run = QueueListDiff::Run;

// The keys of `keys` that `other` also holds, in their order.
QStringList keptKeys(const QStringList& keys, const QSet<QString>& other)
{
    QStringList kept;
    for (const QString& key : keys) {
        if (other.contains(key)) {
            kept.append(key);
        }
    }
    return kept;
}

// Runs of the indices whose key `other` does not hold.
QList<Run> missingRuns(const QStringList& keys, const QSet<QString>& other)
{
    QList<Run> runs;
    for (int index = 0; index < keys.size(); ++index) {
        if (other.contains(keys.at(index))) {
            continue;
        }
        if (!runs.isEmpty() && runs.last().last == index - 1) {
            runs.last().last = index;
        } else {
            runs.append(Run{index, index});
        }
    }
    return runs;
}

// The same keys in another order: one row moved, or a Reset. Between the
// first and the last place that differ, either the first row went down to
// the end of that stretch and the rest moved up by one, or the last row went
// up to its start.
QueueListDiff singleMove(const QStringList& before, const QStringList& after)
{
    QueueListDiff diff;
    diff.kind = QueueListDiff::Kind::Reset;
    int first = 0;
    while (first < before.size() && before.at(first) == after.at(first)) {
        ++first;
    }
    int last = static_cast<int>(before.size()) - 1;
    while (last > first && before.at(last) == after.at(last)) {
        --last;
    }
    const int span = last - first;
    if (span <= 0) {
        return diff;
    }
    if (after.at(last) == before.at(first) && before.mid(first + 1, span) == after.mid(first, span)) {
        diff.kind = QueueListDiff::Kind::Move;
        diff.from = first;
        diff.to = last;
    } else if (after.at(first) == before.at(last) && before.mid(first, span) == after.mid(first + 1, span)) {
        diff.kind = QueueListDiff::Kind::Move;
        diff.from = last;
        diff.to = first;
    }
    return diff;
}

} // namespace

QueueListDiff QueueListDiff::between(const QStringList& before, const QStringList& after)
{
    QueueListDiff diff;
    if (before == after) {
        return diff;
    }
    // A list that appears whole has nothing to glide from: it is drawn at
    // once, without the entrance of new rows.
    if (before.isEmpty()) {
        diff.kind = Kind::Reset;
        return diff;
    }
    const QSet<QString> beforeKeys(before.cbegin(), before.cend());
    const QSet<QString> afterKeys(after.cbegin(), after.cend());
    diff.removed = missingRuns(before, afterKeys);
    diff.inserted = missingRuns(after, beforeKeys);
    if (diff.removed.isEmpty() && diff.inserted.isEmpty()) {
        return singleMove(before, after);
    }
    const bool orderKept = keptKeys(before, afterKeys) == keptKeys(after, beforeKeys);
    if (!orderKept || diff.removed.size() + diff.inserted.size() > MaxRuns) {
        diff.kind = Kind::Reset;
        diff.removed.clear();
        diff.inserted.clear();
        return diff;
    }
    diff.kind = Kind::Splice;
    return diff;
}

} // namespace lmp::ui
