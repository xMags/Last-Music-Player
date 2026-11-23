#pragma once

#include "core/playback/StreamCache.h"

#include <QNetworkAccessManager>

namespace lmp::core {

// Fetches a stream over HTTP(S) into a file, writing each piece as it
// arrives, so a whole song is never held in memory. Redirects are followed
// only to the same origin (the address carries the provider's key). A
// transfer that stalls for 30 s fails. The address is never logged.
//
// GUI thread only.
class HttpStreamDownloader final : public QObject, public IStreamDownloader {
    Q_OBJECT

public:
    static constexpr int StallTimeoutMs = 30000;

    explicit HttpStreamDownloader(QObject* parent = nullptr);

    [[nodiscard]] QFuture<Result<QString>> download(const QUrl& streamUrl, const QString& partPath) override;

private:
    QNetworkAccessManager m_network;
};

} // namespace lmp::core
