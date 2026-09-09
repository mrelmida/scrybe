#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <wayland-server.h>
#ifdef TEST_LEGACY
#include "wlr-data-control-unstable-v1-server.h"
#define ext_data_control_device_v1_interface zwlr_data_control_device_v1_interface
#define ext_data_control_device_v1_send_data_offer zwlr_data_control_device_v1_send_data_offer
#define ext_data_control_device_v1_send_selection zwlr_data_control_device_v1_send_selection
#define ext_data_control_manager_v1_interface zwlr_data_control_manager_v1_interface
#define ext_data_control_offer_v1_interface zwlr_data_control_offer_v1_interface
#define ext_data_control_offer_v1_send_offer zwlr_data_control_offer_v1_send_offer
#define ext_data_control_source_v1_interface zwlr_data_control_source_v1_interface
#define ext_data_control_source_v1_send_cancelled zwlr_data_control_source_v1_send_cancelled
#define ext_data_control_source_v1_send_send zwlr_data_control_source_v1_send_send
#else
#include "ext-data-control-v1-server.h"
#endif

// A private in-process compositor: tests exercise the real helper/protocol
// without accessing the user's Wayland socket or generating keyboard events.
class ClipboardServer {
    struct Offer { ClipboardServer *server; QMap<QString, QByteArray> bytes; wl_resource *source; };
    wl_display *display = wl_display_create();
    wl_event_loop *loop = wl_display_get_event_loop(display);
    QTimer notifier;
    QList<wl_resource *> devices;
    QMap<wl_resource *, QStringList> sources;
    wl_resource *selectedSource = nullptr;
    QMap<QString, QByteArray> external;
    static ClipboardServer *self(wl_resource *resource) { return static_cast<ClipboardServer *>(wl_resource_get_user_data(resource)); }
    static void destroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }
    static void offerMime(wl_client *, wl_resource *resource, const char *mime) { self(resource)->sources[resource].append(QString::fromUtf8(mime)); }
    static void sourceDestroyed(wl_resource *resource) {
        auto *server = self(resource);
        server->sources.remove(resource);
        if (server->selectedSource == resource) server->selectedSource = nullptr;
    }
    static void createSource(wl_client *client, wl_resource *resource, uint32_t id) {
        auto *source = wl_resource_create(client, &ext_data_control_source_v1_interface, 1, id);
        static const struct ext_data_control_source_v1_interface implementation{offerMime, destroy};
        wl_resource_set_implementation(source, &implementation, self(resource), sourceDestroyed);
    }
    static void receive(wl_client *, wl_resource *resource, const char *mime, int fd) {
        const auto *offer = static_cast<Offer *>(wl_resource_get_user_data(resource));
        if (offer->source) ext_data_control_source_v1_send_send(offer->source, mime, fd);
        else {
            const auto bytes = offer->bytes.value(QString::fromUtf8(mime));
            // Test payloads are intentionally below pipe capacity.
            const auto ignored = write(fd, bytes.constData(), bytes.size()); (void)ignored;
        }
        close(fd);
    }
    static void offerDestroyed(wl_resource *resource) { delete static_cast<Offer *>(wl_resource_get_user_data(resource)); }
    void sendSelection(wl_resource *device) {
        const auto formats = selectedSource ? sources.value(selectedSource) : external.keys();
        if (formats.isEmpty()) { ext_data_control_device_v1_send_selection(device, nullptr); return; }
        auto *offer = wl_resource_create(wl_resource_get_client(device), &ext_data_control_offer_v1_interface, 1, 0);
        static const struct ext_data_control_offer_v1_interface implementation{receive, destroy};
        wl_resource_set_implementation(offer, &implementation, new Offer{this, external, selectedSource}, offerDestroyed);
        ext_data_control_device_v1_send_data_offer(device, offer);
        for (const auto &format : formats) ext_data_control_offer_v1_send_offer(offer, format.toUtf8().constData());
        ext_data_control_device_v1_send_selection(device, offer);
    }
    static void select(wl_client *, wl_resource *resource, wl_resource *source) {
        auto *server = self(resource);
        if (server->selectedSource) ext_data_control_source_v1_send_cancelled(server->selectedSource);
        server->selectedSource = source;
        server->external.clear();
        ++server->selectionCount;
        for (auto *device : server->devices) server->sendSelection(device);
    }
    static void primary(wl_client *, wl_resource *, wl_resource *) {}
    static void deviceDestroyed(wl_resource *resource) { self(resource)->devices.removeAll(resource); }
    static void getDevice(wl_client *client, wl_resource *resource, uint32_t id, wl_resource *) {
        auto *server = self(resource);
        auto *device = wl_resource_create(client, &ext_data_control_device_v1_interface, 1, id);
        static const struct ext_data_control_device_v1_interface implementation{select, destroy, primary};
        wl_resource_set_implementation(device, &implementation, server, deviceDestroyed);
        server->devices.append(device);
        server->sendSelection(device);
    }
    static void bindManager(wl_client *client, void *data, uint32_t, uint32_t id) {
        auto *resource = wl_resource_create(client, &ext_data_control_manager_v1_interface, 1, id);
        static const struct ext_data_control_manager_v1_interface implementation{createSource, getDevice, destroy};
        wl_resource_set_implementation(resource, &implementation, data, nullptr);
    }
    static void bindSeat(wl_client *client, void *, uint32_t, uint32_t id) {
        auto *seat = wl_resource_create(client, &wl_seat_interface, 1, id);
        wl_resource_set_implementation(seat, nullptr, nullptr, nullptr);
        wl_seat_send_capabilities(seat, 0);
    }
