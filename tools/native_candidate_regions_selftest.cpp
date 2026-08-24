#include "deterministic/CandidateCheckpoint.hpp"
#include "deterministic/CallbackTopology.hpp"
#include "deterministic/CandidateGameStateAdapter.hpp"
#include "deterministic/InputTimeline.hpp"
#include "deterministic/NativeCandidateRegions.hpp"
#include "deterministic/HgCpuStream.hpp"
#include "deterministic/HgCpuCoverageProbe.hpp"
#include "deterministic/MoveDispatchState.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/Schema.hpp"
#include "deterministic/SimulationSession.hpp"
#include "deterministic/SnapshotStore.hpp"
#include "deterministic/StageBreakListenerDiagnostics.hpp"
#include "deterministic/StageWindGraphTransaction.hpp"
#include "deterministic/StageWindTopology.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

using namespace Horse::Deterministic;

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeNativeMemory final : public INativeMemory
{
public:
    explicit FakeNativeMemory(std::uintptr_t base, std::size_t size)
        : base_(base), bytes_(size)
    {
    }

    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        const auto offset = resolve(address, destination.size());
        if (offset == invalid) return false;
        std::memcpy(destination.data(), bytes_.data() + offset, destination.size());
        return true;
    }

    bool Write(std::uintptr_t address, std::span<const std::byte> source) noexcept override
    {
        ++write_calls_;
        if (fail_write_call_ != 0 && write_calls_ == fail_write_call_)
            return false;
        const auto offset = resolve(address, source.size());
        if (offset == invalid) return false;
        std::memcpy(bytes_.data() + offset, source.data(), source.size());
        if (corrupt_after_write_call_ == write_calls_)
            bytes_.at(resolve(corrupt_address_, 1)) = corrupt_value_;
        return true;
    }

    template <typename T>
    void Set(std::uintptr_t address, const T& value)
    {
        SetBytes(address, std::as_bytes(std::span{&value, 1}));
    }

    void SetBytes(std::uintptr_t address, std::span<const std::byte> source)
    {
        const auto offset = resolve(address, source.size());
        if (offset == invalid) throw std::runtime_error("fake memory address out of range");
        std::memcpy(bytes_.data() + offset, source.data(), source.size());
    }

    void Fill(std::uintptr_t address, std::size_t count, std::byte value)
    {
        const auto offset = resolve(address, count);
        if (offset == invalid) throw std::runtime_error("fake memory fill out of range");
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count, value);
    }

    std::byte Get(std::uintptr_t address) const
    {
        return bytes_.at(resolve(address, 1));
    }

    void FailWrite(std::size_t call) noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = call;
        corrupt_after_write_call_ = 0;
    }

    void AllowWrites() noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = 0;
        corrupt_after_write_call_ = 0;
    }

    void CorruptAfterWrite(
        std::size_t call, std::uintptr_t address, std::byte value) noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = 0;
        corrupt_after_write_call_ = call;
        corrupt_address_ = address;
        corrupt_value_ = value;
    }

    const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

private:
    std::size_t resolve(std::uintptr_t address, std::size_t size) const noexcept
    {
        if (address < base_) return invalid;
        const auto offset = static_cast<std::size_t>(address - base_);
        return offset <= bytes_.size() && size <= bytes_.size() - offset
            ? offset : invalid;
    }

    static constexpr std::size_t invalid = static_cast<std::size_t>(-1);
    std::uintptr_t base_{};
    std::vector<std::byte> bytes_;
    std::size_t write_calls_{};
    std::size_t fail_write_call_{};
    std::size_t corrupt_after_write_call_{};
    std::uintptr_t corrupt_address_{};
    std::byte corrupt_value_{};
};

struct Fixture
{
    static constexpr std::uintptr_t memory_base = 0x10000000;
    static constexpr std::uintptr_t image_base = 0x140000000;

    Fixture()
        : memory(memory_base, 0x20000), regions(memory)
    {
        addresses.image_base = image_base;
        addresses.move_dispatch = memory_base + 0x1000;
        addresses.pump_state = memory_base + 0x3000;
        addresses.scheduler_base = memory_base + 0x4000;
        addresses.move_command_base = memory_base + 0x7000;
        addresses.slot_param_base = memory_base + 0xF000;
        addresses.lcg_rng = memory_base + 0x10000;
        addresses.lfsr_rng = memory_base + 0x10100;
        addresses.xorshift_rng = memory_base + 0x10200;
        addresses.wind_rng = memory_base + 0x10300;
        addresses.fighter_roots = {
            memory_base + 0x12000, memory_base + 0x13000};
        addresses.session_generation = 11;
        addresses.round_generation = 7;
        initialize();
    }

    void initialize()
    {
        memory.Set(addresses.lcg_rng, std::uint32_t{0x12345678});
        for (std::size_t index = 0; index < 25; ++index)
            memory.Set(addresses.lfsr_rng + index * 4,
                static_cast<std::uint32_t>(0x1000 + index));
        memory.Set(addresses.lfsr_rng + 0x64, std::uint32_t{7});
        for (std::size_t index = 0; index < 3; ++index)
            memory.Set(addresses.xorshift_rng + index * 4,
                static_cast<std::uint32_t>(0x2000 + index));
        for (std::size_t index = 0; index < 6; ++index)
            memory.Set(addresses.wind_rng + index * 4,
                static_cast<std::uint32_t>(0x3000 + index));

        event_masks = memory_base + 0x2000;
        memory.Set(addresses.move_dispatch + 0x4A8, event_masks);
        memory.Set(addresses.move_dispatch + 0x4B0, std::int32_t{2});
        memory.Set(addresses.move_dispatch + 0x4B4, std::int32_t{2});
        memory.Set(event_masks, std::uint64_t{0x1111222233334444});
        memory.Set(event_masks + 8, std::uint64_t{0xAAAABBBBCCCCDDDD});

        constexpr std::size_t pump_ids[]{0, 8, 0x10, 0x18, 0x40, 0x48};
        for (std::size_t i = 0; i < std::size(pump_ids); ++i)
            memory.Set(addresses.pump_state + pump_ids[i], memory_base + 0x11000 + i * 0x100);
        memory.Fill(addresses.pump_state + 0x20, 0x1C, std::byte{0x21});
        memory.Fill(addresses.pump_state + 0x50, 0x1C, std::byte{0x22});
        memory.Fill(addresses.pump_state + 0x70, 0x18, std::byte{0});
        memory.Set(addresses.pump_state + 0x70, std::int32_t{2});
        memory.Set(addresses.pump_state + 0x7C, std::uint32_t{1});

        for (std::size_t lane = 0; lane < 2; ++lane)
        {
            const auto scheduler = addresses.scheduler_base + lane * 0x60;
            const auto subvm = memory_base + 0x5000 + lane * 0x1000;
            const auto fighter = memory_base + 0x12000 + lane * 0x1000;
            memory.Set(scheduler, image_base + std::uintptr_t{0x3E80000});
            memory.Set(scheduler + 0x10, fighter);
            memory.Set(scheduler + 0x50, subvm);
            memory.Fill(scheduler + 0x08, 4, std::byte{static_cast<unsigned char>(0x20 + lane)});
            memory.Fill(scheduler + 0x30, 0x20, std::byte{static_cast<unsigned char>(0x24 + lane)});
            memory.Set(scheduler + 0x58, std::uint32_t{static_cast<std::uint32_t>(lane)});
            memory.Set(subvm, image_base + std::uintptr_t{0x3E863D0});
            memory.Set(subvm + 0x10, fighter);
            memory.Set(subvm + 0x18, memory_base + 0x14000 - lane * 0x1000);
            memory.Set(subvm + 0x60, scheduler);
            memory.Fill(subvm + 0x08, 4, std::byte{static_cast<unsigned char>(0x30 + lane)});
            memory.Fill(subvm + 0x20, 0x3C, std::byte{static_cast<unsigned char>(0x40 + lane)});

            const auto command = addresses.move_command_base + lane * 0x3038;
            memory.Fill(command, 0x3038, std::byte{static_cast<unsigned char>(0x50 + lane)});
            constexpr std::size_t ids[]{
                0x0008,0x0010,0x0028,0x0030,0x0340,0x0BA8,0x0BB0,0x0BB8,
                0x0BC0,0x0BC8,0x0BD0,0x0BD8,0x0BE0,0x0CC8,0x0CD8,0x0CE0,0x1998};
            for (std::size_t i = 0; i < std::size(ids); ++i)
                memory.Set(command + ids[i], memory_base + 0x1000 + lane * 0x100 + i * 8);

            memory.Fill(
                addresses.slot_param_base + lane * 0x2C,
                0x2C, std::byte{static_cast<unsigned char>(0x70 + lane)});
        }
    }

