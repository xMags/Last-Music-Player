#include <QColor>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSize>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>
#include <vector>

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

namespace {

// Records the size every picture is asked for at.
class RecordingProvider final : public QQuickImageProvider {
public:
    RecordingProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        Q_UNUSED(id)
        {
            const QMutexLocker locker(&m_lock);
            m_sizes.push_back(requestedSize);
        }
        const QSize side = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(2000, 2000);
        QImage image(side, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(40, 80, 120));
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

    [[nodiscard]] std::vector<QSize> sizes() const
    {
        const QMutexLocker locker(&m_lock);
        return m_sizes;
    }

private:
    mutable QMutex m_lock;
    std::vector<QSize> m_sizes;
};

// Holds every picture back until the test lets it through; "missing" never
// has one.
class GatedProvider final : public QQuickImageProvider {
public:
    GatedProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        if (id == QStringLiteral("missing")) {
            return {};
        }
        m_gate.acquire();
        QImage image(requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(8, 8),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(40, 80, 120));
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

    void open() { m_gate.release(); }

private:
    QSemaphore m_gate;
};

constexpr char TileQml[] = R"(
import QtQuick
import LastMusicPlayer
ArtTile {
    width: 0
    height: 0
}
)";

[[nodiscard]] bool allSized(const std::vector<QSize>& sizes)
{
    for (const QSize& size : sizes) {
        if (!size.isValid() || size.isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace

// A cover tile decodes its picture at its own size, never at full size.
class ArtTileTest : public QObject {
    Q_OBJECT

private slots:
    void picturesAreOnlyAskedForAtTheTilesSize()
    {
        QQmlEngine engine;
        auto* provider = new RecordingProvider(); // the engine owns it
        engine.addImageProvider(QStringLiteral("recording"), provider);
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setProperty("source", QUrl(QStringLiteral("image://recording/cover")));

        // Without a size or a window there is nothing to decode at.
        QTest::qWait(100);
        QVERIFY(provider->sizes().empty());

        tile->setParentItem(window.contentItem());
        QTest::qWait(100);
        QVERIFY(provider->sizes().empty());

        tile->setSize(QSizeF(100, 100));
        QTRY_COMPARE(provider->sizes().size(), std::size_t{1});
        const QSize decoded = provider->sizes().front();
        QVERIFY(!decoded.isEmpty());
        QTRY_VERIFY(tile->property("loaded").toBool());

        // Torn down: losing the size or the window loads nothing again.
        tile->setSize(QSizeF(0, 0));
        tile->setParentItem(nullptr);
        QTest::qWait(100);
        QCOMPARE(provider->sizes().size(), std::size_t{1});

        // A new picture comes at the size the tile last had.
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        tile->setProperty("source", QUrl(QStringLiteral("image://recording/other")));
        QTRY_COMPARE(provider->sizes().size(), std::size_t{2});
        QCOMPARE(provider->sizes().back(), decoded);
        QVERIFY(allSized(provider->sizes()));
    }

    // Loading shows the skeleton block, a loaded picture neither placeholder,
    // and no picture (none given, or one that fails) the lettered tile.
    void placeholdersFollowThePicture()
    {
        QQmlEngine engine;
        auto* provider = new GatedProvider(); // the engine owns it
        engine.addImageProvider(QStringLiteral("gated"), provider);
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        auto* skeleton = tile->findChild<QQuickItem*>(QStringLiteral("coverSkeleton"));
        auto* letter = tile->findChild<QObject*>(QStringLiteral("coverLetterTile"));
        QVERIFY(skeleton);
        QVERIFY(letter);
        const auto letterShown = [letter]() { return letter->property("active").toBool(); };

        // No picture at all.
        QVERIFY(letterShown());
        QVERIFY(!skeleton->isVisible());

        // On its way.
        tile->setProperty("source", QUrl(QStringLiteral("image://gated/cover")));
        QTRY_VERIFY(skeleton->isVisible());
        QVERIFY(!letterShown());

        // Loaded.
        provider->open();
        QTRY_VERIFY(tile->property("loaded").toBool());
        QTRY_VERIFY(!skeleton->isVisible());
        QVERIFY(!letterShown());

        // A picture that cannot load.
        tile->setProperty("source", QUrl(QStringLiteral("image://gated/missing")));
        QTRY_VERIFY(letterShown());
        QVERIFY(!skeleton->isVisible());
        QVERIFY(!tile->property("loaded").toBool());
    }

    // While the owner is still loading, a tile with no picture yet shows the
    // skeleton, not the lettered tile; once loading ends with no picture, the
    // lettered tile shows.
    void aPendingTileWithoutAPictureShowsTheSkeleton()
    {
        QQmlEngine engine;
        QQuickWindow window; // never shown
        QQmlComponent component(&engine);
        component.setData(TileQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tile = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(tile);
        tile->setParentItem(window.contentItem());
        tile->setSize(QSizeF(100, 100));
        auto* skeleton = tile->findChild<QQuickItem*>(QStringLiteral("coverSkeleton"));
        auto* letter = tile->findChild<QObject*>(QStringLiteral("coverLetterTile"));
        QVERIFY(skeleton);
        QVERIFY(letter);

        tile->setProperty("pending", true);
        QTRY_VERIFY(skeleton->isVisible());
        QVERIFY(!letter->property("active").toBool());
        QVERIFY(!tile->property("pictureMissing").toBool());

        tile->setProperty("pending", false);
        QTRY_VERIFY(letter->property("active").toBool());
        QVERIFY(!skeleton->isVisible());
    }
};

QTEST_MAIN(ArtTileTest)
#include "tst_art_tile.moc"
