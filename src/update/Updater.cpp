#include "Updater.h"

#include "util/Terminal.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace {
// Where to look for the latest published version + how to update. Overridable
// via QSettings so a fork/mirror can point elsewhere without a rebuild.
constexpr char kDefaultVersionUrl[] =
    "https://raw.githubusercontent.com/mrelmida/scrybe/main/VERSION";
constexpr char kDefaultInstallUrl[] =
    "https://raw.githubusercontent.com/mrelmida/scrybe/main/install.sh";

// Parse "1.4.10" → {1,4,10}; ignores a leading 'v' and any trailing suffix.
QVector<int> parseVersion(QString v) {
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v = v.mid(1);
    QVector<int> parts;
    const auto segs = v.split(QLatin1Char('.'));
    for (const QString &s : segs) {
        static const QRegularExpression re(QStringLiteral("^(\\d+)"));
        const auto m = re.match(s);
        parts.append(m.hasMatch() ? m.captured(1).toInt() : 0);
    }
    return parts;
}

// Returns true if `latest` is strictly newer than `current`.
bool isNewer(const QString &latest, const QString &current) {
    const QVector<int> a = parseVersion(latest);
    const QVector<int> b = parseVersion(current);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int ai = i < a.size() ? a[i] : 0;
        const int bi = i < b.size() ? b[i] : 0;
        if (ai != bi)
            return ai > bi;
    }
    return false;
}

} // namespace

Updater::Updater(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_current = QStringLiteral(SCRYBE_VERSION);
    m_status = QStringLiteral("Current version %1.").arg(m_current);
}

void Updater::setStatus(const QString &s) {
    if (m_status == s)
        return;
    m_status = s;
    emit changed();
}

void Updater::check(bool silent) {
    if (m_checking)
        return;
    m_checking = true;
    setStatus(QStringLiteral("Checking for updates…"));
    emit changed();

    const QString url = QSettings()
        .value(QStringLiteral("update/versionUrl"),
               QString::fromLatin1(kDefaultVersionUrl)).toString();
    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(10000);
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, silent]() {
        reply->deleteLater();
        m_checking = false;
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QStringLiteral("Update check failed: %1").arg(reply->errorString()));
            emit changed();
            return;
        }
        const QString latest =
            QString::fromUtf8(reply->readAll()).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
        if (latest.isEmpty()) {
            setStatus(QStringLiteral("Update check failed: empty version file."));
            emit changed();
            return;
        }
        m_latest = latest;
        m_updateAvailable = isNewer(latest, m_current);
        if (m_updateAvailable) {
            setStatus(QStringLiteral("Update available: %1 → %2").arg(m_current, latest));
            emit notify(QStringLiteral("Scrybe %1 is available (you have %2).")
                            .arg(latest, m_current));
        } else if (!silent) {
            setStatus(QStringLiteral("You're up to date (%1).").arg(m_current));
        } else {
            setStatus(QStringLiteral("Current version %1.").arg(m_current));
        }
        emit changed();
    });
}

void Updater::installUpdate() {
    const QString installUrl = QSettings()
        .value(QStringLiteral("update/installUrl"),
               QString::fromLatin1(kDefaultInstallUrl)).toString();
    // Prefer a local checkout if present, otherwise curl the online installer.
    const QString shellCmd = QStringLiteral(
        "set -e; echo 'Updating Scrybe…'; "
        "if [ -f \"$HOME/.local/src/scrybe/install.sh\" ]; then "
        "  bash \"$HOME/.local/src/scrybe/install.sh\"; "
        "else curl -fsSL '%1' | bash; fi; "
        "echo; echo 'Update finished. Restart Scrybe to run the new version.'")
        .arg(installUrl);

    if (!scrybe::launchTerminal(shellCmd)) {
        emit notify(QStringLiteral(
            "No terminal found. Update manually: curl -fsSL %1 | bash").arg(installUrl));
        return;
    }
    emit notify(QStringLiteral("Updating Scrybe in a terminal…"));
}
