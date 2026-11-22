#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUuid>

#include <optional>
#include <string>

#include "platform/windows/WindowsCredentialStore.h"

// Last, so its macros cannot reach the Qt headers.
#include <windows.h>
#include <wincred.h>

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::platform::CredentialRemoval;
using lmp::platform::windows::WindowsCredentialStore;

namespace {

// Writes a blob directly, bypassing the store's own checks, to plant values
// the store must refuse to read.
bool writeRawBlob(const QString& target, QByteArray blob)
{
    std::wstring targetName = target.toStdWString();
    std::wstring userName(L"Last Music Player");
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = targetName.data();
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    // The API types the byte buffer as LPBYTE.
    credential.CredentialBlob = blob.isEmpty() ? nullptr : reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_SESSION;
    credential.UserName = userName.data();
    return CredWriteW(&credential, 0) != FALSE;
}

QString valueOf(const Result<std::optional<QString>>& result)
{
    if (!result) {
        return QStringLiteral("<failed>");
    }
    return result.value().value_or(QStringLiteral("<none>"));
}

} // namespace

class WindowsCredentialStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void writeStoresTheInstalledFormat();
    void writeReplacesAndRemoveDeletes();
    void missingCredentialReadsAsNothing();
    void conditionalRemoval();
    void unreadableBlobsAreFailures();
    void writeRefusesWhatCannotBeStored();

private:
    // Never one of the application's real targets.
    QString m_target;
};

void WindowsCredentialStoreTest::init()
{
    m_target = QStringLiteral("LastMusicPlayer/Test/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void WindowsCredentialStoreTest::cleanup()
{
    // Runs after every test, also a failed one, so no test entry is left.
    const std::wstring targetName = m_target.toStdWString();
    if (CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0) == FALSE) {
        QCOMPARE(GetLastError(), static_cast<DWORD>(ERROR_NOT_FOUND));
    }
}

void WindowsCredentialStoreTest::writeStoresTheInstalledFormat()
{
    WindowsCredentialStore store;
    const QString secret = QString::fromUtf8("s\xC3\xA9" "cret-\xE2\x98\x83");
    QVERIFY(store.write(m_target, secret));
    QCOMPARE(valueOf(store.read(m_target)), secret);

    const std::wstring targetName = m_target.toStdWString();
    PCREDENTIALW raw = nullptr;
    QVERIFY(CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &raw) != FALSE);
    const DWORD type = raw->Type;
    const DWORD persist = raw->Persist;
    const QString userName = QString::fromWCharArray(raw->UserName);
    // The API types the byte buffer as LPBYTE.
    const QByteArray blob(reinterpret_cast<const char*>(raw->CredentialBlob),
                          static_cast<qsizetype>(raw->CredentialBlobSize));
    CredFree(raw);

    QCOMPARE(type, static_cast<DWORD>(CRED_TYPE_GENERIC));
    QCOMPARE(persist, static_cast<DWORD>(CRED_PERSIST_LOCAL_MACHINE));
    QCOMPARE(userName, QStringLiteral("Last Music Player"));
    // Raw UTF-8, no terminator, no wrapper.
    QCOMPARE(blob, secret.toUtf8());
}

void WindowsCredentialStoreTest::writeReplacesAndRemoveDeletes()
{
    WindowsCredentialStore store;
    QVERIFY(store.write(m_target, QStringLiteral("first")));
    QVERIFY(store.write(m_target, QStringLiteral("second")));
    QCOMPARE(valueOf(store.read(m_target)), QStringLiteral("second"));

    QVERIFY(store.remove(m_target));
    QCOMPARE(valueOf(store.read(m_target)), QStringLiteral("<none>"));
    // Removing what is already gone succeeds.
    QVERIFY(store.remove(m_target));
}

void WindowsCredentialStoreTest::missingCredentialReadsAsNothing()
{
    WindowsCredentialStore store;
    const Result<std::optional<QString>> result = store.read(m_target);
    QVERIFY(result);
    QVERIFY(!result.value().has_value());
}

void WindowsCredentialStoreTest::conditionalRemoval()
{
    WindowsCredentialStore store;
    QVERIFY(store.write(m_target, QStringLiteral("account-session")));

    const Result<CredentialRemoval> different = store.removeIfMatches(m_target, QStringLiteral("other"));
    QVERIFY(different);
    QCOMPARE(different.value(), CredentialRemoval::Mismatch);
    const Result<CredentialRemoval> empty = store.removeIfMatches(m_target, QString());
    QVERIFY(empty);
    QCOMPARE(empty.value(), CredentialRemoval::Mismatch);
    QCOMPARE(valueOf(store.read(m_target)), QStringLiteral("account-session"));

    const Result<CredentialRemoval> matching = store.removeIfMatches(m_target, QStringLiteral("account-session"));
    QVERIFY(matching);
    QCOMPARE(matching.value(), CredentialRemoval::Removed);
    QCOMPARE(valueOf(store.read(m_target)), QStringLiteral("<none>"));

    const Result<CredentialRemoval> again = store.removeIfMatches(m_target, QStringLiteral("account-session"));
    QVERIFY(again);
    QCOMPARE(again.value(), CredentialRemoval::NotFound);
}

void WindowsCredentialStoreTest::unreadableBlobsAreFailures()
{
    WindowsCredentialStore store;

    QVERIFY(writeRawBlob(m_target, QByteArray("\xFF\xFE", 2)));
    const Result<std::optional<QString>> invalid = store.read(m_target);
    QVERIFY(!invalid);
    QCOMPARE(invalid.failure().kind, FailureKind::InvalidResponse);
    // An entry that cannot be compared is never removed by a conditional removal.
    const Result<CredentialRemoval> removal = store.removeIfMatches(m_target, QStringLiteral("anything"));
    QVERIFY(!removal);
    QCOMPARE(removal.failure().kind, FailureKind::InvalidResponse);

    QVERIFY(writeRawBlob(m_target, QByteArray()));
    const Result<std::optional<QString>> empty = store.read(m_target);
    QVERIFY(!empty);
    QCOMPARE(empty.failure().kind, FailureKind::InvalidResponse);
}

void WindowsCredentialStoreTest::writeRefusesWhatCannotBeStored()
{
    WindowsCredentialStore store;

    const Result<void> empty = store.write(m_target, QString());
    QVERIFY(!empty);
    QCOMPARE(empty.failure().kind, FailureKind::Storage);

    const QChar unpaired(static_cast<char16_t>(0xD800));
    const Result<void> surrogate = store.write(m_target, QString(unpaired));
    QVERIFY(!surrogate);
    QCOMPARE(surrogate.failure().kind, FailureKind::Storage);

    const Result<void> tooLarge =
        store.write(m_target, QString(static_cast<qsizetype>(CRED_MAX_CREDENTIAL_BLOB_SIZE) + 1, QLatin1Char('k')));
    QVERIFY(!tooLarge);
    QCOMPARE(tooLarge.failure().kind, FailureKind::Storage);

    const Result<void> unnamed = store.write(QString(), QStringLiteral("secret"));
    QVERIFY(!unnamed);

    QCOMPARE(valueOf(store.read(m_target)), QStringLiteral("<none>"));
}

QTEST_GUILESS_MAIN(WindowsCredentialStoreTest)
#include "tst_windows_credential_store.moc"
