#include "core/Controller.h"
#include "stt/Models.h"
#include "stt/SttEngine.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QKeySequence>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTimer>

#include <KGlobalAccel>
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include <cstdio>
#include <cstring>

namespace {
constexpr char kServerName[] = "scrybe.ipc";

// Map the model choice (QSettings stt/model) to its on-disk directory.
QString resolveModelDir() {
    QSettings s;
    const QString key =
        s.value(QStringLiteral("stt/model"), QStringLiteral("small")).toString();
    const QString root =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("scrybe/models"));
    return QDir(root).filePath(scrybe::modelSubdir(key));
}

// Minimal WAV reader (PCM16 / float32, mono or stereo) → mono float + rate.
QVector<float> loadWav(const QString &path, int *sampleRate) {
    QVector<float> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray d = f.readAll();
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
        if (id == "fmt ") {
            fmt = u16(body); channels = u16(body + 2);
            rate = u32(body + 4); bits = u16(body + 14);
        } else if (id == "data") {
            dataOff = body; dataLen = int(qMin<quint32>(sz, d.size() - body));
        }
        pos = body + int(sz) + (sz & 1);
    }
    if (dataOff < 0) return out;
    if (sampleRate) *sampleRate = int(rate);

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

// Headless STT test: transcribe a WAV and print the result. Used to verify the
// OpenVINO pipeline end-to-end without a microphone.
int runTranscribeFile(int argc, char **argv, const QString &wav,
                      const QString &device) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("scrybe"));
    app.setOrganizationName(QStringLiteral("scrybe"));

    int rate = 0;
    const QVector<float> pcm = loadWav(wav, &rate);
    if (pcm.isEmpty()) {
        std::fprintf(stderr, "Could not read WAV (PCM16/float32): %s\n",
                     qPrintable(wav));
        return 2;
    }
    std::fprintf(stderr, "Loaded %lld samples @ %d Hz. Loading model on %s ...\n",
                 (long long)pcm.size(), rate, qPrintable(device));

    SttEngine stt;
    int rc = 1;
    QObject::connect(&stt, &SttEngine::transcript, &app,
                     [&](const QString &t, const QString &lang) {
                         std::printf("LANG=%s\nTEXT=%s\n",
                                     qPrintable(lang), qPrintable(t));
                         rc = 0; app.quit();
                     });
    QObject::connect(&stt, &SttEngine::error, &app, [&](const QString &m) {
        std::fprintf(stderr, "ERROR: %s\n", qPrintable(m));
        rc = 3; app.quit();
    });
    QSettings s;
    const QString key = s.value(QStringLiteral("stt/model"),
                                QStringLiteral("small")).toString();
    QString backend = s.value(QStringLiteral("stt/backend"),
                              QStringLiteral("openvino")).toString();
    if (backend == QLatin1String("auto"))
        backend = QStringLiteral("openvino");
    const QString model = (backend == QLatin1String("faster-whisper"))
                              ? scrybe::fasterWhisperModel(key)
                              : resolveModelDir();
    stt.load(backend, model, device);
    stt.transcribe(pcm, rate, QStringLiteral("auto"), /*isFinal=*/true);
    QTimer::singleShot(180000, &app, [&]() {
        std::fprintf(stderr, "Timed out.\n"); app.quit();
    });
    app.exec();
    return rc;
}

// Draw a crisp monochrome microphone glyph in the given colour. Used for the
// tray icon so it can be recoloured to contrast with a light or dark panel.
QIcon makeTrayIcon(const QColor &c) {
    const int S = 64;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // Capsule body.
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const QRectF body(S * 0.375, S * 0.16, S * 0.25, S * 0.40);
    p.drawRoundedRect(body, body.width() / 2.0, body.width() / 2.0);

    // U-shaped holder + stand + base.
    QPen pen(c);
    pen.setWidthF(S * 0.055);
    pen.setCapStyle(Qt::RoundCap);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen);
    p.drawArc(QRectF(S * 0.28, S * 0.20, S * 0.44, S * 0.46), 190 * 16, 160 * 16);
    p.drawLine(QPointF(S * 0.5, S * 0.66), QPointF(S * 0.5, S * 0.80));
    p.drawLine(QPointF(S * 0.36, S * 0.81), QPointF(S * 0.64, S * 0.81));
    p.end();
    return QIcon(pm);
}

