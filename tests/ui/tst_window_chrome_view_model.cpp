#include <QObject>
#include <QRectF>
#include <QSignalSpy>
#include <QTest>

#include <utility>

#include "platform/IWindowChrome.h"
#include "ui/shell/WindowChromeViewModel.h"

namespace {

using lmp::platform::WindowBackdrop;

// Records the backdrop asked for and grants every one.
class FakeChrome final : public lmp::platform::IWindowChrome {
public:
    bool attach(quintptr) override { return true; }
    void setDarkFrame(bool) override {}
    bool setBackdrop(WindowBackdrop backdrop) override
    {
        asked = backdrop;
        ++calls;
        if (m_handler) {
            m_handler();
        }
        return backdrop != WindowBackdrop::None;
    }
    bool backdropActive() const override { return asked != WindowBackdrop::None; }
    void setDragStripHeight(qreal) override {}
    void setCaptionButtons(const QRectF&, const QRectF&, const QRectF&) override {}
    void minimize() override {}
    void toggleMaximize() override {}
    void close() override {}
    bool maximized() const override { return false; }
    bool active() const override { return true; }
    bool maximizeHovered() const override { return false; }
    bool maximizePressed() const override { return false; }
    bool moving() const override { return false; }
    void setStateHandler(StateHandler handler) override { m_handler = std::move(handler); }

    WindowBackdrop asked = WindowBackdrop::None;
    int calls = 0;

private:
    StateHandler m_handler;
};

} // namespace

// A backdrop is asked of Windows only on a run whose window Windows can see
// through; on any other run the choice waits for a restart.
class WindowChromeViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void opaqueRunWaitsForARestart()
    {
        FakeChrome chrome;
        lmp::ui::WindowChromeViewModel model(&chrome);
        model.setBackdropSupported(false);
        model.setAttached(true);

        model.setBackdrop(WindowBackdrop::Mica);
        QCOMPARE(chrome.asked, WindowBackdrop::None);
        QVERIFY(model.backdropNeedsRestart());
        QVERIFY(!model.backdropActive());

        // Back to Solid: nothing to wait for.
        model.setBackdrop(WindowBackdrop::None);
        QVERIFY(!model.backdropNeedsRestart());
    }

    void seeThroughRunSwitchesAtOnce()
    {
        FakeChrome chrome;
        lmp::ui::WindowChromeViewModel model(&chrome);
        model.setBackdropSupported(true);
        model.setAttached(true);
        QSignalSpy changed(&model, &lmp::ui::WindowChromeViewModel::changed);

        model.setBackdrop(WindowBackdrop::Acrylic);
        QCOMPARE(chrome.asked, WindowBackdrop::Acrylic);
        QVERIFY(model.backdropActive());
        QVERIFY(!model.backdropNeedsRestart());
        QVERIFY(changed.count() > 0);

        model.setBackdrop(WindowBackdrop::Mica);
        QCOMPARE(chrome.asked, WindowBackdrop::Mica);
        model.setBackdrop(WindowBackdrop::None);
        QCOMPARE(chrome.asked, WindowBackdrop::None);
        QVERIFY(!model.backdropActive());
    }

    void nothingIsAskedBeforeTheWindowIsAttached()
    {
        FakeChrome chrome;
        lmp::ui::WindowChromeViewModel model(&chrome);
        model.setBackdropSupported(true);
        model.setBackdrop(WindowBackdrop::Mica);
        QCOMPARE(chrome.calls, 0);
    }
};

QTEST_GUILESS_MAIN(WindowChromeViewModelTest)
#include "tst_window_chrome_view_model.moc"
