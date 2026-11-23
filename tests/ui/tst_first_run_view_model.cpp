#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "core/mode/SourceChoice.h"
#include "core/mode/SourceMode.h"
#include "core/settings/SettingKeys.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/settings/FirstRunViewModel.h"

using lmp::core::mode::SourceChoiceInputs;
using lmp::core::mode::SourceMode;
using lmp::ui::FirstRunViewModel;
using lmp::ui::Route;
namespace keys = lmp::core::settings::keys;

// The first-run page records SourceChoiceSeen and ends on a choice.
class FirstRunViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cleanFolderShowsThePageAndMarksIt();
    void upgradingUserNeverSeesIt();
    void continueKeepsOfflineAndRecordsIt();
    void choosingApiKeyEndsItInSettings();

private:
    [[nodiscard]] QString seenValue() const;

    std::unique_ptr<lmp::test::ModeServiceFixture> m_fixture;
    std::unique_ptr<lmp::ui::Navigator> m_navigator;
    std::unique_ptr<FirstRunViewModel> m_model;
};

void FirstRunViewModelTest::init()
{
    m_fixture = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_fixture->modes.start();
    m_navigator = std::make_unique<lmp::ui::Navigator>();
    m_model = std::make_unique<FirstRunViewModel>(m_fixture->session.settings, m_fixture->modes, *m_navigator);
}

void FirstRunViewModelTest::cleanup()
{
    m_model.reset();
    m_navigator.reset();
    m_fixture.reset();
}

QString FirstRunViewModelTest::seenValue() const
{
    auto& settings = m_fixture->session.settings;
    if (!settings.contains(keys::SourceChoiceSeen)) {
        return QStringLiteral("<none>");
    }
    return settings.boolean(keys::SourceChoiceSeen, false) ? QStringLiteral("true") : QStringLiteral("false");
}

void FirstRunViewModelTest::cleanFolderShowsThePageAndMarksIt()
{
    QSignalSpy shown(m_model.get(), &FirstRunViewModel::shownChanged);
    m_model->decide(SourceChoiceInputs{});
    QVERIFY(m_model->shown());
    QCOMPARE(shown.count(), 1);
    QCOMPARE(seenValue(), QStringLiteral("false"));
}

void FirstRunViewModelTest::upgradingUserNeverSeesIt()
{
    SourceChoiceInputs inputs;
    inputs.modeStoredBefore = true;
    m_model->decide(inputs);
    QVERIFY(!m_model->shown());
    QCOMPARE(seenValue(), QStringLiteral("<none>"));
}

void FirstRunViewModelTest::continueKeepsOfflineAndRecordsIt()
{
    m_model->decide(SourceChoiceInputs{});
    m_model->finish();
    QVERIFY(!m_model->shown());
    QCOMPARE(seenValue(), QStringLiteral("true"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
}

void FirstRunViewModelTest::choosingApiKeyEndsItInSettings()
{
    m_model->decide(SourceChoiceInputs{});
    QVERIFY(m_fixture->modes.setMode(SourceMode::ApiKey));
    QVERIFY(!m_model->shown());
    QCOMPARE(seenValue(), QStringLiteral("true"));
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Settings);
}

QTEST_MAIN(FirstRunViewModelTest)
#include "tst_first_run_view_model.moc"
