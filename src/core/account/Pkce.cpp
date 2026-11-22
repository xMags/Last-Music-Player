#include "core/account/Pkce.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

#include <array>

namespace lmp::core::account {

namespace {

constexpr qsizetype kMinVerifierLength = 43;
constexpr qsizetype kMaxVerifierLength = 128;
constexpr qsizetype kVerifierRandomBytes = 32;
constexpr qsizetype kStateRandomBytes = 24;

QString base64Url(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// Bytes from the system generator, which draws on the operating system's
// cryptographic source.
template <qsizetype Size>
QByteArray randomBytes()
{
    static_assert(Size % sizeof(quint32) == 0, "filled in whole 32-bit words");
    std::array<quint32, Size / sizeof(quint32)> words{};
    QRandomGenerator::system()->fillRange(words.data(), static_cast<qsizetype>(words.size()));
    QByteArray bytes;
    bytes.reserve(Size);
    for (const quint32 word : words) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.append(static_cast<char>((word >> shift) & 0xffU));
        }
    }
    return bytes;
}

} // namespace

Result<QString> createPkceChallenge(const QString& verifier)
{
    if (verifier.size() < kMinVerifierLength || verifier.size() > kMaxVerifierLength) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("PKCE verifier length is invalid"));
    }
    QByteArray ascii;
    ascii.reserve(verifier.size());
    for (const QChar character : verifier) {
        if (character.unicode() > 0x7f) {
            return Failure::of(FailureKind::Unknown, QStringLiteral("PKCE verifier must be ASCII"));
        }
        ascii.append(static_cast<char>(character.unicode()));
    }
    return base64Url(QCryptographicHash::hash(ascii, QCryptographicHash::Sha256));
}

PkceTransaction createPkceTransaction()
{
    PkceTransaction transaction;
    transaction.verifier = base64Url(randomBytes<kVerifierRandomBytes>());
    // A 43-character ASCII verifier always passes the length and alphabet rule.
    transaction.challenge = createPkceChallenge(transaction.verifier).valueOr(QString());
    transaction.state = base64Url(randomBytes<kStateRandomBytes>());
    return transaction;
}

} // namespace lmp::core::account