public:
    int selectionCount = 0;
    int peerFd = -1;
    explicit ClipboardServer(const QMap<QString, QByteArray> &bytes) : external(bytes) {
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) qFatal("Could not create isolated Wayland socket pair");
        if (!wl_client_create(display, pair[0])) qFatal("Could not create mock compositor client");
        peerFd = pair[1];
        wl_global_create(display, &ext_data_control_manager_v1_interface, 1, this, bindManager);
        wl_global_create(display, &wl_seat_interface, 1, this, bindSeat);
        QObject::connect(&notifier, &QTimer::timeout, &notifier, [this]() {
            wl_event_loop_dispatch(loop, 0); wl_display_flush_clients(display);
        });
        notifier.start(1);
    }
    ~ClipboardServer() { if (peerFd >= 0) close(peerFd); notifier.stop(); wl_display_destroy_clients(display); wl_display_destroy(display); }
    QStringList formats() const { return selectedSource ? sources.value(selectedSource) : external.keys(); }
    int request(const QString &mime) {
        int fds[2]; if (pipe2(fds, O_CLOEXEC)) return -1;
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        ext_data_control_source_v1_send_send(selectedSource, mime.toUtf8().constData(), fds[1]);
        close(fds[1]); wl_display_flush_clients(display); return fds[0];
    }
    void replaceExternal(const QMap<QString, QByteArray> &bytes) {
        if (selectedSource) ext_data_control_source_v1_send_cancelled(selectedSource);
        selectedSource = nullptr; external = bytes;
        for (auto *device : devices) sendSelection(device);
        wl_display_flush_clients(display);
    }
};

class ClipboardHelperTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    void start(QProcess &process, ClipboardServer &server, bool restore = true) {
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
        env.insert(QStringLiteral("WAYLAND_SOCKET"), QString::number(server.peerFd));
        const auto fd = server.peerFd;
        process.setChildProcessModifier([fd]() { fcntl(fd, F_SETFD, 0); });
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(CLIPBOARD_HELPER), {});
        QVERIFY(process.waitForStarted(1000)); // test thread only; never application GUI
        close(server.peerFd); server.peerFd = -1;
        const QJsonObject request{{QStringLiteral("text"), QStringLiteral("c3BlZWNo")}, {QStringLiteral("restore"), restore}};
        process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    }
    QByteArray readRepresentation(ClipboardServer &server, const QString &mime) {
        const int fd = server.request(mime);
        QByteArray result;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 1500) {
            char buffer[4096];
            const auto n = read(fd, buffer, sizeof(buffer));
            if (n == 0) break;
            if (n > 0) result.append(buffer, n);
            QTest::qWait(10);
        }
        close(fd); return result;
    }
