#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Lightweight GitHub-based update checker.
//
// Reads a plain-text VERSION file from the repo's default branch
// (raw.githubusercontent.com/<owner>/<repo>/<branch>/VERSION), compares it with
// the built-in SCRYBE_VERSION using semantic-version ordering, and exposes the
// result to the UI. Applying an update just re-runs the online installer (which
// pulls the latest source and rebuilds) in a terminal.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit Updater(QObject *parent = nullptr);

    QString currentVersion() const { return m_current; }
    QString latestVersion() const { return m_latest; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool checking() const { return m_checking; }
    QString status() const { return m_status; }

    // Query GitHub for the latest version. `silent` suppresses the "up to date"
    // status text (used for the automatic check on launch).
    Q_INVOKABLE void check(bool silent = false);

    // Launch the online installer in a terminal to pull + rebuild the latest.
    Q_INVOKABLE void installUpdate();

signals:
    void changed();
    void notify(const QString &message);   // surfaced via the tray

private:
    void setStatus(const QString &s);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_current;
    QString m_latest;
    QString m_status;
    bool m_updateAvailable = false;
    bool m_checking = false;
};
