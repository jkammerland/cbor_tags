#include <vsomeip/vsomeip.hpp>

#include "someip/iface/field.h"
#include "someip/sd/sd.h"
#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/ser/encode.h"
#include "someip/wire/message.h"
#include "someip/wire/return_code.h"
#include "someip/wire/someip.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr std::uint16_t kServiceId   = 0x1234;
constexpr std::uint16_t kInstanceId  = 0x0001;
constexpr std::uint16_t kMethodId    = 0x0001;
constexpr std::uint16_t kSetterW     = 0x0101;
constexpr std::uint16_t kSetterRO    = 0x0201;
constexpr std::uint16_t kShutdown    = 0x00FF;
constexpr std::uint16_t kEventId     = 0x8001;
constexpr std::uint16_t kEventgroup  = 0x0001;
constexpr std::uint16_t kSdPort      = 30490;
constexpr std::uint16_t kServicePort = 30509;
constexpr std::size_t   kEventCount  = 10;
constexpr std::uint32_t kValue       = 0xBEEF;
constexpr std::uint8_t  kSdMajorVersion = 0x00;

constexpr const char *kCustomIp   = "127.0.0.2";
constexpr const char *kVsomeipIp  = "127.0.0.1";
constexpr const char *kMulticastIp = "224.244.224.245";
constexpr std::uint8_t kSdUnicastFlag = 0x40;

const someip::ser::config kCfg{someip::wire::endian::big};

struct FieldValue {
    std::uint32_t value{};
};

struct FieldEvent {
    std::uint32_t seq{};
    std::uint32_t value{};
};

struct MethodReq {
    std::uint32_t x{};
};

struct MethodResp {
    std::uint32_t y{};
};

struct EmptyPayload {};

