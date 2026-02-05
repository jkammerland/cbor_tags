#include <doctest/doctest.h>

#if defined(_WIN32)

TEST_CASE("someip: tcp/udp e2e (unix-only)") {
    DOCTEST_SKIP("Unix sockets + fork() required");
}

#else

#include "someip/iface/field.h"
#include "someip/sd/sd.h"
#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/wire/message.h"
#include "someip/wire/return_code.h"
#include "someip/wire/someip.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char *kServerIp = "127.0.0.1";
constexpr std::size_t kEventCount = 10;
constexpr std::uint32_t kFieldValue = 0xBEEF;

struct unique_fd {
    int value{-1};
    unique_fd() = default;
    explicit unique_fd(int fd) : value(fd) {}
    unique_fd(const unique_fd &) = delete;
    unique_fd &operator=(const unique_fd &) = delete;
    unique_fd(unique_fd &&other) noexcept : value(other.value) { other.value = -1; }
    unique_fd &operator=(unique_fd &&other) noexcept {
        if (this != &other) {
            reset();
            value       = other.value;
            other.value = -1;
        }
        return *this;
    }
    ~unique_fd() { reset(); }

    [[nodiscard]] bool valid() const noexcept { return value >= 0; }
    [[nodiscard]] int  fd() const noexcept { return value; }

    void reset(int fd = -1) noexcept {
        if (value >= 0) {
            ::close(value);
        }
        value = fd;
    }
};

static int poll_one(int fd, short events, int timeout_ms) {
    pollfd pfd{};
    pfd.fd     = fd;
    pfd.events = events;
    for (;;) {
        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return rc;
    }
}

static bool write_all(int fd, const std::byte *data, std::size_t len, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t written = 0;
    while (written < len) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int  prc       = poll_one(fd, POLLOUT, static_cast<int>(remaining));
        if (prc <= 0) {
            return false;
        }
        ssize_t rc = ::write(fd, data + written, len - written);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(rc);
    }
    return true;
}

static bool read_exact(int fd, std::byte *data, std::size_t len, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t readn   = 0;
    while (readn < len) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int  prc       = poll_one(fd, POLLIN, static_cast<int>(remaining));
        if (prc <= 0) {
            return false;
        }
        ssize_t rc = ::read(fd, data + readn, len - readn);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc <= 0) {
            return false;
        }
        readn += static_cast<std::size_t>(rc);
    }
    return true;
}

static std::vector<std::byte> recv_someip_frame(int fd, int timeout_ms) {
    std::array<std::byte, 8> prefix{};
    if (!read_exact(fd, prefix.data(), prefix.size(), timeout_ms)) {
        return {};
    }
    auto total = someip::wire::frame_size_from_prefix(std::span<const std::byte>(prefix.data(), prefix.size()));
    if (!total || *total < 16u) {
        return {};
    }
    std::vector<std::byte> frame(*total);
    std::memcpy(frame.data(), prefix.data(), prefix.size());
    const auto remaining = frame.size() - prefix.size();
    if (remaining > 0) {
        if (!read_exact(fd, frame.data() + prefix.size(), remaining, timeout_ms)) {
            return {};
        }
    }
    return frame;
}

static bool send_frame(int fd, const std::vector<std::byte> &frame, int timeout_ms) {
    return write_all(fd, frame.data(), frame.size(), timeout_ms);
}

static bool recv_udp_datagram(int fd, std::vector<std::byte> &frame, sockaddr_in &from, int timeout_ms) {
    const int ready = poll_one(fd, POLLIN, timeout_ms);
    if (ready <= 0) {
        return false;
    }
    std::array<std::byte, 2048> buffer{};
    socklen_t from_len = sizeof(from);
    ssize_t   rc       = ::recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
    if (rc <= 0) {
        return false;
    }
    frame.assign(buffer.begin(), buffer.begin() + rc);
    return true;
}

