#include <doctest/doctest.h>

#if defined(_WIN32)

TEST_CASE("someip: uds e2e (unix-only)") {
    DOCTEST_SKIP("Unix domain sockets + fork() required");
}

#else

#include "someip/sd/sd.h"
#include "someip/iface/field.h"
#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/wire/message.h"
#include "someip/wire/return_code.h"
#include "someip/wire/someip.h"

#include <array>
#include <cerrno>
#include <poll.h>
#include <csignal>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

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

    [[nodiscard]] int release() noexcept {
        const int fd = value;
        value        = -1;
        return fd;
    }

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
    if (!total) {
        return {};
    }
    if (*total < 16u) {
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

static unique_fd connect_with_retry(const std::string &path, int timeout_ms) {
    unique_fd sock{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        return {};
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const int rc = ::connect(sock.fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
        if (rc == 0) {
            return sock;
        }
        if (errno != ENOENT && errno != ECONNREFUSED) {
            return {};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
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

static someip::sd::ipv4_endpoint_option dummy_ipv4_endpoint() {
    someip::sd::ipv4_endpoint_option opt{};
    opt.discardable = false;
    opt.address     = {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}};
    opt.l4_proto    = 0x06;   // TCP
    opt.port        = 30509;  // dummy
    opt.reserved    = 0x00;
    return opt;
}

static std::vector<std::byte> build_sd_offer_service() {
    someip::sd::service_entry_data e{};
    e.type          = someip::sd::entry_type::offer_service;
    e.service_id    = 0x1234;
    e.instance_id   = 0x0001;
    e.major_version = 1;
    e.ttl           = 5;
    e.minor_version = 0;
    e.run1          = {someip::sd::option{dummy_ipv4_endpoint()}};

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

static std::vector<std::byte> build_sd_subscribe_eventgroup() {
    someip::sd::eventgroup_entry_data e{};
    e.type                = someip::sd::entry_type::subscribe_eventgroup;
    e.service_id          = 0x1234;
    e.instance_id         = 0x0001;
    e.major_version       = 1;
    e.ttl                 = 5;
    e.reserved12_counter4 = 0;
    e.eventgroup_id       = writable_field.eventgroup_id;
    e.run1                = {someip::sd::option{dummy_ipv4_endpoint()}};

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

static int run_server(const std::string &path, int ready_fd) {
    unique_fd ready{ready_fd};
    constexpr std::byte kReady{0x01};
    constexpr std::byte kBindPerm{0xE1};
    constexpr std::byte kInitFail{0xE2};

    auto notify_ready = [&](std::byte code) {
        if (ready.valid()) {
            (void)write_all(ready.fd(), &code, 1, 2000);
            ready.reset();
        }
    };

    unique_fd listen_fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!listen_fd.valid()) {
        std::fprintf(stderr, "server: socket() failed: %s\n", std::strerror(errno));
        notify_ready(kInitFail);
        return 1;
    }

    ::unlink(path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    if (::bind(listen_fd.fd(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "server: bind() failed: %s\n", std::strerror(errno));
        notify_ready(errno == EPERM ? kBindPerm : kInitFail);
        return 1;
    }
    if (::listen(listen_fd.fd(), 1) != 0) {
        std::fprintf(stderr, "server: listen() failed: %s\n", std::strerror(errno));
        notify_ready(kInitFail);
        return 1;
    }

    // Signal readiness to parent.
    notify_ready(kReady);

    struct client_state {
        unique_fd fd{};
        bool      subscribed{false};
    };

    std::array<client_state, 2> clients{};
    for (auto &client : clients) {
        client.fd.reset(::accept(listen_fd.fd(), nullptr, nullptr));
        if (!client.fd.valid()) {
            std::fprintf(stderr, "server: accept() failed: %s\n", std::strerror(errno));
            return 1;
        }
    }

    const someip::ser::config cfg{someip::wire::endian::big};

    std::size_t offered_count = 0;
    FieldValue field_value{.value = 0};
    for (;;) {
        std::array<pollfd, 2> pfds{};
        for (std::size_t i = 0; i < clients.size(); ++i) {
            pfds[i].fd     = clients[i].fd.fd();
            pfds[i].events = POLLIN;
        }

        int rc = 0;
        for (;;) {
            rc = ::poll(pfds.data(), pfds.size(), 5000);
            if (rc < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc <= 0) {
            std::fprintf(stderr, "server: poll failed/timeout\n");
            return 1;
        }

        bool should_exit = false;
        for (std::size_t idx = 0; idx < clients.size(); ++idx) {
            if ((pfds[idx].revents & (POLLERR | POLLHUP)) != 0) {
                std::fprintf(stderr, "server: client socket error/hangup\n");
                return 1;
            }
            if ((pfds[idx].revents & POLLIN) == 0) {
                continue;
            }
            auto frame = recv_someip_frame(clients[idx].fd.fd(), 5000);
            if (frame.empty()) {
                std::fprintf(stderr, "server: recv frame failed\n");
                return 1;
            }

            auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
            if (!parsed) {
                std::fprintf(stderr, "server: try_parse_frame failed\n");
                return 1;
            }

            const auto payload_base = parsed->tp ? 20u : 16u;

            if (parsed->hdr.msg.service_id == someip::sd::kServiceId && parsed->hdr.msg.method_id == someip::sd::kMethodId) {
                auto sdmsg = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
                if (!sdmsg) {
                    std::fprintf(stderr, "server: decode SD failed\n");
                    return 1;
                }

                for (const auto &ent : sdmsg->sd_payload.entries) {
                    if (std::holds_alternative<someip::sd::service_entry>(ent)) {
                        const auto &se = std::get<someip::sd::service_entry>(ent);
                        if (se.c.type == someip::sd::entry_type::find_service) {
                            auto offer = build_sd_offer_service();
                            if (offer.empty()) {
                                std::fprintf(stderr, "server: build offer failed\n");
                                return 1;
                            }
                            if (!send_frame(clients[idx].fd.fd(), offer, 2000)) {
                                std::fprintf(stderr, "server: send offer failed\n");
                                return 1;
                            }
                            offered_count++;
                        }
                    } else if (std::holds_alternative<someip::sd::eventgroup_entry>(ent)) {
                        const auto &eg = std::get<someip::sd::eventgroup_entry>(ent);
                        if (eg.c.type == someip::sd::entry_type::subscribe_eventgroup) {
                            auto ack = build_sd_subscribe_ack();
                            if (ack.empty()) {
                                std::fprintf(stderr, "server: build ack failed\n");
                                return 1;
                            }
                            if (!send_frame(clients[idx].fd.fd(), ack, 2000)) {
                                std::fprintf(stderr, "server: send ack failed\n");
                                return 1;
                            }
                            clients[idx].subscribed = true;
                        }
                    }
                }

                continue;
            }

            // Shutdown
            if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x00FF &&
                parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                should_exit = true;
                break;
            }

            // Field setter (writable)
            if (someip::iface::is_set_request(parsed->hdr, writable_field)) {
                FieldValue req{};
                auto       st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "server: decode field set failed\n");
                    return 1;
                }
                field_value = req;

                someip::wire::header rh{};
                rh.msg.service_id    = parsed->hdr.msg.service_id;
                rh.msg.method_id     = parsed->hdr.msg.method_id;
                rh.req.client_id     = parsed->hdr.req.client_id;
                rh.req.session_id    = parsed->hdr.req.session_id;
                rh.protocol_version  = 1;
                rh.interface_version = parsed->hdr.interface_version;
                rh.msg_type          = someip::wire::message_type::response;
                rh.return_code       = someip::wire::return_code::E_OK;

                EmptyPayload ep{};
                auto         resp_frame = build_someip_message(cfg, rh, ep);
                if (resp_frame.empty()) {
                    std::fprintf(stderr, "server: build set response failed\n");
                    return 1;
                }
                if (!send_frame(clients[idx].fd.fd(), resp_frame, 2000)) {
                    std::fprintf(stderr, "server: send set response failed\n");
                    return 1;
                }

                for (const auto &client : clients) {
                    if (!client.subscribed) {
                        continue;
                    }
                    for (std::uint32_t i = 0; i < 10; ++i) {
                        auto eh = someip::iface::make_notify_header(writable_field, 1);
                        FieldEvent evp{.seq = i, .value = field_value.value};
                        auto       ev = build_someip_message(cfg, eh, evp);
                        if (ev.empty()) {
                            std::fprintf(stderr, "server: build event failed\n");
                            return 1;
                        }
                        if (!send_frame(client.fd.fd(), ev, 2000)) {
                            std::fprintf(stderr, "server: send event failed\n");
                            return 1;
                        }
                    }
                }
                continue;
            }

            // Field setter (read-only -> error)
            if (someip::iface::is_set_request(parsed->hdr, readonly_field)) {
                FieldValue req{};
                auto       st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "server: decode readonly set failed\n");
                    return 1;
                }

                someip::wire::header rh{};
                rh.msg.service_id    = parsed->hdr.msg.service_id;
                rh.msg.method_id     = parsed->hdr.msg.method_id;
                rh.req.client_id     = parsed->hdr.req.client_id;
                rh.req.session_id    = parsed->hdr.req.session_id;
                rh.protocol_version  = 1;
                rh.interface_version = parsed->hdr.interface_version;
                rh.msg_type          = someip::wire::message_type::error;
                rh.return_code       = someip::wire::return_code::E_NOT_OK;

                EmptyPayload ep{};
                auto         resp_frame = build_someip_message(cfg, rh, ep);
                if (resp_frame.empty()) {
                    std::fprintf(stderr, "server: build readonly error failed\n");
                    return 1;
                }
                if (!send_frame(clients[idx].fd.fd(), resp_frame, 2000)) {
                    std::fprintf(stderr, "server: send readonly error failed\n");
                    return 1;
                }
                continue;
            }

            // Method impl: y = x + 1
            if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x0001 &&
                parsed->hdr.msg_type == someip::wire::message_type::request) {
                MethodReq req{};
                auto      st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "server: decode method req failed\n");
                    return 1;
                }

                MethodResp resp{.y = req.x + 1};
                someip::wire::header rh{};
                rh.msg.service_id    = parsed->hdr.msg.service_id;
                rh.msg.method_id     = parsed->hdr.msg.method_id;
                rh.req.client_id     = parsed->hdr.req.client_id;
                rh.req.session_id    = parsed->hdr.req.session_id;
                rh.protocol_version  = 1;
                rh.interface_version = parsed->hdr.interface_version;
                rh.msg_type          = someip::wire::message_type::response;
                rh.return_code       = someip::wire::return_code::E_OK;

                auto resp_frame = build_someip_message(cfg, rh, resp);
                if (resp_frame.empty()) {
                    std::fprintf(stderr, "server: build response failed\n");
                    return 1;
                }
                if (!send_frame(clients[idx].fd.fd(), resp_frame, 2000)) {
                    std::fprintf(stderr, "server: send response failed\n");
                    return 1;
                }
                continue;
            }

            std::fprintf(stderr,
                         "server: unexpected message (offers=%zu) sid=0x%04X mid=0x%04X type=0x%02X\n",
                         offered_count, unsigned(parsed->hdr.msg.service_id), unsigned(parsed->hdr.msg.method_id),
                         unsigned(parsed->hdr.msg_type));
            return 1;
        }

        if (should_exit) {
            break;
        }
    }

    ::unlink(path.c_str());
    return 0;
}