#if !defined(_WIN32)
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
        const int result = ::poll(&pfd, 1, timeout_ms);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return result;
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
        const int  ready     = poll_one(fd, POLLOUT, static_cast<int>(remaining));
        if (ready <= 0) {
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
        const int  ready     = poll_one(fd, POLLIN, static_cast<int>(remaining));
        if (ready <= 0) {
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

static bool send_udp_datagram_from(int fd, const std::vector<std::byte> &frame, const sockaddr_in &to, const char *source_ip) {
    (void)source_ip;
    return send_udp_datagram(fd, frame, to);
}
static sockaddr_in make_addr(const char *ip, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

static unique_fd open_udp_socket(std::uint16_t port, bool join_multicast, const char *bind_ip, const char *mcast_if_ip,
                                 bool reuse_port) {
    unique_fd sock{::socket(AF_INET, SOCK_DGRAM, 0)};
    if (!sock.valid()) {
        return {};
    }
    int reuse = 1;
    (void)::setsockopt(sock.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    if (reuse_port) {
        int reuse_port_flag = 1;
        (void)::setsockopt(sock.fd(), SOL_SOCKET, SO_REUSEPORT, &reuse_port_flag, sizeof(reuse_port_flag));
    }
#endif
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
    if (join_multicast) {
        ip_mreq mreq{};
        ::inet_pton(AF_INET, kMulticastIp, &mreq.imr_multiaddr);
        if (mcast_if_ip != nullptr) {
            ::inet_pton(AF_INET, mcast_if_ip, &mreq.imr_interface);
        } else if (bind_ip != nullptr) {
            ::inet_pton(AF_INET, bind_ip, &mreq.imr_interface);
        } else {
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        }
        if (::setsockopt(sock.fd(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            return {};
        }
        int loopback = 1;
        (void)::setsockopt(sock.fd(), IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback));
        in_addr loop_iface{};
        ::inet_pton(AF_INET, mcast_if_ip ? mcast_if_ip : (bind_ip ? bind_ip : kCustomIp), &loop_iface);
        (void)::setsockopt(sock.fd(), IPPROTO_IP, IP_MULTICAST_IF, &loop_iface, sizeof(loop_iface));
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

static unique_fd connect_tcp_with_retry(const char *target_ip, std::uint16_t port, int timeout_ms) {
    unique_fd sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        return {};
    }
    sockaddr_in addr = make_addr(target_ip, port);
    const auto  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
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

static std::vector<std::byte> build_someip_message(const someip::ser::config &cfg, someip::wire::header header, const auto &payload) {
    std::vector<std::byte> out{};
    auto                   status = someip::wire::encode_message(out, header, cfg, payload);
    if (!status) {
        return {};
    }
    return out;
}

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
    someip::sd::packet_data pd{};
    someip::sd::service_entry_data se{};
    se.type          = someip::sd::entry_type::find_service;
    se.service_id    = kServiceId;
    se.instance_id   = 0xFFFF;
    se.major_version = 0xFF;
    se.ttl           = 3;
    se.minor_version = 0xFFFFFFFFu;
    pd.entries.push_back(se);
    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_offer_service(const char *ip, bool reliable) {
    someip::sd::packet_data pd{};
    pd.hdr.flags |= kSdUnicastFlag;
    someip::sd::service_entry_data se{};
    se.type          = someip::sd::entry_type::offer_service;
    se.service_id    = kServiceId;
    se.instance_id   = kInstanceId;
    se.major_version = kSdMajorVersion;
    se.ttl           = 3;
    se.minor_version = 0;
    se.run1.push_back(make_ipv4_endpoint(ip, reliable, kServicePort));
    pd.entries.push_back(se);
    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_subscribe(const char *ip, bool reliable, std::uint16_t port) {
    someip::sd::packet_data pd{};
    pd.hdr.flags |= kSdUnicastFlag;
    someip::sd::eventgroup_entry_data eg{};
    eg.type          = someip::sd::entry_type::subscribe_eventgroup;
    eg.service_id    = kServiceId;
    eg.instance_id   = kInstanceId;
    eg.major_version = kSdMajorVersion;
    eg.ttl           = 3;
    eg.eventgroup_id = kEventgroup;
    eg.reserved12_counter4 = 0;
    eg.run1.push_back(make_ipv4_endpoint(ip, reliable, port));
    pd.entries.push_back(eg);
    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

static std::vector<std::byte> build_sd_subscribe_ack(std::uint16_t counter_field) {
    someip::sd::packet_data pd{};
    pd.hdr.flags |= kSdUnicastFlag;
    someip::sd::eventgroup_entry_data eg{};
    eg.type          = someip::sd::entry_type::subscribe_eventgroup_ack;
    eg.service_id    = kServiceId;
    eg.instance_id   = kInstanceId;
    eg.major_version = kSdMajorVersion;
    eg.ttl           = 3;
    eg.eventgroup_id = kEventgroup;
    eg.reserved12_counter4 = counter_field;
    pd.entries.push_back(eg);
    auto msg = someip::sd::encode_message(pd);
    if (!msg) {
        return {};
    }
    return *msg;
}

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

static std::uint16_t local_port_for_fd(int fd) {
    sockaddr_in addr{};
    socklen_t   len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

static bool read_byte_timeout(int fd, std::byte &out, int timeout_ms) {
    pollfd pfd{};
    pfd.fd     = fd;
    pfd.events = POLLIN;
    int rc = ::poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return false;
    const auto n = ::read(fd, &out, 1);
    return n == 1;
}

static bool write_byte(int fd, std::byte val) {
    const auto n = ::write(fd, &val, 1);
    return n == 1;
}

static void cleanup_vsomeip_sockets() {
    const char *paths[] = {
        "/tmp/vsomeip-0",
        "/tmp/vsomeip-1000",
        "/tmp/vsomeip-1001",
        "/tmp/vsomeip-1002",
        "/tmp/vsomeip-1003",
        "/tmp/vsomeip.lck",
    };
    for (const auto *path : paths) {
        ::unlink(path);
    }
}

static bool wait_for_socket_path(const char *path, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::access(path, F_OK) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void debug_dump_sockets(const char *label) {
    if (std::getenv("SOMEIP_TEST_DEBUG") == nullptr) {
        return;
    }
    std::fprintf(stderr, "%s\n", label);
    (void)std::system("ss -u -a -p | rg 30490 || true");
}

struct tcp_client_state {
    unique_fd fd{};
    sockaddr_in peer{};
    bool subscribed{false};
};

struct sd_sockets {
    unique_fd unicast{};
    unique_fd multicast{};
};

static sd_sockets open_sd_sockets(const char *bind_ip, const char *mcast_if_ip) {
    sd_sockets out{};
    out.unicast = open_udp_socket(kSdPort, false, bind_ip, mcast_if_ip, false);
    out.multicast = open_udp_socket(kSdPort, true, nullptr, mcast_if_ip, true);
    return out;
}

static int run_custom_server(bool reliable, int ready_fd, const char *server_ip) {
    const bool debug = std::getenv("SOMEIP_TEST_DEBUG") != nullptr;
    auto sd = open_sd_sockets(server_ip, server_ip);
    if (!sd.unicast.valid() || !sd.multicast.valid()) {
        std::fprintf(stderr, "custom server: sd socket failed\n");
        return 1;
    }

    unique_fd service_udp{};
    unique_fd listen_sock{};
    if (reliable) {
        listen_sock = open_tcp_listener(server_ip, kServicePort);
        if (!listen_sock.valid()) {
            std::fprintf(stderr, "custom server: tcp listen failed\n");
            return 1;
        }
    } else {
        service_udp = open_udp_socket(kServicePort, false, server_ip, server_ip, true);
        if (!service_udp.valid()) {
            std::fprintf(stderr, "custom server: udp service socket failed\n");
            return 1;
        }
    }

    if (ready_fd >= 0) {
        (void)write_byte(ready_fd, std::byte{0x01});
        ::close(ready_fd);
    }
    {
        auto offer = build_sd_offer_service(server_ip, reliable);
        if (!offer.empty()) {
            auto multicast_addr = make_addr(kMulticastIp, kSdPort);
            auto unicast_addr = make_addr(kVsomeipIp, kSdPort);
            for (int attempt = 0; attempt < 3; ++attempt) {
                (void)send_udp_datagram(sd.multicast.fd(), offer, multicast_addr);
                (void)send_udp_datagram(sd.unicast.fd(), offer, unicast_addr);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    std::vector<tcp_client_state> tcp_clients{};
    std::vector<sockaddr_in>      tcp_subscribers{};
    std::vector<sockaddr_in>      udp_subscribers{};
    FieldValue                    field_value{};
    bool                          shutdown = false;
    auto                          last_offer = std::chrono::steady_clock::now();
    auto add_unique = [](std::vector<sockaddr_in> &list, const sockaddr_in &addr) {
        for (const auto &existing : list) {
            if (same_endpoint(existing, addr)) {
                return;
            }
        }
        list.push_back(addr);
    };

    while (!shutdown) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_offer > std::chrono::seconds(1)) {
            auto offer = build_sd_offer_service(server_ip, reliable);
            if (!offer.empty()) {
                auto multicast_addr = make_addr(kMulticastIp, kSdPort);
                auto unicast_addr = make_addr(kVsomeipIp, kSdPort);
                (void)send_udp_datagram(sd.multicast.fd(), offer, multicast_addr);
                (void)send_udp_datagram(sd.unicast.fd(), offer, unicast_addr);
            }
            last_offer = now;
        }
        std::vector<pollfd> poll_fds{};
        poll_fds.push_back({sd.unicast.fd(), POLLIN, 0});
        poll_fds.push_back({sd.multicast.fd(), POLLIN, 0});
        if (reliable) {
            poll_fds.push_back({listen_sock.fd(), POLLIN, 0});
            for (const auto &client : tcp_clients) {
                poll_fds.push_back({client.fd.fd(), POLLIN, 0});
            }
        } else {
            poll_fds.push_back({service_udp.fd(), POLLIN, 0});
        }

        const int ready = ::poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), 1000);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            continue;
        }

        std::size_t index = 0;
        if ((poll_fds[index].revents & POLLIN) != 0 || (poll_fds[index + 1].revents & POLLIN) != 0) {
            std::vector<std::byte> sd_frame{};
            sockaddr_in            sender{};
            bool received = false;
            if ((poll_fds[index].revents & POLLIN) != 0) {
                received = recv_udp_datagram(sd.unicast.fd(), sd_frame, sender, 0);
            }
            if (!received && (poll_fds[index + 1].revents & POLLIN) != 0) {
                received = recv_udp_datagram(sd.multicast.fd(), sd_frame, sender, 0);
            }
            if (received) {
                if (debug) {
                    char addr[INET_ADDRSTRLEN]{};
                    ::inet_ntop(AF_INET, &sender.sin_addr, addr, sizeof(addr));
                    std::fprintf(stderr, "custom server: SD datagram from %s:%u len=%zu\n",
                                 addr, ntohs(sender.sin_port), sd_frame.size());
                }
                auto sdmsg = someip::sd::decode_message(std::span<const std::byte>(sd_frame.data(), sd_frame.size()));
                if (!sdmsg) {
                    if (debug) {
                        std::fprintf(stderr, "custom server: SD decode failed (code=%d, size=%zu)\n",
                                     static_cast<int>(sdmsg.error()), sd_frame.size());
                    }
                    continue;
                }
                {
                    for (const auto &entry : sdmsg->sd_payload.entries) {
                        if (std::holds_alternative<someip::sd::service_entry>(entry)) {
                            const auto &se = std::get<someip::sd::service_entry>(entry);
                            if (debug) {
                                std::fprintf(stderr, "custom server: SD service entry type=0x%02X svc=0x%04X inst=0x%04X ttl=%u\n",
                                             se.c.type, se.c.service_id, se.c.instance_id, se.c.ttl);
                            }
                            if (se.c.type == someip::sd::entry_type::find_service) {
                                auto offer = build_sd_offer_service(server_ip, reliable);
                                if (!offer.empty()) {
                                    (void)send_udp_datagram(sd.unicast.fd(), offer, sender);
                                }
                            }
                        } else if (std::holds_alternative<someip::sd::eventgroup_entry>(entry)) {
                            const auto &eg = std::get<someip::sd::eventgroup_entry>(entry);
                            if (debug) {
                                std::fprintf(stderr,
                                             "custom server: SD eventgroup entry type=0x%02X svc=0x%04X inst=0x%04X eg=0x%04X ttl=%u\n",
                                             eg.c.type, eg.c.service_id, eg.c.instance_id, eg.eventgroup_id, eg.c.ttl);
                            }
                            if (eg.c.type == someip::sd::entry_type::subscribe_eventgroup) {
                                auto ack = build_sd_subscribe_ack(eg.reserved12_counter4);
                                if (!ack.empty()) {
                                    (void)send_udp_datagram(sd.unicast.fd(), ack, sender);
                                }
                                struct sd_endpoint {
                                    sockaddr_in addr{};
                                    std::uint8_t proto{0};
                                };
                                std::vector<sd_endpoint> endpoints{};
                                if (auto runs = someip::sd::resolve_option_runs(sdmsg->sd_payload, eg.c)) {
                                    auto handle_option = [&](const someip::sd::option &opt) {
                                        if (auto ipv4 = std::get_if<someip::sd::ipv4_endpoint_option>(&opt)) {
                                            sockaddr_in addr{};
                                            if (sockaddr_from_ipv4_option(*ipv4, addr)) {
                                                endpoints.push_back({addr, ipv4->l4_proto});
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
                                bool matched = false;
                                for (const auto &endpoint : endpoints) {
                                    if (reliable && endpoint.proto == 6) {
                                        add_unique(tcp_subscribers, endpoint.addr);
                                        for (auto &client : tcp_clients) {
                                            if (same_endpoint(client.peer, endpoint.addr)) {
                                                client.subscribed = true;
                                            }
                                        }
                                        matched = true;
                                    }
                                    if (!reliable && endpoint.proto == 17) {
                                        add_unique(udp_subscribers, endpoint.addr);
                                        matched = true;
                                    }
                                }
                                if (!matched && !reliable) {
                                    add_unique(udp_subscribers, sender);
                                }
                            }
                        }
                    }
                }
            }
        }
        index += 2;

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
                    for (const auto &subscriber : tcp_subscribers) {
                        if (same_endpoint(tcp_clients.back().peer, subscriber)) {
                            tcp_clients.back().subscribed = true;
                            break;
                        }
                    }
                }
            }
            index++;

            for (std::size_t client_index = 0; client_index < tcp_clients.size(); ++client_index, ++index) {
                if ((poll_fds[index].revents & (POLLERR | POLLHUP)) != 0) {
                    std::fprintf(stderr, "custom server: tcp client error\n");
                    shutdown = true;
                    break;
                }
                if ((poll_fds[index].revents & POLLIN) == 0) {
                    continue;
                }
                auto frame = recv_someip_frame(tcp_clients[client_index].fd.fd(), 5000);
                if (frame.empty()) {
                    std::fprintf(stderr, "custom server: tcp recv failed\n");
                    shutdown = true;
                    break;
                }
                auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
                if (!parsed) {
                    std::fprintf(stderr, "custom server: parse failed\n");
                    shutdown = true;
                    break;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kShutdown &&
                    parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                    shutdown = true;
                    break;
                }

                const auto payload_base = parsed->tp ? 20u : 16u;
                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kSetterW) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, kCfg, req, payload_base)) {
                        std::fprintf(stderr, "custom server: setter decode failed\n");
                        shutdown = true;
                        break;
                    }
                    field_value = req;

                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::response;
                    response_header.return_code       = someip::wire::return_code::E_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, EmptyPayload{});
                    if (!response_frame.empty()) {
                        (void)send_frame(tcp_clients[client_index].fd.fd(), response_frame, 2000);
                    }

                    for (const auto &client : tcp_clients) {
                        if (!client.subscribed) {
                            continue;
                        }
                        for (std::uint32_t seq_index = 0; seq_index < kEventCount; ++seq_index) {
                            someip::iface::field_descriptor fd{};
                            fd.service_id       = kServiceId;
                            fd.notifier_event_id = kEventId;
                            auto notify_header = someip::iface::make_notify_header(fd, 1);
                            FieldEvent ev{.seq = seq_index, .value = field_value.value};
                            auto       ev_frame = build_someip_message(kCfg, notify_header, ev);
                            if (!ev_frame.empty()) {
                                (void)send_frame(client.fd.fd(), ev_frame, 2000);
                            }
                        }
                    }
                    continue;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kSetterRO) {
                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::error;
                    response_header.return_code       = someip::wire::return_code::E_NOT_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, EmptyPayload{});
                    if (!response_frame.empty()) {
                        (void)send_frame(tcp_clients[client_index].fd.fd(), response_frame, 2000);
                    }
                    continue;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kMethodId) {
                    MethodReq req{};
                    if (!someip::ser::decode(parsed->payload, kCfg, req, payload_base)) {
                        std::fprintf(stderr, "custom server: method decode failed\n");
                        shutdown = true;
                        break;
                    }
                    MethodResp resp{.y = req.x + 1};
                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::response;
                    response_header.return_code       = someip::wire::return_code::E_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, resp);
                    if (!response_frame.empty()) {
                        (void)send_frame(tcp_clients[client_index].fd.fd(), response_frame, 2000);
                    }
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
                    std::fprintf(stderr, "custom server: udp parse failed\n");
                    continue;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kShutdown &&
                    parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                    shutdown = true;
                    continue;
                }

                const auto payload_base = parsed->tp ? 20u : 16u;
                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kSetterW) {
                    FieldValue req{};
                    if (!someip::ser::decode(parsed->payload, kCfg, req, payload_base)) {
                        std::fprintf(stderr, "custom server: udp setter decode failed\n");
                        continue;
                    }
                    field_value = req;

                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::response;
                    response_header.return_code       = someip::wire::return_code::E_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, EmptyPayload{});
                    if (!response_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), response_frame, sender);
                    }

                    for (const auto &subscriber : udp_subscribers) {
                        for (std::uint32_t seq_index = 0; seq_index < kEventCount; ++seq_index) {
                            someip::iface::field_descriptor fd{};
                            fd.service_id       = kServiceId;
                            fd.notifier_event_id = kEventId;
                            auto notify_header = someip::iface::make_notify_header(fd, 1);
                            FieldEvent ev{.seq = seq_index, .value = field_value.value};
                            auto       ev_frame = build_someip_message(kCfg, notify_header, ev);
                            if (!ev_frame.empty()) {
                                (void)send_udp_datagram(service_udp.fd(), ev_frame, subscriber);
                            }
                        }
                    }
                    continue;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kSetterRO) {
                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::error;
                    response_header.return_code       = someip::wire::return_code::E_NOT_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, EmptyPayload{});
                    if (!response_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), response_frame, sender);
                    }
                    continue;
                }

                if (parsed->hdr.msg.service_id == kServiceId && parsed->hdr.msg.method_id == kMethodId) {
                    MethodReq req{};
                    if (!someip::ser::decode(parsed->payload, kCfg, req, payload_base)) {
                        std::fprintf(stderr, "custom server: udp method decode failed\n");
                        continue;
                    }
                    MethodResp resp{.y = req.x + 1};
                    someip::wire::header response_header{};
                    response_header.msg.service_id    = parsed->hdr.msg.service_id;
                    response_header.msg.method_id     = parsed->hdr.msg.method_id;
                    response_header.req               = parsed->hdr.req;
                    response_header.protocol_version  = 1;
                    response_header.interface_version = parsed->hdr.interface_version;
                    response_header.msg_type          = someip::wire::message_type::response;
                    response_header.return_code       = someip::wire::return_code::E_OK;
                    auto response_frame = build_someip_message(kCfg, response_header, resp);
                    if (!response_frame.empty()) {
                        (void)send_udp_datagram(service_udp.fd(), response_frame, sender);
                    }
                    continue;
                }
            }
        }
    }

    return 0;
}

