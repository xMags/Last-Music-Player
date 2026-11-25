#include "core/downloads/HttpDownloadFetcher.h"

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>

#include <memory>

namespace lmp::core {

HttpDownloadFetcher::HttpDownloadFetcher(QObject* parent)
    : QObject(parent)
{
}

QFuture<Result<IDownloadFetcher::Outcome>> HttpDownloadFetcher::fetch(const QUrl& url, const QString& filePath,
                                                                      std::uint64_t offset, const QString& ifRange,
                                                                      Stop stop, Progress progress,
                                                                      Validated validated)
{
    auto promise = std::make_shared<QPromise<Result<Outcome>>>();
    promise->start();
    QFuture<Result<Outcome>> future = promise->future();

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(StallTimeoutMs);
    if (offset > 0) {
        request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(static_cast<qulonglong>(offset)) + '-');
        // The range only of the same file: a changed one comes back whole
        // (200), and the file is written from the start.
        if (!ifRange.isEmpty()) {
            request.setRawHeader("If-Range", ifRange.toUtf8());
        }
    }
    QNetworkReply* reply = m_network.get(request);
    // The file's validator, for a later resume: a strong ETag, else the
    // Last-Modified date (a weak ETag cannot be used for If-Range).
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, validated]() {
        if (!validated) {
            return;
        }
        const QByteArray etag = reply->rawHeader("ETag");
        if (!etag.isEmpty() && !etag.startsWith("W/")) {
            validated(QString::fromLatin1(etag));
            return;
        }
        const QByteArray modified = reply->rawHeader("Last-Modified");
        validated(QString::fromLatin1(modified));
    });

    struct Transfer {
        QFile file;
        bool opened = false;
        bool append = false;
        bool stopped = false;
        bool writeFailed = false;
        std::uint64_t written = 0;
        std::uint64_t total = 0;
    };
    auto transfer = std::make_shared<Transfer>();
    transfer->file.setFileName(filePath);

    // Opens the file once the status says whether the range was honoured.
    const auto openFile = [reply, transfer, offset]() {
        if (transfer->opened) {
            return transfer->file.isOpen();
        }
        transfer->opened = true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        transfer->append = offset > 0 && status == 206;
        const QIODevice::OpenMode mode =
            QIODevice::WriteOnly | (transfer->append ? QIODevice::Append : QIODevice::Truncate);
        if (!transfer->file.open(mode)) {
            return false;
        }
        transfer->written = transfer->append ? offset : 0;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        transfer->total = length > 0 ? transfer->written + static_cast<std::uint64_t>(length) : 0;
        return true;
    };
    const auto writeAvailable = [reply, transfer, openFile, progress]() {
        const QByteArray piece = reply->readAll();
        if (piece.isEmpty() || transfer->writeFailed) {
            return;
        }
        if (!openFile() || transfer->file.write(piece) != piece.size()) {
            transfer->writeFailed = true;
            reply->abort();
            return;
        }
        transfer->written += static_cast<std::uint64_t>(piece.size());
        if (progress) {
            progress(transfer->written, transfer->total);
        }
    };

    connect(reply, &QNetworkReply::readyRead, reply, [reply, transfer, writeAvailable, stop]() {
        if (stop && stop()) {
            transfer->stopped = true;
            reply->abort();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            writeAvailable();
        }
    });
    connect(reply, &QNetworkReply::finished, reply, [reply, transfer, writeAvailable, promise]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = status >= 200 && status < 300;
        if (success && !transfer->stopped) {
            writeAvailable();
        }
        if (transfer->file.isOpen()) {
            transfer->file.flush();
            transfer->file.close();
        }
        Result<Outcome> result = Failure::of(FailureKind::Network, QStringLiteral("The download was interrupted"));
        if (transfer->stopped) {
            result = Outcome{.stopped = true, .appended = transfer->append, .mediaType = {}};
        } else if (transfer->writeFailed) {
            result = Failure::of(FailureKind::Unknown, QStringLiteral("Could not write to the download folder"));
        } else if (status >= 400) {
            result = Failure::of(FailureKind::Server, QStringLiteral("The media server rejected the download"), status);
        } else if (success && reply->error() == QNetworkReply::NoError) {
            result = Outcome{.stopped = false,
                             .appended = transfer->append,
                             .mediaType = reply->header(QNetworkRequest::ContentTypeHeader).toString()};
        }
        promise->addResult(std::move(result));
        promise->finish();
    });
    return future;
}

} // namespace lmp::core
