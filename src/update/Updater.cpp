#include "Updater.h"

#include "util/Terminal.h"
#include "util/Version.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace {
// Where to look for the latest published version + how to update. Overridable
// via QSettings so a fork/mirror can point elsewhere without a rebuild.
constexpr char kDefaultVersionUrl[] =
    "https://raw.githubusercontent.com/mrelmida/scrybe/main/VERSION";
constexpr char kDefaultInstallUrl[] =
    "https://raw.githubusercontent.com/mrelmida/scrybe/main/install.sh";
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
        m_updateAvailable = scrybe::isNewerVersion(latest, m_current);
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
    // Prefer a local checkout if present. Otherwise download the installer to a
    // temp file first (never `curl | bash`: a dropped connection can't execute a
    // half-downloaded script) and verify its SHA-256 when a companion
    // `install.sh.sha256` is published alongside it.
    const QString shellCmd = QStringLiteral(
        "set -e; echo 'Updating Scrybe…'; "
        "if [ -f \"$HOME/.local/src/scrybe/install.sh\" ]; then "
        "  bash \"$HOME/.local/src/scrybe/install.sh\"; "
        "else "
        "  tmp=$(mktemp /tmp/scrybe-install.XXXXXX.sh); trap 'rm -f \"$tmp\"' EXIT; "
        "  curl -fsSL '%1' -o \"$tmp\"; "
        "  if sum=$(curl -fsSL '%1.sha256' 2>/dev/null) && [ -n \"$sum\" ]; then "
        "    echo \"${sum%% *}  $tmp\" | sha256sum -c - "
        "      || { echo 'Installer checksum mismatch — aborting.'; exit 1; }; "
        "  else echo 'No published checksum; continuing without verification.'; fi; "
        "  bash \"$tmp\"; "
        "fi; "
        "echo; echo 'Update finished. Restart Scrybe to run the new version.'")
        .arg(installUrl);

    if (!scrybe::launchTerminal(shellCmd)) {
        emit notify(tr("No terminal found. Update manually by downloading and "
                       "running %1").arg(installUrl));
        return;
    }
    emit notify(tr("Updating Scrybe in a terminal…"));
}
