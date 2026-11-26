#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "ui/catalog/CatalogCardModel.h"
#include "ui/home/HomeListModels.h"

using lmp::ui::CatalogCardModel;
using lmp::ui::HomeSectionListModel;
using Section = lmp::ui::HomeSectionListModel::Section;

namespace {

Section section(const QString& key, const QString& title = {})
{
    return Section{.key = key, .title = title, .tier = {}, .capability = QStringLiteral("local"),
                   .kind = QStringLiteral("songs"), .link = false};
}

lmp::core::catalog::CatalogItem card(const QString& id)
{
    lmp::core::catalog::CatalogItem item;
    item.catalogId = id;
    item.type = lmp::core::catalog::ResourceType::Album;
    item.title = id;
    return item;
}

} // namespace

// Lists replaced by key keep the rows that stay (and the views' delegates for
// them): no reset, only the rows that came, went or changed.
class KeyedListModelTest : public QObject {
    Q_OBJECT

private slots:
    void aChangedRowIsUpdatedInPlace();
    void rowsThatComeOrGoAreInsertedOrRemoved();
    void theSameListTellsNobody();
    void cardsSayWhenTheyChange();
};

void KeyedListModelTest::aChangedRowIsUpdatedInPlace()
{
    HomeSectionListModel model;
    model.setSections({section("hero"), section("mixes", "Your mixes"), section("liked")});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.setSections({section("hero"), section("mixes", "Mixes for you"), section("liked")});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 1);
}

void KeyedListModelTest::rowsThatComeOrGoAreInsertedOrRemoved()
{
    HomeSectionListModel model;
    model.setSections({section("hero"), section("mixes"), section("liked")});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy counted(&model, &HomeSectionListModel::countChanged);

    model.setSections({section("hero"), section("mixes"), section("liked"), section("catalogStatus")});
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 3);
    model.setSections({section("hero"), section("liked"), section("catalogStatus")});
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(counted.count(), 2);
    QCOMPARE(model.count(), 3);
}

void KeyedListModelTest::theSameListTellsNobody()
{
    HomeSectionListModel model;
    model.setSections({section("hero"), section("mixes")});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setSections({section("hero"), section("mixes")});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 0);
}

void KeyedListModelTest::cardsSayWhenTheyChange()
{
    CatalogCardModel cards;
    QSignalSpy items(&cards, &CatalogCardModel::itemsChanged);
    QSignalSpy reset(&cards, &QAbstractItemModel::modelReset);
    cards.setItems({card("a"), card("b")});
    QCOMPARE(items.count(), 1);

    // A further page of cards is added below, without a reset.
    reset.clear();
    cards.setItems({card("a"), card("b"), card("c")});
    QCOMPARE(items.count(), 2);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(cards.count(), 3);

    cards.setItems({card("a"), card("b"), card("c")});
    QCOMPARE(items.count(), 2);
}

QTEST_GUILESS_MAIN(KeyedListModelTest)
#include "tst_keyed_list_model.moc"
