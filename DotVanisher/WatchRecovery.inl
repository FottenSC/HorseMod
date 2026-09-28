// Included inside namespace DotVanisher after binary verification/read helpers.
// Native evidence: docs/investigations/dotvanisher-general-fix-2026-09-13.md.
class WatchRecoveryHook
{
    using Byte = uint8_t;
    using OwnerFn = void(__fastcall*)(Byte*);
    using PairFn = void(__fastcall*)(Byte*, void*);
    using QueueFn = void(__fastcall*)(Byte*, Byte*, void*, void*, void*, float);
    using AckFn = bool(__fastcall*)(Byte**, void*);
    using UpdateFn = void(__fastcall*)(Byte*, void*, float);
    struct SharedPair { void* object{}; Byte* control{}; };
    struct RouteKey { uintptr_t vtable{}; uint64_t tag{}; };
    struct Probe { bool valid{}, present{}, route{}; WatchAssignment assignment{}; };
    using GetService = void*(__fastcall*)();
    using LookupRoute = SharedPair*(__fastcall*)(void*, SharedPair*, const RouteKey*);
    friend struct WatchRecoveryOfflineTest;

    // The original response handler only accesses +0x14 and +0x18, consumes six
    // bytes, and never releases its archive. Dispatch owns the incoming archive.
    // Replay calls the handler, NOT Dispatch or ReleaseHostSysEventPacketArchive.
    struct BorrowedArchive
    {
        uintptr_t vtable{};
        uint64_t tag{};
        uint32_t state{}, mode{1};
        Byte* cursor{};
        void* byte_array{};
        void* byte_array_ref{};
    };
    static_assert(offsetof(BorrowedArchive, mode) == 0x14);
    static_assert(offsetof(BorrowedArchive, cursor) == 0x18);
    static_assert(sizeof(BorrowedArchive) == 0x30);

public:
    static WatchRecoveryHook& instance()
    {
        // Detours and trampolines stay valid through process teardown, including
        // UE4SS mod removal. No native objects/references are retained here.
        static auto* self = new WatchRecoveryHook;
        return *self;
    }

    bool install()
    {
        if (m_install_attempted.exchange(true)) return m_ready.load();
        const auto module = ::GetModuleHandleW(L"SoulcaliburVI.exe");
        if (!module) return false;
        m_base = reinterpret_cast<uintptr_t>(module);
        m_get_service = reinterpret_cast<GetService>(m_base + 0x2e08a70);
        m_lookup_route = reinterpret_cast<LookupRoute>(m_base + 0x2e06520);
        std::array<wchar_t, 32768> path{};
        const auto length = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size() || !verify_exe_file(path.data())) return false;
        for (const auto& site : recovery_sites)
        {
            std::array<Byte, 32> bytes{};
            if (!read_bytes_seh(reinterpret_cast<void*>(m_base + site.rva), bytes.data(), bytes.size()) ||
                bytes != site.bytes)
            {
                RC::Output::send<RC::LogLevel::Error>(STR("[DotVanisher] recovery site mismatch at RVA 0x{:X}; recovery disabled\n"), site.rva);
                return false;
            }
        }
        for (const auto& site : recovery_dependencies)
        {
            std::array<Byte, 32> bytes{};
            if (!read_bytes_seh(reinterpret_cast<void*>(m_base + site.rva), bytes.data(), bytes.size()) || bytes != site.bytes)
            {
                RC::Output::send<RC::LogLevel::Error>(STR("[DotVanisher] recovery dependency mismatch at RVA 0x{:X}; recovery disabled\n"), site.rva);
                return false;
            }
        }
        HMODULE pinned{};
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&on_response), &pinned)) return false;
        const std::array<uint64_t, recovery_sites.size()> callbacks{
            reinterpret_cast<uint64_t>(&on_queue), reinterpret_cast<uint64_t>(&on_response),
            reinterpret_cast<uint64_t>(&on_ack), reinterpret_cast<uint64_t>(&on_update),
            reinterpret_cast<uint64_t>(&on_reset), reinterpret_cast<uint64_t>(&on_start),
            reinterpret_cast<uint64_t>(&on_finish), reinterpret_cast<uint64_t>(&on_destroy),
            reinterpret_cast<uint64_t>(&on_end_message), reinterpret_cast<uint64_t>(&on_reset_message),
            reinterpret_cast<uint64_t>(&on_request)};
        for (size_t i = 0; i < recovery_sites.size(); ++i)
        {
            m_hooks[i] = std::make_unique<PLH::x64Detour>(m_base + recovery_sites[i].rva, callbacks[i], &m_originals[i]);
            if (!m_hooks[i]->hook())
            {
                // Already installed entries remain pass-through. Do not unhook or
                // free a trampoline that another thread could be executing.
                RC::Output::send<RC::LogLevel::Error>(STR("[DotVanisher] recovery hook installation failed at RVA 0x{:X}; all recovery behavior disabled\n"), recovery_sites[i].rva);
                return false;
            }
        }
        m_ready.store(true);
        RC::Output::send<RC::LogLevel::Default>(STR("[DotVanisher] bounded watch-assignment recovery installed; original request deadline retained; restart required to disable\n"));
        return true;
    }

