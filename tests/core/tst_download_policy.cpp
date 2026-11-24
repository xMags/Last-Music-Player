#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/downloads/DownloadPolicy.h"

using lmp::core::aggregateDownloadState;
using lmp::core::downloadExtensionForMediaType;
using lmp::core::DownloadItemState;
using lmp::core::downloadProgressPercent;
using lmp::core::downloadSchedulingAllowed;
using lmp::core::DownloadSchedulingConditions;

namespace {

// Same argument order as the scheduler's checks.
DownloadSchedulingConditions conditions(bool avoidMeteredNetworks, bool isMetered, bool downloadOnBattery,
                                        bool onBattery, bool allPaused, bool shuttingDown)
{
    return {avoidMeteredNetworks, isMetered, downloadOnBattery, onBattery, allPaused, shuttingDown};
}

} // namespace

class DownloadPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void downloadPolicy();
    void everyStateExtensionAndStop();
    void sizeTextMatchesSettings();
};

void DownloadPolicyTest::sizeTextMatchesSettings()
{
    using lmp::core::downloadSizeText;
    QCOMPARE(downloadSizeText(0), QStringLiteral("0 MB"));
    QCOMPARE(downloadSizeText(512), QStringLiteral("512 B"));
    QCOMPARE(downloadSizeText(1024), QStringLiteral("1 KB"));
    QCOMPARE(downloadSizeText(1536), QStringLiteral("2 KB"));
    QCOMPARE(downloadSizeText(1024ULL * 1024), QStringLiteral("1 MB"));
    QCOMPARE(downloadSizeText(12ULL * 1024 * 1024 + 600 * 1024), QStringLiteral("13 MB"));
    QCOMPARE(downloadSizeText(1024ULL * 1024 * 1024), QStringLiteral("1.0 GB"));
    QCOMPARE(downloadSizeText(1536ULL * 1024 * 1024), QStringLiteral("1.5 GB"));
}

void DownloadPolicyTest::downloadPolicy()
{
    QVERIFY(aggregateDownloadState({DownloadItemState::Completed, DownloadItemState::Downloading})
            == DownloadItemState::Downloading);
    QVERIFY(aggregateDownloadState({DownloadItemState::Completed, DownloadItemState::Queued})
            == DownloadItemState::Queued);
    QVERIFY(aggregateDownloadState({DownloadItemState::Completed, DownloadItemState::Failed})
            == DownloadItemState::Failed);
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/FLAC")), QStringLiteral(".flac"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("application/octet-stream")), QStringLiteral(".audio"));
    QCOMPARE(downloadProgressPercent(150, 100), 100.0);
    QCOMPARE(downloadProgressPercent(1, 0), 0.0);
    QVERIFY(!downloadSchedulingAllowed(conditions(true, true, true, false, false, false)));
    QVERIFY(downloadSchedulingAllowed(conditions(true, false, true, false, false, false)));
    QVERIFY(downloadSchedulingAllowed(conditions(false, true, true, false, false, false)));
    QVERIFY(!downloadSchedulingAllowed(conditions(false, false, false, true, false, false)));
    QVERIFY(downloadSchedulingAllowed(conditions(false, false, true, true, false, false)));
    QVERIFY(!downloadSchedulingAllowed(conditions(false, true, true, false, true, false)));
}

void DownloadPolicyTest::everyStateExtensionAndStop()
{
    QVERIFY(aggregateDownloadState({}) == DownloadItemState::Queued);
    QVERIFY(aggregateDownloadState({DownloadItemState::Completed, DownloadItemState::Completed})
            == DownloadItemState::Completed);
    QVERIFY(aggregateDownloadState({DownloadItemState::Failed, DownloadItemState::Paused})
            == DownloadItemState::Paused);
    QVERIFY(aggregateDownloadState({DownloadItemState::Paused, DownloadItemState::Queued})
            == DownloadItemState::Queued);

    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/mpeg")), QStringLiteral(".mp3"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/MP4")), QStringLiteral(".m4a"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/x-m4a")), QStringLiteral(".m4a"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/ogg")), QStringLiteral(".ogg"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/opus")), QStringLiteral(".opus"));
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/wav")), QStringLiteral(".wav"));
    // Earlier rules win: an Ogg container holding Opus is ".ogg".
    QCOMPARE(downloadExtensionForMediaType(QStringLiteral("audio/ogg; codecs=opus")), QStringLiteral(".ogg"));
    QCOMPARE(downloadExtensionForMediaType(QString()), QStringLiteral(".audio"));

    QCOMPARE(downloadProgressPercent(25, 100), 25.0);
    QCOMPARE(downloadProgressPercent(0, 100), 0.0);

    QVERIFY(!downloadSchedulingAllowed(conditions(false, false, true, false, false, true)));
    QVERIFY(downloadSchedulingAllowed(DownloadSchedulingConditions{}));
}

QTEST_GUILESS_MAIN(DownloadPolicyTest)
#include "tst_download_policy.moc"