private slots:
    void cancellationDuringPreparationLeavesOriginalUntouched() {
        ClipboardServer server({{QStringLiteral("text/html"), "<b>original</b>"}});
        QProcess helper; start(helper, server);
        helper.write("RESTORE\n");
        QTRY_COMPARE(helper.state(), QProcess::NotRunning);
        QVERIFY(helper.readAllStandardOutput().contains("DONE\n"));
        QCOMPARE(server.selectionCount, 0);
        QCOMPARE(server.formats(), QStringList{QStringLiteral("text/html")});
    }
    void tooManyRepresentationsFailsWithoutChangingClipboard() {
        QMap<QString, QByteArray> original;
        for (int i = 0; i < 65; ++i) original.insert(QStringLiteral("application/test-%1").arg(i), "data");
        ClipboardServer server(original); QProcess helper; start(helper, server);
        QTRY_COMPARE(helper.state(), QProcess::NotRunning);
        QVERIFY(helper.readAllStandardOutput().contains("ERROR"));
        QCOMPARE(server.selectionCount, 0);
        QCOMPARE(server.formats().size(), 65);
    }
    void restoresEveryMimeByte() {
        const QMap<QString, QByteArray> original{{QStringLiteral("text/plain"), "original\n"},
            {QStringLiteral("text/html"), "<b>original</b>"}, {QStringLiteral("image/png"), QByteArray("\x89PNG\0\r\n", 7)}};
        ClipboardServer server(original);
        QProcess helper; start(helper, server);
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += helper.readAllStandardOutput()).contains("READY\n") || helper.state() == QProcess::NotRunning, 10000);
        QVERIFY2(output.contains("READY\n"), qPrintable(QString::fromUtf8(output + helper.readAllStandardError()) + QString::number(helper.exitCode())));
        QCOMPARE(server.selectionCount, 1);
        QCOMPARE(readRepresentation(server, QStringLiteral("text/plain")), QByteArray("speech"));
        helper.write("RESTORE\n");
        QTRY_VERIFY((output += helper.readAllStandardOutput()).contains("DONE\n"));
        QCOMPARE(server.selectionCount, 2);
        const auto restoredFormats = server.formats();
        QCOMPARE(QSet<QString>(restoredFormats.cbegin(), restoredFormats.cend()), QSet<QString>(original.keyBegin(), original.keyEnd()));
        for (auto it = original.cbegin(); it != original.cend(); ++it) QCOMPARE(readRepresentation(server, it.key()), it.value());
        helper.closeWriteChannel();
        QTest::qWait(50); QCOMPARE(helper.state(), QProcess::Running);
        QCOMPARE(readRepresentation(server, QStringLiteral("text/html")), original.value(QStringLiteral("text/html")));
        server.replaceExternal({{QStringLiteral("text/plain"), "new owner"}});
        QTRY_COMPARE(helper.state(), QProcess::NotRunning); QCOMPARE(helper.exitCode(), 0);
    }
    void userCopyIsNeverOverwritten() {
        ClipboardServer server({{QStringLiteral("text/plain"), "previous"}});
        QProcess helper; start(helper, server);
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += helper.readAllStandardOutput()).contains("READY\n") || helper.state() == QProcess::NotRunning, 10000);
        QVERIFY2(output.contains("READY\n"), qPrintable(QString::fromUtf8(output + helper.readAllStandardError()) + QString::number(helper.exitCode())));
        // Identical text still represents a new owner, so content comparison
        // would be insufficient to make restoration safe.
        server.replaceExternal({{QStringLiteral("text/plain"), "speech"}});
        helper.write("RESTORE\n");
        QTRY_COMPARE(helper.state(), QProcess::NotRunning);
        output += helper.readAllStandardOutput(); QVERIFY(output.contains("LOST\n"));
        QCOMPARE(server.selectionCount, 1);
    }
    void emptyClipboardRestoresToEmpty() {
        ClipboardServer server({});
        QProcess helper; start(helper, server); QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += helper.readAllStandardOutput()).contains("READY\n") || helper.state() == QProcess::NotRunning, 10000);
        QVERIFY2(output.contains("READY\n"), qPrintable(QString::fromUtf8(output + helper.readAllStandardError()) + QString::number(helper.exitCode())));
        helper.write("RESTORE\n");
        QTRY_COMPARE(helper.state(), QProcess::NotRunning);
        output += helper.readAllStandardOutput(); QVERIFY(output.contains("DONE\n"));
        QVERIFY(server.formats().isEmpty()); QCOMPARE(server.selectionCount, 2);
    }
};
QTEST_GUILESS_MAIN(ClipboardHelperTest)
#include "tst_clipboard_helper.moc"