private:
    template<class Fn> Fn original(RecoverySite site) const
    {
        return reinterpret_cast<Fn>(m_originals[static_cast<size_t>(site)]);
    }
    template<class T> static bool read(const void* address, T& out)
    {
        return read_bytes_seh(address, reinterpret_cast<Byte*>(&out), sizeof(out));
    }
    static bool write_cursor(void* packet, Byte* cursor) noexcept
    {
        __try { *reinterpret_cast<Byte**>(static_cast<Byte*>(packet) + 0x18) = cursor; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool payload(void* packet, size_t size, Byte*& cursor)
    {
        // Validate against the owning byte array, not just readable address space.
        uint32_t mode{}, count{};
        uintptr_t array{}, data{};
        return packet && read(static_cast<Byte*>(packet) + 0x14, mode) && mode == 1 &&
            read(static_cast<Byte*>(packet) + 0x18, cursor) && cursor &&
            read(static_cast<Byte*>(packet) + 0x20, array) && array &&
            read(reinterpret_cast<void*>(array), data) &&
            read(reinterpret_cast<void*>(array + 8), count) &&
            reinterpret_cast<uintptr_t>(cursor) >= data && size <= count &&
            reinterpret_cast<uintptr_t>(cursor) - data <= count - size;
    }
    bool snapshot(Byte* owner, WatchAttemptIdentity& out)
    {
        uintptr_t vtable{};
        out.owner = reinterpret_cast<uintptr_t>(owner);
        out.thread = ::GetCurrentThreadId();
        return owner && read(owner, vtable) && vtable == m_base + 0x3d277a0 &&
            read(owner + 0x30, out.session) && read(owner + 0x268, out.request_list) && out.request_list &&
            read(owner + 0x3c, out.role) && read(owner + 0x3d, out.state) && read(owner + 0x3e, out.substate) &&
            out.state == 3 && out.substate != 7 && out.substate != 9;
    }
    static void release(SharedPair& pair, bool atomic)
    {
        if (!pair.control) return;
        auto* strong = reinterpret_cast<volatile LONG*>(pair.control + 8);
        auto* weak = reinterpret_cast<volatile LONG*>(pair.control + 12);
        if ((atomic ? ::InterlockedDecrement(strong) : --*strong) == 0)
        {
            auto** table = *reinterpret_cast<void***>(pair.control);
            reinterpret_cast<void(__fastcall*)(void*)>(table[0])(pair.control);
            if ((atomic ? ::InterlockedDecrement(weak) : --*weak) == 0)
                reinterpret_cast<void(__fastcall*)(void*, uint32_t)>(table[1])(pair.control, 1);
        }
        pair = {};
    }
    Probe probe(Byte* owner, uint16_t id)
    {
        Probe result{};
        uintptr_t transport_table{};
        if (!read(owner + 0xa8, transport_table) || transport_table != m_base + 0x3d274c8) return result;
        using LookupPeer = SharedPair*(__fastcall*)(void*, SharedPair*, uint16_t);
        auto lookup = reinterpret_cast<LookupPeer>(*reinterpret_cast<uintptr_t*>(transport_table + 0x58));
        SharedPair peer{};
        lookup(owner + 0xa8, &peer, id);
        result.valid = true;
        if (!peer.object) { release(peer, false); return result; }
        result.present = true;
        result.assignment.peer_id = id;
        result.assignment.peer = reinterpret_cast<uintptr_t>(peer.object);
        using KeyFn = RouteKey*(__fastcall*)(void*, RouteKey*);
        auto** peer_table = *reinterpret_cast<void***>(peer.object);
        RouteKey key{};
        const auto* returned_key = reinterpret_cast<KeyFn>(peer_table[0])(peer.object, &key);
        // GetKey returns a value key; 142E01570 only resets its vtable, no refs.
        result.assignment.route_tag = returned_key->tag;
        void* service = m_get_service();
        uintptr_t service_table{};
        if (!service || !read(service, service_table) || service_table != m_base + 0x3d06478)
            result.valid = false;
        else
        {
            SharedPair route{};
            m_lookup_route(service, &route, returned_key);
            result.route = route.object != nullptr;
            release(route, true);
        }
        release(peer, false);
        return result;
    }
    void clear(Byte* owner)
    {
        std::lock_guard lock(m_mutex);
        if (m_state.identity().owner == reinterpret_cast<uintptr_t>(owner)) m_state.clear();
    }
    void lifecycle(Byte* owner)
    {
        std::lock_guard lock(m_mutex);
        clear(owner);
        ++m_lifecycle_generation;
        if (m_observed_owner == owner) { m_observed_owner = nullptr; m_observed_thread = 0; }
    }

    static void __fastcall on_queue(Byte* owner, Byte* opcode, void* start, void* response, void* timeout, float seconds)
    {
        auto& self = instance();
        if (self.m_ready.load())
        {
            std::lock_guard lock(self.m_mutex);
            const bool overlap = self.m_state.active();
            Byte op{};
            const bool valid_opcode = read(opcode, op);
            if (self.m_state.identity().owner == reinterpret_cast<uintptr_t>(owner))
            {
                if (valid_opcode && (op == 9 || op == 10)) self.m_state.clear();
                else if (self.m_state.pending()) self.m_state.require_native();
                else self.m_state.clear();
            }
            int64_t count{};
            WatchAttemptIdentity identity{};
            if (!overlap && self.m_observed_owner == owner && self.m_observed_thread == ::GetCurrentThreadId() &&
                valid_opcode && op == 7 && read(owner + 0x270, count) && count == 0 &&
                std::isfinite(seconds) && seconds > 0 && self.snapshot(owner, identity))
            {
                const auto budget = static_cast<uint64_t>((std::min)(seconds * 1000.0f, 20'000.0f));
                self.m_state.begin(identity, self.m_clock(), budget);
            }
        }
        self.original<QueueFn>(RecoverySite::queue)(owner, opcode, start, response, timeout, seconds);
    }
    static void __fastcall on_request(Byte* owner, void* route)
    {
        // Invalidates even when the native pre-queue peer lookup fails.
        auto& self = instance();
        self.clear(owner);
        self.original<PairFn>(RecoverySite::request)(owner, route);
    }
    static bool __fastcall on_ack(Byte** context, void* packet)
    {
        auto& self = instance();
        if (self.m_ready.load())
        {
            std::lock_guard lock(self.m_mutex);
            Byte* owner{}; Byte* cursor{}; uint32_t status{};
            if (read(context, owner) && self.m_state.identity().owner == reinterpret_cast<uintptr_t>(owner))
            {
                if (self.m_state.identity().thread != ::GetCurrentThreadId() ||
                    !payload(packet, 4, cursor) || !read(cursor, status)) self.m_state.require_native();
                else self.m_state.acknowledge(status);
            }
        }
        // Always deliver the native acknowledgment, including failures.
        return self.original<AckFn>(RecoverySite::ack)(context, packet);
    }
    bool defer(Byte* owner, void* packet)
    {
        std::lock_guard lock(m_mutex);
        if (!m_state.active()) return false;
        WatchAttemptIdentity identity{};
        if (!snapshot(owner, identity) || !m_state.matches(identity)) { clear(owner); return false; }
        Byte* cursor{}; uint32_t status{}; uint16_t id{};
        if (!payload(packet, 6, cursor) || !read(cursor, status) || !read(cursor + 4, id) || status != 0 || id == 0xffff)
        { m_state.clear(); return false; }
        const auto generation = m_state.generation();
        const auto found = probe(owner, id);
        WatchAttemptIdentity after_probe{};
        if (generation != m_state.generation() || !snapshot(owner, after_probe) || !m_state.matches(after_probe))
        {
            // A reentrant native lookup/release invalidated this response. Do not
            // invoke the handler on a disposed owner or a replacement attempt.
            write_cursor(packet, cursor + 6);
            return true;
        }
        if (!found.valid || !found.present) { m_state.clear(); return false; }
        if (m_state.pending() && m_state.assignment() != found.assignment)
        { m_state.clear(); return false; }
        if (found.route) { m_state.clear(); return false; }
        const auto decision = m_state.hold(found.assignment, m_clock());
        if (decision == WatchRecoveryState::Hold::rejected || !write_cursor(packet, cursor + 6))
        { m_state.clear(); return false; }
        if (decision == WatchRecoveryState::Hold::admitted)
            RC::Output::send<RC::LogLevel::Default>(STR("[DotVanisher] watch assignment deferred for peer {}; waiting for native route within original deadline\n"), id);
        return true;
    }
    static void __fastcall on_response(Byte* owner, void* packet)
    {
        auto& self = instance();
        if (!self.m_ready.load() || !self.defer(owner, packet))
            self.original<PairFn>(RecoverySite::response)(owner, packet);
    }
    void pump(Byte* owner)
    {
        std::lock_guard lock(m_mutex);
        if (!m_state.active() || m_state.identity().owner != reinterpret_cast<uintptr_t>(owner)) return;
        WatchAttemptIdentity identity{};
        if (m_observed_owner != owner || !snapshot(owner, identity))
        { m_state.clear(); return; }
        const bool changed_thread = identity.thread != m_state.identity().thread;
        identity.thread = m_state.identity().thread;
        if (!m_state.matches(identity)) { m_state.clear(); return; }
        // Keep value-only pending work for the original owner thread or a terminal
        // lifecycle callback. A foreign task thread is not authority to replay it.
        if (changed_thread) { m_state.require_native(); return; }
        const auto now = m_clock();
        if (!m_state.pending())
        {
            if (m_state.expired(now)) m_state.clear();
            return;
        }
        const auto held = m_state.assignment();
        const auto generation = m_state.generation();
        const auto found = probe(owner, held.peer_id);
        WatchAttemptIdentity after_probe{};
        if (generation != m_state.generation() || !snapshot(owner, after_probe) || !m_state.matches(after_probe)) return;
        if (!found.valid || (found.present && found.assignment != held))
        { m_state.clear(); return; }
        if (found.present && !found.route && !m_state.expired(now) && !m_state.native_required()) return;
        // Missing peer uses the original failure branch; a changed peer never
        // receives the old assignment. Deadline stops deferral, not native success.
        m_state.consume();
        std::array<Byte, 6> decoded{};
        std::memcpy(decoded.data() + 4, &held.peer_id, sizeof(held.peer_id));
        BorrowedArchive packet{};
        packet.cursor = decoded.data();
        RC::Output::send<RC::LogLevel::Default>(STR("[DotVanisher] resuming native watch assignment for peer {} (route_present={})\n"), held.peer_id, found.route);
        // Recursive lock permits same-thread native callbacks; lifecycle hooks on
        // other threads invalidate before teardown, after this bounded call exits.
        original<PairFn>(RecoverySite::response)(owner, &packet);
    }
    static void __fastcall on_update(Byte* owner, void* task, float delta)
    {
        auto& self = instance();
        uint64_t lifecycle_generation{};
        if (self.m_ready.load())
        {
            std::lock_guard lock(self.m_mutex);
            self.m_observed_owner = owner;
            self.m_observed_thread = ::GetCurrentThreadId();
            lifecycle_generation = self.m_lifecycle_generation;
        }
        // Native request expiry, network failure, and peer-state repair run first.
        self.original<UpdateFn>(RecoverySite::update)(owner, task, delta);
        if (self.m_ready.load())
        {
            std::lock_guard lock(self.m_mutex);
            if (lifecycle_generation == self.m_lifecycle_generation) self.pump(owner);
        }
    }
    static void __fastcall on_reset(Byte* owner)
    { auto& s = instance(); s.clear(owner); s.original<OwnerFn>(RecoverySite::reset)(owner); }
    static void __fastcall on_start(Byte* owner, void* name)
    { auto& s = instance(); s.lifecycle(owner); s.original<PairFn>(RecoverySite::start)(owner, name); }
    static void __fastcall on_finish(Byte* owner)
    { auto& s = instance(); s.lifecycle(owner); s.original<OwnerFn>(RecoverySite::finish)(owner); }
    static void __fastcall on_destroy(Byte* owner)
    { auto& s = instance(); s.lifecycle(owner); s.original<OwnerFn>(RecoverySite::destroy)(owner); }
    static void __fastcall on_end_message(Byte* owner)
    { auto& s = instance(); s.clear(owner); s.original<OwnerFn>(RecoverySite::end_message)(owner); }
    static void __fastcall on_reset_message(Byte* owner, void* packet)
    { auto& s = instance(); s.clear(owner); s.original<PairFn>(RecoverySite::reset_message)(owner, packet); }

    std::array<std::unique_ptr<PLH::x64Detour>, recovery_sites.size()> m_hooks{};
    std::array<uint64_t, recovery_sites.size()> m_originals{};
    std::atomic<bool> m_install_attempted{}, m_ready{};
    uintptr_t m_base{};
    GetService m_get_service{};
    LookupRoute m_lookup_route{};
    ULONGLONG(WINAPI* m_clock)(){&::GetTickCount64};
    std::recursive_mutex m_mutex;
    WatchRecoveryState m_state;
    Byte* m_observed_owner{}; // Compared only; not dereferenced across callbacks.
    DWORD m_observed_thread{};
    uint64_t m_lifecycle_generation{};
};
