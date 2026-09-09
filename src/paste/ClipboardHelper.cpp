// Clipboard ownership lives in this helper, so slow clipboard producers cannot
// block the GUI. The data-control protocols work without a focused input serial.
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>
#include "ext-data-control-v1-client.h"
#include "wlr-data-control-unstable-v1-client.h"

namespace {
constexpr qsizetype MaxBytes = 16 * 1024 * 1024;
void report(const char *line) { puts(line); fflush(stdout); }

// The stable and legacy protocols have the same operations used here.
#define PROTOCOL(Name, prefix) \
struct Name { \
    using Manager = prefix##_manager_v1; using Device = prefix##_device_v1; \
    using Source = prefix##_source_v1; using Offer = prefix##_offer_v1; \
    using DeviceListener = prefix##_device_v1_listener; \
    using SourceListener = prefix##_source_v1_listener; \
    using OfferListener = prefix##_offer_v1_listener; \
    static constexpr auto interface = &prefix##_manager_v1_interface; \
    static constexpr auto createSource = prefix##_manager_v1_create_data_source; \
    static constexpr auto getDevice = prefix##_manager_v1_get_data_device; \
    static constexpr auto listenDevice = prefix##_device_v1_add_listener; \
    static constexpr auto listenSource = prefix##_source_v1_add_listener; \
    static constexpr auto listenOffer = prefix##_offer_v1_add_listener; \
    static constexpr auto receive = prefix##_offer_v1_receive; \
    static constexpr auto destroyOffer = prefix##_offer_v1_destroy; \
    static constexpr auto destroySource = prefix##_source_v1_destroy; \
    static constexpr auto offer = prefix##_source_v1_offer; \
    static constexpr auto select = prefix##_device_v1_set_selection; \
};
PROTOCOL(Ext, ext_data_control)
PROTOCOL(Wlr, zwlr_data_control)
#undef PROTOCOL

struct Globals {
    uint32_t ext = 0, wlr = 0, seat = 0;
    static void global(void *data, wl_registry *, uint32_t id, const char *name, uint32_t) {
        auto &g = *static_cast<Globals *>(data);
        if (!strcmp(name, Ext::interface->name)) g.ext = id;
        if (!strcmp(name, Wlr::interface->name)) g.wlr = id;
        if (!strcmp(name, wl_seat_interface.name) && !g.seat) g.seat = id;
    }
    static void removed(void *, wl_registry *, uint32_t) {}
};

template<class P> class Clipboard {
    using Source = typename P::Source;
    using Offer = typename P::Offer;
    using Device = typename P::Device;
    wl_display *display;
    Device *device = nullptr;
    typename P::Manager *manager = nullptr;
    Offer *selection = nullptr;
    QMap<Offer *, QStringList> offers;
    QMap<QString, QByteArray> saved, current;
    QMap<Source *, QMap<QString, QByteArray>> sourceData;
    Source *source = nullptr;
    bool lost = false, complete = false, valid = true, parentClosed = false;

    static void mime(void *data, Offer *offer, const char *type) {
        auto *self = static_cast<Clipboard *>(data);
        self->offers[offer].append(QString::fromUtf8(type));
    }
    static void newOffer(void *data, Device *, Offer *offer) {
        static const typename P::OfferListener listener{mime};
        P::listenOffer(offer, &listener, data);
    }
    static void selected(void *data, Device *, Offer *offer) {
        auto *self = static_cast<Clipboard *>(data);
        if (self->selection) {
            self->offers.remove(self->selection);
            P::destroyOffer(self->selection);
        }
        self->selection = offer;
    }
    static void primary(void *data, Device *, Offer *offer) {
        if (offer) {
            static_cast<Clipboard *>(data)->offers.remove(offer);
            P::destroyOffer(offer);
        }
    }
    static void finished(void *data, Device *) { static_cast<Clipboard *>(data)->valid = false; }
    static void cancelled(void *data, Source *source) {
        auto *self = static_cast<Clipboard *>(data);
        if (self->source == source) {
            self->lost = true;
            self->source = nullptr;
            report("LOST");
        }
        self->sourceData.remove(source);
        P::destroySource(source);
    }
    static void send(void *data, Source *source, const char *mimeType, int fd) {
        auto *self = static_cast<Clipboard *>(data);
        const auto bytes = self->sourceData.value(source).value(QString::fromUtf8(mimeType));
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        QElapsedTimer timer; timer.start();
        qsizetype offset = 0;
        while (offset < bytes.size() && timer.elapsed() < 2000) {
            const auto n = write(fd, bytes.constData() + offset, bytes.size() - offset);
            if (n > 0) { offset += n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno != EAGAIN) break;
            pollfd p{fd, POLLOUT, 0};
            if (poll(&p, 1, 100) < 0 && errno != EINTR) break;
        }
        close(fd);
    }
    bool snapshot() {
        const auto formats = offers.value(selection);
        if (formats.size() > 64) return false;
        QElapsedTimer timer; timer.start();
        qsizetype total = 0;
        for (const auto &format : formats) {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC)) return false;
            fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
            P::receive(selection, format.toUtf8().constData(), fds[1]);
            close(fds[1]);
            if (wl_display_flush(display) < 0) { close(fds[0]); return false; }
            QByteArray bytes;
            bool eof = false;
            while (timer.elapsed() < 4000) {
                char buffer[8192];
                const auto n = read(fds[0], buffer, sizeof(buffer));
                if (n == 0) { eof = true; break; }
                if (n > 0) {
                    total += n;
                    if (total > MaxBytes) break;
                    bytes.append(buffer, n);
                    continue;
                }
                if (errno == EINTR) continue;
                if (errno != EAGAIN) break;
                pollfd p{fds[0], POLLIN, 0};
                if (poll(&p, 1, 100) < 0 && errno != EINTR) break;
            }
            close(fds[0]);
            if (!eof) return false;
            saved.insert(format, bytes);
        }
        // Drain changes before overwriting: a snapshot from a replaced offer
        // must not later resurrect an older user clipboard.
        const auto *original = selection;
        return wl_display_roundtrip(display) >= 0 && original == selection;
    }
    bool install(const QMap<QString, QByteArray> &data) {
        current = data;
        source = nullptr;
        if (!data.isEmpty()) {
            source = P::createSource(manager);
            sourceData.insert(source, data);
            static const typename P::SourceListener listener{send, cancelled};
            P::listenSource(source, &listener, this);
            for (auto it = data.cbegin(); it != data.cend(); ++it)
                P::offer(source, it.key().toUtf8().constData());
        }
        lost = false;
        P::select(device, source);
        return wl_display_roundtrip(display) >= 0 && !lost;
    }
