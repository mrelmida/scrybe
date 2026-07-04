#include "Wav.h"

#include <QFile>
#include <QtGlobal>

namespace scrybe {

QVector<float> decodeWav(const QByteArray &d, int *sampleRate) {
    QVector<float> out;
    if (d.size() < 44 || d.left(4) != "RIFF" || d.mid(8, 4) != "WAVE")
        return out;

    quint16 fmt = 0, channels = 1, bits = 16;
    quint32 rate = 16000;
    int pos = 12, dataOff = -1, dataLen = 0;
    auto u16 = [&](int o) { return quint16(quint8(d[o]) | (quint8(d[o + 1]) << 8)); };
    auto u32 = [&](int o) {
        return quint32(quint8(d[o]) | (quint8(d[o + 1]) << 8) |
                       (quint8(d[o + 2]) << 16) | (quint8(d[o + 3]) << 24));
    };
    while (pos + 8 <= d.size()) {
        const QByteArray id = d.mid(pos, 4);
        const quint32 sz = u32(pos + 4);
        const int body = pos + 8;
        if (body > d.size())
            break;
        if (id == "fmt " && body + 16 <= d.size()) {
            fmt = u16(body); channels = u16(body + 2);
            rate = u32(body + 4); bits = u16(body + 14);
        } else if (id == "data") {
            dataOff = body; dataLen = int(qMin<quint32>(sz, quint32(d.size() - body)));
        }
        pos = body + int(sz) + (sz & 1);
    }
    if (dataOff < 0)
        return out;
    if (sampleRate)
        *sampleRate = int(rate);

    const int ch = qMax<quint16>(1, channels);
    if (fmt == 3 && bits == 32) {          // float32
        const int n = dataLen / 4;
        const auto *s = reinterpret_cast<const float *>(d.constData() + dataOff);
        for (int i = 0; i + ch <= n; i += ch) {
            float m = 0; for (int c = 0; c < ch; ++c) m += s[i + c];
            out.push_back(m / ch);
        }
    } else if (fmt == 1 && bits == 16) {   // PCM16
        const int n = dataLen / 2;
        const auto *s = reinterpret_cast<const qint16 *>(d.constData() + dataOff);
        for (int i = 0; i + ch <= n; i += ch) {
            float m = 0; for (int c = 0; c < ch; ++c) m += s[i + c] / 32768.f;
            out.push_back(m / ch);
        }
    }
    return out;
}

QVector<float> loadWavFile(const QString &path, int *sampleRate) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return decodeWav(f.readAll(), sampleRate);
}

QByteArray encodeWav16(const std::vector<float> &pcm, int sampleRate) {
    const int ch = 1, bits = 16;
    const int dataBytes = int(pcm.size()) * 2;
    QByteArray out;
    auto put32 = [&](quint32 v) { out.append(reinterpret_cast<const char *>(&v), 4); };
    auto put16 = [&](quint16 v) { out.append(reinterpret_cast<const char *>(&v), 2); };
    out.append("RIFF"); put32(36 + dataBytes); out.append("WAVE");
    out.append("fmt "); put32(16); put16(1); put16(quint16(ch));
    put32(quint32(sampleRate)); put32(quint32(sampleRate * ch * bits / 8));
    put16(quint16(ch * bits / 8)); put16(quint16(bits));
    out.append("data"); put32(quint32(dataBytes));
    for (float f : pcm) {
        int v = int(f * 32767.0f);
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        put16(quint16(qint16(v)));
    }
    return out;
}

} // namespace scrybe