class vsomeip_client_runner {
  public:
    vsomeip_client_runner(std::string name,
                          bool reliable,
                          bool is_a,
                          int go_fd,
                          int done_fd,
                          int ready_fd = -1,
                          int subscribed_fd = -1)
        : name_(std::move(name)),
          reliable_(reliable),
          is_a_(is_a),
          go_fd_(go_fd),
          done_fd_(done_fd),
          ready_fd_(ready_fd),
          subscribed_fd_(subscribed_fd),
          debug_(std::getenv("SOMEIP_TEST_DEBUG") != nullptr) {}

    int run() {
        ::setenv("VSOMEIP_APPLICATION_NAME", name_.c_str(), 1);
        app_ = vsomeip::runtime::get()->create_application(name_);
        if (!app_->init()) {
            std::fprintf(stderr, "%s: init failed\n", name_.c_str());
            return 1;
        }

        app_->register_state_handler([this](vsomeip::state_type_e s) { on_state(s); });
        app_->register_availability_handler(kServiceId, kInstanceId, [this](vsomeip::service_t, vsomeip::instance_t, bool avail) {
            on_availability(avail);
        });
        app_->register_subscription_status_handler(kServiceId, kInstanceId, kEventgroup, kEventId,
                                                    [this](auto, auto, auto, auto, std::uint16_t status) {
                                                        on_subscription_status(status);
                                                    });

        app_->register_message_handler(kServiceId, kInstanceId, kEventId,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_event(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kMethodId,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kSetterW,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kSetterRO,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });

        worker_ = std::thread([this] { worker(); });
        app_->start();
        worker_.join();
        return failed_ ? 1 : 0;
    }

