#include <QObject>
#include <QString>
#include <QTest>

#include "ui/menus/MenuItemListModel.h"

using lmp::ui::MenuItemListModel;

class MenuItemListModelTest : public QObject {
    Q_OBJECT

private slots:
    void exposesItemRoles();
    void firstDividerIsDropped();
    void keyboardSkipsDisabledItemsAndWraps();
    void emptyMenuHasNoCurrentItem();
};

void MenuItemListModelTest::exposesItemRoles()
{
    MenuItemListModel model;
    model.addItem(QStringLiteral("play-next"), QStringLiteral("Play next"),
                  QStringLiteral("play-next"));
    model.addItem(QStringLiteral("remove"), QStringLiteral("Remove"), QStringLiteral("trash"),
                  true, true);
    QCOMPARE(model.count(), 2);
    const QModelIndex second = model.index(1);
    QCOMPARE(model.data(second, MenuItemListModel::ItemIdRole).toString(),
             QStringLiteral("remove"));
    QCOMPARE(model.data(second, MenuItemListModel::IconNameRole).toString(),
             QStringLiteral("trash"));
    QCOMPARE(model.data(second, MenuItemListModel::DangerRole).toBool(), true);
    QCOMPARE(model.data(second, MenuItemListModel::SeparatorBeforeRole).toBool(), true);
    QCOMPARE(model.data(second, MenuItemListModel::EnabledRole).toBool(), true);
    QCOMPARE(model.idAt(0), QStringLiteral("play-next"));
    QVERIFY(model.idAt(5).isEmpty());
}

void MenuItemListModelTest::firstDividerIsDropped()
{
    MenuItemListModel model;
    model.addItem(QStringLiteral("a"), QStringLiteral("A"), {}, false, true);
    QCOMPARE(model.data(model.index(0), MenuItemListModel::SeparatorBeforeRole).toBool(), false);
}

void MenuItemListModelTest::keyboardSkipsDisabledItemsAndWraps()
{
    MenuItemListModel model;
    model.addItem(QStringLiteral("a"), QStringLiteral("A"), {}, false, false, false);
    model.addItem(QStringLiteral("b"), QStringLiteral("B"));
    model.addItem(QStringLiteral("c"), QStringLiteral("C"), {}, false, false, false);
    model.addItem(QStringLiteral("d"), QStringLiteral("D"));

    QCOMPARE(model.firstEnabled(), 1);
    QCOMPARE(model.lastEnabled(), 3);
    QCOMPARE(model.nextEnabled(-1), 1);
    QCOMPARE(model.nextEnabled(1), 3);
    QCOMPARE(model.nextEnabled(3), 1);
    QCOMPARE(model.previousEnabled(-1), 3);
    QCOMPARE(model.previousEnabled(1), 3);
    QCOMPARE(model.previousEnabled(3), 1);
}

void MenuItemListModelTest::emptyMenuHasNoCurrentItem()
{
    MenuItemListModel model;
    QCOMPARE(model.firstEnabled(), -1);
    model.addItem(QStringLiteral("a"), QStringLiteral("A"), {}, false, false, false);
    QCOMPARE(model.firstEnabled(), -1);
    QCOMPARE(model.nextEnabled(0), -1);
    model.clear();
    QCOMPARE(model.count(), 0);
}

QTEST_GUILESS_MAIN(MenuItemListModelTest)
#include "tst_menu_item_list_model.moc"
