#include <doctest/doctest.h>

#if !defined(__linux__)

TEST_CASE("someip: shm e2e (linux-only)") {
    DOCTEST_SKIP("Linux futex + shm_open required");
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
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/futex.h>
#include <span>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::size_t kMaxFrameSize = 4096;
constexpr std::size_t kQueueSlots   = 16;

struct queue_slot {
    std::uint32_t            len{0};
    std::array<std::byte, kMaxFrameSize> data{};
};

struct shm_queue {
    alignas(4) int head{0};
    alignas(4) int tail{0};
    alignas(4) int count{0};
    alignas(4) int pad{0};
    std::array<queue_slot, kQueueSlots> slots{};
};

struct shm_region {
    std::uint32_t magic{0x53484D31}; // "SHM1"
    shm_queue c2s[2]{};
    shm_queue s2c[2]{};
};

static int futex_wait_raw(int *addr, int expected, const timespec *ts) {
    return static_cast<int>(::syscall(SYS_futex, addr, FUTEX_WAIT, expected, ts, nullptr, 0));
}

static int futex_wake_raw(int *addr, int count) {
    return static_cast<int>(::syscall(SYS_futex, addr, FUTEX_WAKE, count, nullptr, nullptr, 0));
}

static bool futex_wait_value(int *addr, int expected, int timeout_ms) {
    timespec ts{};
    timespec *pts = nullptr;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        pts = &ts;
    }
    for (;;) {
        const int rc = futex_wait_raw(addr, expected, pts);
        if (rc == 0) return true;
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return true;
        if (errno == ETIMEDOUT) return false;
        return false;
    }
}

static void futex_wake(int *addr) { (void)futex_wake_raw(addr, 1); }

