#include "WhisperCppBackend.h"

#include <QByteArray>
#include <QEventLoop>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "util/Wav.h"

WhisperCppBackend::WhisperCppBackend() = default;

bool WhisperCppBackend::load(const QString &, const QString &,
                             QString *effectiveDevice, QString *err) {
    // Re-read the endpoint on every load so config changes apply without a
    // restart (a backend instance can outlive many settings edits).
    m_endpoint = QSettings()
                     .value(QStringLiteral("whispercpp/endpoint"),
                            QStringLiteral("http://127.0.0.1:8080"))
                     .toString();
    // The server owns the model+device; just confirm it's reachable.
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(m_endpoint)};
    req.setTransferTimeout(3000);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const bool ok = reply->error() == QNetworkReply::NoError ||
                    reply->error() == QNetworkReply::ContentNotFoundError;
    reply->deleteLater();
    if (!ok) {
        *err = QStringLiteral("whisper-server not reachable at %1 (%2).")
                   .arg(m_endpoint, reply->errorString());
        return false;
    }
    *effectiveDevice = QStringLiteral("whisper-server");
    return true;
}

bool WhisperCppBackend::transcribe(const std::vector<float> &pcm16k,
                                   const QString &language, QString *text,
                                   QString *err) {
    QNetworkAccessManager nam;
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"a.wav\""));
    file.setBody(scrybe::encodeWav16(pcm16k));
    multi->append(file);

    auto addField = [&](const QString &name, const QString &value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"%1\"").arg(name));
        p.setBody(value.toUtf8());
        multi->append(p);
    };
    addField(QStringLiteral("response_format"), QStringLiteral("json"));
    if (!language.isEmpty() && language != QLatin1String("auto"))
        addField(QStringLiteral("language"), language);

    QNetworkRequest req{QUrl(m_endpoint + QStringLiteral("/inference"))};
    req.setTransferTimeout(120000);
    QNetworkReply *reply = nam.post(req, multi);
    multi->setParent(reply);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        *err = QStringLiteral("whisper-server request failed: %1")
                   .arg(reply->errorString());
        reply->deleteLater();
        return false;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    *text = obj.value(QStringLiteral("text")).toString().trimmed();
    return true;
}
