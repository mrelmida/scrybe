#include "Resample.h"

#include <QtGlobal>

namespace scrybe {

std::vector<float> resampleTo16k(const QVector<float> &pcm, int srcRate) {
    std::vector<float> out;
    if (pcm.isEmpty())
        return out;
    if (srcRate == 16000 || srcRate <= 0) {
        out.assign(pcm.begin(), pcm.end());
        return out;
    }
    const double ratio = 16000.0 / double(srcRate);
    const auto dstN = qint64(pcm.size() * ratio);
    out.reserve(size_t(dstN));
    for (qint64 i = 0; i < dstN; ++i) {
        const double srcPos = i / ratio;
        const auto i0 = qint64(srcPos);
        const auto i1 = qMin<qint64>(i0 + 1, pcm.size() - 1);
        const double frac = srcPos - i0;
        out.push_back(float(pcm[i0] * (1.0 - frac) + pcm[i1] * frac));
    }
    return out;
}

} // namespace scrybe
