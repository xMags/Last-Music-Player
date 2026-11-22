#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <chrono>

#include "ui/toasts/ToastQueue.h"

using lmp::ui::ToastQueue;

namespace {

QString textAt(const ToastQueue& queue, int row)
{
    return queue.data(queue.index(row), ToastQueue::TextRole).toString();
}

} // namespace

class ToastQueueTest : public QObject {
    Q_OBJECT

private slots:
    void showAddsAToast();
    void dismissesItselfAfterItsLifetime();
    void keepsAtMostThree();
    void actionReportsAndRemoves();
    void dismissByIdIgnoresUnknownIds();
};

void ToastQueueTest::showAddsAToast()
{
    ToastQueue queue;
    QSignalSpy inserted(&queue, &QAbstractItemModel::rowsInserted);
    const int id = queue.show(QStringLiteral("Saved"), false, QStringLiteral("Undo"),
                              QStringLiteral("undo-save"));
    QCOMPARE(queue.count(), 1);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(textAt(queue, 0), QStringLiteral("Saved"));
    QCOMPARE(queue.data(queue.index(0), ToastQueue::ToastIdRole).toInt(), id);
    QCOMPARE(queue.data(queue.index(0), ToastQueue::ActionLabelRole).toString(),
             QStringLiteral("Undo"));
    QCOMPARE(queue.data(queue.index(0), ToastQueue::ErrorRole).toBool(), false);
}

void ToastQueueTest::dismissesItselfAfterItsLifetime()
{
    // A short lifetime stands in for the real four seconds.
    ToastQueue queue(std::chrono::milliseconds(50), nullptr);
    queue.show(QStringLiteral("Gone soon"));
    QCOMPARE(queue.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(queue.count(), 0, 2000);
}

void ToastQueueTest::keepsAtMostThree()
{
    ToastQueue queue;
    QSignalSpy removed(&queue, &QAbstractItemModel::rowsRemoved);
    for (int i = 1; i <= 4; ++i) {
        queue.show(QStringLiteral("Toast %1").arg(i));
    }
    QCOMPARE(queue.count(), ToastQueue::kMaxVisible);
    QCOMPARE(removed.count(), 1);
    // Newest first; the oldest went.
    QCOMPARE(textAt(queue, 0), QStringLiteral("Toast 4"));
    QCOMPARE(textAt(queue, 2), QStringLiteral("Toast 2"));
}

void ToastQueueTest::actionReportsAndRemoves()
{
    ToastQueue queue;
    QSignalSpy triggered(&queue, &ToastQueue::actionTriggered);
    queue.show(QStringLiteral("First"));
    const int id = queue.show(QStringLiteral("Removed from queue"), false,
                              QStringLiteral("Undo"), QStringLiteral("undo-remove"));
    queue.triggerAction(id);
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(triggered.first().first().toString(), QStringLiteral("undo-remove"));
    QCOMPARE(queue.count(), 1);
    QCOMPARE(textAt(queue, 0), QStringLiteral("First"));
    // A second press on a toast that is gone does nothing.
    queue.triggerAction(id);
    QCOMPARE(triggered.count(), 1);
}

void ToastQueueTest::dismissByIdIgnoresUnknownIds()
{
    ToastQueue queue;
    const int id = queue.show(QStringLiteral("Kept"));
    queue.dismiss(id + 100);
    QCOMPARE(queue.count(), 1);
    queue.dismiss(id);
    QCOMPARE(queue.count(), 0);
}

QTEST_GUILESS_MAIN(ToastQueueTest)
#include "tst_toast_queue.moc"