    FakeNativeMemory memory;
    NativeCandidateAddresses addresses{};
    NativeCandidateRegions regions;
    std::uintptr_t event_masks{};
};

bool contains_qword(const std::vector<std::byte>& bytes, std::uintptr_t value)
{
    std::array<std::byte, sizeof(value)> needle{};
    std::memcpy(needle.data(), &value, sizeof(value));
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

std::array<std::byte, 32> hgcpu_payload{};
bool hgcpu_read_matched = false;
UcrtRandBroker candidate_ucrt_broker{};
constexpr std::uint32_t candidate_thread_id = 77;

UcrtRandBrokerImage candidate_ucrt_image(std::uint32_t state = 0x12345678u)
{
    return {
        Schema::Sc6UcrtLayout::algorithm_version,
        Schema::Sc6UcrtLayout::allowlist_version,
        state,
        0,
        true,
    };
}

void prepare_candidate_ucrt_broker()
{
    candidate_ucrt_broker.Start();
    candidate_ucrt_broker.HandleSrand(candidate_thread_id,
        Schema::Sc6UcrtLayout::rng_init_srand_return_rva,
        0x12345678u, nullptr);
    candidate_ucrt_broker.AcquireOwnership(candidate_thread_id);
}

void* __fastcall fake_hgcpu_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    write(shim, hgcpu_payload.data(), hgcpu_payload.size());
    return shim;
}

void* __fastcall fake_hgcpu_overflow_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    std::byte value{};
    write(shim, &value, hgcpu_stream_capacity + 1);
    return shim;
}

void* __fastcall fake_hgcpu_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    std::array<std::byte, 32> actual{};
    read(shim, actual.data(), actual.size());
    hgcpu_read_matched = actual == hgcpu_payload;
    hgcpu_payload = actual;
    return shim;
}

bool fail_next_hgcpu_read = false;

void* __fastcall flaky_hgcpu_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    if (fail_next_hgcpu_read)
    {
        fail_next_hgcpu_read = false;
        read(shim, hgcpu_payload.data(), hgcpu_payload.size() / 2);
        return shim;
    }
    return fake_hgcpu_reader(shim);
}

void* __fastcall fake_hgcpu_short_reader(HgCpuStreamShim* shim)
{
    using ReadFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto read = reinterpret_cast<ReadFn>(vtable[6]);
    std::array<std::byte, 16> ignored{};
    read(shim, ignored.data(), ignored.size());
    return shim;
}

std::array<std::byte, 32> coverage_source{};

void* __fastcall fake_coverage_writer(HgCpuStreamShim* shim)
{
    using WriteFn = std::int64_t (__fastcall*)(HgCpuStreamShim*, void*, std::size_t);
    auto** vtable = *reinterpret_cast<void***>(shim);
    auto write = reinterpret_cast<WriteFn>(vtable[5]);
    write(shim, coverage_source.data(), coverage_source.size());
    return shim;
}

class DirectNativeMemory final : public INativeMemory
{
public:
    bool Read(std::uintptr_t address, std::span<std::byte> destination) noexcept override
    {
        if (address == 0 || destination.empty()) return false;
        std::memcpy(destination.data(), reinterpret_cast<const void*>(address), destination.size());
        return true;
    }

    bool Write(std::uintptr_t address, std::span<const std::byte> source) noexcept override
    {
        if (address == 0 || source.empty()) return false;
        std::memcpy(reinterpret_cast<void*>(address), source.data(), source.size());
        return true;
    }
};

HgCpuGenerationContext hgcpu_context()
{
    return {0x231, Schema::snapshot_schema_version, 11, 7, {101, 102}, 201};
}

Status noop_reconcile(void*, FrameCoordinate) noexcept
{
    return Status::success();
}

void test_hgcpu_stream_contract()
{
    for (std::size_t i = 0; i < hgcpu_payload.size(); ++i)
        hgcpu_payload[i] = std::byte{static_cast<unsigned char>(i + 1)};
    HgCpuStreamShim shim;
    HgCpuLocalImage image{};
    std::array<HgCpuWriteSpan, 4> span_storage{};
    HgCpuWriteTrace trace{span_storage};
    const auto context = hgcpu_context();
    expect(shim.Capture(&fake_hgcpu_writer, context, image, &trace).ok(), "capture bounded HgCpu stream");
    expect(image.cursor == hgcpu_payload.size(), "record exact HgCpu cursor");
    expect(image.bytes == std::vector<std::byte>(
        hgcpu_payload.begin(), hgcpu_payload.end()), "capture exact HgCpu bytes");
    expect(trace.count == 1 && !trace.truncated, "record exact HgCpu write span count");
    expect(trace.storage[0].source_address == reinterpret_cast<std::uintptr_t>(hgcpu_payload.data())
        && trace.storage[0].stream_offset == 0
        && trace.storage[0].size == hgcpu_payload.size(), "record local-only HgCpu source mapping");
    hgcpu_read_matched = false;
    expect(shim.Restore(&fake_hgcpu_reader, context, image).ok(), "restore bounded HgCpu stream");
    expect(hgcpu_read_matched, "HgCpu reader receives exact stream");

    auto wrong_generation = context;
    ++wrong_generation.round_generation;
    expect(
        shim.Restore(&fake_hgcpu_reader, wrong_generation, image).code
            == FailureCode::RestorePreflightFailed,
        "HgCpu generation mismatch fails before native reader");
    expect(
        shim.Restore(&fake_hgcpu_short_reader, context, image).code
            == FailureCode::RestoreVerificationFailed,
        "HgCpu cursor mismatch fails verification");
    ++image.checksum;
    expect(
        shim.Restore(&fake_hgcpu_reader, context, image).code
            == FailureCode::RestorePreflightFailed,
        "HgCpu checksum mismatch fails preflight");

    HgCpuLocalImage overflow{};
    expect(
        shim.Capture(&fake_hgcpu_overflow_writer, context, overflow).code
            == FailureCode::CapacityExceeded,
        "HgCpu stream rejects native overflow");
}

