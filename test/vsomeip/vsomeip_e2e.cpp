#include <vsomeip/vsomeip.hpp>

#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/ser/encode.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr vsomeip::service_t    kService   = 0x1234;
constexpr vsomeip::instance_t   kInstance  = 0x0001;
constexpr vsomeip::method_t     kMethod    = 0x0001;
constexpr vsomeip::method_t     kSetterW   = 0x0101;
constexpr vsomeip::method_t     kSetterRO  = 0x0201;
constexpr vsomeip::method_t     kShutdown  = 0x00FF;
constexpr vsomeip::event_t      kEvent     = 0x8001;
constexpr vsomeip::eventgroup_t kEventGrp  = 0x0001;
constexpr std::uint32_t         kValue     = 0xBEEF;
constexpr std::size_t           kEvents    = 10;

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

static std::shared_ptr<vsomeip::payload> to_payload(const std::vector<std::byte> &bytes) {
    auto payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        data[i] = static_cast<vsomeip::byte_t>(std::to_integer<std::uint8_t>(bytes[i]));
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
    for (std::size_t i = 0; i < len; ++i) {
        bytes[i] = std::byte(data[i]);
    }
    auto st = someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), kCfg, out, 0);
    return st.has_value();
}

#if !defined(_WIN32)
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
#endif

class client_runner {
  public:
    client_runner(std::string name, bool reliable, bool is_a, int go_fd, int done_fd)
        : name_(std::move(name)), reliable_(reliable), is_a_(is_a), go_fd_(go_fd), done_fd_(done_fd) {}

    int run() {
        app_ = vsomeip::runtime::get()->create_application(name_);
        if (!app_->init()) {
            std::fprintf(stderr, "%s: init failed\n", name_.c_str());
            return 1;
        }

        app_->register_state_handler([this](vsomeip::state_type_e s) { on_state(s); });
        app_->register_availability_handler(kService, kInstance, [this](vsomeip::service_t, vsomeip::instance_t, bool avail) {
            on_availability(avail);
        });
        app_->register_subscription_status_handler(kService, kInstance, kEventGrp, kEvent,
                                                    [this](auto, auto, auto, auto, std::uint16_t status) {
                                                        on_subscription_status(status);
                                                    });

        app_->register_message_handler(kService, kInstance, kEvent, [this](const std::shared_ptr<vsomeip::message> &m) { on_event(m); });
        app_->register_message_handler(kService, kInstance, kMethod, [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });
        app_->register_message_handler(kService, kInstance, kSetterW, [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });
        app_->register_message_handler(kService, kInstance, kSetterRO, [this](const std::shared_ptr<vsomeip::message> &m) { on_response(m); });

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
        }
    }

    void on_availability(bool avail) {
        if (avail) {
            std::lock_guard<std::mutex> lock(mutex_);
            available_ = true;
            cv_.notify_all();
        }
    }

    void on_subscription_status(std::uint16_t status) {
        if (status == 0x00u) {
            std::lock_guard<std::mutex> lock(mutex_);
            subscribed_ = true;
            cv_.notify_all();
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
        if (expected_seq_ >= kEvents) {
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
        } else if (method == kMethod) {
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
        app_->request_service(kService, kInstance);
        if (!wait_for_flag(available_, std::chrono::seconds(10), "client: service not available")) return;

        std::set<vsomeip::eventgroup_t> groups{ kEventGrp };
        app_->request_event(kService, kInstance, kEvent, groups, vsomeip::event_type_e::ET_FIELD,
                            reliable_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->subscribe(kService, kInstance, kEventGrp);
        if (!wait_for_flag(subscribed_, std::chrono::seconds(10), "client: subscribe not accepted")) return;

        if (is_a_) {
            send_setter_writable();
            if (!wait_for_flag(setter_ok_, std::chrono::seconds(10), "client: setter ok timeout")) return;
            if (!wait_for_flag(events_done_, std::chrono::seconds(10), "client: events timeout")) return;
            send_method();
            if (!wait_for_flag(method_ok_, std::chrono::seconds(10), "client: method timeout")) return;

#if !defined(_WIN32)
            if (go_fd_ >= 0) {
                std::byte sig{};
                if (!read_byte_timeout(go_fd_, sig, 10000)) {
                    fail("client: shutdown signal timeout");
                    return;
                }
            }
#endif
            send_shutdown();
        } else {
            if (!wait_for_flag(events_done_, std::chrono::seconds(10), "client: events timeout")) return;
            send_setter_readonly();
            if (!wait_for_flag(readonly_ok_, std::chrono::seconds(10), "client: readonly timeout")) return;
#if !defined(_WIN32)
            if (done_fd_ >= 0) {
                (void)write_byte(done_fd_, std::byte{0x01});
            }
#endif
        }

        app_->stop();
    }

    void send_setter_writable() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kService);
        req->set_instance(kInstance);
        req->set_method(kSetterW);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        FieldValue fv{.value = kValue};
        req->set_payload(encode_payload(fv));
        app_->send(req);
    }

    void send_setter_readonly() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kService);
        req->set_instance(kInstance);
        req->set_method(kSetterRO);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        FieldValue fv{.value = 0x1234};
        req->set_payload(encode_payload(fv));
        app_->send(req);
    }

    void send_method() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kService);
        req->set_instance(kInstance);
        req->set_method(kMethod);
        req->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        MethodReq mr{.x = 41};
        req->set_payload(encode_payload(mr));
        app_->send(req);
    }

    void send_shutdown() {
        auto req = vsomeip::runtime::get()->create_request(reliable_);
        req->set_service(kService);
        req->set_instance(kInstance);
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

    std::shared_ptr<vsomeip::application> app_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool registered_{false};
    bool available_{false};
    bool subscribed_{false};
    bool setter_ok_{false};
    bool readonly_ok_{false};
    bool method_ok_{false};
    bool events_done_{false};
    std::size_t expected_seq_{0};
    bool failed_{false};
    std::string error_{};
};

