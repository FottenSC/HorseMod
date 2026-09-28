// Standalone offline adapter test. No game, UE4SS, DLL loading, or real hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "WatchRecoveryState.hpp"
#include "WatchRecoverySites.hpp"

#define STR(x) L##x
namespace RC
{
    enum class LogLevel { Error, Default };
    struct Output { template<LogLevel, class... T> static void send(const wchar_t*, T...) {} };
}
namespace PLH
{
    // Installation is deliberately unavailable in this executable.
    struct x64Detour { x64Detour(uint64_t, uint64_t, uint64_t*) {} bool hook() { std::abort(); } };
}
namespace DotVanisher
{
    bool verify_exe_file(const wchar_t*) { std::abort(); }
    bool read_bytes_seh(const void* address, uint8_t* out, size_t size) noexcept
    {
        __try { std::memcpy(out, address, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    #include "WatchRecovery.inl"

    struct WatchRecoveryOfflineTest
    {
        using H = WatchRecoveryHook;
        inline static std::array<uint8_t, 0x2a0> owner{};
        inline static std::array<uint8_t, 0x120> sentinel{};
        inline static std::array<uintptr_t, 1> peer_table{};
        inline static uintptr_t peer{}, replacement_peer{}, service{};
        inline static uint64_t tag{77}, now{1000};
        inline static unsigned deliveries{}, successes{}, failures{}, acknowledgments{}, updates{}, queues{};
        inline static bool peer_present{true}, route_present{}, fail_in_update{}, cancel_in_ack{}, destroy_in_update{};
        inline static bool reenter_on_success{};
        inline static unsigned reenter_probe{}, ref_destroyed{}, ref_disposed{};
        inline static std::array<uintptr_t, 2> ref_table{};
        alignas(8) inline static std::array<uint8_t, 24> peer_ref{}, route_ref{};
        inline static uint8_t* image{};

        template<class T> static void put(void* target, size_t offset, T value)
        { std::memcpy(static_cast<uint8_t*>(target) + offset, &value, sizeof(value)); }
        static void check(bool result, const char* message)
        { if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
        static ULONGLONG WINAPI clock() { return now; }
        static H::RouteKey* __fastcall key(void*, H::RouteKey* out)
        { out->tag = tag; return out; }
        static H::SharedPair* __fastcall lookup_peer(void*, H::SharedPair* out, uint16_t id)
        {
            *out = {};
            if (peer_present && id == 12)
            {
                out->object = &peer; out->control = peer_ref.data();
                ++*reinterpret_cast<LONG*>(peer_ref.data() + 8);
            }
            return out;
        }
        static void* __fastcall get_service() { return &service; }
        static H::SharedPair* __fastcall lookup_route(void*, H::SharedPair* out, const H::RouteKey*)
        {
            if (reenter_probe)
            {
                const auto identity = H::instance().m_state.identity();
                const auto action = reenter_probe; reenter_probe = 0;
                H::on_reset(owner.data());
                if (action == 2) H::instance().m_state.begin(identity, now, 20'000);
            }
            *out = {};
            if (route_present)
            {
                out->object = &service; out->control = route_ref.data();
                ::InterlockedIncrement(reinterpret_cast<volatile LONG*>(route_ref.data() + 8));
            }
            return out;
        }
        static void __fastcall destroy_ref(void*) { ++ref_destroyed; }
        static void __fastcall dispose_ref(void*, uint32_t flag)
        { check(flag == 1, "ref dispose ABI flag"); ++ref_disposed; }
        static void __fastcall queue(uint8_t* o, uint8_t*, void*, void*, void*, float)
        { ++queues; put(o, 0x270, int64_t{1}); }
        static void __fastcall no_op(uint8_t*) {}
        static void __fastcall no_pair(uint8_t*, void*) {}
        static void __fastcall update(uint8_t* o, void*, float)
        {
            ++updates;
            if (fail_in_update) o[0x3d] = 5;
            if (destroy_in_update) H::on_destroy(o);
        }
        static bool __fastcall ack(uint8_t** o, void*)
        {
            ++acknowledgments;
            put(*o, 0x270, int64_t{0});
            if (cancel_in_ack) H::on_reset(*o);
            return true;
        }
        static void __fastcall response(uint8_t* o, void* archive)
        {
            auto& h = H::instance();
            check(!h.m_state.pending(), "pending cleared before native callback");
            auto* packet = static_cast<H::BorrowedArchive*>(archive);
            check(packet->mode == 1 && packet->cursor, "native replay read-mode archive");
            uint32_t status{}; uint16_t id{};
            std::memcpy(&status, packet->cursor, 4);
            std::memcpy(&id, packet->cursor + 4, 2);
            packet->cursor += 6;
            ++deliveries;
            if (status != 0) return;
            check(id == 12, "decoded assignment preserved");
            if (peer_present && route_present)
            {
                ++successes;
                o[0x3c] = 3; o[0x3e] = 7;
                if (reenter_on_success) H::on_reset(o);
            }
            else { ++failures; H::on_reset(o); o[0x3e] = 9; }
        }
        struct Packet
        {
            std::array<uint8_t, 6> bytes{};
            struct Array { uint8_t* data; uint32_t size; uint32_t capacity; } array{bytes.data(), 6, 6};
            H::BorrowedArchive archive{};
            explicit Packet(uint32_t status = 0)
            {
                uint16_t id = 12;
                std::memcpy(bytes.data(), &status, 4);
                std::memcpy(bytes.data() + 4, &id, 2);
                archive.cursor = bytes.data(); archive.byte_array = &array;
            }
        };
        static void reset()
        {
            auto& h = H::instance();
            h.m_state.clear(); h.m_observed_owner = nullptr;
            owner.fill(0); sentinel.fill(0);
            put(owner.data(), 0, h.m_base + 0x3d277a0);
            put(owner.data(), 0xa8, h.m_base + 0x3d274c8);
            put(owner.data(), 0x30, uint64_t{99});
            put(owner.data(), 0x268, reinterpret_cast<uintptr_t>(sentinel.data()));
            owner[0x3c] = 0xff; owner[0x3d] = 3;
            peer = reinterpret_cast<uintptr_t>(peer_table.data());
            replacement_peer = peer;
            service = h.m_base + 0x3d06478;
            tag = 77; now = 1000;
            for (auto* ref : {&peer_ref, &route_ref})
            {
                ref->fill(0);
                put(ref->data(), 0, reinterpret_cast<uintptr_t>(ref_table.data()));
                put(ref->data(), 8, LONG{1}); put(ref->data(), 12, LONG{1});
            }
            ref_destroyed = ref_disposed = reenter_probe = 0;
            deliveries = successes = failures = acknowledgments = updates = queues = 0;
            peer_present = true; route_present = fail_in_update = cancel_in_ack = destroy_in_update = reenter_on_success = false;
            H::on_update(owner.data(), nullptr, 0);
        }
        static void begin(float seconds = 20)
        { uint8_t op = 7; H::on_queue(owner.data(), &op, nullptr, nullptr, nullptr, seconds); }
        static void hold()
        {
            Packet p;
            H::on_response(owner.data(), &p.archive);
            check(H::instance().m_state.pending(), "assignment retained");
            check(deliveries == 0 && p.archive.cursor == p.bytes.data() + 6, "input consumed exactly once without dispatch");
            // Packet dies here. A subsequent pump must not retain its pointers.
        }
        static void acknowledge(uint32_t status = 0)
        { Packet p(status); auto* o = owner.data(); H::on_ack(&o, &p.archive); }

        static void run()
        {
            auto& h = H::instance();
            // Reserve address space for relative native vtable identities; commit
            // just two pages of fake data. No executable memory or patched code.
            image = static_cast<uint8_t*>(::VirtualAlloc(nullptr, 0x4000000, MEM_RESERVE, PAGE_NOACCESS));
            check(image != nullptr, "reserve fake image");
            check(::VirtualAlloc(image + 0x3d27000, 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "fake transport table");
            h.m_base = reinterpret_cast<uintptr_t>(image);
            put(image + 0x3d274c8, 0x58, reinterpret_cast<uintptr_t>(&lookup_peer));
            peer_table[0] = reinterpret_cast<uintptr_t>(&key);
            ref_table = {reinterpret_cast<uintptr_t>(&destroy_ref), reinterpret_cast<uintptr_t>(&dispose_ref)};
            h.m_get_service = &get_service; h.m_lookup_route = &lookup_route; h.m_clock = &clock;
            h.m_ready = true;
            auto bind = [&](RecoverySite site, auto fn) { h.m_originals[static_cast<size_t>(site)] = reinterpret_cast<uint64_t>(fn); };
            bind(RecoverySite::queue, &queue); bind(RecoverySite::response, &response);
            bind(RecoverySite::ack, &ack); bind(RecoverySite::update, &update);
            for (auto site : {RecoverySite::reset, RecoverySite::finish, RecoverySite::destroy, RecoverySite::end_message}) bind(site, &no_op);
            for (auto site : {RecoverySite::start, RecoverySite::request, RecoverySite::reset_message}) bind(site, &no_pair);

            reset(); begin(); hold(); acknowledge(); route_present = true;
            H::on_update(owner.data(), nullptr, 0.016f);
            H::on_update(owner.data(), nullptr, 0.016f);
            check(deliveries == 1 && successes == 1 && acknowledgments == 1, "late route recovers exactly once; ack independent");
            check(*reinterpret_cast<LONG*>(peer_ref.data() + 8) == 1 && *reinterpret_cast<LONG*>(route_ref.data() + 8) == 1,
                "temporary peer and route references released");

            reset(); begin(); acknowledge(); hold(); route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(successes == 1, "ack before assignment also supported");

            reset(); begin(); route_present = true; { Packet p; H::on_response(owner.data(), &p.archive); }
            check(deliveries == 1 && successes == 1, "already-ready path stays native");

            reset(); begin(); hold(); now = 20'999;
            { Packet duplicate; H::on_response(owner.data(), &duplicate.archive); }
            check(deliveries == 0, "duplicate does not complete early");
            now = 21'000; H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 1 && failures == 1, "duplicate does not extend request deadline");

            reset(); begin(1); hold(); now = 2000; route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(successes == 1, "deadline stops deferral but preserves native success");

            reset(); begin(); hold(); acknowledge(11); route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0 && acknowledgments == 1, "negative ack cancels retained assignment");

            reset(); begin(); hold(); cancel_in_ack = true; acknowledge(); route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0, "reentrant ack cancellation wins");

            for (auto op : {uint8_t{9}, uint8_t{10}})
            {
                reset(); begin(); hold();
                H::on_queue(owner.data(), &op, nullptr, nullptr, nullptr, 20);
                route_present = true; H::on_update(owner.data(), nullptr, 0);
                check(deliveries == 0 && !h.m_state.active(), "explicit watch cancel/end cancels recovery");
            }
            for (auto op : {uint8_t{6}, uint8_t{7}, uint8_t{4}})
            {
                reset(); begin(); hold();
                H::on_queue(owner.data(), &op, nullptr, nullptr, nullptr, 20);
                check(h.m_state.pending(), "unrelated work cannot silently discard a consumed assignment");
                H::on_update(owner.data(), nullptr, 0);
                check(deliveries == 1 && failures == 1, "competing queue work resumes native behavior at owner update");
            }
            for (auto fn : {&H::on_reset, &H::on_finish, &H::on_destroy, &H::on_end_message})
            {
                reset(); begin(); hold(); fn(owner.data()); route_present = true;
                H::on_update(owner.data(), nullptr, 0);
                check(deliveries == 0, "reset/finish/destruction/end invalidate before native callback");
            }
            for (auto fn : {&H::on_start, &H::on_request, &H::on_reset_message})
            {
                reset(); begin(); hold(); fn(owner.data(), nullptr); route_present = true;
                H::on_update(owner.data(), nullptr, 0);
                check(deliveries == 0, "new session/request/incoming reset cancels recovery");
            }

            reset(); begin(); hold(); fail_in_update = true; route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0, "native failure runs before recovery");
            reset(); begin(); hold(); destroy_in_update = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0, "destruction inside update prevents post-call access");

            reset(); begin(); hold(); tag = 78; route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0, "changed route identity cannot receive old assignment");
            reset(); begin(); hold(); put(owner.data(), 0x30, uint64_t{100}); route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0, "room generation mismatch rejects replay");
            reset(); begin(); hold(); peer_present = false;
            H::on_update(owner.data(), nullptr, 0);
            check(failures == 1, "departed peer reaches native failure");

            reset(); begin(); hold();
            std::thread moved([] { H::on_update(owner.data(), nullptr, 0); }); moved.join();
            check(deliveries == 0 && h.m_state.pending(), "foreign thread cannot replay or discard consumed packet");
            H::on_update(owner.data(), nullptr, 0);
            check(failures == 1, "original owner thread resolves unsupported migration through native behavior");

            reset(); begin(); reenter_probe = 1;
            { Packet p; H::on_response(owner.data(), &p.archive); }
            check(deliveries == 0 && !h.m_state.active(), "reentrant probe cancellation blocks incoming replay");
            reset(); begin(); reenter_probe = 2;
            { Packet p; H::on_response(owner.data(), &p.archive); }
            check(deliveries == 0 && h.m_state.active() && !h.m_state.pending(), "probe cannot attach stale response to replacement attempt");
            reset(); begin(); hold(); reenter_probe = 2; route_present = true;
            H::on_update(owner.data(), nullptr, 0);
            check(deliveries == 0 && h.m_state.active(), "pump cannot consume replacement generation during probe");

            reset(); begin(); hold(); H::on_finish(owner.data()); reset(); begin(); hold(); route_present = true;
            reenter_on_success = true; H::on_update(owner.data(), nullptr, 0);
            check(successes == 1 && !h.m_state.active(), "re-entry and reentrant completion leave no retained state");

            reset(); begin(); { Packet p; p.array.size = 5; H::on_response(owner.data(), &p.archive); }
            check(deliveries == 1 && !h.m_state.pending(), "unqualified archive falls through unchanged");
            reset(); begin(); { Packet p(11); H::on_response(owner.data(), &p.archive); }
            check(deliveries == 1 && !h.m_state.active(), "explicit assignment rejection stays native");

            reset();
            for (bool atomic : {false, true})
            {
                put(peer_ref.data(), 8, LONG{1}); put(peer_ref.data(), 12, LONG{1});
                H::SharedPair temporary{&peer, peer_ref.data()};
                H::release(temporary, atomic);
                check(!temporary.control && !temporary.object, "ref pair reset after final release");
            }
            check(ref_destroyed == 2 && ref_disposed == 2, "strong/weak final release ABI on both native refcount modes");

            h.m_ready = false; h.m_state.clear(); h.m_observed_owner = nullptr;
            ::VirtualFree(image, 0, MEM_RELEASE);
            std::puts("PASS: offline native-adapter watch recovery scenarios");
        }
    };
}
int main() { DotVanisher::WatchRecoveryOfflineTest::run(); }