void test_candidate_checkpoint_codec()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind checkpoint candidate regions");
    NativeCandidateImage native{};
    expect(fixture.regions.Capture(native).ok(),
        "capture checkpoint candidate regions");

    for (std::size_t i = 0; i < hgcpu_payload.size(); ++i)
        hgcpu_payload[i] = std::byte{static_cast<unsigned char>(0x80 + i)};
    HgCpuStreamShim shim;
    HgCpuLocalImage hgcpu{};
    expect(shim.Capture(&fake_hgcpu_writer, hgcpu_context(), hgcpu).ok(),
        "capture checkpoint HgCpu image");

    CandidateCheckpointImage image{native, hgcpu, candidate_ucrt_image()};
    image.wind.generation = native.round_generation;
    const auto* ring_in_layout = FindStageWindNodeLayout(StageWindNodeKind::RingIn);
    StageWindNodeImage ring_in{};
    ring_in.kind = StageWindNodeKind::RingIn;
    ring_in.semantic_state.assign(
        StageWindSemanticStateSize(*ring_in_layout), std::byte{0xCC});
    ring_in.derived_state.assign(
        StageWindDerivedStateSize(*ring_in_layout), std::byte{0xDD});
    image.wind.nodes.push_back(std::move(ring_in));
    Snapshot snapshot{};
    expect(CandidateCheckpointCodec::Encode({7, 30}, 0x9191, image, snapshot).ok(),
        "encode pointer-free candidate checkpoint");
    expect(!snapshot.bytes.empty()
            && snapshot.canonical_hash != CanonicalHash{},
        "checkpoint contains versioned payload and canonical component hash");

    CandidateCheckpointImage decoded{};
    expect(CandidateCheckpointCodec::Decode(snapshot, decoded).ok(),
        "decode candidate checkpoint");
    expect(decoded.native == native,
        "candidate checkpoint round-trips typed native image");
    expect(decoded.hgcpu.context == hgcpu.context
            && decoded.hgcpu.cursor == hgcpu.cursor
            && decoded.hgcpu.checksum == hgcpu.checksum
            && decoded.hgcpu.bytes == hgcpu.bytes,
        "candidate checkpoint round-trips local HgCpu reconstruction image");
    expect(decoded.ucrt == image.ucrt,
        "candidate checkpoint round-trips value-only UCRT state");
    expect(decoded.wind == image.wind,
        "candidate checkpoint round-trips pointer-free wind state");

    Snapshot corrupted_wind = snapshot;
    const std::array derived_marker{
        std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD},
        std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}, std::byte{0xDD}};
    const auto marker = std::search(corrupted_wind.bytes.begin(),
        corrupted_wind.bytes.end(), derived_marker.begin(), derived_marker.end());
    expect(marker != corrupted_wind.bytes.end(),
        "checkpoint contains local wind-derived payload");
    if (marker != corrupted_wind.bytes.end()) *marker ^= std::byte{1};
    expect(CandidateCheckpointCodec::Decode(corrupted_wind, decoded).code
            == FailureCode::CaptureFailed,
        "checkpoint rejects corrupted non-canonical wind-derived bytes");

    Snapshot corrupted = snapshot;
    corrupted.bytes.back() ^= std::byte{1};
    expect(CandidateCheckpointCodec::Decode(corrupted, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects corrupted local reconstruction bytes");

    Snapshot wrong_generation = snapshot;
    ++wrong_generation.coordinate.generation;
    expect(CandidateCheckpointCodec::Decode(wrong_generation, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects native generation drift");
}

class EmptyStageWindAllocator final : public IStageWindAllocator
{
public:
    std::uintptr_t Allocate(std::size_t) noexcept override { return 0; }
    void Free(std::uintptr_t) noexcept override {}
};

struct CandidateWindFixture
{
    explicit CandidateWindFixture(Fixture& fixture)
        : probe(fixture.memory), transaction(fixture.memory, allocator)
    {
        addresses = {Fixture::image_base, 0x4300000,
            Fixture::memory_base + 0x1F000, 7};
        root = Fixture::memory_base + 0x1E000;
        fixture.memory.Set(addresses.root_pointer, root);
        fixture.memory.Set(root, std::uintptr_t{});
        fixture.memory.Set(root + 0x98, std::uint32_t{});
        fixture.memory.Set(root + 0x9C, std::int32_t{});
        expect(probe.Bind(addresses).ok(), "bind empty candidate wind fixture");
    }

    EmptyStageWindAllocator allocator;
    StageWindTopologyProbe probe;
    StageWindGraphTransaction transaction;
    StageWindTopologyAddresses addresses{};
    std::uintptr_t root{};
};

CandidateAdapterBinding candidate_binding(
    HgCpuExecFn reader, CandidateWindFixture& wind)
{
    prepare_candidate_ucrt_broker();
    CandidateAdapterBinding binding{};
    binding.context = NativeContext{7, 11, {101, 102}, 201};
    binding.hgcpu_context = hgcpu_context();
    binding.hgcpu_writer = &fake_hgcpu_writer;
    binding.hgcpu_reader = reader;
    binding.ucrt_broker = &candidate_ucrt_broker;
    binding.wind_probe = &wind.probe;
    binding.wind_transaction = &wind.transaction;
    binding.wind_addresses = wind.addresses;
    binding.simulation_thread_id = candidate_thread_id;
    binding.reconcile = &noop_reconcile;
    return binding;
}

void test_candidate_adapter_restore_and_outer_undo()
{
    Fixture restored_fixture;
    expect(restored_fixture.regions.Bind(restored_fixture.addresses).ok(),
        "bind candidate adapter restore fixture");
    HgCpuStreamShim restored_hgcpu;
    CandidateGameStateAdapter restored_adapter{
        restored_fixture.regions, restored_hgcpu};
    CandidateWindFixture restored_wind{restored_fixture};
    const auto restored_binding = candidate_binding(&fake_hgcpu_reader, restored_wind);
    expect(restored_adapter.Configure(restored_binding).ok(),
        "configure candidate adapter restore fixture");
    InputTimeline restored_inputs{16};
    SnapshotStore restored_snapshots{
        1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal restored_journal{16, 1024};
    SimulationSession restored_session{restored_adapter, restored_inputs,
        restored_snapshots, restored_journal};
    hgcpu_payload.fill(std::byte{0x21});
    const auto initial_hgcpu = hgcpu_payload;
    const auto initial_memory = restored_fixture.memory.bytes();
    UcrtRandBrokerImage initial_ucrt{};
    expect(candidate_ucrt_broker.Capture(
        candidate_thread_id, initial_ucrt).ok(),
        "capture candidate adapter UCRT baseline");
    expect(restored_session.BindAndCaptureBaseline(
        restored_binding.context, {7, 0}).ok(),
        "capture real candidate adapter baseline");
    restored_fixture.memory.Fill(
        restored_fixture.event_masks, 0x10, std::byte{0x91});
    restored_fixture.memory.Fill(
        restored_fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0x92});
    hgcpu_payload.fill(std::byte{0x93});
    candidate_ucrt_broker.HandleRand(candidate_thread_id,
        Schema::Sc6UcrtLayout::movevm_rand_return_rva, nullptr);
    expect(restored_session.RestoreAndResimulate({7, 0}, {7, 0}).ok(),
        "restore real candidate adapter through SimulationSession transaction");
    expect(restored_fixture.memory.bytes() == initial_memory,
        "candidate adapter restores exact typed native image");
    expect(hgcpu_payload == initial_hgcpu,
        "candidate adapter restores exact HgCpu reconstruction image");
    UcrtRandBrokerImage restored_ucrt{};
    expect(candidate_ucrt_broker.Capture(
            candidate_thread_id, restored_ucrt).ok()
            && restored_ucrt == initial_ucrt,
        "candidate adapter restores exact value-only UCRT image");

    Fixture failed_fixture;
    expect(failed_fixture.regions.Bind(failed_fixture.addresses).ok(),
        "bind candidate adapter failure fixture");
    HgCpuStreamShim failed_hgcpu;
    CandidateGameStateAdapter failed_adapter{failed_fixture.regions, failed_hgcpu};
    CandidateWindFixture failed_wind{failed_fixture};
    const auto failed_binding = candidate_binding(&flaky_hgcpu_reader, failed_wind);
    expect(failed_adapter.Configure(failed_binding).ok(),
        "configure candidate adapter failure fixture");
    InputTimeline failed_inputs{16};
    SnapshotStore failed_snapshots{1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal failed_journal{16, 1024};
    SimulationSession failed_session{
        failed_adapter, failed_inputs, failed_snapshots, failed_journal};
    hgcpu_payload.fill(std::byte{0x31});
    expect(failed_session.BindAndCaptureBaseline(
        failed_binding.context, {7, 0}).ok(),
        "capture candidate adapter failure baseline");
    failed_fixture.memory.Fill(
        failed_fixture.event_masks, 0x10, std::byte{0xA1});
    hgcpu_payload.fill(std::byte{0xA2});
    const auto before_failed_memory = failed_fixture.memory.bytes();
    const auto before_failed_hgcpu = hgcpu_payload;
    fail_next_hgcpu_read = true;
    expect(failed_session.RestoreAndResimulate({7, 0}, {7, 0}).code
            == FailureCode::RestoreWriteFailed,
        "partial HgCpu reader failure reports typed restore failure");
    expect(failed_fixture.memory.bytes() == before_failed_memory
            && hgcpu_payload == before_failed_hgcpu,
        "partial HgCpu reader failure restores exact outer undo image");
}

void test_candidate_adapter_native_failure_undoes_hgcpu()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(),
        "bind candidate adapter native-failure fixture");
    HgCpuStreamShim hgcpu;
    CandidateGameStateAdapter adapter{fixture.regions, hgcpu};
    CandidateWindFixture wind{fixture};
    const auto binding = candidate_binding(&fake_hgcpu_reader, wind);
    expect(adapter.Configure(binding).ok(),
        "configure candidate adapter native-failure fixture");
    InputTimeline inputs{16};
    SnapshotStore snapshots{1024 * 1024, 8, CapacityPolicy::RejectNew};
    PresentationJournal journal{16, 1024};
    SimulationSession session{adapter, inputs, snapshots, journal};
    hgcpu_payload.fill(std::byte{0x41});
    expect(session.BindAndCaptureBaseline(binding.context, {7, 0}).ok(),
        "capture candidate adapter native-failure baseline");
    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xB1});
    fixture.memory.Fill(
        fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0xB2});
    hgcpu_payload.fill(std::byte{0xB3});
    const auto before_memory = fixture.memory.bytes();
    const auto before_hgcpu = hgcpu_payload;
    fixture.memory.FailWrite(5);
    expect(session.RestoreAndResimulate({7, 0}, {7, 0}).code
            == FailureCode::RestoreWriteFailed,
        "native write after HgCpu reconstruction reports typed failure");
    expect(fixture.memory.bytes() == before_memory
            && hgcpu_payload == before_hgcpu,
        "native write failure restores native and HgCpu outer undo image");
}

