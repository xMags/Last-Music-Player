#include "core/playback/HttpStreamDownloader.h"

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>

#include <memory>

namespace lmp::core {

HttpStreamDownloader::HttpStreamDownloader(QObject* parent)
    : QObject(parent)
{
}

QFuture<Result<QString>> HttpStreamDownloader::download(const QUrl& streamUrl, const QString& partPath)
{
    auto promise = std::make_shared<QPromise<Result<QString>>>();
    promise->start();
    QFuture<Result<QString>> future = promise->future();
    const auto finishWith = [promise](Result<QString> result) {
        promise->addResult(std::move(result));
        promise->finish();
    };

    auto file = std::make_shared<QFile>(partPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishWith(Failure::of(FailureKind::Unknown, QStringLiteral("The cache file could not be written.")));
        return future;
    }
    QNetworkRequest request(streamUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(StallTimeoutMs);
    QNetworkReply* reply = m_network.get(request);

    auto failed = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, reply, [reply, file, failed]() {
        const QByteArray piece = reply->readAll();
        if (!*failed && file->write(piece) != piece.size()) {
            *failed = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, reply, [reply, file, failed, finishWith]() {
        reply->deleteLater();
        const QByteArray rest = reply->readAll();
        if (!*failed && !rest.isEmpty() && file->write(rest) != rest.size()) {
            *failed = true;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = !*failed && reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
            && file->flush();
        file->close();
        if (!ok) {
            finishWith(Failure::of(FailureKind::Network, QStringLiteral("The song could not be fetched ahead.")));
            return;
        }
        finishWith(streamExtensionForContentType(reply->header(QNetworkRequest::ContentTypeHeader).toString()));
    });
    return future;
}

} // namespace lmp::core