class server_runner {
  public:
    server_runner(bool reliable, int ready_fd) : reliable_(reliable), ready_fd_(ready_fd) {}

    int run() {
        app_ = vsomeip::runtime::get()->create_application("server");
        if (!app_->init()) {
            std::fprintf(stderr, "server: init failed\n");
            return 1;
        }
        app_->register_state_handler([this](vsomeip::state_type_e s) { on_state(s); });
        app_->register_subscription_handler(kService, kInstance, kEventGrp,
                                             [this](vsomeip::client_t, vsomeip::uid_t, vsomeip::gid_t, bool subscribed) {
                                                 if (subscribed) {
                                                     std::lock_guard<std::mutex> lock(mutex_);
                                                     subscribed_count_++;
                                                     cv_.notify_all();
                                                 }
                                                 return true;
                                             });
        app_->register_message_handler(kService, kInstance, kMethod, [this](const std::shared_ptr<vsomeip::message> &m) { on_method(m); });
        app_->register_message_handler(kService, kInstance, kSetterW, [this](const std::shared_ptr<vsomeip::message> &m) { on_setter_w(m); });
        app_->register_message_handler(kService, kInstance, kSetterRO, [this](const std::shared_ptr<vsomeip::message> &m) { on_setter_ro(m); });
        app_->register_message_handler(kService, kInstance, kShutdown, [this](const std::shared_ptr<vsomeip::message> &m) { on_shutdown(m); });

        app_->start();
        return 0;
    }

  private:
    void on_state(vsomeip::state_type_e s) {
        if (s != vsomeip::state_type_e::ST_REGISTERED) return;
        std::set<vsomeip::eventgroup_t> groups{ kEventGrp };
        app_->offer_event(kService, kInstance, kEvent, groups, vsomeip::event_type_e::ET_FIELD,
                          std::chrono::milliseconds::zero(), false, true, nullptr,
                          reliable_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE);
        app_->offer_service(kService, kInstance);
        if (ready_fd_ >= 0) {
            (void)write_byte(ready_fd_, std::byte{0x01});
            ::close(ready_fd_);
            ready_fd_ = -1;
        }
    }

    void on_method(const std::shared_ptr<vsomeip::message> &msg) {
        MethodReq req{};
        if (!decode_payload(msg->get_payload(), req)) {
            std::fprintf(stderr, "server: method decode failed\n");
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
            std::fprintf(stderr, "server: setter decode failed\n");
            return;
        }
        field_value_ = req.value;

        auto response = vsomeip::runtime::get()->create_response(msg);
        response->set_return_code(vsomeip::return_code_e::E_OK);
        app_->send(response);

        for (std::uint32_t i = 0; i < kEvents; ++i) {
            FieldEvent ev{.seq = i, .value = field_value_};
            app_->notify(kService, kInstance, kEvent, encode_payload(ev), true);
        }
    }