void test_hgcpu_direct_source_coverage()
{
    DirectNativeMemory memory;
    HgCpuCoverageProbe probe(memory);
    std::array<std::byte, 16> other_target{};
    std::array<HgCpuCoverageTarget, 2> targets{{
        {reinterpret_cast<std::uintptr_t>(coverage_source.data()), coverage_source.size(), 101},
        {reinterpret_cast<std::uintptr_t>(other_target.data()), other_target.size(), 102},
    }};
    expect(probe.Bind(targets).ok(), "bind HgCpu coverage targets");
    HgCpuCoverageSample sample{};
    expect(probe.Observe(&fake_coverage_writer, hgcpu_context(), 1, sample).ok(),
        "capture HgCpu coverage baseline");
    coverage_source[3] = std::byte{0x44};
    other_target[5] = std::byte{0x55};
    expect(probe.Observe(&fake_coverage_writer, hgcpu_context(), 2, sample).ok(),
        "capture HgCpu coverage delta");
    expect(sample.changed_bytes == 2 && sample.directly_sourced_changed_bytes == 1,
        "classify direct and unmapped fighter mutations");
    expect(sample.unmapped_deltas.size() == 1
        && sample.unmapped_deltas[0].target == 1
        && sample.unmapped_deltas[0].offset == 5,
        "report pointer-free unmapped target offset");
}

void test_capture_restore_preserves_exclusions()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind candidate regions");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture candidate image");

    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xE1});
    fixture.memory.Fill(fixture.addresses.pump_state + 0x20, 1, std::byte{0xE2});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x08, 1, std::byte{0xE6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x30, 1, std::byte{0xE7});
    fixture.memory.Set(fixture.addresses.scheduler_base + 0x58, std::uint32_t{1});
    fixture.memory.Fill(Fixture::memory_base + 0x5008, 1, std::byte{0xE3});
    fixture.memory.Fill(fixture.addresses.move_command_base, 1, std::byte{0xE4});
    fixture.memory.Fill(fixture.addresses.slot_param_base, 1, std::byte{0xE5});
    fixture.memory.Fill(fixture.addresses.lcg_rng, 4, std::byte{0xE8});
    fixture.memory.Fill(fixture.addresses.lfsr_rng, 0x64, std::byte{0xE9});
    fixture.memory.Set(fixture.addresses.lfsr_rng + 0x64, std::uint32_t{8});
    fixture.memory.Fill(fixture.addresses.xorshift_rng, 0x0C, std::byte{0xEA});
    fixture.memory.Fill(fixture.addresses.wind_rng, 0x18, std::byte{0xEB});

    fixture.memory.Fill(fixture.addresses.pump_state + 0x3C, 1, std::byte{0xA1});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x0C, 1, std::byte{0xA6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x18, 1, std::byte{0xA7});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x5C, 1, std::byte{0xA8});
    fixture.memory.Fill(Fixture::memory_base + 0x500C, 1, std::byte{0xA2});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x2A28, 1, std::byte{0xA3});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x3034, 1, std::byte{0xA4});
    fixture.memory.Fill(fixture.addresses.slot_param_base + 0x28, 1, std::byte{0xA5});

    expect(fixture.regions.RestoreTransactional(baseline).ok(),
        "restore native candidate regions and explicit RNG streams");
    NativeCandidateImage restored{};
    expect(fixture.regions.Capture(restored).ok() && restored == baseline, "recapture exact semantic image");
    expect(fixture.memory.Get(fixture.addresses.pump_state + 0x3C) == std::byte{0xA1}, "preserve pump tail");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x0C) == std::byte{0xA6}, "preserve scheduler constructor residue");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x18) == std::byte{0xA7}, "preserve scheduler reserved bytes");
    expect(fixture.memory.Get(fixture.addresses.scheduler_base + 0x5C) == std::byte{0xA8}, "preserve scheduler allocator residue");
    expect(fixture.memory.Get(Fixture::memory_base + 0x500C) == std::byte{0xA2}, "preserve SubVM gap");
    expect(fixture.memory.Get(fixture.addresses.move_command_base + 0x2A28) == std::byte{0xA3}, "preserve diagnostic text");
    expect(fixture.memory.Get(fixture.addresses.move_command_base + 0x3034) == std::byte{0xA4}, "preserve uninitialized tail");
    expect(fixture.memory.Get(fixture.addresses.slot_param_base + 0x28) == std::byte{0xA5}, "preserve slot padding");
    expect(restored.rng == baseline.rng,
        "restore all four explicit Lux RNG streams exactly");

    const auto canonical = NativeCandidateRegions::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, fixture.event_masks), "canonical bytes exclude event owner pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x5000), "canonical bytes exclude SubVM pointer");
}

