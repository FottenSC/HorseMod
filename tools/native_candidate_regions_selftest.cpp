#include "deterministic/CandidateCheckpoint.hpp"
#include "deterministic/NativeCandidateRegions.hpp"
#include "deterministic/HgCpuStream.hpp"
#include "deterministic/HgCpuCoverageProbe.hpp"
#include "deterministic/MoveDispatchState.hpp"

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
    }

    void AllowWrites() noexcept
    {
        write_calls_ = 0;
        fail_write_call_ = 0;
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
        addresses.fighter_roots = {
            memory_base + 0x12000, memory_base + 0x13000};
        addresses.session_generation = 11;
        addresses.round_generation = 7;
        initialize();
    }

    void initialize()
    {
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
    return shim;
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
    return {0x231, 1, 11, 7, {101, 102}, 201};
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

    CandidateCheckpointImage image{native, hgcpu};
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

    fixture.memory.Fill(fixture.addresses.pump_state + 0x3C, 1, std::byte{0xA1});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x0C, 1, std::byte{0xA6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x18, 1, std::byte{0xA7});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x5C, 1, std::byte{0xA8});
    fixture.memory.Fill(Fixture::memory_base + 0x500C, 1, std::byte{0xA2});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x2A28, 1, std::byte{0xA3});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x3034, 1, std::byte{0xA4});
    fixture.memory.Fill(fixture.addresses.slot_param_base + 0x28, 1, std::byte{0xA5});

    expect(fixture.regions.RestoreTransactional(baseline).ok(), "restore all eight candidate regions");
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
}

int main()
{
    test_hgcpu_stream_contract();
    test_candidate_checkpoint_codec();
    test_hgcpu_direct_source_coverage();
    test_capture_restore_preserves_exclusions();
    test_preflight_is_atomic();
    test_unknown_class_and_invalid_header_fail_closed();
    test_partial_write_undoes_exactly();
    test_move_dispatch_action_phase_restore();
    test_move_dispatch_phase_drift_is_atomic();
    test_move_dispatch_pending_phase_restore();
    test_move_dispatch_partial_write_undoes_exactly();
    if (failures == 0)
        std::cout << "NativeCandidateRegionsSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