    void on_setter_ro(const std::shared_ptr<vsomeip::message> &msg) {
        auto response = vsomeip::runtime::get()->create_response(msg);
        response->set_message_type(vsomeip::message_type_e::MT_ERROR);
        response->set_return_code(vsomeip::return_code_e::E_NOT_OK);
        app_->send(response);
    }

    void on_shutdown(const std::shared_ptr<vsomeip::message> &) {
        app_->stop_offer_service(kService, kInstance);
        app_->clear_all_handler();
        app_->stop();
    }

    bool reliable_{false};
    int  ready_fd_{-1};
    std::shared_ptr<vsomeip::application> app_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t subscribed_count_{0};
    std::uint32_t field_value_{0};
};

#if !defined(_WIN32)
static int run_transport(const std::string &config_path, bool reliable) {
    ::setenv("VSOMEIP_CONFIGURATION", config_path.c_str(), 1);

    int server_pipe[2]{-1, -1};
    int done_pipe[2]{-1, -1};
    int go_pipe[2]{-1, -1};
    if (::pipe(server_pipe) != 0 || ::pipe(done_pipe) != 0 || ::pipe(go_pipe) != 0) {
        std::fprintf(stderr, "pipe failed: %s\n", std::strerror(errno));
        return 1;
    }

    const pid_t server_pid = ::fork();
    if (server_pid == 0) {
        ::close(server_pipe[0]);
        server_runner server{reliable, server_pipe[1]};
        const int rc = server.run();
        if (server_pipe[1] >= 0) {
            ::close(server_pipe[1]);
        }
        std::_Exit(rc);
    }
    ::close(server_pipe[1]);

    std::byte ready{};
    if (!read_byte_timeout(server_pipe[0], ready, 5000)) {
        std::fprintf(stderr, "server ready timeout\n");
        return 1;
    }
    ::close(server_pipe[0]);

    const pid_t client_b_pid = ::fork();
    if (client_b_pid == 0) {
        ::close(done_pipe[0]);
        ::close(go_pipe[0]);
        ::close(go_pipe[1]);
        client_runner client_b{"client_b", reliable, false, -1, done_pipe[1]};
        const int rc = client_b.run();
        ::close(done_pipe[1]);
        std::_Exit(rc);
    }
    ::close(done_pipe[1]);

    const pid_t client_a_pid = ::fork();
    if (client_a_pid == 0) {
        ::close(go_pipe[1]);
        ::close(done_pipe[0]);
        client_runner client_a{"client_a", reliable, true, go_pipe[0], -1};
        const int rc = client_a.run();
        ::close(go_pipe[0]);
        std::_Exit(rc);
    }
    ::close(go_pipe[0]);

    std::byte done{};
    if (!read_byte_timeout(done_pipe[0], done, 20000)) {
        std::fprintf(stderr, "client_b did not finish in time\n");
        (void)::kill(client_a_pid, SIGKILL);
        (void)::kill(server_pid, SIGKILL);
        return 1;
    }
    ::close(done_pipe[0]);

    (void)write_byte(go_pipe[1], std::byte{0x01});
    ::close(go_pipe[1]);

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
        std::fprintf(stderr, "server failed\n");
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
    std::printf("vsomeip tests skipped on Windows\n");
    return 0;
#else
    std::string transport = "all";
    if (argc >= 2) {
        transport = argv[1];
    }

    std::string config_dir = VSOMEIP_TEST_CONFIG_DIR;
    const auto tcp_cfg = config_dir + "/vsomeip_tcp.json";
    const auto udp_cfg = config_dir + "/vsomeip_udp.json";

    if (transport == "tcp") {
        return run_transport(tcp_cfg, true);
    }
    if (transport == "udp") {
        return run_transport(udp_cfg, false);
    }

    const int tcp_rc = run_transport(tcp_cfg, true);
    if (tcp_rc != 0) return tcp_rc;
    return run_transport(udp_cfg, false);
#endif
}