void test_preflight_is_atomic()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind identity-drift fixture");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture identity-drift baseline");
    fixture.memory.Fill(fixture.event_masks, 1, std::byte{0xF1});
    fixture.memory.Set(fixture.addresses.move_command_base + 0x08, std::uintptr_t{0xDEADBEEF});
    const auto before = fixture.memory.bytes();
    expect(
        fixture.regions.RestoreTransactional(baseline).code == FailureCode::IdentityMismatch,
        "identity drift rejects restore");
    expect(fixture.memory.bytes() == before, "identity rejection performs zero mutation");
}

void test_unknown_class_and_invalid_header_fail_closed()
{
    Fixture unknown;
    unknown.memory.Set(Fixture::memory_base + 0x5000, Fixture::image_base + std::uintptr_t{0x1234});
    expect(
        unknown.regions.Bind(unknown.addresses).code == FailureCode::AdapterUnqualified,
        "unknown SubVM class is unqualified");

    Fixture header;
    expect(header.regions.Bind(header.addresses).ok(), "bind invalid-header fixture");
    NativeCandidateImage baseline{};
    expect(header.regions.Capture(baseline).ok(), "capture invalid-header baseline");
    header.memory.Set(header.addresses.move_dispatch + 0x4B0, std::int32_t{3});
    const auto before = header.memory.bytes();
    expect(
        header.regions.RestoreTransactional(baseline).code == FailureCode::IdentityMismatch,
        "invalid event-mask count rejects restore");
    expect(header.memory.bytes() == before, "invalid header performs zero mutation");
}

void test_lfsr_refill_sentinel_is_bounded()
{
    Fixture sentinel;
    sentinel.memory.Set(sentinel.addresses.lfsr_rng + 0x64, std::uint32_t{25});
    expect(sentinel.regions.Bind(sentinel.addresses).ok(),
        "LFSR index 25 is the valid native refill sentinel");
    NativeCandidateImage image{};
    expect(sentinel.regions.Capture(image).ok() && image.rng.lfsr_index == 25,
        "capture the valid LFSR refill sentinel exactly");

    Fixture invalid;
    invalid.memory.Set(invalid.addresses.lfsr_rng + 0x64, std::uint32_t{26});
    expect(invalid.regions.Bind(invalid.addresses).code
            == FailureCode::CapturePreflightFailed,
        "LFSR index above the refill sentinel fails closed");
}

void test_partial_write_undoes_exactly()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind undo fixture");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture undo target");
    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xD1});
    fixture.memory.Fill(fixture.addresses.pump_state + 0x20, 0x1C, std::byte{0xD2});
    const auto before = fixture.memory.bytes();
    fixture.memory.FailWrite(5);
    expect(
        fixture.regions.RestoreTransactional(baseline).code == FailureCode::RestoreWriteFailed,
        "partial write reports typed failure");
    expect(fixture.memory.bytes() == before, "partial write restores exact undo image");
}

struct MoveDispatchFixture
{
    static constexpr std::uintptr_t memory_base = Fixture::memory_base;
    static constexpr std::uintptr_t object = memory_base + 0x16000;
    static constexpr std::uintptr_t frame_slots = memory_base + 0x17000;
    static constexpr std::uintptr_t sub_elements = memory_base + 0x18000;

    MoveDispatchFixture() : memory(memory_base, 0x20000), state(memory)
    {
        memory.Set(object + 0x470, frame_slots);
        memory.Set(object + 0x478, std::int32_t{3});
        memory.Set(object + 0x47C, std::int32_t{2});
        memory.Set(object + 0x480, std::uint8_t{5});
        memory.Set(object + 0x484, std::int32_t{10});
        memory.Set(object + 0x488, std::uint8_t{1});
        memory.Set(object + 0x490, std::uint32_t{0});
        memory.Set(object + 0x494, std::int32_t{4});
        memory.Set(object + 0x498, sub_elements);
        memory.Set(object + 0x4A0, std::int32_t{2});
        memory.Set(object + 0x4A4, std::int32_t{2});
        memory.Set(sub_elements, std::int32_t{20});
        memory.Set(sub_elements + 4, std::uint8_t{0});
        memory.Fill(sub_elements + 8, 24, std::byte{0x31});
        memory.Set(sub_elements + 0x20, std::int32_t{21});
        memory.Set(sub_elements + 0x24, std::uint8_t{1});
        memory.Fill(sub_elements + 0x28, 24, std::byte{0x32});
    }

    FakeNativeMemory memory;
    MoveDispatchState state;
};

void test_move_dispatch_action_phase_restore()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch action phase");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch action phase");

    fixture.memory.Set(MoveDispatchFixture::object + 0x484, std::int32_t{44});
    fixture.memory.Set(MoveDispatchFixture::sub_elements, std::int32_t{99});
    fixture.memory.Fill(
        MoveDispatchFixture::sub_elements + 8, 24, std::byte{0x7A});
    expect(fixture.state.RestoreTransactional(baseline).ok(),
        "restore MoveDispatch semantic state");
    MoveDispatchImage restored{};
    expect(fixture.state.Capture(restored).ok() && restored == baseline,
        "recapture exact MoveDispatch semantic state");
    expect(fixture.memory.Get(MoveDispatchFixture::sub_elements + 8)
        == std::byte{0x7A}, "preserve MoveDispatch derived scratch bytes");

    const auto canonical = MoveDispatchState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, MoveDispatchFixture::frame_slots),
        "MoveDispatch canonical bytes exclude authored table pointer");
    expect(!contains_qword(canonical, MoveDispatchFixture::sub_elements),
        "MoveDispatch canonical bytes exclude subelement owner pointer");
}

void test_move_dispatch_phase_drift_is_atomic()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch phase-drift fixture");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch phase-drift baseline");
    fixture.memory.Set(MoveDispatchFixture::object + 0x490, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x480,
        MoveDispatchFixture::memory_base + 0x19000);
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{0});
    fixture.memory.Set(MoveDispatchFixture::object + 0x48C, std::int32_t{2});
    const auto before = fixture.memory.bytes();
    expect(fixture.state.RestoreTransactional(baseline).code
            == FailureCode::IdentityMismatch,
        "MoveDispatch phase drift rejects restore");
    expect(fixture.memory.bytes() == before,
        "MoveDispatch phase drift performs zero mutation");
}

void test_move_dispatch_pending_phase_restore()
{
    MoveDispatchFixture fixture;
    constexpr auto pending_windows = MoveDispatchFixture::memory_base + 0x19000;
    fixture.memory.Set(MoveDispatchFixture::object + 0x490, std::uint32_t{1});
    fixture.memory.Set(MoveDispatchFixture::object + 0x480, pending_windows);
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{2});
    fixture.memory.Set(MoveDispatchFixture::object + 0x48C, std::int32_t{3});
    const MoveDispatchPendingWindow first{1, 2, 3, 4, 5, 6};
    const MoveDispatchPendingWindow second{7, 8, 9, 10, 11, 12};
    fixture.memory.Set(pending_windows, first);
    fixture.memory.Set(pending_windows + 0x20, second);

    expect(fixture.state.Bind(MoveDispatchFixture::object, 20).ok(),
        "bind MoveDispatch pending phase");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch pending phase");
    fixture.memory.Set(pending_windows + 0x18, std::int32_t{77});
    fixture.memory.Set(MoveDispatchFixture::object + 0x488, std::int32_t{1});
    expect(fixture.state.RestoreTransactional(baseline).ok(),
        "restore MoveDispatch pending-window values and count");
    MoveDispatchImage restored{};
    expect(fixture.state.Capture(restored).ok() && restored == baseline,
        "recapture exact MoveDispatch pending phase");

    auto malformed = baseline;
    malformed.saved_input_and_gates = 0;
    const auto before = fixture.memory.bytes();
    expect(fixture.state.RestoreTransactional(malformed).code
            == FailureCode::RestorePreflightFailed,
        "reject inconsistent MoveDispatch phase tag");
    expect(fixture.memory.bytes() == before,
        "inconsistent MoveDispatch phase tag performs zero mutation");
    const auto canonical = MoveDispatchState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, pending_windows),
        "MoveDispatch canonical bytes exclude pending-window owner pointer");
}

