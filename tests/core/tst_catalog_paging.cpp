#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/catalog/CatalogPaging.h"

namespace catalog = lmp::core::catalog;
using catalog::PagerStatus;

namespace {

struct Item {
    QString id;
};

using Pager = catalog::CatalogPager<Item>;
using Page = catalog::CatalogPage<Item>;

QList<Item> items(std::initializer_list<const char*> ids)
{
    QList<Item> list;
    for (const char* id : ids) {
        list.append(Item{QString::fromLatin1(id)});
    }
    return list;
}

QStringList ids(const QList<Item>& list)
{
    QStringList result;
    for (const Item& item : list) {
        result.append(item.id);
    }
    return result;
}

Pager pager(QList<Item> list, std::optional<int> nextOffset, bool seed = false)
{
    return Pager(std::move(list), nextOffset, [](const Item& item) { return item.id; }, seed);
}

QStringList names(std::initializer_list<const char*> values)
{
    QStringList result;
    for (const char* value : values) {
        result.append(QString::fromLatin1(value));
    }
    return result;
}

} // namespace

// The reference client's paging cases, one slot each.
class CatalogPagingTest : public QObject {
    Q_OBJECT

private slots:
    void pagesAppendUntilTheListEnds();
    void aListWithoutNextOffsetNeverAsks();
    void oneRequestAtATime();
    void shownItemsAreNotAddedTwice();
    void firstPageDuplicatesAreDropped();
    void staleAnswersAreDropped();
    void aFailedPageCanBeRetried();
    void aStuckOrEmptyPageEndsTheList();
    void aSeedIsReplacedByTheFirstPage();
    void aPageStartingWithTheSeedOnlyAdds();
    void aFailedOrEmptyFirstPageKeepsTheSeed();
};

void CatalogPagingTest::pagesAppendUntilTheListEnds()
{
    Pager list = pager(items({"a", "b"}), 2);
    QCOMPARE(list.status(), PagerStatus::Idle);
    const std::optional<catalog::PageRequest> first = list.begin();
    QVERIFY(first);
    QCOMPARE(first->offset, 2);
    QCOMPARE(list.status(), PagerStatus::Loading);
    const std::optional<catalog::PageUpdate<Item>> update = list.complete(*first, Page{items({"c", "d"}), 4});
    QVERIFY(update && !update->replaced);
    QCOMPARE(ids(update->added), names({"c", "d"}));
    const std::optional<catalog::PageRequest> second = list.begin();
    QCOMPARE(second->offset, 4);
    QCOMPARE(ids(list.complete(*second, Page{items({"e"}), std::nullopt})->added), names({"e"}));
    QCOMPARE(list.status(), PagerStatus::Done);
    QVERIFY(!list.begin());
    QCOMPARE(ids(list.items()), names({"a", "b", "c", "d", "e"}));
}

void CatalogPagingTest::aListWithoutNextOffsetNeverAsks()
{
    Pager list = pager(items({"a"}), std::nullopt);
    QCOMPARE(list.status(), PagerStatus::Done);
    QVERIFY(!list.begin());
    // A negative offset means the same.
    QCOMPARE(pager(items({"a"}), -1).status(), PagerStatus::Done);
}

void CatalogPagingTest::oneRequestAtATime()
{
    Pager list = pager(items({"a"}), 1);
    QVERIFY(list.begin());
    QVERIFY(!list.begin());
}

void CatalogPagingTest::shownItemsAreNotAddedTwice()
{
    Pager list = pager(items({"a", "b", "c"}), 3);
    const catalog::PageRequest request = *list.begin();
    const std::optional<catalog::PageUpdate<Item>> update =
        list.complete(request, Page{items({"c", "d", "d", "a", "e"}), 8});
    QCOMPARE(ids(update->added), names({"d", "e"}));
    QCOMPARE(ids(list.items()), names({"a", "b", "c", "d", "e"}));
    // A page of mostly duplicates still moves on.
    QCOMPARE(list.status(), PagerStatus::Idle);
}

void CatalogPagingTest::firstPageDuplicatesAreDropped()
{
    QCOMPARE(ids(pager(items({"a", "a", "b"}), std::nullopt).items()), names({"a", "b"}));
}