// Try to hand a command to an already-running instance.
// Returns true if a running instance accepted the command.
bool handOffToRunning(const QByteArray &command) {
    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kServerName));
    if (!sock.waitForConnected(200))
        return false;
    sock.write(command);
    sock.flush();
    sock.waitForBytesWritten(200);
    sock.disconnectFromServer();
    return true;
}
} // namespace

int main(int argc, char **argv) {
    // --- CLI: --toggle just pokes the running daemon (used by KDE shortcut) --
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const bool toggleOnly = args.contains(QStringLiteral("--toggle"));

    if (args.contains(QStringLiteral("--version")) || args.contains(QStringLiteral("-v"))) {
        std::printf("scrybe %s\n", SCRYBE_VERSION);
        return 0;
    }
    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        std::printf(
            "Scrybe %s — on-device voice dictation for Linux\n\n"
            "Usage: scrybe [option]\n"
            "  (no option)                 start the background daemon (system tray)\n"
            "  --toggle                    start/stop dictation on the running daemon\n"
            "  --transcribe-file F [DEV]   transcribe a 16 kHz WAV file and print text\n"
            "  --version, -v               print version and exit\n"
            "  --help, -h                  show this help and exit\n\n"
            "Default hotkey: Meta+Alt+D (rebind in System Settings > Shortcuts).\n",
            SCRYBE_VERSION);
        return 0;
    }

    // --- CLI: --transcribe-file <wav> [device]  (headless STT self-test) -----
    const int fileIdx = args.indexOf(QStringLiteral("--transcribe-file"));
    if (fileIdx >= 0 && fileIdx + 1 < args.size()) {
        const QString device =
            (fileIdx + 2 < args.size()) ? args[fileIdx + 2] : QStringLiteral("CPU");
        return runTranscribeFile(argc, argv, args[fileIdx + 1], device);
    }

    const bool settingsCmd = args.contains(QStringLiteral("--settings"));
    const char *cmd = settingsCmd ? "settings\n" : (toggleOnly ? "toggle\n" : "show\n");
    if (handOffToRunning(cmd)) {
        // A daemon was already running; it handled our request.
        return 0;
    }
    if (toggleOnly) {
        // No daemon to toggle. Fall through and start one, then toggle it.
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("scrybe"));
    app.setOrganizationName(QStringLiteral("scrybe"));
    app.setQuitOnLastWindowClosed(false);

    QSettings settings;
    const QString islandPos =
        settings.value(QStringLiteral("island/position"), QStringLiteral("top"))
            .toString();

    Controller controller;

    // --- Single-instance IPC server ----------------------------------------
    QLocalServer::removeServer(QString::fromLatin1(kServerName)); // clear stale
    QLocalServer server;
    if (!server.listen(QString::fromLatin1(kServerName)))
        std::fprintf(stderr, "scrybe: could not start IPC server: %s\n",
                     qPrintable(server.errorString()));
    else
        std::fprintf(stderr, "scrybe: IPC server listening (%s)\n", kServerName);

    QObject::connect(&server, &QLocalServer::newConnection, [&]() {
        while (server.hasPendingConnections()) {
            QLocalSocket *c = server.nextPendingConnection();
            QObject::connect(c, &QLocalSocket::readyRead, [c, &controller]() {
                const QByteArray data = c->readAll();
                if (data.contains("settings"))
                    controller.setSettingsOpen(true);
                else if (data.contains("toggle"))
                    controller.toggle();
                else if (data.contains("show"))
                    controller.startListening();
                c->deleteLater();
            });
        }
    });

    // --- QML overlay --------------------------------------------------------
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),
                                             &controller);
    engine.loadFromModule("Scrybe", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    // --- Anchor the overlay to the top or bottom edge via layer-shell --------
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        if (auto *ls = LayerShellQt::Window::get(win)) {
            using LSW = LayerShellQt::Window;
            const bool bottom = (islandPos == QLatin1String("bottom"));
            ls->setLayer(LSW::LayerOverlay);
            ls->setScope(QStringLiteral("scrybe"));
            // Grab the keyboard while the bubble is mapped so Esc/Enter work.
            // Focus returns to the previously-focused app when we hide it.
            ls->setKeyboardInteractivity(LSW::KeyboardInteractivityExclusive);
            ls->setExclusiveZone(0);
            ls->setAnchors(bottom ? LSW::Anchors(LSW::AnchorBottom)
                                  : LSW::Anchors(LSW::AnchorTop));
            ls->setMargins(QMargins(0, bottom ? 0 : 12, 0, bottom ? 12 : 0));
        }
    }

    // --- Global hotkey: register directly with KDE (KGlobalAccel) ------------
    // Works live for a background daemon, shows in System Settings > Shortcuts,
    // and does not need kglobalshortcutsrc editing or a relogin.
    auto *toggleAction = new QAction(QStringLiteral("Toggle dictation"), &app);
    toggleAction->setObjectName(QStringLiteral("toggle-dictation"));
    toggleAction->setProperty("componentName", QStringLiteral("scrybe"));
    toggleAction->setProperty("componentDisplayName", QStringLiteral("scrybe"));
    KGlobalAccel::self()->setShortcut(
        toggleAction,
        {QKeySequence(Qt::META | Qt::ALT | Qt::Key_D)},
        KGlobalAccel::NoAutoloading);
    QObject::connect(toggleAction, &QAction::triggered,
                     &controller, &Controller::toggle);

    // --- Tray icon ----------------------------------------------------------
    const QIcon appIcon = QIcon::fromTheme(
        QStringLiteral("scrybe"),
        QIcon::fromTheme(QStringLiteral("audio-input-microphone")));
    app.setWindowIcon(appIcon);

    QSystemTrayIcon tray;
    tray.setToolTip(QStringLiteral("Scrybe — press the hotkey to dictate"));

    // Monochrome tray icon that adapts to the panel: a dark desktop scheme gets
    // a near-white glyph, a light scheme a near-black one, updated live when the
    // system switches between light and dark.
    auto refreshTrayIcon = [&tray, &app]() {
        const bool dark =
            app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        tray.setIcon(makeTrayIcon(dark ? QColor(0xf4, 0xf5, 0xf7)
                                       : QColor(0x1c, 0x1d, 0x22)));
    };
    refreshTrayIcon();
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [refreshTrayIcon](Qt::ColorScheme) { refreshTrayIcon(); });
    QMenu menu;
    QAction *actToggle = menu.addAction(QStringLiteral("Start / Stop listening"));
    QObject::connect(actToggle, &QAction::triggered, &controller, &Controller::toggle);
    QAction *actLlm = menu.addAction(QStringLiteral("LLM beautifier"));
    actLlm->setCheckable(true);
    actLlm->setChecked(controller.llmEnabled());
    QObject::connect(actLlm, &QAction::toggled, &controller, &Controller::setLlmEnabled);

    QAction *actPreview = menu.addAction(QStringLiteral("Live preview text"));
    actPreview->setCheckable(true);
    actPreview->setChecked(controller.previewEnabled());
    QObject::connect(actPreview, &QAction::toggled, &controller, &Controller::setPreviewEnabled);

    // Model submenu: pick the STT model (downloads on first use).
    QMenu *modelMenu = menu.addMenu(QStringLiteral("Speech model"));
    auto *modelGroup = new QActionGroup(&app);
    modelGroup->setExclusive(true);
    for (const auto &m : scrybe::models()) {
        QAction *a = modelMenu->addAction(m.label);
        a->setCheckable(true);
        a->setChecked(m.key == controller.model());
        a->setData(m.key);
        modelGroup->addAction(a);
        QObject::connect(a, &QAction::triggered, &controller,
                         [&controller, key = m.key]() { controller.setModel(key); });
    }
    menu.addSeparator();
    QAction *actUpdate = menu.addAction(QStringLiteral("Check for updates…"));
    QObject::connect(actUpdate, &QAction::triggered, &controller, [&controller]() {
        QMetaObject::invokeMethod(controller.updater(), "check", Q_ARG(bool, false));
    });
    QAction *actSettings = menu.addAction(QStringLiteral("Settings…"));
    QObject::connect(actSettings, &QAction::triggered, &controller,
                     [&controller]() { controller.setSettingsOpen(true); });
    QAction *actQuit = menu.addAction(QStringLiteral("Quit"));
    QObject::connect(actQuit, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.show();

    QObject::connect(&controller, &Controller::notify, &tray,
                     [&tray](const QString &msg) {
                         tray.showMessage(QStringLiteral("Scrybe"), msg,
                                          QSystemTrayIcon::Warning, 4000);
                     });

    if (toggleOnly)
        controller.toggle();

    // Quietly check GitHub for a newer release a few seconds after launch.
    QTimer::singleShot(5000, &controller, [&controller]() {
        QMetaObject::invokeMethod(controller.updater(), "check", Q_ARG(bool, true));
    });

    return app.exec();
}