void test_move_dispatch_partial_write_undoes_exactly()
{
    MoveDispatchFixture fixture;
    expect(fixture.state.Bind(MoveDispatchFixture::object, 19).ok(),
        "bind MoveDispatch undo fixture");
    MoveDispatchImage baseline{};
    expect(fixture.state.Capture(baseline).ok(),
        "capture MoveDispatch undo target");
    fixture.memory.Set(MoveDispatchFixture::object + 0x484, std::int32_t{80});
    fixture.memory.Set(MoveDispatchFixture::sub_elements, std::int32_t{81});
    const auto before = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(fixture.state.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "MoveDispatch partial write reports typed failure");
    expect(fixture.memory.bytes() == before,
        "MoveDispatch partial write restores exact undo image");
}

void test_stage_break_listener_topology_is_value_only_and_bounded()
{
    constexpr std::uintptr_t base = 0x10000000;
    constexpr std::size_t image_size = 0x430000;
    FakeNativeMemory memory{base, 0x440000};
    constexpr auto wall = base + 0x1000;
    constexpr auto wall_emitter = wall + 0x3B0;
    constexpr auto wall_vtable = base + 0x8000;
    constexpr auto wall_callback = base + 0x9000;
    memory.Set(wall + 0x450, std::int32_t{7});
    memory.Set(wall_emitter + 0x40, std::uintptr_t{});
    memory.Set(wall_emitter + 0x50, std::int32_t{1});
    memory.Set(wall_emitter + 0x54, std::int32_t{1});
    memory.Set(wall_emitter, wall_vtable);
    memory.Set(wall_emitter + 0x20, std::uintptr_t{});
    memory.Set(wall_emitter + 0x30, std::int32_t{1});
    memory.Set(wall_vtable + 0x68, wall_callback);

    constexpr auto barrier = base + 0x3000;
    constexpr auto barrier_emitter = barrier + 0x390;
    constexpr auto heap_entries = base + 0x12000;
    constexpr auto listener_object = base + 0x15000;
    constexpr auto barrier_vtable = base + 0x8100;
    constexpr auto barrier_callback = base + 0x9100;
    memory.Set(barrier + 0x420, std::int32_t{9});
    memory.Set(barrier_emitter + 0x40, heap_entries);
    memory.Set(barrier_emitter + 0x50, std::int32_t{2});
    memory.Set(barrier_emitter + 0x54, std::int32_t{2});
    memory.Set(heap_entries + 0x30, std::int32_t{});
    memory.Set(heap_entries + 0x40 + 0x20, listener_object);
    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(listener_object, barrier_vtable);
    memory.Set(barrier_vtable + 0x68, barrier_callback);

    StageBreakListenerTopologyProbe probe{memory};
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    StageBreakListenerTopology topology{};
    expect(probe.Capture(base, image_size, actors, topology).ok(),
        "capture bounded stage-break listener topology");
    expect(topology.actors.size() == 2 && topology.listeners.size() == 2,
        "retain actors and only active listeners");
    expect(topology.listeners[0].actor_id == 7
            && topology.listeners[0].slot_index == 0
            && topology.listeners[0].listener_vtable_rva == 0x8000
            && topology.listeners[0].callback_rva == 0x9000,
        "inline wall listener becomes module-relative values");
    expect(topology.listeners[1].actor_id == 9
            && topology.listeners[1].dispatch_order == 0
            && topology.listeners[1].slot_index == 1
            && topology.listeners[1].listener_vtable_rva == 0x8100
            && topology.listeners[1].callback_rva == 0x9100,
        "heap listener preserves reverse dispatch order without pointers");

    const std::array repeated_actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
    };
    StageBreakListenerTopology repeated_topology{};
    expect(probe.Capture(base, image_size, repeated_actors, repeated_topology).ok()
            && repeated_topology.actors.size() == 3
            && repeated_topology.listeners.size() == 3
            && repeated_topology.actors[0].repeated_reference_of
                == no_repeated_actor_reference
            && repeated_topology.actors[2].actor_id == 7
            && repeated_topology.actors[2].repeated_reference_of == 0,
        "ordered native list may repeat an actor reference without exposing its pointer");

    constexpr auto weak_delegate_wrapper = base + 0x41D870;
    constexpr auto bound_callback = base + 0xA000;
    memory.Set(wall_vtable + 0x68, weak_delegate_wrapper);
    memory.Set(wall_emitter + 0x10, bound_callback);
    StageBreakListenerTopology bound_topology{};
    expect(probe.Capture(base, image_size, actors, bound_topology).ok()
            && bound_topology.listeners[0].callback_rva == 0x41D870
            && bound_topology.listeners[0].bound_callback_rva == 0xA000
            && bound_topology.listeners[1].bound_callback_rva
                == no_bound_stage_break_callback,
        "verified weak-delegate wrapper exposes its value-only bound callback RVA");
    memory.Set(wall_vtable + 0x68, wall_callback);

    const auto first_signature = topology.signature;
    memory.Set(barrier_vtable + 0x68, base + 0x9200);
    expect(probe.Capture(base, image_size, actors, topology).ok()
            && topology.signature != first_signature,
        "callback target drift changes the value-only signature");

    const auto callback_drift_signature = topology.signature;
    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{});
    expect(probe.Capture(base, image_size, actors, topology).ok()
            && topology.actors.size() == 2
            && topology.listeners.size() == 1
            && topology.signature != callback_drift_signature,
        "actors with no active listeners remain visible in topology");

    memory.Set(heap_entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(barrier_vtable + 0x68, base + image_size + 0x100);
    StageBreakListenerProbeFailure failure{};
    expect(probe.Capture(base, image_size, actors, topology, &failure).code
            == FailureCode::IdentityMismatch
            && topology.listeners.empty()
            && failure.fault == StageBreakListenerProbeFault::CallbackOutsideImage
            && failure.actor_order == 1
            && failure.slot_index == 1,
        "callback targets outside the executable image fail closed");
}

Status resolve_fake_callback_owner(
    void*, std::int32_t object_index, std::int32_t serial_number,
    std::uint64_t& class_token) noexcept
{
    if (object_index <= 0 || serial_number <= 0)
        return Status::failure(FailureCode::IdentityMismatch);
    class_token = (static_cast<std::uint64_t>(object_index) << 32)
        | static_cast<std::uint32_t>(serial_number);
    return Status::success();
}

