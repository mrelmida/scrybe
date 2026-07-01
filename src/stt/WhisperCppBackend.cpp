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

#include <cstring>

namespace {
// Encode 16 kHz mono float PCM as a 16-bit WAV blob for the server.
QByteArray toWav(const std::vector<float> &pcm) {
    const int rate = 16000, ch = 1, bits = 16;
    const int dataBytes = int(pcm.size()) * 2;
    QByteArray out;
    auto put32 = [&](quint32 v) { out.append(reinterpret_cast<const char *>(&v), 4); };
    auto put16 = [&](quint16 v) { out.append(reinterpret_cast<const char *>(&v), 2); };
    out.append("RIFF"); put32(36 + dataBytes); out.append("WAVE");
    out.append("fmt "); put32(16); put16(1); put16(ch);
    put32(rate); put32(rate * ch * bits / 8); put16(ch * bits / 8); put16(bits);
    out.append("data"); put32(dataBytes);
    for (float f : pcm) {
        int v = int(f * 32767.0f);
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        put16(quint16(qint16(v)));
    }
    return out;
}
} // namespace

WhisperCppBackend::WhisperCppBackend() {
    m_endpoint = QSettings()
                     .value(QStringLiteral("whispercpp/endpoint"),
                            QStringLiteral("http://127.0.0.1:8080"))
                     .toString();
}

bool WhisperCppBackend::load(const QString &, const QString &,
                             QString *effectiveDevice, QString *err) {
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
    file.setBody(toWav(pcm16k));
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