  private:
    void fail(const char *msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_) {
            failed_ = true;
            error_  = msg;
            std::fprintf(stderr, "%s: %s\n", name_.c_str(), msg);
        }
        cv_.notify_all();
        if (app_) {
            app_->stop();
        }
    }

    void on_state(vsomeip::state_type_e s) {
        if (s == vsomeip::state_type_e::ST_REGISTERED) {
            std::lock_guard<std::mutex> lock(mutex_);
            registered_ = true;
            cv_.notify_all();
            if (ready_fd_ >= 0) {
                (void)write_byte(ready_fd_, std::byte{0x01});
                ::close(ready_fd_);
                ready_fd_ = -1;
            }
        }
    }

    void on_availability(bool avail) {
        if (avail) {
            std::lock_guard<std::mutex> lock(mutex_);
            available_ = true;
            cv_.notify_all();
        }
        if (debug_) {
            std::fprintf(stderr, "%s: availability %s\n", name_.c_str(), avail ? "true" : "false");
        }
    }

    void on_subscription_status(std::uint16_t status) {
        if (status == 0x00u) {
            std::lock_guard<std::mutex> lock(mutex_);
            subscribed_ = true;
            if (!subscription_notified_ && subscribed_fd_ >= 0) {
                (void)write_byte(subscribed_fd_, std::byte{0x01});
                subscription_notified_ = true;
            }
            cv_.notify_all();
        }
        if (debug_) {
            std::fprintf(stderr, "%s: subscription status 0x%02X\n", name_.c_str(), status);
        }
    }

    void on_event(const std::shared_ptr<vsomeip::message> &msg) {
        if (msg->get_message_type() != vsomeip::message_type_e::MT_NOTIFICATION) {
            fail("event: wrong message type");
            return;
        }
        FieldEvent ev{};
        if (!decode_payload(msg->get_payload(), ev)) {
            fail("event: decode failed");
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (ev.seq != expected_seq_) {
            fail("event: seq mismatch");
            return;
        }
        if (ev.value != kValue) {
            fail("event: value mismatch");
            return;
        }
        expected_seq_++;
        if (expected_seq_ >= kEventCount) {
            events_done_ = true;
            cv_.notify_all();
        }
    }

    void on_response(const std::shared_ptr<vsomeip::message> &msg) {
        const auto method = msg->get_method();
        const auto type   = msg->get_message_type();

        std::lock_guard<std::mutex> lock(mutex_);
        if (method == kSetterW) {
            if (type != vsomeip::message_type_e::MT_RESPONSE ||
                msg->get_return_code() != vsomeip::return_code_e::E_OK) {
                fail("setter writable: bad response");
                return;
            }
            setter_ok_ = true;
        } else if (method == kSetterRO) {
            if (type != vsomeip::message_type_e::MT_ERROR ||
                msg->get_return_code() != vsomeip::return_code_e::E_NOT_OK) {
                fail("setter readonly: bad response");
                return;
            }
            readonly_ok_ = true;
        } else if (method == kMethodId) {
            MethodResp resp{};
            if (!decode_payload(msg->get_payload(), resp)) {
                fail("method: decode failed");
                return;
            }
            if (resp.y != 42u) {
                fail("method: wrong response");
                return;
            }
            method_ok_ = true;
        }
        cv_.notify_all();
    }

    bool wait_for_flag(bool &flag, std::chrono::milliseconds timeout, const char *msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [&] { return flag || failed_; })) {
            fail(msg);
            return false;
        }
        return !failed_;
    }

    void worker() {
        if (!wait_for_flag(registered_, std::chrono::seconds(5), "client: not registered")) return;
        app_->request_service(kServiceId, kInstanceId);
        if (!wait_for_flag(available_, std::chrono::seconds(10), "client: service not available")) return;

        std::set<vsomeip::eventgroup_t> groups{ kEventgroup };
        app_->request_event(kServiceId, kInstanceId, kEventId, groups, vsomeip::event_type_e::ET_FIELD,
                            reliable_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->subscribe(kServiceId, kInstanceId, kEventgroup);
        if (!wait_for_flag(subscribed_, std::chrono::seconds(10), "client: subscribe not accepted")) return;

        if (is_a_) {
            if (go_fd_ >= 0) {
                std::byte sig{};
                if (!read_byte_timeout(go_fd_, sig, 10000)) {
                    fail("client: start signal timeout");
                    return;
                }
            }
            send_setter_writable();
            if (!wait_for_flag(setter_ok_, std::chrono::seconds(10), "client: setter ok timeout")) return;
            if (!wait_for_flag(events_done_, std::chrono::seconds(10), "client: events timeout")) return;
            send_method();
            if (!wait_for_flag(method_ok_, std::chrono::seconds(10), "client: method timeout")) return;

            if (go_fd_ >= 0) {
                std::byte sig{};
                if (!read_byte_timeout(go_fd_, sig, 10000)) {
                    fail("client: shutdown signal timeout");
                    return;
                }
            }
            send_shutdown();
        } else {
            if (!wait_for_flag(events_done_, std::chrono::seconds(10), "client: events timeout")) return;
            send_setter_readonly();
            if (!wait_for_flag(readonly_ok_, std::chrono::seconds(10), "client: readonly timeout")) return;
            if (done_fd_ >= 0) {
                (void)write_byte(done_fd_, std::byte{0x01});
            }
        }

        app_->stop();
    }

    static std::shared_ptr<vsomeip::payload> to_payload(const std::vector<std::byte> &bytes) {
        auto payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> data(bytes.size());
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            data[index] = static_cast<vsomeip::byte_t>(std::to_integer<std::uint8_t>(bytes[index]));
        }
        payload->set_data(data);
        return payload;
    }

    template <class T>
    static std::shared_ptr<vsomeip::payload> encode_payload(const T &value) {
        std::vector<std::byte> bytes{};
        auto st = someip::ser::encode(bytes, kCfg, value, 0);
        if (!st) {
            return {};
        }
        return to_payload(bytes);
    }

    template <class T>
    static bool decode_payload(const std::shared_ptr<vsomeip::payload> &payload, T &out) {
        if (!payload) {
            return false;
        }
        const auto len = payload->get_length();
        const auto data = payload->get_data();
        if (data == nullptr || len == 0) {
            return false;
        }
        std::vector<std::byte> bytes(len);
        for (std::size_t index = 0; index < len; ++index) {
            bytes[index] = std::byte(data[index]);
        }
        auto st = someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), kCfg, out, 0);
        return st.has_value();
    }

    void send_setter_writable() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kServiceId);
        req->set_instance(kInstanceId);
        req->set_method(kSetterW);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        FieldValue fv{.value = kValue};
        req->set_payload(encode_payload(fv));
        app_->send(req);
    }

    void send_setter_readonly() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kServiceId);
        req->set_instance(kInstanceId);
        req->set_method(kSetterRO);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        FieldValue fv{.value = 0x1234};
        req->set_payload(encode_payload(fv));
        app_->send(req);
    }

    void send_method() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kServiceId);
        req->set_instance(kInstanceId);
        req->set_method(kMethodId);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        MethodReq mr{.x = 41};
        req->set_payload(encode_payload(mr));
        app_->send(req);
    }

    void send_shutdown() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kServiceId);
        req->set_instance(kInstanceId);
        req->set_method(kShutdown);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST_NO_RETURN);
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(nullptr, 0);
        req->set_payload(payload);
        app_->send(req);
    }

    std::string name_;
    bool        reliable_{false};
    bool        is_a_{false};
    int         go_fd_{-1};
    int         done_fd_{-1};
    int         ready_fd_{-1};
    int         subscribed_fd_{-1};

    std::shared_ptr<vsomeip::application> app_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool registered_{false};
    bool available_{false};
    bool subscribed_{false};
    bool subscription_notified_{false};
    bool setter_ok_{false};
    bool readonly_ok_{false};
    bool method_ok_{false};
    bool events_done_{false};
    std::size_t expected_seq_{0};
    bool failed_{false};
    std::string error_{};
    bool debug_{false};
};

class vsomeip_server_runner {
  public:
    vsomeip_server_runner(bool reliable, int ready_fd) : reliable_(reliable), ready_fd_(ready_fd) {}