void test_callback_topology_is_generation_bound_and_pointer_free()
{
    constexpr std::uintptr_t base = 0x20000000;
    constexpr std::size_t image_size = 0x10000;
    FakeNativeMemory memory{base, 0x20000};
    constexpr auto input = base + 0x1000;
    memory.Set(input, base + 0x5000);
    memory.Set(input + 0x08, std::int32_t{11});
    memory.Set(input + 0x0C, std::int32_t{21});
    memory.Set(input + 0x10, base + 0x6000);
    memory.Set(input + 0x20, std::uintptr_t{});
    memory.Set(input + 0x30, std::int32_t{1});
    memory.Set(input + 0x40, std::uintptr_t{});
    memory.Set(input + 0x50, std::int32_t{1});
    memory.Set(input + 0x54, std::int32_t{1});

    constexpr auto round = base + 0x2000;
    constexpr auto entries = base + 0x11000;
    constexpr auto override_callback = base + 0x15000;
    memory.Set(round + 0x40, entries);
    memory.Set(round + 0x50, std::int32_t{2});
    memory.Set(round + 0x54, std::int32_t{2});
    memory.Set(entries, base + 0x5100);
    memory.Set(entries + 0x08, std::int32_t{12});
    memory.Set(entries + 0x0C, std::int32_t{22});
    memory.Set(entries + 0x10, base + 0x6100);
    memory.Set(entries + 0x20, std::uintptr_t{});
    memory.Set(entries + 0x30, std::int32_t{1});
    memory.Set(entries + 0x40 + 0x20, override_callback);
    memory.Set(entries + 0x40 + 0x30, std::int32_t{1});
    memory.Set(override_callback, base + 0x5200);
    memory.Set(override_callback + 0x08, std::int32_t{13});
    memory.Set(override_callback + 0x0C, std::int32_t{23});
    memory.Set(override_callback + 0x10, base + 0x6200);

    const std::array collections{
        CallbackCollectionRef{CallbackCollectionRole::InputFilter, input},
        CallbackCollectionRef{CallbackCollectionRole::Round, round},
    };
    CallbackTopologyProbe probe{memory};
    CallbackTopology topology{};
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).ok()
            && topology.records.size() == 3
            && topology.records[0].wrapper_vtable_rva == 0x5000
            && topology.records[0].callback_rva == 0x6000
            && topology.records[1].dispatch_order == 1
            && topology.records[2].dispatch_order == 0
            && topology.records[2].owner_class_token
                == ((std::uint64_t{13} << 32) | 23),
        "callback topology retains only ordered weak generations, class tokens, and RVAs");

    const auto signature = topology.signature;
    memory.Set(override_callback + 0x10, base + 0x6300);
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).ok()
            && topology.signature != signature,
        "callback target drift changes the binding signature");
    memory.Set(entries + 0x40 + 0x30, std::int32_t{});
    expect(probe.Capture(base, image_size, collections,
            &resolve_fake_callback_owner, nullptr, topology).code
            == FailureCode::IdentityMismatch,
        "inactive callback entries fail binding closed before capture");
}

void test_stage_wind_topology_is_bounded_and_pointer_free()
{
    constexpr std::uintptr_t base = 0x30000000;
    constexpr std::uintptr_t image_base = 0x140000000;
    FakeNativeMemory memory{base, 0x20000};
    constexpr auto root_pointer = base + 0x100;
    constexpr auto root = base + 0x1000;
    constexpr auto first = base + 0x3000;
    constexpr auto second = base + 0x5000;
    memory.Set(root_pointer, root);
    memory.Set(root, first);
    memory.Fill(root + 0x08, 12, std::byte{0x11});
    memory.Set(root + 0x18, image_base + std::uintptr_t{0x334430});
    memory.Set(root + 0x98, std::uint32_t{1});
    memory.Set(root + 0x9C, std::int32_t{1});
    memory.Fill(root + 0xA0, 0x10, std::byte{0x22});
    memory.Fill(root + 0xB0, 0x10, std::byte{0x33});
    memory.Fill(root + 0xC0, 0x30, std::byte{0x44});

    memory.Set(first, image_base + std::uintptr_t{0x3E88C88});
    memory.Set(first + 0x10, second);
    memory.Set(first + 0x18, std::uintptr_t{});
    memory.Set(first + 0x28, root);
    memory.Fill(first + 0x20, 2, std::byte{0x51});
    memory.Fill(first + 0x30, 4, std::byte{0x52});
    memory.Fill(first + 0x40, 0x30, std::byte{0x53});
    memory.Fill(first + 0x70, 0x70, std::byte{0x54});
    memory.Fill(first + 0x120, 0x0C, std::byte{0x55});

    memory.Set(second, image_base + std::uintptr_t{0x3E88D18});
    memory.Set(second + 0x10, std::uintptr_t{});
    memory.Set(second + 0x18, first);
    memory.Set(second + 0x28, root);
    memory.Fill(second + 0x20, 2, std::byte{0x61});
    memory.Fill(second + 0x30, 4, std::byte{0x62});
    memory.Fill(second + 0x40, 0x30, std::byte{0x63});
    memory.Fill(second + 0x70, 0xA0, std::byte{0x64});
    memory.Fill(second + 0x120, 0x0C, std::byte{0x65});
    memory.Fill(second + 0x130, 0x50, std::byte{0x66});

    StageWindTopologyProbe probe{memory};
    const StageWindTopologyAddresses addresses{
        image_base, 0x4300000, root_pointer, 9};
    StageWindTopologyImage image{};
    expect(probe.Bind(addresses).ok() && probe.Capture(image).ok()
            && image.nodes.size() == 2
            && image.nodes[0].kind == StageWindNodeKind::Parallel
            && image.nodes[1].kind == StageWindNodeKind::ShockWave
            && image.pending_callback_rvas[0] == 0x334430,
        "wind topology captures ordered value-only node classes and callback RVAs");
    const auto canonical = StageWindTopologyProbe::CanonicalBytes(image);
    expect(!contains_qword(canonical, root) && !contains_qword(canonical, first)
            && !contains_qword(canonical, second),
        "wind canonical bytes contain no native root or node pointer");

    const auto canonical_before_residue = canonical;
    memory.Fill(first + 0x34, 0x0C, std::byte{0x7A});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                == canonical_before_residue,
        "wind canonical bytes exclude base allocator residue");
    memory.Fill(first + 0x30, 4, std::byte{0x7B});
    expect(probe.Capture(image).ok()
            && StageWindTopologyProbe::CanonicalBytes(image)
                != canonical_before_residue,
        "wind lifecycle changes alter canonical state");

    memory.Set(second + 0x10, first);
    expect(probe.Capture(image).code == FailureCode::IdentityMismatch,
        "wind topology rejects cycles");
    memory.Set(second + 0x10, std::uintptr_t{});
    memory.Set(second, image_base + std::uintptr_t{0x3E88000});
    expect(probe.Capture(image).code == FailureCode::AdapterUnqualified,
        "wind topology rejects unknown node vtables");
    memory.Set(second, image_base + std::uintptr_t{0x3E88D18});
    memory.Set(second + 0x18, std::uintptr_t{});
    expect(probe.Capture(image).code == FailureCode::IdentityMismatch,
        "wind topology rejects broken reverse links");
}

class FixedStageWindAllocator final : public IStageWindAllocator
{
public:
    explicit FixedStageWindAllocator(std::uintptr_t next) : next_(next) {}

    std::uintptr_t Allocate(std::size_t size) noexcept override
    {
        ++allocation_calls_;
        if (fail_allocation_ == allocation_calls_) return 0;
        const auto result = next_;
        next_ += (size + 0xFF) & ~std::size_t{0xFF};
        allocations.push_back(result);
        return result;
    }

    void Free(std::uintptr_t address) noexcept override
    {
        frees.push_back(address);
    }

    void FailAllocation(std::size_t call) noexcept
    {
        allocation_calls_ = 0;
        fail_allocation_ = call;
    }