struct child_guard {
    pid_t       pid{-1};
    std::string socket_path{};
    bool        active{true};

    ~child_guard() {
        if (!active) {
            return;
        }
        cleanup();
    }

    void cleanup() noexcept {
        if (pid > 0) {
            int  status = 0;
            auto rc     = ::waitpid(pid, &status, WNOHANG);
            if (rc == 0) {
                (void)::kill(pid, SIGKILL);
                (void)::waitpid(pid, &status, 0);
            }
        }
        if (!socket_path.empty()) {
            ::unlink(socket_path.c_str());
        }
        active = false;
    }

    void release() noexcept { active = false; }
};

} // namespace

TEST_CASE("someip: uds e2e multi-client subscribe + setter + notify") {
    const someip::ser::config cfg{someip::wire::endian::big};

    const auto socket_path = std::string("/tmp/someip_e2e_") + std::to_string(::getpid()) + ".sock";

    int pipefd[2]{-1, -1};
    REQUIRE(::pipe(pipefd) == 0);
    unique_fd pipe_r{pipefd[0]};
    unique_fd pipe_w{pipefd[1]};

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        pipe_r.reset();
        const int rc = run_server(socket_path, pipe_w.release());
        pipe_w.reset();
        std::_Exit(rc);
    }

    pipe_w.reset();

    child_guard guard{.pid = pid, .socket_path = socket_path};

    std::byte ready{};
    REQUIRE(read_exact(pipe_r.fd(), &ready, 1, 2000));
    pipe_r.reset();
    if (ready != std::byte{0x01}) {
        int status = 0;
        (void)::waitpid(pid, &status, 0);
        if (ready == std::byte{0xE1}) {
            INFO("server bind not permitted in current environment");
            ::unlink(socket_path.c_str());
            return;
        }
        FAIL("server initialization failed");
    }

    auto client_a = connect_with_retry(socket_path, 2000);
    REQUIRE(client_a.valid());
    auto client_b = connect_with_retry(socket_path, 2000);
    REQUIRE(client_b.valid());

    // Find -> Offer
    {
        const auto find = build_sd_find_service();
        REQUIRE_FALSE(find.empty());
        REQUIRE(send_frame(client_a.fd(), find, 2000));

        const auto offer_frame = recv_someip_frame(client_a.fd(), 2000);
        REQUIRE_FALSE(offer_frame.empty());
        auto offer = someip::sd::decode_message(std::span<const std::byte>(offer_frame.data(), offer_frame.size()));
        REQUIRE(offer.has_value());
        REQUIRE(offer->sd_payload.entries.size() == 1);
        REQUIRE(offer->sd_payload.options.size() >= 1);
        REQUIRE(std::holds_alternative<someip::sd::service_entry>(offer->sd_payload.entries[0]));

        const auto &se = std::get<someip::sd::service_entry>(offer->sd_payload.entries[0]);
        CHECK(se.c.type == someip::sd::entry_type::offer_service);
        CHECK(se.c.service_id == 0x1234);
        CHECK(se.c.instance_id == 0x0001);
        CHECK(se.c.major_version == 1);

        const auto runs = someip::sd::resolve_option_runs(offer->sd_payload, se.c);
        REQUIRE(runs.has_value());
        REQUIRE_FALSE(runs->run1.empty());
        REQUIRE(std::holds_alternative<someip::sd::ipv4_endpoint_option>(runs->run1[0]));
    }

    // Find -> Offer (client B)
    {
        const auto find = build_sd_find_service();
        REQUIRE_FALSE(find.empty());
        REQUIRE(send_frame(client_b.fd(), find, 2000));

        const auto offer_frame = recv_someip_frame(client_b.fd(), 2000);
        REQUIRE_FALSE(offer_frame.empty());
        auto offer = someip::sd::decode_message(std::span<const std::byte>(offer_frame.data(), offer_frame.size()));
        REQUIRE(offer.has_value());
        REQUIRE(offer->sd_payload.entries.size() == 1);
        REQUIRE(offer->sd_payload.options.size() >= 1);
        REQUIRE(std::holds_alternative<someip::sd::service_entry>(offer->sd_payload.entries[0]));
    }

    // Subscribe -> Ack (client A)
    {
        const auto sub = build_sd_subscribe_eventgroup();
        REQUIRE_FALSE(sub.empty());
        REQUIRE(send_frame(client_a.fd(), sub, 2000));

        const auto ack_frame = recv_someip_frame(client_a.fd(), 2000);
        REQUIRE_FALSE(ack_frame.empty());
        auto ack = someip::sd::decode_message(std::span<const std::byte>(ack_frame.data(), ack_frame.size()));
        REQUIRE(ack.has_value());
        REQUIRE(ack->sd_payload.entries.size() == 1);
        REQUIRE(std::holds_alternative<someip::sd::eventgroup_entry>(ack->sd_payload.entries[0]));

        const auto &eg = std::get<someip::sd::eventgroup_entry>(ack->sd_payload.entries[0]);
        CHECK(eg.c.type == someip::sd::entry_type::subscribe_eventgroup_ack);
        CHECK(eg.c.service_id == 0x1234);
        CHECK(eg.c.instance_id == 0x0001);
        CHECK(eg.eventgroup_id == writable_field.eventgroup_id);
    }

    // Subscribe -> Ack (client B)
    {
        const auto sub = build_sd_subscribe_eventgroup();
        REQUIRE_FALSE(sub.empty());
        REQUIRE(send_frame(client_b.fd(), sub, 2000));

        const auto ack_frame = recv_someip_frame(client_b.fd(), 2000);
        REQUIRE_FALSE(ack_frame.empty());
        auto ack = someip::sd::decode_message(std::span<const std::byte>(ack_frame.data(), ack_frame.size()));
        REQUIRE(ack.has_value());
        REQUIRE(ack->sd_payload.entries.size() == 1);
        REQUIRE(std::holds_alternative<someip::sd::eventgroup_entry>(ack->sd_payload.entries[0]));
    }

    // Setter (writable) -> Response OK (client A)
    {
        const someip::wire::request_id req_id{0x0001, 0x0001};
        auto h = someip::iface::make_set_request_header(writable_field, req_id, 1);
        FieldValue req{.value = 0xBEEF};

        const auto req_frame = build_someip_message(cfg, h, req);
        REQUIRE_FALSE(req_frame.empty());
        REQUIRE(send_frame(client_a.fd(), req_frame, 2000));

        const auto resp_frame = recv_someip_frame(client_a.fd(), 2000);
        REQUIRE_FALSE(resp_frame.empty());
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg.service_id == writable_field.service_id);
        CHECK(parsed->hdr.msg.method_id == writable_field.setter_method_id);
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::response);
        CHECK(parsed->hdr.return_code == someip::wire::return_code::E_OK);
    }

    // Receive 10 notifications after write (client A).
    for (std::uint32_t i = 0; i < 10; ++i) {
        const auto ev_frame = recv_someip_frame(client_a.fd(), 2000);
        REQUIRE_FALSE(ev_frame.empty());
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(ev_frame.data(), ev_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg.service_id == writable_field.service_id);
        CHECK(parsed->hdr.msg.method_id == writable_field.notifier_event_id);
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::notification);

        FieldEvent ep{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        REQUIRE(someip::ser::decode(parsed->payload, cfg, ep, payload_base).has_value());
        CHECK(ep.seq == i);
        CHECK(ep.value == 0xBEEFu);
    }

    // Receive 10 notifications after write (client B).
    for (std::uint32_t i = 0; i < 10; ++i) {
        const auto ev_frame = recv_someip_frame(client_b.fd(), 2000);
        REQUIRE_FALSE(ev_frame.empty());
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(ev_frame.data(), ev_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg.service_id == writable_field.service_id);
        CHECK(parsed->hdr.msg.method_id == writable_field.notifier_event_id);
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::notification);

        FieldEvent ep{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        REQUIRE(someip::ser::decode(parsed->payload, cfg, ep, payload_base).has_value());
        CHECK(ep.seq == i);
        CHECK(ep.value == 0xBEEFu);
    }

    // Setter (read-only) -> ERROR E_NOT_OK (client B)
    {
        const someip::wire::request_id req_id{0x0001, 0x0002};
        auto h = someip::iface::make_set_request_header(readonly_field, req_id, 1);
        FieldValue req{.value = 0x1234};

        const auto req_frame = build_someip_message(cfg, h, req);
        REQUIRE_FALSE(req_frame.empty());
        REQUIRE(send_frame(client_b.fd(), req_frame, 2000));

        const auto resp_frame = recv_someip_frame(client_b.fd(), 2000);
        REQUIRE_FALSE(resp_frame.empty());
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg.service_id == readonly_field.service_id);
        CHECK(parsed->hdr.msg.method_id == readonly_field.setter_method_id);
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::error);
        CHECK(parsed->hdr.return_code == someip::wire::return_code::E_NOT_OK);
    }

    // Method call (REQUEST/RESPONSE) after events.
    {
        someip::wire::header h{};
        h.msg.service_id    = 0x1234;
        h.msg.method_id     = 0x0001;
        h.req.client_id     = 0x0001;
        h.req.session_id    = 0x0001;
        h.protocol_version  = 1;
        h.interface_version = 1;
        h.msg_type          = someip::wire::message_type::request;
        h.return_code       = 0;

        MethodReq req{.x = 41};
        const auto req_frame = build_someip_message(cfg, h, req);
        REQUIRE_FALSE(req_frame.empty());
        REQUIRE(send_frame(client_a.fd(), req_frame, 2000));

        const auto resp_frame = recv_someip_frame(client_a.fd(), 2000);
        REQUIRE_FALSE(resp_frame.empty());
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg.service_id == 0x1234);
        CHECK(parsed->hdr.msg.method_id == 0x0001);
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::response);
        CHECK(parsed->hdr.req.client_id == 0x0001);
        CHECK(parsed->hdr.req.session_id == 0x0001);
        CHECK(parsed->hdr.return_code == someip::wire::return_code::E_OK);

        MethodResp resp{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        REQUIRE(someip::ser::decode(parsed->payload, cfg, resp, payload_base).has_value());
        CHECK(resp.y == 42u);
    }

    // Shutdown server.
    {
        someip::wire::header h{};
        h.msg.service_id    = 0x1234;
        h.msg.method_id     = 0x00FF;
        h.req.client_id     = 0x0001;
        h.req.session_id    = 0x0002;
        h.protocol_version  = 1;
        h.interface_version = 1;
        h.msg_type          = someip::wire::message_type::request_no_return;
        h.return_code       = 0;

        EmptyPayload ep{};
        const auto shutdown_frame = build_someip_message(cfg, h, ep);
        REQUIRE_FALSE(shutdown_frame.empty());
        REQUIRE(send_frame(client_a.fd(), shutdown_frame, 2000));
    }

    // Wait for child.
    int status = 0;
    for (int i = 0; i < 500; ++i) {
        const auto rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status)) {
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
    }
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);

    guard.release();
    ::unlink(socket_path.c_str());
}

#endif