    int run() {
        ::setenv("VSOMEIP_APPLICATION_NAME", "server", 1);
        const bool debug = std::getenv("SOMEIP_TEST_DEBUG") != nullptr;
        if (debug) {
            const char *cfg = std::getenv("VSOMEIP_CONFIGURATION");
            std::fprintf(stderr, "vsomeip server: VSOMEIP_CONFIGURATION=%s\n", cfg ? cfg : "(null)");
        }
        app_ = vsomeip::runtime::get()->create_application("server");
        if (!app_->init()) {
            std::fprintf(stderr, "vsomeip server: init failed\n");
            return 1;
        }
        app_->register_state_handler([this](vsomeip::state_type_e s) { on_state(s); });
        app_->register_subscription_handler(kServiceId, kInstanceId, kEventgroup,
                                             [this](vsomeip::client_t, vsomeip::uid_t, vsomeip::gid_t, bool subscribed) {
                                                 if (subscribed) {
                                                     std::lock_guard<std::mutex> lock(mutex_);
                                                     subscribed_count_++;
                                                     cv_.notify_all();
                                                 }
                                                 return true;
                                             });
        app_->register_message_handler(kServiceId, kInstanceId, kMethodId,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_method(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kSetterW,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_setter_w(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kSetterRO,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_setter_ro(m); });
        app_->register_message_handler(kServiceId, kInstanceId, kShutdown,
                                       [this](const std::shared_ptr<vsomeip::message> &m) { on_shutdown(m); });

        app_->start();
        return 0;
    }

  private:
    void on_state(vsomeip::state_type_e s) {
        if (s != vsomeip::state_type_e::ST_REGISTERED) return;
        std::set<vsomeip::eventgroup_t> groups{ kEventgroup };
        app_->offer_event(kServiceId, kInstanceId, kEventId, groups, vsomeip::event_type_e::ET_FIELD,
                          std::chrono::milliseconds::zero(), false, true, nullptr,
                          reliable_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->offer_service(kServiceId, kInstanceId);
        if (ready_fd_ >= 0) {
            (void)write_byte(ready_fd_, std::byte{0x01});
            ::close(ready_fd_);
            ready_fd_ = -1;
        }
    }

    void on_method(const std::shared_ptr<vsomeip::message> &msg) {
        MethodReq req{};
        if (!decode_payload(msg->get_payload(), req)) {
            std::fprintf(stderr, "vsomeip server: method decode failed\n");
            return;
        }
        MethodResp resp{.y = req.x + 1};
        auto response = vsomeip::runtime::get()->create_response(msg);
        response->set_payload(encode_payload(resp));
        app_->send(response);
    }

    void on_setter_w(const std::shared_ptr<vsomeip::message> &msg) {
        FieldValue req{};
        if (!decode_payload(msg->get_payload(), req)) {
            std::fprintf(stderr, "vsomeip server: setter decode failed\n");
            return;
        }
        field_value_ = req.value;

        auto response = vsomeip::runtime::get()->create_response(msg);
        response->set_return_code(vsomeip::return_code_e::E_OK);
        app_->send(response);

        for (std::uint32_t seq_index = 0; seq_index < kEventCount; ++seq_index) {
            FieldEvent ev{.seq = seq_index, .value = field_value_};
            app_->notify(kServiceId, kInstanceId, kEventId, encode_payload(ev), true);
        }
    }

    void on_setter_ro(const std::shared_ptr<vsomeip::message> &msg) {
        auto response = vsomeip::runtime::get()->create_response(msg);
        response->set_message_type(vsomeip::message_type_e::MT_ERROR);
        response->set_return_code(vsomeip::return_code_e::E_NOT_OK);
        app_->send(response);
    }

    void on_shutdown(const std::shared_ptr<vsomeip::message> &) {
        app_->stop_offer_service(kServiceId, kInstanceId);
        app_->clear_all_handler();
        app_->stop();
    }

    template <class T>
    static std::shared_ptr<vsomeip::payload> encode_payload(const T &value) {
        std::vector<std::byte> bytes{};
        auto st = someip::ser::encode(bytes, kCfg, value, 0);
        if (!st) {
            return {};
        }
        auto payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> data(bytes.size());
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            data[index] = static_cast<vsomeip::byte_t>(std::to_integer<std::uint8_t>(bytes[index]));
        }
        payload->set_data(data);
        return payload;
    }

    template <class T>
    static bool decode_payload(const std::shared_ptr<vsomeip::payload> &payload, T &out) {
        if (!payload) {
            return false;
        }
        const auto len = payload->get_length();
        const auto data = payload->get_data();
        if (data == nullptr || len == 0) {
            return false;
        }
        std::vector<std::byte> bytes(len);
        for (std::size_t index = 0; index < len; ++index) {
            bytes[index] = std::byte(data[index]);
        }
        auto st = someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), kCfg, out, 0);
        return st.has_value();
    }

    bool reliable_{false};
    int  ready_fd_{-1};
    std::shared_ptr<vsomeip::application> app_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t subscribed_count_{0};
    std::uint32_t field_value_{0};
};

class vsomeip_routing_runner {
  public:
    vsomeip_routing_runner(int ready_fd, int stop_fd) : ready_fd_(ready_fd), stop_fd_(stop_fd) {}

    int run() {
        ::setenv("VSOMEIP_APPLICATION_NAME", "routingmanagerd", 1);
        const bool debug = std::getenv("SOMEIP_TEST_DEBUG") != nullptr;
        if (debug) {
            const char *cfg = std::getenv("VSOMEIP_CONFIGURATION");
            std::fprintf(stderr, "routingmanagerd: VSOMEIP_CONFIGURATION=%s\n", cfg ? cfg : "(null)");
        }
        app_ = vsomeip::runtime::get()->create_application("routingmanagerd");
        if (!app_->init()) {
            std::fprintf(stderr, "routingmanagerd: init failed\n");
            return 1;
        }
        app_->register_state_handler([this](vsomeip::state_type_e s) {
            if (s == vsomeip::state_type_e::ST_REGISTERED && ready_fd_ >= 0) {
                (void)write_byte(ready_fd_, std::byte{0x01});
                ::close(ready_fd_);
                ready_fd_ = -1;
            }
        });

        worker_ = std::thread([this] { wait_for_stop(); });
        app_->start();
        worker_.join();
        return 0;
    }

  private:
    void wait_for_stop() {
        if (stop_fd_ >= 0) {
            std::byte sig{};
            (void)read_byte_timeout(stop_fd_, sig, 60000);
            ::close(stop_fd_);
            stop_fd_ = -1;
        }
        if (app_) {
            app_->stop();
        }
    }

    int ready_fd_{-1};
    int stop_fd_{-1};
    std::shared_ptr<vsomeip::application> app_;
    std::thread worker_;
};

class custom_client_runner {
  public:
    custom_client_runner(bool reliable, bool is_a, int go_fd, int done_fd, const char *server_ip, const char *client_ip)
        : reliable_(reliable),
          is_a_(is_a),
          go_fd_(go_fd),
          done_fd_(done_fd),
          server_ip_(server_ip),
          client_ip_(client_ip),
          debug_(std::getenv("SOMEIP_TEST_DEBUG") != nullptr) {}

    int run() {
        sd_ = open_sd_sockets(client_ip_, client_ip_);
        if (!sd_.unicast.valid() || !sd_.multicast.valid()) {
            std::fprintf(stderr, "custom client: sd socket failed\n");
            return 1;
        }

        if (!send_sd_find()) {
            std::fprintf(stderr, "custom client: sd find failed\n");
            return 1;
        }

        if (!wait_for_offer()) {
            std::fprintf(stderr, "custom client: offer timeout\n");
            return 1;
        }

        if (reliable_) {
            if (have_service_addr_) {
                tcp_sock_ = connect_tcp_with_retry(service_addr_, 5000);
            } else {
                tcp_sock_ = connect_tcp_with_retry(server_ip_, kServicePort, 5000);
            }
            if (!tcp_sock_.valid()) {
                std::fprintf(stderr, "custom client: tcp connect failed\n");
                return 1;
            }
            local_port_ = local_port_for_fd(tcp_sock_.fd());
            if (local_port_ == 0) {
                std::fprintf(stderr, "custom client: tcp local port failed\n");
                return 1;
            }
        } else {
            udp_sock_ = open_udp_socket(0, false, client_ip_, client_ip_, true);
            if (!udp_sock_.valid()) {
                std::fprintf(stderr, "custom client: udp socket failed\n");
                return 1;
            }
            local_port_ = local_port_for_fd(udp_sock_.fd());
            if (local_port_ == 0) {
                std::fprintf(stderr, "custom client: udp local port failed\n");
                return 1;
            }
            if (!have_service_addr_) {
                service_addr_ = make_addr(server_ip_, kServicePort);
                have_service_addr_ = true;
            }
        }

        if (!send_sd_subscribe()) {
            std::fprintf(stderr, "custom client: subscribe send failed\n");
            return 1;
        }

        if (!wait_for_subscribe_ack()) {
            std::fprintf(stderr, "custom client: subscribe ack timeout\n");
            return 1;
        }

        if (is_a_) {
            if (!send_setter_writable()) return 1;
            if (!wait_for_setter_ok()) return 1;
            if (!wait_for_events()) return 1;
            if (!send_method()) return 1;
            if (!wait_for_method_ok()) return 1;
            if (go_fd_ >= 0) {
                std::byte sig{};
                if (!read_byte_timeout(go_fd_, sig, 10000)) {
                    std::fprintf(stderr, "custom client: go timeout\n");
                    return 1;
                }
            }
            (void)send_shutdown();
        } else {
            if (!wait_for_events()) return 1;
            if (!send_setter_readonly()) return 1;
            if (!wait_for_readonly_err()) return 1;
            if (done_fd_ >= 0) {
                (void)write_byte(done_fd_, std::byte{0x01});
            }
        }
        return 0;
    }