    std::vector<std::uintptr_t> allocations;
    std::vector<std::uintptr_t> frees;

private:
    std::uintptr_t next_{};
    std::size_t allocation_calls_{};
    std::size_t fail_allocation_{};
};

void test_stage_wind_graph_restore_is_transactional()
{
    constexpr std::uintptr_t base = 0x31000000;
    constexpr std::uintptr_t image_base = 0x140000000;
    constexpr auto root_pointer = base + 0x100;
    constexpr auto root = base + 0x1000;
    constexpr auto old_node = base + 0x3000;
    FakeNativeMemory memory{base, 0x40000};
    const auto* ring_out_layout = FindStageWindNodeLayout(StageWindNodeKind::RingOut);
    expect(ring_out_layout != nullptr && ring_out_layout->allocation_size == 0x130
            && ring_out_layout->class_ranges.back().offset
                + ring_out_layout->class_ranges.back().size <= 0x130,
        "ring-out layout is bounded by the assembly-proven 0x130 allocation");
    memory.Set(root_pointer, root);
    memory.Set(root, old_node);
    memory.Fill(root + 0x08, 12, std::byte{0x11});
    memory.Set(root + 0x98, std::uint32_t{});
    memory.Set(root + 0x9C, std::int32_t{});
    memory.Fill(root + 0xB0, 0x10, std::byte{0x33});
    memory.Fill(root + 0xC0, 0x30, std::byte{0x44});
    memory.Set(old_node, image_base + std::uintptr_t{0x3E88CE8});
    memory.Set(old_node + 0x10, std::uintptr_t{});
    memory.Set(old_node + 0x18, std::uintptr_t{});
    memory.Set(old_node + 0x28, root);
    memory.Fill(old_node + 0x20, 2, std::byte{0x21});
    memory.Fill(old_node + 0x30, 4, std::byte{0x22});
    memory.Fill(old_node + 0x40, 0x30, std::byte{0x23});
    memory.Fill(old_node + 0x70, 0x84, std::byte{0x24});
    memory.Fill(old_node + 0xF8, 0x24, std::byte{0x25});
    memory.Fill(old_node + 0x120, 0x10, std::byte{0x26});
    memory.Fill(old_node + 0x130, 0x04, std::byte{0x27});
    memory.Fill(old_node + 0x134, 0x10, std::byte{0x28});
    memory.Fill(old_node + 0x148, 0x04, std::byte{0x29});
    memory.Fill(old_node + 0x150, 0x90, std::byte{0x2A});

    const StageWindTopologyAddresses addresses{
        image_base, 0x4300000, root_pointer, 10};
    StageWindTopologyProbe probe{memory};
    StageWindTopologyImage target{};
    expect(probe.Bind(addresses).ok() && probe.Capture(target).ok(),
        "wind transaction fixture captures a qualified source graph");
    const auto canonical_before_derived_change =
        StageWindTopologyProbe::CanonicalBytes(target);
    target.root_clock[0] = std::byte{0x7A};
    target.nodes[0].semantic_state[0] = std::byte{0x6A};
    target.nodes[0].derived_state[0] = std::byte{0x5A};
    auto canonical_without_derived = target;
    canonical_without_derived.nodes[0].derived_state[0] = std::byte{0x4A};
    expect(StageWindTopologyProbe::CanonicalBytes(target)
            == StageWindTopologyProbe::CanonicalBytes(canonical_without_derived)
            && StageWindTopologyProbe::CanonicalBytes(target)
                != canonical_before_derived_change,
        "wind canonical bytes exclude local ring-in matrices and travel state");

    FixedStageWindAllocator allocator{base + 0x10000};
    StageWindGraphTransaction transaction{memory, allocator};
    expect(transaction.Restore(addresses, target).ok(),
        "wind graph restore commits a verified replacement graph");
    StageWindTopologyImage restored{};
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph replacement reconstructs the exact pointer-free target");
    expect(allocator.frees.size() == 1 && allocator.frees[0] == old_node,
        "wind graph commit frees the detached old graph only after verification");

    const auto committed_head = allocator.allocations.back();
    StageWindTopologyImage second_target = target;
    second_target.root_clock[1] = std::byte{0x5A};
    FixedStageWindAllocator failing_allocator{base + 0x20000};
    StageWindGraphTransaction failing_transaction{memory, failing_allocator};
    memory.FailWrite(2); // one replacement-node write, then the root publication
    expect(failing_transaction.Restore(addresses, second_target).code
            == FailureCode::RestoreWriteFailed,
        "wind graph root publication failure aborts the transaction");
    memory.AllowWrites();
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph publication failure leaves the committed graph unchanged");
    expect(failing_allocator.frees.size() == 1
            && failing_allocator.frees[0] != committed_head,
        "wind graph publication failure frees only unpublished replacements");

    FixedStageWindAllocator corrupting_allocator{base + 0x28000};
    StageWindGraphTransaction corrupting_transaction{memory, corrupting_allocator};
    memory.CorruptAfterWrite(2, base + 0x28000 + 0x20, std::byte{0xEE});
    expect(corrupting_transaction.Restore(addresses, second_target).code
            == FailureCode::RestoreVerificationFailed,
        "wind graph post-publication verification failure reports restore failure");
    memory.AllowWrites();
    expect(probe.Capture(restored).ok() && restored == target,
        "wind graph post-publication verification restores the exact old root");
    expect(corrupting_allocator.frees.size() == 1
            && corrupting_allocator.frees[0] == base + 0x28000,
        "wind graph verification failure retires only the rejected replacement");

    FixedStageWindAllocator exhausted_allocator{base + 0x30000};
    exhausted_allocator.FailAllocation(1);
    StageWindGraphTransaction exhausted_transaction{memory, exhausted_allocator};
    expect(exhausted_transaction.Restore(addresses, second_target).code
            == FailureCode::CapacityExceeded
            && probe.Capture(restored).ok() && restored == target,
        "wind graph allocation failure is mutation-free");

    StageWindTopologyImage invalid_schedule = target;
    std::uint32_t invalid_bank = 2;
    std::memcpy(invalid_schedule.schedule_state.data(), &invalid_bank,
        sizeof(invalid_bank));
    FixedStageWindAllocator unused_allocator{base + 0x38000};
    StageWindGraphTransaction invalid_transaction{memory, unused_allocator};
    expect(invalid_transaction.Restore(addresses, invalid_schedule).code
            == FailureCode::RestorePreflightFailed
            && unused_allocator.allocations.empty()
            && probe.Capture(restored).ok() && restored == target,
        "wind graph invalid callback-bank state fails before allocation or mutation");
}
}

int main()
{
    test_hgcpu_stream_contract();
    test_candidate_checkpoint_codec();
    test_candidate_adapter_restore_and_outer_undo();
    test_candidate_adapter_native_failure_undoes_hgcpu();
    test_hgcpu_direct_source_coverage();
    test_capture_restore_preserves_exclusions();
    test_preflight_is_atomic();
    test_unknown_class_and_invalid_header_fail_closed();
    test_lfsr_refill_sentinel_is_bounded();
    test_partial_write_undoes_exactly();
    test_move_dispatch_action_phase_restore();
    test_move_dispatch_phase_drift_is_atomic();
    test_move_dispatch_pending_phase_restore();
    test_move_dispatch_partial_write_undoes_exactly();
    test_stage_break_listener_topology_is_value_only_and_bounded();
    test_callback_topology_is_generation_bound_and_pointer_free();
    test_stage_wind_topology_is_bounded_and_pointer_free();
    test_stage_wind_graph_restore_is_transactional();
    if (failures == 0)
        std::cout << "NativeCandidateRegionsSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