public:
    explicit Clipboard(wl_display *d) : display(d) {}
    int run(wl_registry *registry, uint32_t managerId, uint32_t seatId,
            const QByteArray &text, bool restore) {
        manager = static_cast<typename P::Manager *>(wl_registry_bind(registry, managerId, P::interface, 1));
        auto *seat = static_cast<wl_seat *>(wl_registry_bind(registry, seatId, &wl_seat_interface, 1));
        device = P::getDevice(manager, seat);
        static const typename P::DeviceListener listener{newOffer, selected, finished, primary};
        P::listenDevice(device, &listener, this);
        if (wl_display_roundtrip(display) < 0 || !valid) return 1;
        if (restore && !snapshot()) { report("ERROR Could not safely save the clipboard (changed, too large, or unresponsive)."); return 1; }
        // Cancellation queued while a slow producer was being read must prevent
        // even the initial selection replacement.
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 0) > 0) { report("DONE"); return 0; }
        if (!install({{QStringLiteral("text/plain;charset=utf-8"), text},
                      {QStringLiteral("text/plain"), text},
                      {QStringLiteral("UTF8_STRING"), text}})) return 1;
        alarm(0); // startup bounded by parent and alarm; serving is intentionally long-lived
        report("READY");
        QByteArray commands;
        while (valid && !lost) {
            wl_display_flush(display);
            pollfd fds[] = {{wl_display_get_fd(display), POLLIN, 0}, {parentClosed ? -1 : STDIN_FILENO, POLLIN, 0}};
            if (poll(fds, 2, -1) < 0) { if (errno == EINTR) continue; return 1; }
            if (fds[0].revents && wl_display_dispatch(display) < 0) return 1;
            if (fds[1].revents) {
                char buffer[128];
                const auto n = read(STDIN_FILENO, buffer, sizeof(buffer));
                if (n <= 0) {
                    // Preserve clipboard ownership across application shutdown,
                    // just as wl-copy's background owner does. An unfinished
                    // transaction restores its snapshot before serving it.
                    if (wl_display_roundtrip(display) < 0) return 1;
                    if (!complete && restore && !lost && !install(saved)) return 1;
                    complete = true;
                    parentClosed = true;
                    if (current.isEmpty() || lost) return 0;
                    continue;
                }
                commands.append(buffer, n);
                while (commands.contains('\n')) {
                    const auto line = commands.left(commands.indexOf('\n'));
                    commands.remove(0, line.size() + 1);
                    if (complete) continue;
                    // Roundtrip drains a compositor cancellation before deciding
                    // whether restoration still belongs to this transaction.
                    if (wl_display_roundtrip(display) < 0) return 1;
                    if (line == "RESTORE" && restore && !lost) {
                        if (!install(saved)) return 1;
                    }
                    complete = true;
                    report("DONE");
                    if (current.isEmpty()) return 0;
                }
            }
        }
        return valid ? 0 : 1;
    }
};
} // namespace

int main() {
    signal(SIGPIPE, SIG_IGN);
    alarm(8);
    // One JSON line followed by RESTORE or KEEP. Payload is UTF-8/base64 so no
    // transcript or clipboard content appears in argv or diagnostic output.
    QByteArray input;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n') {
        input.append(c);
        if (input.size() > MaxBytes) return 1;
    }
    const auto json = QJsonDocument::fromJson(input).object();
    if (!json.contains(QStringLiteral("text"))) return 1;
    auto *display = wl_display_connect(nullptr);
    if (!display) { report("ERROR Could not connect to the Wayland clipboard."); return 1; }
    auto *registry = wl_display_get_registry(display);
    Globals globals;
    static const wl_registry_listener listener{Globals::global, Globals::removed};
    wl_registry_add_listener(registry, &listener, &globals);
    if (wl_display_roundtrip(display) < 0) return 1;
    if (!globals.seat || (!globals.ext && !globals.wlr)) {
        report("ERROR This compositor does not support clipboard data control."); return 1;
    }
    const auto text = QByteArray::fromBase64(json.value(QStringLiteral("text")).toString().toLatin1());
    const bool restore = json.value(QStringLiteral("restore")).toBool(true);
    const int result = globals.ext
        ? Clipboard<Ext>(display).run(registry, globals.ext, globals.seat, text, restore)
        : Clipboard<Wlr>(display).run(registry, globals.wlr, globals.seat, text, restore);
    wl_display_disconnect(display);
    return result;
}