void CatalogPagingTest::staleAnswersAreDropped()
{
    Pager list = pager(items({"a"}), 1);
    const catalog::PageRequest stale = *list.begin();
    list.cancel();
    QVERIFY(!list.complete(stale, Page{items({"x"}), 2}));
    QVERIFY(!list.fail(stale));
    QCOMPARE(ids(list.items()), names({"a"}));
    const catalog::PageRequest fresh = *list.begin();
    QVERIFY(fresh.token != stale.token);
    QVERIFY(!list.complete(stale, Page{items({"x"}), 2}));
    QCOMPARE(ids(list.complete(fresh, Page{items({"b"}), std::nullopt})->added), names({"b"}));
}

void CatalogPagingTest::aFailedPageCanBeRetried()
{
    Pager list = pager(items({"a", "b"}), 2);
    const catalog::PageRequest request = *list.begin();
    QVERIFY(list.fail(request));
    QCOMPARE(list.status(), PagerStatus::Failed);
    QCOMPARE(ids(list.items()), names({"a", "b"}));
    const catalog::PageRequest retry = *list.begin();
    QCOMPARE(retry.offset, 2);
    QCOMPARE(list.status(), PagerStatus::Loading);
    (void)list.complete(retry, Page{items({"c"}), std::nullopt});
    QCOMPARE(ids(list.items()), names({"a", "b", "c"}));
}

void CatalogPagingTest::aStuckOrEmptyPageEndsTheList()
{
    Pager backwards = pager(items({"a"}), 10);
    (void)backwards.complete(*backwards.begin(), Page{items({"b"}), 10});
    QCOMPARE(backwards.status(), PagerStatus::Done);
    Pager empty = pager(items({"a"}), 10);
    (void)empty.complete(*empty.begin(), Page{{}, 20});
    QCOMPARE(empty.status(), PagerStatus::Done);
}

void CatalogPagingTest::aSeedIsReplacedByTheFirstPage()
{
    Pager list = pager(items({"x", "y"}), 0, true);
    QVERIFY(list.isSeed());
    const catalog::PageRequest request = *list.begin();
    QCOMPARE(request.offset, 0);
    const std::optional<catalog::PageUpdate<Item>> update = list.complete(request, Page{items({"a", "b", "c"}), 3});
    QVERIFY(update->replaced);
    QCOMPARE(ids(update->added), names({"a", "b", "c"}));
    QVERIFY(!list.isSeed());
    QCOMPARE(ids(list.items()), names({"a", "b", "c"}));
    const catalog::PageRequest next = *list.begin();
    QCOMPARE(next.offset, 3);
    QCOMPARE(ids(list.complete(next, Page{items({"a", "d"}), std::nullopt})->added), names({"d"}));
}

void CatalogPagingTest::aPageStartingWithTheSeedOnlyAdds()
{
    Pager list = pager(items({"a", "b"}), 0, true);
    const std::optional<catalog::PageUpdate<Item>> update = list.complete(*list.begin(), Page{items({"a", "b", "c"}), 3});
    QVERIFY(!update->replaced);
    QCOMPARE(ids(update->added), names({"c"}));
}

void CatalogPagingTest::aFailedOrEmptyFirstPageKeepsTheSeed()
{
    Pager failed = pager(items({"a", "b"}), 0, true);
    QVERIFY(failed.fail(*failed.begin()));
    QCOMPARE(failed.status(), PagerStatus::Done);
    QCOMPARE(ids(failed.items()), names({"a", "b"}));
    Pager empty = pager(items({"a", "b"}), 0, true);
    const std::optional<catalog::PageUpdate<Item>> update = empty.complete(*empty.begin(), Page{{}, std::nullopt});
    QVERIFY(update && !update->replaced && update->added.isEmpty());
    QCOMPARE(ids(empty.items()), names({"a", "b"}));
    QCOMPARE(empty.status(), PagerStatus::Done);
}

QTEST_GUILESS_MAIN(CatalogPagingTest)
#include "tst_catalog_paging.moc"