  private:
    bool send_sd_find() {
        auto frame = build_sd_find_service();
        if (frame.empty()) return false;
        auto to = make_addr(kMulticastIp, kSdPort);
        if (!send_udp_datagram(sd_.multicast.fd(), frame, to)) {
            if (debug_) {
                std::fprintf(stderr, "custom client: sd find multicast send failed: %s\n", std::strerror(errno));
            }
            return false;
        }
        auto unicast = make_addr(server_ip_, kSdPort);
        if (!send_udp_datagram(sd_.unicast.fd(), frame, unicast)) {
            if (debug_) {
                std::fprintf(stderr, "custom client: sd find unicast send failed: %s\n", std::strerror(errno));
            }
            return false;
        }
        return true;
    }

    bool send_sd_subscribe() {
        auto frame = build_sd_subscribe(client_ip_, reliable_, local_port_);
        if (frame.empty()) return false;
        sockaddr_in to{};
        if (have_sd_addr_) {
            to = sd_server_addr_;
        } else {
            to = make_addr(server_ip_, kSdPort);
        }
        if (!send_udp_datagram(sd_.unicast.fd(), frame, to)) {
            if (debug_) {
                std::fprintf(stderr, "custom client: sd subscribe send failed: %s\n", std::strerror(errno));
            }
            return false;
        }
        return true;
    }

    bool wait_for_offer() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            std::vector<std::byte> frame{};
            sockaddr_in sender{};
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (!recv_sd_datagram(frame, sender, static_cast<int>(remaining))) {
                continue;
            }
            if (debug_) {
                char addr[INET_ADDRSTRLEN]{};
                ::inet_ntop(AF_INET, &sender.sin_addr, addr, sizeof(addr));
                std::fprintf(stderr, "custom client: SD datagram from %s:%u len=%zu\n", addr, ntohs(sender.sin_port), frame.size());
            }
            auto offer = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
            if (!offer) {
                if (debug_) {
                    std::fprintf(stderr, "custom client: SD decode failed (code=%d)\n", static_cast<int>(offer.error()));
                }
                continue;
            }
            for (const auto &entry : offer->sd_payload.entries) {
                if (!std::holds_alternative<someip::sd::service_entry>(entry)) {
                    continue;
                }
                const auto &se = std::get<someip::sd::service_entry>(entry);
                if (debug_) {
                    std::fprintf(stderr, "custom client: SD service entry type=0x%02X svc=0x%04X inst=0x%04X\n",
                                 se.c.type, se.c.service_id, se.c.instance_id);
                }
                if (se.c.type != someip::sd::entry_type::offer_service) {
                    continue;
                }
                if (se.c.service_id != kServiceId || se.c.instance_id != kInstanceId) {
                    continue;
                }
                sd_server_addr_ = sender;
                have_sd_addr_ = true;

                if (auto runs = someip::sd::resolve_option_runs(offer->sd_payload, se.c)) {
                    auto pick_endpoint = [&](const someip::sd::option &opt) {
                        if (auto ipv4 = std::get_if<someip::sd::ipv4_endpoint_option>(&opt)) {
                            if (reliable_ && ipv4->l4_proto != 6) {
                                return;
                            }
                            if (!reliable_ && ipv4->l4_proto != 17) {
                                return;
                            }
                            sockaddr_in addr{};
                            if (sockaddr_from_ipv4_option(*ipv4, addr)) {
                                service_addr_ = addr;
                                have_service_addr_ = true;
                            }
                        }
                    };
                    for (const auto &opt : runs->run1) {
                        pick_endpoint(opt);
                        if (have_service_addr_) break;
                    }
                    if (!have_service_addr_) {
                        for (const auto &opt : runs->run2) {
                            pick_endpoint(opt);
                            if (have_service_addr_) break;
                        }
                    }
                }
                return true;
            }
        }
        return false;
    }

    bool wait_for_subscribe_ack() {
        std::vector<std::byte> frame{};
        sockaddr_in sender{};
        if (!recv_sd_datagram(frame, sender, 2000)) {
            return false;
        }
        auto ack = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
        if (!ack) {
            return false;
        }
        return true;
    }

    bool send_setter_writable() {
        someip::wire::header header{};
        header.msg.service_id    = kServiceId;
        header.msg.method_id     = kSetterW;
        header.req.client_id     = 1;
        header.req.session_id    = next_session_++;
        header.protocol_version  = 1;
        header.interface_version = 1;
        header.msg_type          = someip::wire::message_type::request;
        header.return_code       = 0;
        FieldValue payload{.value = kValue};
        auto frame = build_someip_message(kCfg, header, payload);
        if (frame.empty()) return false;
        return send_service_frame(frame);
    }

    bool send_setter_readonly() {
        someip::wire::header header{};
        header.msg.service_id    = kServiceId;
        header.msg.method_id     = kSetterRO;
        header.req.client_id     = 2;
        header.req.session_id    = next_session_++;
        header.protocol_version  = 1;
        header.interface_version = 1;
        header.msg_type          = someip::wire::message_type::request;
        header.return_code       = 0;
        FieldValue payload{.value = 0x1234};
        auto frame = build_someip_message(kCfg, header, payload);
        if (frame.empty()) return false;
        return send_service_frame(frame);
    }

    bool send_method() {
        someip::wire::header header{};
        header.msg.service_id    = kServiceId;
        header.msg.method_id     = kMethodId;
        header.req.client_id     = 1;
        header.req.session_id    = next_session_++;
        header.protocol_version  = 1;
        header.interface_version = 1;
        header.msg_type          = someip::wire::message_type::request;
        header.return_code       = 0;
        MethodReq payload{.x = 41};
        auto frame = build_someip_message(kCfg, header, payload);
        if (frame.empty()) return false;
        return send_service_frame(frame);
    }

    bool send_shutdown() {
        someip::wire::header header{};
        header.msg.service_id    = kServiceId;
        header.msg.method_id     = kShutdown;
        header.req.client_id     = 1;
        header.req.session_id    = next_session_++;
        header.protocol_version  = 1;
        header.interface_version = 1;
        header.msg_type          = someip::wire::message_type::request_no_return;
        header.return_code       = 0;
        auto frame = build_someip_message(kCfg, header, EmptyPayload{});
        if (frame.empty()) return false;
        return send_service_frame(frame);
    }

    bool send_service_frame(const std::vector<std::byte> &frame) {
        if (reliable_) {
            return send_frame(tcp_sock_.fd(), frame, 2000);
        }
        sockaddr_in to{};
        if (have_service_addr_) {
            to = service_addr_;
        } else {
            to = make_addr(server_ip_, kServicePort);
        }
        return send_udp_datagram(udp_sock_.fd(), frame, to);
    }

    bool wait_for_setter_ok() {
        someip::wire::parsed_frame parsed{};
        if (!recv_service_message(parsed)) return false;
        return parsed.hdr.msg_type == someip::wire::message_type::response &&
               parsed.hdr.return_code == someip::wire::return_code::E_OK &&
               parsed.hdr.msg.method_id == kSetterW;
    }

    bool wait_for_readonly_err() {
        someip::wire::parsed_frame parsed{};
        if (!recv_service_message(parsed)) return false;
        return parsed.hdr.msg_type == someip::wire::message_type::error &&
               parsed.hdr.return_code == someip::wire::return_code::E_NOT_OK &&
               parsed.hdr.msg.method_id == kSetterRO;
    }

    bool wait_for_method_ok() {
        someip::wire::parsed_frame parsed{};
        if (!recv_service_message(parsed)) return false;
        if (parsed.hdr.msg_type != someip::wire::message_type::response ||
            parsed.hdr.msg.method_id != kMethodId) {
            return false;
        }
        MethodResp resp{};
        const auto payload_base = parsed.tp ? 20u : 16u;
        if (!someip::ser::decode(parsed.payload, kCfg, resp, payload_base)) {
            return false;
        }
        return resp.y == 42u;
    }

    bool wait_for_events() {
        std::size_t expected_seq = 0;
        while (expected_seq < kEventCount) {
            someip::wire::parsed_frame parsed{};
            if (!recv_service_message(parsed)) return false;
            if (parsed.hdr.msg_type != someip::wire::message_type::notification ||
                parsed.hdr.msg.method_id != kEventId) {
                return false;
            }
            FieldEvent ev{};
            const auto payload_base = parsed.tp ? 20u : 16u;
            if (!someip::ser::decode(parsed.payload, kCfg, ev, payload_base)) {
                return false;
            }
            if (ev.seq != expected_seq || ev.value != kValue) {
                return false;
            }
            expected_seq++;
        }
        return true;
    }

    bool recv_service_message(someip::wire::parsed_frame &out) {
        if (reliable_) {
            auto frame = recv_someip_frame(tcp_sock_.fd(), 5000);
            if (frame.empty()) return false;
            auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
            if (!parsed) return false;
            out = *parsed;
            return true;
        }
        std::vector<std::byte> frame{};
        sockaddr_in sender{};
        if (!recv_udp_datagram(udp_sock_.fd(), frame, sender, 5000)) return false;
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
        if (!parsed) return false;
        out = *parsed;
        return true;
    }

    bool reliable_{false};
    bool is_a_{false};
    int  go_fd_{-1};
    int  done_fd_{-1};
    const char *server_ip_{nullptr};
    const char *client_ip_{nullptr};
    std::uint16_t local_port_{0};
    sockaddr_in sd_server_addr_{};
    sockaddr_in service_addr_{};
    bool have_sd_addr_{false};
    bool have_service_addr_{false};
    bool debug_{false};
    sd_sockets sd_{};
    unique_fd tcp_sock_{};
    unique_fd udp_sock_{};
    std::uint16_t next_session_{1};

    bool recv_sd_datagram(std::vector<std::byte> &frame, sockaddr_in &sender, int timeout_ms) {
        std::vector<pollfd> fds{};
        if (sd_.unicast.valid()) {
            fds.push_back({sd_.unicast.fd(), POLLIN, 0});
        }
        if (sd_.multicast.valid()) {
            fds.push_back({sd_.multicast.fd(), POLLIN, 0});
        }
        if (fds.empty()) {
            return false;
        }
        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
        if (ready <= 0) {
            return false;
        }
        for (const auto &pfd : fds) {
            if ((pfd.revents & POLLIN) == 0) {
                continue;
            }
            if (pfd.fd == sd_.unicast.fd()) {
                return recv_udp_datagram(sd_.unicast.fd(), frame, sender, 0);
            }
            if (pfd.fd == sd_.multicast.fd()) {
                return recv_udp_datagram(sd_.multicast.fd(), frame, sender, 0);
            }
        }
        return false;
    }
};

