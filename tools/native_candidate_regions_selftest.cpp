#include "deterministic/NativeCandidateRegions.hpp"
#include "deterministic/HgCpuStream.hpp"

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
            memory.Set(scheduler + 0x50, subvm);
            memory.Set(subvm, image_base + std::uintptr_t{0x3E863D0});
            memory.Set(subvm + 0x10, memory_base + 0x12000 + lane * 0x1000);
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
    const auto context = hgcpu_context();
    expect(shim.Capture(&fake_hgcpu_writer, context, image).ok(), "capture bounded HgCpu stream");
    expect(image.cursor == hgcpu_payload.size(), "record exact HgCpu cursor");
    expect(image.bytes == std::vector<std::byte>(
        hgcpu_payload.begin(), hgcpu_payload.end()), "capture exact HgCpu bytes");
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

void test_capture_restore_preserves_exclusions()
{
    Fixture fixture;
    expect(fixture.regions.Bind(fixture.addresses).ok(), "bind candidate regions");
    NativeCandidateImage baseline{};
    expect(fixture.regions.Capture(baseline).ok(), "capture candidate image");

    fixture.memory.Fill(fixture.event_masks, 0x10, std::byte{0xE1});
    fixture.memory.Fill(fixture.addresses.pump_state + 0x20, 1, std::byte{0xE2});
    fixture.memory.Fill(Fixture::memory_base + 0x5008, 1, std::byte{0xE3});
    fixture.memory.Fill(fixture.addresses.move_command_base, 1, std::byte{0xE4});
    fixture.memory.Fill(fixture.addresses.slot_param_base, 1, std::byte{0xE5});

    fixture.memory.Fill(fixture.addresses.pump_state + 0x3C, 1, std::byte{0xA1});
    fixture.memory.Fill(Fixture::memory_base + 0x500C, 1, std::byte{0xA2});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x2A28, 1, std::byte{0xA3});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x3034, 1, std::byte{0xA4});
    fixture.memory.Fill(fixture.addresses.slot_param_base + 0x28, 1, std::byte{0xA5});

    expect(fixture.regions.RestoreTransactional(baseline).ok(), "restore all eight candidate regions");
    NativeCandidateImage restored{};
    expect(fixture.regions.Capture(restored).ok() && restored == baseline, "recapture exact semantic image");
    expect(fixture.memory.Get(fixture.addresses.pump_state + 0x3C) == std::byte{0xA1}, "preserve pump tail");
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
}

int main()
{
    test_hgcpu_stream_contract();
    test_capture_restore_preserves_exclusions();
    test_preflight_is_atomic();
    test_unknown_class_and_invalid_header_fail_closed();
    test_partial_write_undoes_exactly();
    if (failures == 0)
        std::cout << "NativeCandidateRegionsSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
