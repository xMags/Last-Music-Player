#pragma once

#include "core/downloads/DownloadManager.h"

#include <QNetworkAccessManager>

namespace lmp::core {

// Streams a download over HTTP(S) into its file, a piece at a time, so a
// song is never held in memory. A non-zero offset asks for the rest with a
// range request; a server that answers 200 instead starts the file again.
// Redirects are followed to the same origin only (a stream address can
// carry a key). A transfer that stalls for 30 s fails. The address is never
// logged.
//
// GUI thread only.
class HttpDownloadFetcher final : public QObject, public IDownloadFetcher {
    Q_OBJECT

public:
    static constexpr int StallTimeoutMs = 30000;

    explicit HttpDownloadFetcher(QObject* parent = nullptr);

    [[nodiscard]] QFuture<Result<Outcome>> fetch(const QUrl& url, const QString& filePath, std::uint64_t offset,
                                                 const QString& ifRange, Stop stop, Progress progress,
                                                 Validated validated) override;

private:
    QNetworkAccessManager m_network;
};

} // namespace lmp::core