static bool send_udp_datagram(int fd, const std::vector<std::byte> &frame, const sockaddr_in &to) {
    const ssize_t rc = ::sendto(fd, frame.data(), frame.size(), 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
    return rc == static_cast<ssize_t>(frame.size());
}

static sockaddr_in make_addr(const char *ip, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

static unique_fd open_udp_socket(std::uint16_t port, const char *bind_ip) {
    unique_fd sock{::socket(AF_INET, SOCK_DGRAM, 0)};
    if (!sock.valid()) {
        return {};
    }
    int reuse = 1;
    (void)::setsockopt(sock.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (bind_ip != nullptr) {
        ::inet_pton(AF_INET, bind_ip, &addr.sin_addr);
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (::bind(sock.fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        return {};
    }
    return sock;
}

static unique_fd open_tcp_listener(const char *bind_ip, std::uint16_t port) {
    unique_fd sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        return {};
    }
    int reuse = 1;
    (void)::setsockopt(sock.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr = make_addr(bind_ip, port);
    if (::bind(sock.fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        return {};
    }
    if (::listen(sock.fd(), 4) != 0) {
        return {};
    }
    return sock;
}

static unique_fd connect_tcp_with_retry(const sockaddr_in &addr, int timeout_ms) {
    unique_fd sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        return {};
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const int rc = ::connect(sock.fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
        if (rc == 0) {
            return sock;
        }
        if (errno != ECONNREFUSED && errno != ENOENT) {
            return {};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static std::uint16_t local_port_for_fd(int fd) {
    sockaddr_in addr{};
    socklen_t   len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

struct MethodReq {
    std::uint32_t x{};
};

struct MethodResp {
    std::uint32_t y{};
};

struct FieldValue {
    std::uint32_t value{};
};

struct FieldEvent {
    std::uint32_t seq{};
    std::uint32_t value{};
};

struct EmptyPayload {};

static std::vector<std::byte> build_someip_message(const someip::ser::config &cfg, someip::wire::header h, const auto &payload) {
    std::vector<std::byte> out{};
    auto                   st = someip::wire::encode_message(out, h, cfg, payload);
    if (!st) {
        return {};
    }
    return out;
}

static constexpr someip::iface::field_descriptor writable_field{
    .service_id        = 0x1234,
    .getter_method_id  = 0x0100,
    .setter_method_id  = 0x0101,
    .notifier_event_id = 0x8001,
    .eventgroup_id     = 0x0001,
    .readable          = true,
    .writable          = true,
    .notifies          = true,
};

static constexpr someip::iface::field_descriptor readonly_field{
    .service_id        = 0x1234,
    .getter_method_id  = 0x0200,
    .setter_method_id  = 0x0201,
    .notifier_event_id = 0x8002,
    .eventgroup_id     = 0x0002,
    .readable          = true,
    .writable          = false,
    .notifies          = false,
};

static someip::sd::ipv4_endpoint_option make_ipv4_endpoint(const char *ip, bool reliable, std::uint16_t port) {
    someip::sd::ipv4_endpoint_option opt{};
    opt.discardable = false;
    in_addr addr{};
    ::inet_pton(AF_INET, ip, &addr);
    const auto host = ntohl(addr.s_addr);
    opt.address = {
        std::byte{static_cast<std::uint8_t>((host >> 24) & 0xFFu)},
        std::byte{static_cast<std::uint8_t>((host >> 16) & 0xFFu)},
        std::byte{static_cast<std::uint8_t>((host >> 8) & 0xFFu)},
        std::byte{static_cast<std::uint8_t>(host & 0xFFu)}
    };
    opt.l4_proto = reliable ? 6 : 17;
    opt.port = port;
    opt.reserved = 0;
    return opt;
}

static std::vector<std::byte> build_sd_find_service() {
    someip::sd::service_entry_data e{};
    e.type          = someip::sd::entry_type::find_service;
    e.service_id    = 0x1234;
    e.instance_id   = 0xFFFF;
    e.major_version = 1;
    e.ttl           = 3;
    e.minor_version = 0;

    someip::sd::packet_data pd{};
    pd.hdr.flags      = 0x00;
    pd.hdr.reserved24 = 0;
    pd.entries.push_back(someip::sd::entry_data{e});

    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_offer_service(bool reliable, std::uint16_t port) {
    someip::sd::service_entry_data e{};
    e.type          = someip::sd::entry_type::offer_service;
    e.service_id    = 0x1234;
    e.instance_id   = 0x0001;
    e.major_version = 1;
    e.ttl           = 5;
    e.minor_version = 0;
    e.run1          = {someip::sd::option{make_ipv4_endpoint(kServerIp, reliable, port)}};

    someip::sd::packet_data pd{};
    pd.hdr.flags      = 0x00;
    pd.hdr.reserved24 = 0;
    pd.entries.push_back(someip::sd::entry_data{e});

    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_subscribe_eventgroup(bool reliable, std::uint16_t port) {
    someip::sd::eventgroup_entry_data e{};
    e.type                = someip::sd::entry_type::subscribe_eventgroup;
    e.service_id          = 0x1234;
    e.instance_id         = 0x0001;
    e.major_version       = 1;
    e.ttl                 = 5;
    e.reserved12_counter4 = 0;
    e.eventgroup_id       = writable_field.eventgroup_id;
    e.run1                = {someip::sd::option{make_ipv4_endpoint(kServerIp, reliable, port)}};

    someip::sd::packet_data pd{};
    pd.hdr.flags      = 0x00;
    pd.hdr.reserved24 = 0;
    pd.entries.push_back(someip::sd::entry_data{e});

    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_subscribe_ack() {
    someip::sd::eventgroup_entry_data e{};
    e.type                = someip::sd::entry_type::subscribe_eventgroup_ack;
    e.service_id          = 0x1234;
    e.instance_id         = 0x0001;
    e.major_version       = 1;
    e.ttl                 = 5;
    e.reserved12_counter4 = 0;
    e.eventgroup_id       = writable_field.eventgroup_id;

    someip::sd::packet_data pd{};
    pd.hdr.flags      = 0x00;
    pd.hdr.reserved24 = 0;
    pd.entries.push_back(someip::sd::entry_data{e});

    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

struct ready_ports {
    std::uint16_t sd_port{0};
    std::uint16_t service_port{0};
};

static bool write_ready_ports(int fd, std::byte status, std::uint16_t sd_port, std::uint16_t service_port) {
    std::array<std::byte, 5> buf{};
    buf[0] = status;
    buf[1] = std::byte(static_cast<std::uint8_t>((sd_port >> 8) & 0xFFu));
    buf[2] = std::byte(static_cast<std::uint8_t>(sd_port & 0xFFu));
    buf[3] = std::byte(static_cast<std::uint8_t>((service_port >> 8) & 0xFFu));
    buf[4] = std::byte(static_cast<std::uint8_t>(service_port & 0xFFu));
    return write_all(fd, buf.data(), buf.size(), 2000);
}

static bool read_ready_ports(int fd, std::byte &status, ready_ports &ports) {
    std::array<std::byte, 5> buf{};
    if (!read_exact(fd, buf.data(), buf.size(), 2000)) {
        return false;
    }
    status = buf[0];
    ports.sd_port = (std::uint16_t(std::to_integer<std::uint8_t>(buf[1])) << 8) |
                    std::uint16_t(std::to_integer<std::uint8_t>(buf[2]));
    ports.service_port = (std::uint16_t(std::to_integer<std::uint8_t>(buf[3])) << 8) |
                         std::uint16_t(std::to_integer<std::uint8_t>(buf[4]));
    return true;
}

struct tcp_client_state {
    unique_fd   fd{};
    sockaddr_in peer{};
    bool        subscribed{false};
};

static bool same_endpoint(const sockaddr_in &left, const sockaddr_in &right) {
    return left.sin_addr.s_addr == right.sin_addr.s_addr && left.sin_port == right.sin_port;
}

static bool sockaddr_from_ipv4_option(const someip::sd::ipv4_endpoint_option &opt, sockaddr_in &out) {
    if (opt.port == 0) {
        return false;
    }
    std::uint8_t bytes[4] = {
        std::to_integer<std::uint8_t>(opt.address[0]),
        std::to_integer<std::uint8_t>(opt.address[1]),
        std::to_integer<std::uint8_t>(opt.address[2]),
        std::to_integer<std::uint8_t>(opt.address[3])
    };
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port   = htons(opt.port);
    std::memcpy(&out.sin_addr.s_addr, bytes, sizeof(bytes));
    return true;
}

static int run_server(bool reliable, int ready_fd) {
    unique_fd ready{ready_fd};
    constexpr std::byte kReady{0x01};
    constexpr std::byte kBindPerm{0xE1};
    constexpr std::byte kInitFail{0xE2};

    auto notify_ready = [&](std::byte code, std::uint16_t sd_port, std::uint16_t service_port) {
        if (ready.valid()) {
            (void)write_ready_ports(ready.fd(), code, sd_port, service_port);
            ready.reset();
        }
    };

    unique_fd sd_sock = open_udp_socket(0, kServerIp);
    if (!sd_sock.valid()) {
        notify_ready(kInitFail, 0, 0);
        return 1;
    }
    const std::uint16_t sd_port = local_port_for_fd(sd_sock.fd());
    if (sd_port == 0) {
        notify_ready(kInitFail, 0, 0);
        return 1;
    }

    unique_fd service_udp{};
    unique_fd listen_sock{};
    std::uint16_t service_port = 0;
    if (reliable) {
        listen_sock = open_tcp_listener(kServerIp, 0);
        if (!listen_sock.valid()) {
            notify_ready(errno == EPERM ? kBindPerm : kInitFail, 0, 0);
            return 1;
        }
        service_port = local_port_for_fd(listen_sock.fd());
    } else {
        service_udp = open_udp_socket(0, kServerIp);
        if (!service_udp.valid()) {
            notify_ready(errno == EPERM ? kBindPerm : kInitFail, 0, 0);
            return 1;
        }
        service_port = local_port_for_fd(service_udp.fd());
    }
    if (service_port == 0) {
        notify_ready(kInitFail, 0, 0);
        return 1;
    }

    notify_ready(kReady, sd_port, service_port);

    const someip::ser::config cfg{someip::wire::endian::big};
    std::vector<tcp_client_state> tcp_clients{};
    std::vector<sockaddr_in>      udp_subscribers{};
    FieldValue                    field_value{};
    bool                          shutdown = false;

    auto add_unique = [](std::vector<sockaddr_in> &list, const sockaddr_in &addr) {
        for (const auto &existing : list) {
            if (same_endpoint(existing, addr)) {
                return;
            }
        }
        list.push_back(addr);
    };

    while (!shutdown) {
        std::vector<pollfd> poll_fds{};
        poll_fds.push_back({sd_sock.fd(), POLLIN, 0});
        const std::size_t polled_tcp_clients = tcp_clients.size();
        if (reliable) {
            poll_fds.push_back({listen_sock.fd(), POLLIN, 0});
            for (const auto &client : tcp_clients) {
                poll_fds.push_back({client.fd.fd(), POLLIN, 0});
            }
        } else {
            poll_fds.push_back({service_udp.fd(), POLLIN, 0});
        }

        const int ready_count = ::poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), 1000);
        if (ready_count < 0 && errno == EINTR) {
            continue;
        }
        if (ready_count <= 0) {
            continue;
        }

        std::size_t index = 0;
        if ((poll_fds[index].revents & POLLIN) != 0) {
            std::vector<std::byte> frame{};
            sockaddr_in sender{};
            if (recv_udp_datagram(sd_sock.fd(), frame, sender, 0)) {
                auto sdmsg = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
                if (sdmsg) {
                    for (const auto &entry : sdmsg->sd_payload.entries) {
                        if (std::holds_alternative<someip::sd::service_entry>(entry)) {
                            const auto &se = std::get<someip::sd::service_entry>(entry);
                            if (se.c.type == someip::sd::entry_type::find_service) {
                                auto offer = build_sd_offer_service(reliable, service_port);
                                if (!offer.empty()) {
                                    (void)send_udp_datagram(sd_sock.fd(), offer, sender);
                                }
                            }
                        } else if (std::holds_alternative<someip::sd::eventgroup_entry>(entry)) {
                            const auto &eg = std::get<someip::sd::eventgroup_entry>(entry);
                            if (eg.c.type == someip::sd::entry_type::subscribe_eventgroup) {
                                auto ack = build_sd_subscribe_ack();
                                if (!ack.empty()) {
                                    (void)send_udp_datagram(sd_sock.fd(), ack, sender);
                                }
                                if (auto runs = someip::sd::resolve_option_runs(sdmsg->sd_payload, eg.c)) {
                                    auto handle_option = [&](const someip::sd::option &opt) {
                                        if (auto ipv4 = std::get_if<someip::sd::ipv4_endpoint_option>(&opt)) {
                                            sockaddr_in addr{};
                                            if (sockaddr_from_ipv4_option(*ipv4, addr)) {
                                                if (reliable && ipv4->l4_proto == 6) {
                                                    for (auto &client : tcp_clients) {
                                                        if (same_endpoint(client.peer, addr)) {
                                                            client.subscribed = true;
                                                        }
                                                    }
                                                } else if (!reliable && ipv4->l4_proto == 17) {
                                                    add_unique(udp_subscribers, addr);
                                                }
                                            }
                                        }
                                    };
                                    for (const auto &opt : runs->run1) {
                                        handle_option(opt);
                                    }
                                    for (const auto &opt : runs->run2) {
                                        handle_option(opt);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        index++;

        if (reliable) {
            if ((poll_fds[index].revents & POLLIN) != 0) {
                sockaddr_in client_addr{};
                socklen_t   client_len = sizeof(client_addr);
                int         client_fd  = ::accept(listen_sock.fd(), reinterpret_cast<sockaddr *>(&client_addr), &client_len);
                if (client_fd >= 0) {
                    tcp_client_state state{};
                    state.fd = unique_fd{client_fd};
                    state.peer = client_addr;
                    state.subscribed = false;
                    tcp_clients.push_back(std::move(state));
                }
            }
            index++;

            for (std::size_t client_index = 0; client_index < polled_tcp_clients; ++client_index, ++index) {
                if ((poll_fds[index].revents & (POLLERR | POLLHUP)) != 0) {
                    return 1;
                }
                if ((poll_fds[index].revents & POLLIN) == 0) {
                    continue;
                }
                auto frame = recv_someip_frame(tcp_clients[client_index].fd.fd(), 5000);
                if (frame.empty()) {
                    return 1;
                }
                auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
                if (!parsed) {
                    return 1;
                }
                const auto payload_base = parsed->tp ? 20u : 16u;

                if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x00FF &&
                    parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                    shutdown = true;
                    break;
                }

                if (someip::iface::is_set_request(parsed->hdr, writable_field)) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    field_value = req;

                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::response;
                    rh.return_code       = someip::wire::return_code::E_OK;

                    auto resp_frame = build_someip_message(cfg, rh, EmptyPayload{});
                    if (resp_frame.empty()) {
                        return 1;
                    }
                    (void)send_frame(tcp_clients[client_index].fd.fd(), resp_frame, 2000);

                    for (const auto &client : tcp_clients) {
                        if (!client.subscribed) {
                            continue;
                        }
                        for (std::uint32_t i = 0; i < kEventCount; ++i) {
                            auto eh = someip::iface::make_notify_header(writable_field, 1);
                            FieldEvent evp{.seq = i, .value = field_value.value};
                            auto       ev = build_someip_message(cfg, eh, evp);
                            if (ev.empty()) {
                                return 1;
                            }
                            (void)send_frame(client.fd.fd(), ev, 2000);
                        }
                    }
                    continue;
                }

                if (someip::iface::is_set_request(parsed->hdr, readonly_field)) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::error;
                    rh.return_code       = someip::wire::return_code::E_NOT_OK;
                    auto resp_frame = build_someip_message(cfg, rh, EmptyPayload{});
                    if (resp_frame.empty()) {
                        return 1;
                    }
                    (void)send_frame(tcp_clients[client_index].fd.fd(), resp_frame, 2000);
                    continue;
                }

                if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x0001 &&
                    parsed->hdr.msg_type == someip::wire::message_type::request) {
                    MethodReq req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    MethodResp resp{.y = req.x + 1};
                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::response;
                    rh.return_code       = someip::wire::return_code::E_OK;
                    auto resp_frame = build_someip_message(cfg, rh, resp);
                    if (resp_frame.empty()) {
                        return 1;
                    }
                    (void)send_frame(tcp_clients[client_index].fd.fd(), resp_frame, 2000);
                    continue;
                }
            }
        } else {
            if ((poll_fds[index].revents & POLLIN) != 0) {
                std::vector<std::byte> frame{};
                sockaddr_in            sender{};
                if (!recv_udp_datagram(service_udp.fd(), frame, sender, 0)) {
                    continue;
                }
                auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
                if (!parsed) {
                    continue;
                }
                const auto payload_base = parsed->tp ? 20u : 16u;

                if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x00FF &&
                    parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                    shutdown = true;
                    continue;
                }

                if (someip::iface::is_set_request(parsed->hdr, writable_field)) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    field_value = req;

                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::response;
                    rh.return_code       = someip::wire::return_code::E_OK;
                    auto resp_frame = build_someip_message(cfg, rh, EmptyPayload{});
                    if (!resp_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), resp_frame, sender);
                    }

                    for (const auto &subscriber : udp_subscribers) {
                        for (std::uint32_t i = 0; i < kEventCount; ++i) {
                            auto eh = someip::iface::make_notify_header(writable_field, 1);
                            FieldEvent evp{.seq = i, .value = field_value.value};
                            auto       ev = build_someip_message(cfg, eh, evp);
                            if (ev.empty()) {
                                return 1;
                            }
                            (void)send_udp_datagram(service_udp.fd(), ev, subscriber);
                        }
                    }
                    continue;
                }

                if (someip::iface::is_set_request(parsed->hdr, readonly_field)) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::error;
                    rh.return_code       = someip::wire::return_code::E_NOT_OK;
                    auto resp_frame = build_someip_message(cfg, rh, EmptyPayload{});
                    if (!resp_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), resp_frame, sender);
                    }
                    continue;
                }

                if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x0001 &&
                    parsed->hdr.msg_type == someip::wire::message_type::request) {
                    MethodReq req{};
                    if (!someip::ser::decode(parsed->payload, cfg, req, payload_base)) {
                        return 1;
                    }
                    MethodResp resp{.y = req.x + 1};
                    someip::wire::header rh{};
                    rh.msg.service_id    = parsed->hdr.msg.service_id;
                    rh.msg.method_id     = parsed->hdr.msg.method_id;
                    rh.req               = parsed->hdr.req;
                    rh.protocol_version  = 1;
                    rh.interface_version = parsed->hdr.interface_version;
                    rh.msg_type          = someip::wire::message_type::response;
                    rh.return_code       = someip::wire::return_code::E_OK;
                    auto resp_frame = build_someip_message(cfg, rh, resp);
                    if (!resp_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), resp_frame, sender);
                    }
                    continue;
                }
            }
        }
    }

    return 0;
}

struct child_guard {
    pid_t pid{-1};
    bool  active{true};

    ~child_guard() {
        if (!active) {
            return;
        }
        if (pid > 0) {
            int  status = 0;
            auto rc     = ::waitpid(pid, &status, WNOHANG);
            if (rc == 0) {
                (void)::kill(pid, SIGKILL);
                (void)::waitpid(pid, &status, 0);
            }
        }
    }

    void release() noexcept { active = false; }
};

static bool wait_for_sd_offer(int sd_fd, std::uint16_t &service_port, int timeout_ms) {
    std::vector<std::byte> frame{};
    sockaddr_in sender{};
    if (!recv_udp_datagram(sd_fd, frame, sender, timeout_ms)) {
        return false;
    }
    auto offer = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    if (!offer) {
        return false;
    }
    for (const auto &entry : offer->sd_payload.entries) {
        if (!std::holds_alternative<someip::sd::service_entry>(entry)) {
            continue;
        }
        const auto &se = std::get<someip::sd::service_entry>(entry);
        if (se.c.type != someip::sd::entry_type::offer_service) {
            continue;
        }
        if (auto runs = someip::sd::resolve_option_runs(offer->sd_payload, se.c)) {
            for (const auto &opt : runs->run1) {
                if (auto ipv4 = std::get_if<someip::sd::ipv4_endpoint_option>(&opt)) {
                    service_port = ipv4->port;
                    return service_port != 0;
                }
            }
        }
    }
    return false;
}

static bool wait_for_sd_ack(int sd_fd, int timeout_ms) {
    std::vector<std::byte> frame{};
    sockaddr_in sender{};
    if (!recv_udp_datagram(sd_fd, frame, sender, timeout_ms)) {
        return false;
    }
    auto ack = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    return ack.has_value();
}

static bool wait_for_response(int fd, bool reliable, std::uint16_t method_id, std::uint8_t expected_type,
                              std::uint8_t expected_rc, const someip::ser::config &cfg,
                              bool expect_payload = false) {
    std::vector<std::byte> frame{};
    if (reliable) {
        frame = recv_someip_frame(fd, 5000);
        if (frame.empty()) return false;
    } else {
        sockaddr_in sender{};
        if (!recv_udp_datagram(fd, frame, sender, 5000)) return false;
    }
    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    if (!parsed) return false;

    if (parsed->hdr.msg.method_id != method_id || parsed->hdr.msg_type != expected_type ||
        parsed->hdr.return_code != expected_rc) {
        return false;
    }
    if (expect_payload) {
        MethodResp resp{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        if (!someip::ser::decode(parsed->payload, cfg, resp, payload_base)) {
            return false;
        }
        return resp.y == 42u;
    }
    return true;
}

static bool wait_for_events(int fd, bool reliable, std::size_t count, const someip::ser::config &cfg) {
    std::vector<bool> seen(count, false);
    std::size_t received = 0;
    while (received < count) {
        std::vector<std::byte> frame{};
        if (reliable) {
            frame = recv_someip_frame(fd, 5000);
            if (frame.empty()) return false;
        } else {
            sockaddr_in sender{};
            if (!recv_udp_datagram(fd, frame, sender, 5000)) return false;
        }
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
        if (!parsed) return false;
        if (parsed->hdr.msg_type != someip::wire::message_type::notification ||
            parsed->hdr.msg.method_id != writable_field.notifier_event_id) {
            return false;
        }
        FieldEvent ev{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        if (!someip::ser::decode(parsed->payload, cfg, ev, payload_base)) {
            return false;
        }
        if (ev.seq >= count || ev.value != kFieldValue) {
            return false;
        }
        if (!seen[ev.seq]) {
            seen[ev.seq] = true;
            received++;
        }
    }
    return true;
}

static int run_client(bool reliable, const ready_ports &ports) {
    const someip::ser::config cfg{someip::wire::endian::big};

    unique_fd sd_sock = open_udp_socket(0, kServerIp);
    if (!sd_sock.valid()) {
        return 1;
    }

    const auto find = build_sd_find_service();
    if (find.empty()) {
        return 1;
    }
    auto sd_addr = make_addr(kServerIp, ports.sd_port);
    if (!send_udp_datagram(sd_sock.fd(), find, sd_addr)) {
        return 1;
    }

    std::uint16_t service_port = 0;
    if (!wait_for_sd_offer(sd_sock.fd(), service_port, 2000)) {
        return 1;
    }

    unique_fd client_a{};
    unique_fd client_b{};
    std::uint16_t port_a = 0;
    std::uint16_t port_b = 0;
    sockaddr_in service_addr = make_addr(kServerIp, service_port);

    if (reliable) {
        client_a = connect_tcp_with_retry(service_addr, 5000);
        client_b = connect_tcp_with_retry(service_addr, 5000);
        if (!client_a.valid() || !client_b.valid()) {
            return 1;
        }
        port_a = local_port_for_fd(client_a.fd());
        port_b = local_port_for_fd(client_b.fd());
    } else {
        client_a = open_udp_socket(0, kServerIp);
        client_b = open_udp_socket(0, kServerIp);
        if (!client_a.valid() || !client_b.valid()) {
            return 1;
        }
        port_a = local_port_for_fd(client_a.fd());
        port_b = local_port_for_fd(client_b.fd());
    }

    if (port_a == 0 || port_b == 0) {
        return 1;
    }

    auto sub_a = build_sd_subscribe_eventgroup(reliable, port_a);
    if (sub_a.empty()) {
        return 1;
    }
    if (!send_udp_datagram(sd_sock.fd(), sub_a, sd_addr)) {
        return 1;
    }
    if (!wait_for_sd_ack(sd_sock.fd(), 2000)) {
        return 1;
    }

    auto sub_b = build_sd_subscribe_eventgroup(reliable, port_b);
    if (sub_b.empty()) {
        return 1;
    }
    if (!send_udp_datagram(sd_sock.fd(), sub_b, sd_addr)) {
        return 1;
    }
    if (!wait_for_sd_ack(sd_sock.fd(), 2000)) {
        return 1;
    }

    auto send_service = [&](unique_fd &sock, const std::vector<std::byte> &frame) {
        if (reliable) {
            return send_frame(sock.fd(), frame, 2000);
        }
        return send_udp_datagram(sock.fd(), frame, service_addr);
    };

    // Setter (writable) from client A
    someip::wire::header set_hdr{};
    set_hdr.msg.service_id    = 0x1234;
    set_hdr.msg.method_id     = writable_field.setter_method_id;
    set_hdr.req.client_id     = 1;
    set_hdr.req.session_id    = 1;
    set_hdr.protocol_version  = 1;
    set_hdr.interface_version = 1;
    set_hdr.msg_type          = someip::wire::message_type::request;
    FieldValue set_payload{.value = kFieldValue};
    auto set_frame = build_someip_message(cfg, set_hdr, set_payload);
    if (set_frame.empty() || !send_service(client_a, set_frame)) {
        return 1;
    }
    if (!wait_for_response(client_a.fd(), reliable, writable_field.setter_method_id,
                           someip::wire::message_type::response, someip::wire::return_code::E_OK, cfg)) {
        return 1;
    }

    if (!wait_for_events(client_a.fd(), reliable, kEventCount, cfg)) {
        return 1;
    }
    if (!wait_for_events(client_b.fd(), reliable, kEventCount, cfg)) {
        return 1;
    }

    // Setter (read-only) from client B
    someip::wire::header ro_hdr{};
    ro_hdr.msg.service_id    = 0x1234;
    ro_hdr.msg.method_id     = readonly_field.setter_method_id;
    ro_hdr.req.client_id     = 2;
    ro_hdr.req.session_id    = 2;
    ro_hdr.protocol_version  = 1;
    ro_hdr.interface_version = 1;
    ro_hdr.msg_type          = someip::wire::message_type::request;
    FieldValue ro_payload{.value = 0x1234};
    auto ro_frame = build_someip_message(cfg, ro_hdr, ro_payload);
    if (ro_frame.empty() || !send_service(client_b, ro_frame)) {
        return 1;
    }
    if (!wait_for_response(client_b.fd(), reliable, readonly_field.setter_method_id,
                           someip::wire::message_type::error, someip::wire::return_code::E_NOT_OK, cfg)) {
        return 1;
    }

    // Method call from client A
    someip::wire::header m_hdr{};
    m_hdr.msg.service_id    = 0x1234;
    m_hdr.msg.method_id     = 0x0001;
    m_hdr.req.client_id     = 1;
    m_hdr.req.session_id    = 3;
    m_hdr.protocol_version  = 1;
    m_hdr.interface_version = 1;
    m_hdr.msg_type          = someip::wire::message_type::request;
    MethodReq mr{.x = 41};
    auto m_frame = build_someip_message(cfg, m_hdr, mr);
    if (m_frame.empty() || !send_service(client_a, m_frame)) {
        return 1;
    }
    if (!wait_for_response(client_a.fd(), reliable, m_hdr.msg.method_id,
                           someip::wire::message_type::response, someip::wire::return_code::E_OK, cfg, true)) {
        return 1;
    }

    // Shutdown
    someip::wire::header s_hdr{};
    s_hdr.msg.service_id    = 0x1234;
    s_hdr.msg.method_id     = 0x00FF;
    s_hdr.req.client_id     = 1;
    s_hdr.req.session_id    = 4;
    s_hdr.protocol_version  = 1;
    s_hdr.interface_version = 1;
    s_hdr.msg_type          = someip::wire::message_type::request_no_return;
    auto s_frame = build_someip_message(cfg, s_hdr, EmptyPayload{});
    if (s_frame.empty() || !send_service(client_a, s_frame)) {
        return 1;
    }

    return 0;
}

} // namespace

TEST_CASE("someip: tcp e2e multi-client subscribe + setter + notify") {
    int pipefd[2]{-1, -1};
    REQUIRE(::pipe(pipefd) == 0);
    unique_fd pipe_r{pipefd[0]};
    unique_fd pipe_w{pipefd[1]};

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        pipe_r.reset();
        const int rc = run_server(true, pipe_w.fd());
        pipe_w.reset();
        std::_Exit(rc);
    }
    pipe_w.reset();

    child_guard guard{.pid = pid};

    std::byte status{};
    ready_ports ports{};
    REQUIRE(read_ready_ports(pipe_r.fd(), status, ports));
    pipe_r.reset();
    if (status != std::byte{0x01}) {
        int exit_status = 0;
        (void)::waitpid(pid, &exit_status, 0);
        if (status == std::byte{0xE1}) {
            INFO("tcp bind not permitted in current environment");
            return;
        }
        FAIL("tcp server initialization failed");
    }

    REQUIRE(run_client(true, ports) == 0);

    int exit_status = 0;
    (void)::waitpid(pid, &exit_status, 0);
    REQUIRE(WIFEXITED(exit_status));
    REQUIRE(WEXITSTATUS(exit_status) == 0);
    guard.release();
}

TEST_CASE("someip: udp e2e multi-client subscribe + setter + notify") {
    int pipefd[2]{-1, -1};
    REQUIRE(::pipe(pipefd) == 0);
    unique_fd pipe_r{pipefd[0]};
    unique_fd pipe_w{pipefd[1]};

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        pipe_r.reset();
        const int rc = run_server(false, pipe_w.fd());
        pipe_w.reset();
        std::_Exit(rc);
    }
    pipe_w.reset();

    child_guard guard{.pid = pid};

    std::byte status{};
    ready_ports ports{};
    REQUIRE(read_ready_ports(pipe_r.fd(), status, ports));
    pipe_r.reset();
    if (status != std::byte{0x01}) {
        int exit_status = 0;
        (void)::waitpid(pid, &exit_status, 0);
        if (status == std::byte{0xE1}) {
            INFO("udp bind not permitted in current environment");
            return;
        }
        FAIL("udp server initialization failed");
    }

    REQUIRE(run_client(false, ports) == 0);

    int exit_status = 0;
    (void)::waitpid(pid, &exit_status, 0);
    REQUIRE(WIFEXITED(exit_status));
    REQUIRE(WEXITSTATUS(exit_status) == 0);
    guard.release();
}

#endif