static bool queue_push(shm_queue &q, std::span<const std::byte> bytes, int timeout_ms) {
    if (bytes.size() > kMaxFrameSize) {
        return false;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::atomic_ref<int> count_ref(q.count);
    std::atomic_ref<int> tail_ref(q.tail);

    for (;;) {
        const int count = count_ref.load(std::memory_order_acquire);
        if (count < static_cast<int>(kQueueSlots)) {
            break;
        }
        if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const auto remaining =
            timeout_ms < 0 ? -1
                           : static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (!futex_wait_value(&q.count, static_cast<int>(kQueueSlots), remaining)) {
            return false;
        }
    }

    const int tail = tail_ref.load(std::memory_order_relaxed);
    auto &slot = q.slots[static_cast<std::size_t>(tail)];
    slot.len = static_cast<std::uint32_t>(bytes.size());
    std::memcpy(slot.data.data(), bytes.data(), bytes.size());
    std::atomic_thread_fence(std::memory_order_release);
    tail_ref.store((tail + 1) % static_cast<int>(kQueueSlots), std::memory_order_release);
    count_ref.fetch_add(1, std::memory_order_release);
    futex_wake(&q.count);
    return true;
}

static bool queue_try_pop(shm_queue &q, std::vector<std::byte> &out) {
    std::atomic_ref<int> count_ref(q.count);
    std::atomic_ref<int> head_ref(q.head);

    const int count = count_ref.load(std::memory_order_acquire);
    if (count <= 0) {
        return false;
    }

    const int head = head_ref.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    const auto &slot = q.slots[static_cast<std::size_t>(head)];
    if (slot.len > kMaxFrameSize) {
        return false;
    }
    out.resize(slot.len);
    std::memcpy(out.data(), slot.data.data(), slot.len);
    head_ref.store((head + 1) % static_cast<int>(kQueueSlots), std::memory_order_release);
    count_ref.fetch_sub(1, std::memory_order_release);
    futex_wake(&q.count);
    return true;
}

static bool queue_pop(shm_queue &q, std::vector<std::byte> &out, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::atomic_ref<int> count_ref(q.count);
    std::atomic_ref<int> head_ref(q.head);

    for (;;) {
        const int count = count_ref.load(std::memory_order_acquire);
        if (count > 0) {
            break;
        }
        if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const auto remaining =
            timeout_ms < 0 ? -1
                           : static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (!futex_wait_value(&q.count, 0, remaining)) {
            return false;
        }
    }

    const int head = head_ref.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    const auto &slot = q.slots[static_cast<std::size_t>(head)];
    if (slot.len > kMaxFrameSize) {
        return false;
    }
    out.resize(slot.len);
    std::memcpy(out.data(), slot.data.data(), slot.len);
    head_ref.store((head + 1) % static_cast<int>(kQueueSlots), std::memory_order_release);
    count_ref.fetch_sub(1, std::memory_order_release);
    futex_wake(&q.count);
    return true;
}

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
    opt.l4_proto    = 0x06;
    opt.port        = 30509;
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

static int run_server(shm_region *shm, int ready_fd) {
    const std::byte kReady{0x01};
    const std::byte kInitFail{0xE2};

    auto notify_ready = [&](std::byte code) {
        (void)write(ready_fd, &code, 1);
    };

    if (shm == nullptr) {
        notify_ready(kInitFail);
        return 1;
    }
    notify_ready(kReady);

    const someip::ser::config cfg{someip::wire::endian::big};
    std::array<bool, 2> subscribed{false, false};
    FieldValue           field_value{.value = 0};

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        bool progressed = false;
        for (std::size_t idx = 0; idx < 2; ++idx) {
            std::vector<std::byte> frame{};
            if (!queue_try_pop(shm->c2s[idx], frame)) {
                continue;
            }
            progressed = true;

            auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
            if (!parsed) {
                std::fprintf(stderr, "shm server: try_parse_frame failed\n");
                return 1;
            }

            const auto payload_base = parsed->tp ? 20u : 16u;

            if (parsed->hdr.msg.service_id == someip::sd::kServiceId && parsed->hdr.msg.method_id == someip::sd::kMethodId) {
                auto sdmsg = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
                if (!sdmsg) {
                    std::fprintf(stderr, "shm server: decode SD failed\n");
                    return 1;
                }
                for (const auto &ent : sdmsg->sd_payload.entries) {
                    if (std::holds_alternative<someip::sd::service_entry>(ent)) {
                        const auto &se = std::get<someip::sd::service_entry>(ent);
                        if (se.c.type == someip::sd::entry_type::find_service) {
                            auto offer = build_sd_offer_service();
                            if (offer.empty() || !queue_push(shm->s2c[idx], offer, 2000)) {
                                std::fprintf(stderr, "shm server: offer send failed\n");
                                return 1;
                            }
                        }
                    } else if (std::holds_alternative<someip::sd::eventgroup_entry>(ent)) {
                        const auto &eg = std::get<someip::sd::eventgroup_entry>(ent);
                        if (eg.c.type == someip::sd::entry_type::subscribe_eventgroup) {
                            auto ack = build_sd_subscribe_ack();
                            if (ack.empty() || !queue_push(shm->s2c[idx], ack, 2000)) {
                                std::fprintf(stderr, "shm server: ack send failed\n");
                                return 1;
                            }
                            subscribed[idx] = true;
                        }
                    }
                }
                continue;
            }

            if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x00FF &&
                parsed->hdr.msg_type == someip::wire::message_type::request_no_return) {
                return 0;
            }

            if (someip::iface::is_set_request(parsed->hdr, writable_field)) {
                FieldValue req{};
                auto       st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "shm server: decode field set failed\n");
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
                if (resp_frame.empty() || !queue_push(shm->s2c[idx], resp_frame, 2000)) {
                    std::fprintf(stderr, "shm server: send set response failed\n");
                    return 1;
                }

                for (std::size_t j = 0; j < 2; ++j) {
                    if (!subscribed[j]) {
                        continue;
                    }
                    for (std::uint32_t i = 0; i < 10; ++i) {
                        auto eh = someip::iface::make_notify_header(writable_field, 1);
                        FieldEvent evp{.seq = i, .value = field_value.value};
                        auto       ev = build_someip_message(cfg, eh, evp);
                        if (ev.empty() || !queue_push(shm->s2c[j], ev, 2000)) {
                            std::fprintf(stderr, "shm server: send event failed\n");
                            return 1;
                        }
                    }
                }
                continue;
            }

            if (someip::iface::is_set_request(parsed->hdr, readonly_field)) {
                FieldValue req{};
                auto       st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "shm server: decode readonly set failed\n");
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
                if (resp_frame.empty() || !queue_push(shm->s2c[idx], resp_frame, 2000)) {
                    std::fprintf(stderr, "shm server: send readonly error failed\n");
                    return 1;
                }
                continue;
            }

            if (parsed->hdr.msg.service_id == 0x1234 && parsed->hdr.msg.method_id == 0x0001 &&
                parsed->hdr.msg_type == someip::wire::message_type::request) {
                MethodReq req{};
                auto      st = someip::ser::decode(parsed->payload, cfg, req, payload_base);
                if (!st) {
                    std::fprintf(stderr, "shm server: decode method req failed\n");
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
                if (resp_frame.empty() || !queue_push(shm->s2c[idx], resp_frame, 2000)) {
                    std::fprintf(stderr, "shm server: send response failed\n");
                    return 1;
                }
                continue;
            }

            std::fprintf(stderr, "shm server: unexpected message\n");
            return 1;
        }

        if (!progressed) {
            if (std::chrono::steady_clock::now() > deadline) {
                std::fprintf(stderr, "shm server: timeout waiting for messages\n");
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

} // namespace

TEST_CASE("someip: shm e2e multi-client subscribe + setter + notify") {
    const someip::ser::config cfg{someip::wire::endian::big};
    const std::string shm_name = std::string("/someip_shm_e2e_") + std::to_string(::getpid());

    const int shm_fd = ::shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd < 0) {
        if (errno == EPERM || errno == EACCES || errno == ENOSYS) {
            INFO("shm_open not permitted in current environment");
            return;
        }
        FAIL("shm_open failed");
    }
    REQUIRE(::ftruncate(shm_fd, static_cast<off_t>(sizeof(shm_region))) == 0);

    auto *shm = static_cast<shm_region *>(
        ::mmap(nullptr, sizeof(shm_region), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm == MAP_FAILED) {
        if (errno == EPERM || errno == EACCES) {
            INFO("mmap not permitted in current environment");
            ::close(shm_fd);
            ::shm_unlink(shm_name.c_str());
            return;
        }
        FAIL("mmap failed");
    }
    ::close(shm_fd);
    std::memset(shm, 0, sizeof(shm_region));
    ::shm_unlink(shm_name.c_str());

    int pipefd[2]{-1, -1};
    REQUIRE(::pipe(pipefd) == 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::close(pipefd[0]);
        const int rc = run_server(shm, pipefd[1]);
        ::close(pipefd[1]);
        std::_Exit(rc);
    }

    ::close(pipefd[1]);
    std::byte ready{};
    REQUIRE(::read(pipefd[0], &ready, 1) == 1);
    ::close(pipefd[0]);
    if (ready != std::byte{0x01}) {
        int status = 0;
        (void)::waitpid(pid, &status, 0);
        FAIL("shm server initialization failed");
    }

    // Client A: Find -> Offer.
    {
        const auto find = build_sd_find_service();
        REQUIRE_FALSE(find.empty());
        REQUIRE(queue_push(shm->c2s[0], find, 2000));

        std::vector<std::byte> offer_frame{};
        REQUIRE(queue_pop(shm->s2c[0], offer_frame, 2000));
        auto offer = someip::sd::decode_message(std::span<const std::byte>(offer_frame.data(), offer_frame.size()));
        REQUIRE(offer.has_value());
    }

    // Client B: Find -> Offer.
    {
        const auto find = build_sd_find_service();
        REQUIRE_FALSE(find.empty());
        REQUIRE(queue_push(shm->c2s[1], find, 2000));

        std::vector<std::byte> offer_frame{};
        REQUIRE(queue_pop(shm->s2c[1], offer_frame, 2000));
        auto offer = someip::sd::decode_message(std::span<const std::byte>(offer_frame.data(), offer_frame.size()));
        REQUIRE(offer.has_value());
    }

    // Subscribe -> Ack (client A).
    {
        const auto sub = build_sd_subscribe_eventgroup();
        REQUIRE_FALSE(sub.empty());
        REQUIRE(queue_push(shm->c2s[0], sub, 2000));

        std::vector<std::byte> ack_frame{};
        REQUIRE(queue_pop(shm->s2c[0], ack_frame, 2000));
        auto ack = someip::sd::decode_message(std::span<const std::byte>(ack_frame.data(), ack_frame.size()));
        REQUIRE(ack.has_value());
    }

    // Subscribe -> Ack (client B).
    {
        const auto sub = build_sd_subscribe_eventgroup();
        REQUIRE_FALSE(sub.empty());
        REQUIRE(queue_push(shm->c2s[1], sub, 2000));

        std::vector<std::byte> ack_frame{};
        REQUIRE(queue_pop(shm->s2c[1], ack_frame, 2000));
        auto ack = someip::sd::decode_message(std::span<const std::byte>(ack_frame.data(), ack_frame.size()));
        REQUIRE(ack.has_value());
    }

    // Setter (writable) -> Response OK (client A).
    {
        const someip::wire::request_id req_id{0x0001, 0x0001};
        auto h = someip::iface::make_set_request_header(writable_field, req_id, 1);
        FieldValue req{.value = 0xBEEF};

        const auto req_frame = build_someip_message(cfg, h, req);
        REQUIRE_FALSE(req_frame.empty());
        REQUIRE(queue_push(shm->c2s[0], req_frame, 2000));

        std::vector<std::byte> resp_frame{};
        REQUIRE(queue_pop(shm->s2c[0], resp_frame, 2000));
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.return_code == someip::wire::return_code::E_OK);
    }

    // Receive 10 notifications after write (client A).
    for (std::uint32_t i = 0; i < 10; ++i) {
        std::vector<std::byte> ev_frame{};
        REQUIRE(queue_pop(shm->s2c[0], ev_frame, 2000));
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(ev_frame.data(), ev_frame.size()));
        REQUIRE(parsed.has_value());
        FieldEvent ep{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        REQUIRE(someip::ser::decode(parsed->payload, cfg, ep, payload_base).has_value());
        CHECK(ep.seq == i);
        CHECK(ep.value == 0xBEEFu);
    }

    // Receive 10 notifications after write (client B).
    for (std::uint32_t i = 0; i < 10; ++i) {
        std::vector<std::byte> ev_frame{};
        REQUIRE(queue_pop(shm->s2c[1], ev_frame, 2000));
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(ev_frame.data(), ev_frame.size()));
        REQUIRE(parsed.has_value());
        FieldEvent ep{};
        const auto payload_base = parsed->tp ? 20u : 16u;
        REQUIRE(someip::ser::decode(parsed->payload, cfg, ep, payload_base).has_value());
        CHECK(ep.seq == i);
        CHECK(ep.value == 0xBEEFu);
    }

    // Setter (read-only) -> ERROR E_NOT_OK (client B).
    {
        const someip::wire::request_id req_id{0x0001, 0x0002};
        auto h = someip::iface::make_set_request_header(readonly_field, req_id, 1);
        FieldValue req{.value = 0x1234};

        const auto req_frame = build_someip_message(cfg, h, req);
        REQUIRE_FALSE(req_frame.empty());
        REQUIRE(queue_push(shm->c2s[1], req_frame, 2000));

        std::vector<std::byte> resp_frame{};
        REQUIRE(queue_pop(shm->s2c[1], resp_frame, 2000));
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
        CHECK(parsed->hdr.msg_type == someip::wire::message_type::error);
        CHECK(parsed->hdr.return_code == someip::wire::return_code::E_NOT_OK);
    }

    // Method call (REQUEST/RESPONSE) after events (client A).
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
        REQUIRE(queue_push(shm->c2s[0], req_frame, 2000));

        std::vector<std::byte> resp_frame{};
        REQUIRE(queue_pop(shm->s2c[0], resp_frame, 2000));
        auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(resp_frame.data(), resp_frame.size()));
        REQUIRE(parsed.has_value());
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
        REQUIRE(queue_push(shm->c2s[0], shutdown_frame, 2000));
    }

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

    ::munmap(shm, sizeof(shm_region));
}

#endif