static int run_custom_server_with_vsomeip_clients(const std::string &config_path, bool reliable) {
    ::setenv("VSOMEIP_CONFIGURATION", config_path.c_str(), 1);

    int server_pipe[2]{-1, -1};
    int done_pipe[2]{-1, -1};
    int go_pipe[2]{-1, -1};
    int routing_ready_pipe[2]{-1, -1};
    int routing_stop_pipe[2]{-1, -1};
    int sub_a_pipe[2]{-1, -1};
    int sub_b_pipe[2]{-1, -1};
    if (::pipe(server_pipe) != 0 || ::pipe(done_pipe) != 0 || ::pipe(go_pipe) != 0 ||
        ::pipe(routing_ready_pipe) != 0 || ::pipe(routing_stop_pipe) != 0 ||
        ::pipe(sub_a_pipe) != 0 || ::pipe(sub_b_pipe) != 0) {
        std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
        return 1;
    }

    const pid_t server_pid = ::fork();
    if (server_pid == 0) {
        ::close(server_pipe[0]);
        const int rc = run_custom_server(reliable, server_pipe[1], kCustomIp);
        if (server_pipe[1] >= 0) {
            ::close(server_pipe[1]);
        }
        std::_Exit(rc);
    }
    ::close(server_pipe[1]);

    std::byte ready{};
    if (!read_byte_timeout(server_pipe[0], ready, 5000)) {
        std::fprintf(stderr, "custom server ready timeout\n");
        return 1;
    }
    ::close(server_pipe[0]);

    const pid_t routing_pid = ::fork();
    if (routing_pid == 0) {
        ::close(routing_ready_pipe[0]);
        ::close(routing_stop_pipe[1]);
        ::close(go_pipe[0]);
        ::close(go_pipe[1]);
        ::close(done_pipe[0]);
        ::close(done_pipe[1]);
        ::close(sub_a_pipe[0]);
        ::close(sub_a_pipe[1]);
        ::close(sub_b_pipe[0]);
        ::close(sub_b_pipe[1]);
        vsomeip_routing_runner routing{routing_ready_pipe[1], routing_stop_pipe[0]};
        const int rc = routing.run();
        ::close(routing_ready_pipe[1]);
        ::close(routing_stop_pipe[0]);
        std::_Exit(rc);
    }
    ::close(routing_ready_pipe[1]);
    ::close(routing_stop_pipe[0]);

    std::byte routing_ready{};
    if (!read_byte_timeout(routing_ready_pipe[0], routing_ready, 5000)) {
        std::fprintf(stderr, "routing manager ready timeout\n");
        (void)::kill(routing_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    ::close(routing_ready_pipe[0]);
    if (!wait_for_socket_path("/tmp/vsomeip-0", 2000)) {
        std::fprintf(stderr, "routing manager socket timeout\n");
        (void)::kill(routing_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    debug_dump_sockets("custom-server: after routingmanagerd");

    const pid_t client_a_pid = ::fork();
    if (client_a_pid == 0) {
        ::close(go_pipe[1]);
        ::close(done_pipe[0]);
        ::close(routing_stop_pipe[1]);
        ::close(sub_a_pipe[0]);
        ::close(sub_b_pipe[0]);
        ::close(sub_b_pipe[1]);
        vsomeip_client_runner client_a{"client_a", reliable, true, go_pipe[0], -1, -1, sub_a_pipe[1]};
        const int rc = client_a.run();
        ::close(go_pipe[0]);
        ::close(routing_stop_pipe[1]);
        ::close(sub_a_pipe[1]);
        std::_Exit(rc);
    }
    ::close(go_pipe[0]);
    ::close(sub_a_pipe[1]);

    const pid_t client_b_pid = ::fork();
    if (client_b_pid == 0) {
        ::close(done_pipe[0]);
        ::close(go_pipe[1]);
        ::close(routing_stop_pipe[1]);
        ::close(sub_b_pipe[0]);
        ::close(sub_a_pipe[0]);
        vsomeip_client_runner client_b{"client_b", reliable, false, -1, done_pipe[1], -1, sub_b_pipe[1]};
        const int rc = client_b.run();
        ::close(done_pipe[1]);
        ::close(routing_stop_pipe[1]);
        ::close(sub_b_pipe[1]);
        std::_Exit(rc);
    }
    ::close(done_pipe[1]);
    ::close(sub_b_pipe[1]);

    std::byte sub_ready{};
    if (!read_byte_timeout(sub_a_pipe[0], sub_ready, 10000)) {
        std::fprintf(stderr, "client_a subscribe timeout\n");
        (void)::kill(client_a_pid, SIGKILL);
        (void)::kill(client_b_pid, SIGKILL);
        (void)::kill(routing_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    ::close(sub_a_pipe[0]);
    if (!read_byte_timeout(sub_b_pipe[0], sub_ready, 10000)) {
        std::fprintf(stderr, "client_b subscribe timeout\n");
        (void)::kill(client_a_pid, SIGKILL);
        (void)::kill(client_b_pid, SIGKILL);
        (void)::kill(routing_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    ::close(sub_b_pipe[0]);

    (void)write_byte(go_pipe[1], std::byte{0x01});

    std::byte done{};
    if (!read_byte_timeout(done_pipe[0], done, 20000)) {
        std::fprintf(stderr, "client_b did not finish in time\n");
        (void)::kill(client_a_pid, SIGKILL);
        (void)::kill(routing_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    ::close(done_pipe[0]);

    (void)write_byte(go_pipe[1], std::byte{0x02});
    ::close(go_pipe[1]);

    if (routing_stop_pipe[1] >= 0) {
        (void)write_byte(routing_stop_pipe[1], std::byte{0x01});
        ::close(routing_stop_pipe[1]);
    }

    int status = 0;
    (void)::waitpid(client_b_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "client_b failed\n");
        return 1;
    }
    (void)::waitpid(client_a_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "client_a failed\n");
        return 1;
    }
    (void)::waitpid(server_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "custom server failed\n");
        return 1;
    }

    (void)::waitpid(routing_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "routing manager failed\n");
        return 1;
    }

    return 0;
}

static int run_vsomeip_server_with_custom_clients(const std::string &config_path, bool reliable) {
    ::setenv("VSOMEIP_CONFIGURATION", config_path.c_str(), 1);

    int server_pipe[2]{-1, -1};
    int done_pipe[2]{-1, -1};
    int go_pipe[2]{-1, -1};
    int routing_ready_pipe[2]{-1, -1};
    int routing_stop_pipe[2]{-1, -1};
    if (::pipe(server_pipe) != 0 || ::pipe(done_pipe) != 0 || ::pipe(go_pipe) != 0 ||
        ::pipe(routing_ready_pipe) != 0 || ::pipe(routing_stop_pipe) != 0) {
        std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
        return 1;
    }

    const pid_t routing_pid = ::fork();
    if (routing_pid == 0) {
        ::close(routing_ready_pipe[0]);
        ::close(routing_stop_pipe[1]);
        ::close(server_pipe[0]);
        ::close(server_pipe[1]);
        ::close(done_pipe[0]);
        ::close(done_pipe[1]);
        ::close(go_pipe[0]);
        ::close(go_pipe[1]);
        vsomeip_routing_runner routing{routing_ready_pipe[1], routing_stop_pipe[0]};
        const int rc = routing.run();
        ::close(routing_ready_pipe[1]);
        ::close(routing_stop_pipe[0]);
        std::_Exit(rc);
    }
    ::close(routing_ready_pipe[1]);
    ::close(routing_stop_pipe[0]);

    std::byte routing_ready{};
    if (!read_byte_timeout(routing_ready_pipe[0], routing_ready, 5000)) {
        std::fprintf(stderr, "routing manager ready timeout\n");
        (void)::kill(routing_pid, SIGKILL);
        return 1;
    }
    ::close(routing_ready_pipe[0]);
    if (!wait_for_socket_path("/tmp/vsomeip-0", 2000)) {
        std::fprintf(stderr, "routing manager socket timeout\n");
        (void)::kill(routing_pid, SIGKILL);
        return 1;
    }
    debug_dump_sockets("vsomeip-server: after routingmanagerd");

    const pid_t server_pid = ::fork();
    if (server_pid == 0) {
        ::close(server_pipe[0]);
        ::close(routing_stop_pipe[1]);
        vsomeip_server_runner server{reliable, server_pipe[1]};
        const int rc = server.run();
        if (server_pipe[1] >= 0) {
            ::close(server_pipe[1]);
        }
        if (routing_stop_pipe[1] >= 0) {
            ::close(routing_stop_pipe[1]);
        }
        std::_Exit(rc);
    }
    ::close(server_pipe[1]);

    std::byte ready{};
    if (!read_byte_timeout(server_pipe[0], ready, 5000)) {
        std::fprintf(stderr, "vsomeip server ready timeout\n");
        (void)::kill(routing_pid, SIGKILL);
        return 1;
    }
    ::close(server_pipe[0]);

    const pid_t client_b_pid = ::fork();
    if (client_b_pid == 0) {
        ::close(done_pipe[0]);
        ::close(go_pipe[0]);
        ::close(go_pipe[1]);
        ::close(routing_stop_pipe[1]);
        custom_client_runner client_b{reliable, false, -1, done_pipe[1], kVsomeipIp, kCustomIp};
        const int rc = client_b.run();
        ::close(done_pipe[1]);
        ::close(routing_stop_pipe[1]);
        std::_Exit(rc);
    }
    ::close(done_pipe[1]);

    const pid_t client_a_pid = ::fork();
    if (client_a_pid == 0) {
        ::close(go_pipe[1]);
        ::close(done_pipe[0]);
        ::close(routing_stop_pipe[1]);
        custom_client_runner client_a{reliable, true, go_pipe[0], -1, kVsomeipIp, kCustomIp};
        const int rc = client_a.run();
        ::close(go_pipe[0]);
        ::close(routing_stop_pipe[1]);
        std::_Exit(rc);
    }
    ::close(go_pipe[0]);

    std::byte done{};
    if (!read_byte_timeout(done_pipe[0], done, 20000)) {
        std::fprintf(stderr, "custom client_b did not finish in time\n");
        (void)::kill(client_a_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        (void)::kill(routing_pid, SIGKILL);
        return 1;
    }
    ::close(done_pipe[0]);

    (void)write_byte(go_pipe[1], std::byte{0x01});
    ::close(go_pipe[1]);

    if (routing_stop_pipe[1] >= 0) {
        (void)write_byte(routing_stop_pipe[1], std::byte{0x01});
        ::close(routing_stop_pipe[1]);
    }

    int status = 0;
    (void)::waitpid(client_b_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "custom client_b failed\n");
        return 1;
    }
    (void)::waitpid(client_a_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "custom client_a failed\n");
        return 1;
    }
    (void)::waitpid(server_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "vsomeip server failed\n");
        return 1;
    }
    (void)::waitpid(routing_pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        std::fprintf(stderr, "routing manager failed\n");
        return 1;
    }

    return 0;
}

#endif

} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    std::printf("vsomeip interop tests skipped on Windows\n");
    return 0;
#else
    cleanup_vsomeip_sockets();
    std::string mode = "custom-server";
    std::string transport = "tcp";
    if (argc >= 2) {
        mode = argv[1];
    }
    if (argc >= 3) {
        transport = argv[2];
    }

    const std::string config_dir = VSOMEIP_TEST_CONFIG_DIR;
    const auto tcp_server_cfg = config_dir + "/vsomeip_tcp_server.json";
    const auto udp_server_cfg = config_dir + "/vsomeip_udp_server.json";
    const auto tcp_client_cfg = config_dir + "/vsomeip_tcp_client.json";
    const auto udp_client_cfg = config_dir + "/vsomeip_udp_client.json";

    const bool reliable = (transport == "tcp");
    if (mode == "custom-server") {
        const auto cfg = reliable ? tcp_client_cfg : udp_client_cfg;
        return run_custom_server_with_vsomeip_clients(cfg, reliable);
    }
    if (mode == "vsomeip-server") {
        const auto cfg = reliable ? tcp_server_cfg : udp_server_cfg;
        return run_vsomeip_server_with_custom_clients(cfg, reliable);
    }

    std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
    return 2;
#endif
}
