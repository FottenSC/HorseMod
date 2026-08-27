#include "deterministic/CandidateCheckpoint.hpp"
#include "deterministic/BattleAudioSelectorState.hpp"
#include "deterministic/CallbackTopology.hpp"
#include "deterministic/CharaAnimationState.hpp"
#include "deterministic/CandidateGameStateAdapter.hpp"
#include "deterministic/InputTimeline.hpp"
#include "deterministic/NativeCandidateRegions.hpp"
#include "deterministic/HgCpuStream.hpp"
#include "deterministic/HgCpuCoverageProbe.hpp"
#include "deterministic/MoveDispatchState.hpp"
#include "deterministic/MotionBankSnapshot.hpp"
#include "deterministic/PresentationJournal.hpp"
#include "deterministic/Schema.hpp"
#include "deterministic/SecondaryEventState.hpp"
#include "deterministic/SimulationSession.hpp"
#include "deterministic/SnapshotStore.hpp"
#include "deterministic/StageBreakListenerDiagnostics.hpp"
#include "deterministic/StageBreakPresentationIdentity.hpp"
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
        : memory(memory_base, 0x160000), regions(memory)
    {
        addresses.image_base = image_base;
        addresses.battle_manager = memory_base + 0x15000;
        addresses.input_log = memory_base + 0x17000;
        addresses.frame_counter = memory_base + 0x1F000;
        addresses.move_dispatch = memory_base + 0x1000;
        addresses.pump_state = memory_base + 0x3000;
        addresses.scheduler_base = memory_base + 0x4000;
        addresses.move_command_base = memory_base + 0x7000;
        addresses.slot_param_base = memory_base + 0xF000;
        addresses.lcg_rng = memory_base + 0x10000;
        addresses.lfsr_rng = memory_base + 0x10100;
        addresses.xorshift_rng = memory_base + 0x10200;
        addresses.wind_rng = memory_base + 0x10300;
        addresses.vm_freeze_record = memory_base + 0x10340;
        addresses.stage_wind_emitter_list = memory_base + 0x10480;
        addresses.pending_hit_record = memory_base + 0x10400;
        addresses.pending_launcher_sync = memory_base + 0x10420;
        addresses.camera_action_backing = memory_base + 0x1D000;
        addresses.fighter_roots = {
            memory_base + 0x12000, memory_base + 0x13000};
        addresses.session_generation = 11;
        addresses.round_generation = 7;
        initialize();
    }

    void initialize()
    {
        const auto previous_inputs = memory_base + 0x1C000;
        const auto input_pairs = memory_base + 0x1C100;
        const auto prior_input_pairs = memory_base + 0x1C200;
        const auto round_sequence = memory_base + 0x1C300;
        memory.Set(addresses.battle_manager + 0x478, addresses.input_log);
        memory.Set(addresses.input_log + 0x10, memory_base + 0x22000);
        memory.Set(addresses.battle_manager + 0x1498, previous_inputs);
        memory.Set(addresses.battle_manager + 0x14A0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14A4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14A8, input_pairs);
        memory.Set(addresses.battle_manager + 0x14B0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14B4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14B8, prior_input_pairs);
        memory.Set(addresses.battle_manager + 0x14C0, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x14C4, std::int32_t{2});
        memory.Set(addresses.battle_manager + 0x1470, round_sequence);
        memory.Set(addresses.battle_manager + 0x1478, std::int32_t{3});
        memory.Set(addresses.battle_manager + 0x147C, std::int32_t{8});
        memory.Set(addresses.battle_manager + 0x1480, std::uint8_t{2});
        memory.Set(round_sequence, std::array<std::uint8_t, 3>{2, 3, 5});
        memory.Set(addresses.frame_counter, std::uint32_t{42});
        memory.Set(addresses.input_log + 0x3A0, std::int32_t{3});
        memory.Set(addresses.input_log + 0x3A4, std::int32_t{42});
        memory.Set(addresses.input_log + 0x398, std::int32_t{2});
        memory.Set(addresses.input_log + 0x3C0, std::int32_t{3});
        memory.Set(addresses.input_log + 0x3C4, std::uint32_t{42});
        memory.Set(addresses.input_log + 0x3C8, std::uint32_t{0x10});
        memory.Set(addresses.input_log + 0x3CC, std::uint8_t{1});
        memory.Set(addresses.battle_manager + 0x1488, std::int32_t{3});
        memory.Set(addresses.battle_manager + 0x148C, std::uint32_t{42});
        memory.Set(addresses.battle_manager + 0x1490, std::uint32_t{11});
        memory.Set(addresses.battle_manager + 0x14F0, std::int32_t{0});
        memory.Set(addresses.battle_manager + 0x1462, std::uint8_t{0});
        memory.Set(addresses.battle_manager + 0x1463, std::uint8_t{0});
        memory.Set(previous_inputs, std::array<std::uint32_t, 2>{0x10, 0x20});
        memory.Set(input_pairs, std::array<PlayerInput, 2>{{{0x10, 0x10}, {0x20, 0x20}}});
        memory.Set(prior_input_pairs, std::array<PlayerInput, 2>{{{0x08, 0x08}, {0x10, 0x10}}});
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
        for (std::size_t index = 0; index < 0x40; ++index)
            memory.Set(addresses.vm_freeze_record + index,
                std::byte{static_cast<unsigned char>(0x40 + index)});
        const auto emitter_sentinel = memory_base + 0x10500;
        const auto emitter_node_one = memory_base + 0x10600;
        const auto emitter_node_two = memory_base + 0x10620;
        const auto emitter_one = memory_base + 0x10700;
        const auto emitter_two = memory_base + 0x10800;
        memory.Set(addresses.stage_wind_emitter_list, emitter_sentinel);
        memory.Set(emitter_sentinel, emitter_node_one);
        memory.Set(emitter_sentinel + 8, emitter_node_two);
        memory.Set(emitter_node_one, emitter_node_two);
        memory.Set(emitter_node_one + 8, emitter_sentinel);
        memory.Set(emitter_node_one + 0x10, emitter_one);
        memory.Set(emitter_node_one + 0x18, memory_base + 0x10900);
        memory.Set(emitter_node_two, emitter_sentinel);
        memory.Set(emitter_node_two + 8, emitter_node_one);
        memory.Set(emitter_node_two + 0x10, emitter_two);
        memory.Set(emitter_node_two + 0x18, memory_base + 0x10920);
        memory.Fill(emitter_one, native_stage_wind_emitter_state_size,
            std::byte{0x31});
        memory.Fill(emitter_two, native_stage_wind_emitter_state_size,
            std::byte{0x42});
        memory.Set(addresses.pending_hit_record, std::uint32_t{0x1234});
        memory.Set(addresses.pending_hit_record + 4, 0.25f);
        memory.Set(addresses.pending_hit_record + 8, addresses.fighter_roots[1]);
        memory.Set(addresses.pending_hit_record + 0x10, std::uint32_t{0x400000});
        memory.Set(addresses.pending_launcher_sync, std::uint8_t{1});

        for (std::size_t index = 0; index < native_camera_action_count; ++index)
        {
            const auto action = addresses.camera_action_backing + index * 0x3E0;
            memory.Set(action, image_base + std::uintptr_t{0x3E88000 + index * 8});
        }
        const auto player_watch = addresses.camera_action_backing + 3 * 0x3E0;
        memory.Set(player_watch, image_base + std::uintptr_t{0x3E87EB0});
        for (std::size_t index = 0; index < 16; ++index)
            memory.Set(player_watch + 0x25C + index * sizeof(float),
                static_cast<float>(100 + index));
        memory.Set(player_watch + 0x29C, std::int32_t{11});
        memory.Set(player_watch + 0x2A0, std::uint32_t{7});

        event_masks = memory_base + 0x2000;
        memory.Set(addresses.move_dispatch + 0x470, memory_base + 0x2100);
        memory.Set(addresses.move_dispatch + 0x478, std::int32_t{3});
        memory.Set(addresses.move_dispatch + 0x47C, std::int32_t{2});
        memory.Set(addresses.move_dispatch + 0x480, std::uint8_t{0});
        memory.Set(addresses.move_dispatch + 0x484, std::int32_t{10});
        memory.Set(addresses.move_dispatch + 0x488, std::uint8_t{1});
        memory.Set(addresses.move_dispatch + 0x490, std::uint32_t{0});
        memory.Set(addresses.move_dispatch + 0x494, std::int32_t{4});
        memory.Set(addresses.move_dispatch + 0x498, std::uintptr_t{});
        memory.Set(addresses.move_dispatch + 0x4A0, std::int32_t{0});
        memory.Set(addresses.move_dispatch + 0x4A4, std::int32_t{0});
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

        constexpr std::array<std::ptrdiff_t, 2> bank_offsets{0x35A0, 0x27760};
        constexpr std::array<std::size_t, 2> bank_bytes{
            motion_bank_primary_bytes, motion_bank_secondary_bytes};
        std::uintptr_t next_buffer = memory_base + 0xC0000;
        for (std::size_t player = 0; player < 2; ++player)
        {
            for (std::size_t index = 0;
                 index < native_movevm_state_short_count; ++index)
            {
                memory.Set(addresses.fighter_roots[player] + 0x197C
                        + index * sizeof(std::uint16_t),
                    static_cast<std::uint16_t>(
                        0x100 * (player + 1) + index));
            }
            memory.Set(addresses.fighter_roots[player] + 0x42550,
                std::int32_t{768});
            for (std::size_t bank_index = 0; bank_index < 2; ++bank_index)
            {
                const auto bank = addresses.fighter_roots[player]
                    + bank_offsets[bank_index];
                memory.Set(bank, image_base + std::uintptr_t{0x3E90000
                    + player * 0x100 + bank_index * 8});
                std::array<std::uintptr_t, 3> buffers{};
                for (std::size_t slot = 0; slot < 3; ++slot)
                {
                    buffers[slot] = next_buffer;
                    next_buffer += bank_bytes[bank_index];
                    memory.Set(bank + 8 + slot * 8, buffers[slot]);
                    memory.Fill(buffers[slot], bank_bytes[bank_index],
                        std::byte{static_cast<unsigned char>(
                            0x10 + player * 8 + bank_index * 3 + slot)});
                }
                memory.Set(bank + 0x20, std::uint32_t{1});
                memory.Set(bank + 0x28, buffers[1]);
                memory.Set(bank + 0x30, buffers[2]);
            }
            memory.Fill(addresses.fighter_roots[player]
                    + motion_tail_fighter_offset,
                motion_tail_bytes,
                std::byte{static_cast<unsigned char>(0x90 + player)});

            const auto stack = addresses.fighter_roots[player]
                + secondary_event_stack_fighter_offset;
            const auto table = memory_base + 0x40000 + player * 0x3000;
            const auto headers = table + 0x1000;
            const auto payloads = table + 0x2000;
            memory.Set(stack + 0x240, table);
            memory.Set(stack + 0x248, headers);
            memory.Set(stack + 0x250, payloads);
            memory.Set(table + 0x14, std::int32_t{3});
            for (std::size_t slot = 0;
                 slot < secondary_event_slot_count; ++slot)
            {
                const auto address = stack + slot * 0x18;
                memory.Fill(address, 8, std::byte{static_cast<unsigned char>(
                    0x20 + player)});
                memory.Set(address + 8, addresses.fighter_roots[player]);
                memory.Fill(address + 0x10, 8,
                    std::byte{static_cast<unsigned char>(0x40 + slot)});
            }
            memory.Fill(stack + 0x258, 8,
                std::byte{static_cast<unsigned char>(0x60 + player)});
            for (std::size_t index = 0; index < 3; ++index)
                memory.Set(headers + index * 8 + 2,
                    static_cast<std::uint16_t>(10 + player * 3 + index));

            const auto packed = memory_base + 0x90000 + player * 0x10000;
            memory.Set(packed, std::array<std::uint32_t, 5>{
                3, 0, 0, 0x100, 0x200});
            const auto section_table = packed + 0x100;
            memory.Set(section_table, std::array<std::uint32_t, 4>{
                2, 0x40, 0x60, 0x80});
            memory.Fill(section_table + 0x40, 0x40,
                std::byte{static_cast<unsigned char>(0x71 + player)});
            const auto clip = addresses.fighter_roots[player]
                + chara_anim_clip_player_offset;
            memory.Set(addresses.fighter_roots[player]
                    + chara_anim_slot_controller_offset,
                packed);
            memory.Set(clip, addresses.fighter_roots[player]);
            memory.Set(clip + 8, section_table + 0x40);
            memory.Fill(clip + 0x10, 0x20,
                std::byte{static_cast<unsigned char>(0x81 + player)});
            const auto runtime = addresses.fighter_roots[player]
                + chara_anim_runtime_offset;
            memory.Set(runtime, section_table + 0x40);
            memory.Fill(runtime + 8, 8,
                std::byte{static_cast<unsigned char>(0x91 + player)});

            const auto cue_owner = addresses.fighter_roots[player]
                + pose_event_cue_owner_offset;
            const auto scheduler = memory_base + 0x80000 + player * 0x2000;
            const auto head = scheduler + 0x100;
            const auto node_one = scheduler + 0x200;
            const auto node_two = scheduler + 0x220;
            const auto object_one = scheduler + 0x400;
            const auto object_two = scheduler + 0x420;
            memory.Set(cue_owner,
                image_base + std::uintptr_t{0x3EA0000 + player * 8});
            memory.Fill(cue_owner + 8, 0x20,
                std::byte{static_cast<unsigned char>(0xA1 + player)});
            memory.Set(cue_owner + 0x28, packed + 0x300);
            memory.Set(cue_owner + 0x30, scheduler);
            memory.Set(scheduler,
                image_base + std::uintptr_t{0x3EA0100 + player * 8});
            memory.Set(scheduler + 8, addresses.fighter_roots[player]);
            memory.Fill(scheduler + 0x10, 0x5C,
                std::byte{static_cast<unsigned char>(0xB1 + player)});
            memory.Set(scheduler + 0x70, head);
            memory.Set(scheduler + 0x78, std::uint64_t{2});
            memory.Set(head, node_one);
            memory.Set(head + 8, node_two);
            memory.Set(node_one, node_two);
            memory.Set(node_one + 8, head);
            memory.Set(node_one + 0x10, object_one);
            memory.Set(node_one + 0x18, scheduler + 0x600);
            memory.Set(node_two, head);
            memory.Set(node_two + 8, node_one);
            memory.Set(node_two + 0x10, object_two);
            memory.Set(node_two + 0x18, scheduler + 0x620);
            memory.Set(object_one,
                image_base + std::uintptr_t{0x3EA0200 + player * 0x10});
            memory.Set(object_two,
                image_base + std::uintptr_t{0x3EA0208 + player * 0x10});
            memory.Fill(object_one + 8, 0x18,
                std::byte{static_cast<unsigned char>(0xC1 + player)});
            memory.Fill(object_two + 8, 0x18,
                std::byte{static_cast<unsigned char>(0xD1 + player)});
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
    return {0x231, Schema::snapshot_schema_version, 11, 7,
        {101, 102}, 201, 301, 7};
}

Status noop_reconcile(void*, FrameCoordinate) noexcept
{
    return Status::success();
}

std::uintptr_t resolve_test_battle_audio_handler(
    void* user, std::size_t index) noexcept
{
    return user != nullptr && index < maximum_battle_audio_handlers
        ? (*static_cast<std::array<std::uintptr_t,
            maximum_battle_audio_handlers>*>(user))[index] : 0;
}

bool test_battle_audio_handler_overflow(void*) noexcept
{
    return false;
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
    auto wrong_allocation = context;
    ++wrong_allocation.allocation_generation;
    expect(
        shim.Restore(&fake_hgcpu_reader, wrong_allocation, image).code
            == FailureCode::RestorePreflightFailed,
        "local reconstruction allocation generation mismatch fails preflight");
    auto wrong_serializer = image;
    ++wrong_serializer.serializer_version;
    expect(
        shim.Restore(&fake_hgcpu_reader, context, wrong_serializer).code
            == FailureCode::RestorePreflightFailed,
        "local reconstruction serializer version mismatch fails preflight");
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

    CandidateCheckpointImage image{};
    image.native = native;
    image.battle_audio_selector.session_generation = native.session_generation;
    image.battle_audio_selector.round_generation = native.round_generation;
    image.battle_audio_selector.alternations[0] = 1;
    image.battle_audio_selector.observed_count = 1;
    image.move_dispatch.generation = native.round_generation;
    image.move_dispatch.phase = MoveDispatchActionModeState{};
    image.local_images.push_back(hgcpu);
    MotionBankSnapshot motion{fixture.memory};
    expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
        "bind checkpoint matrix-bank snapshot");
    LocalReconstructionImage motion_image{};
    expect(motion.Capture(motion_image).ok(),
        "capture checkpoint matrix-bank image");
    image.local_images.push_back(motion_image);
    SecondaryEventState secondary{fixture.memory};
    expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind checkpoint secondary-event state");
    expect(secondary.Capture(image.secondary_events).ok(),
        "capture checkpoint secondary-event state");
    CharaAnimationState animation{fixture.memory};
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind checkpoint character-animation state");
    expect(animation.Capture(image.chara_animation).ok(),
        "capture checkpoint character-animation state");
    image.ucrt = candidate_ucrt_image();
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
    Snapshot captured_snapshot{};
    auto captured_image = image;
    expect(CandidateCheckpointCodec::EncodeCaptured(
            {7, 30}, 0x9191, captured_image, captured_snapshot).ok()
            && captured_snapshot.local_images.size() == 2
            && captured_snapshot.canonical_hash == snapshot.canonical_hash,
        "fresh-capture encoding externalizes local images without changing canonical truth");
    auto canonical_image = image;
    canonical_image.local_images.clear();
    Snapshot canonical_snapshot{};
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 30}, 0x9191, canonical_image, canonical_snapshot).ok()
            && canonical_snapshot.bytes.empty()
            && canonical_snapshot.local_images.empty()
            && canonical_snapshot.canonical_hash == snapshot.canonical_hash
            && canonical_snapshot.canonical_components
                == snapshot.canonical_components
            && canonical_snapshot.canonical_native == snapshot.canonical_native
            && canonical_snapshot.canonical_move_dispatch
                == snapshot.canonical_move_dispatch
            && canonical_snapshot.canonical_input == snapshot.canonical_input
            && canonical_snapshot.canonical_wind_semantic
                == snapshot.canonical_wind_semantic
            && canonical_snapshot.canonical_wind == snapshot.canonical_wind
            && canonical_snapshot.canonical_wind_node
                == snapshot.canonical_wind_node,
        "canonical-only encoding preserves exact identity without restore payloads");
    canonical_image.wind.nodes.clear();
    Snapshot fresh_without_wind_node{};
    expect(CandidateCheckpointCodec::EncodeCanonical(
            {7, 31}, 0x9191, canonical_image, fresh_without_wind_node).ok()
            && CandidateCheckpointCodec::EncodeCanonical(
                {7, 31}, 0x9191, canonical_image, canonical_snapshot).ok()
            && canonical_snapshot.canonical_hash
                == fresh_without_wind_node.canonical_hash
            && canonical_snapshot.canonical_wind
                == fresh_without_wind_node.canonical_wind
            && canonical_snapshot.canonical_wind_semantic
                == fresh_without_wind_node.canonical_wind_semantic
            && canonical_snapshot.canonical_wind_node
                == fresh_without_wind_node.canonical_wind_node,
        "reused canonical output clears variable wind diagnostic tails");
    CandidateCheckpointImage decoded_captured{};
    expect(CandidateCheckpointCodec::Decode(
            captured_snapshot, decoded_captured).ok()
            && decoded_captured.local_images.size() == 2
            && decoded_captured.local_images[0].bytes == hgcpu.bytes,
        "decode validates and rejoins attached local reconstruction images");

    CandidateCheckpointImage decoded{};
    expect(CandidateCheckpointCodec::Decode(snapshot, decoded).ok(),
        "decode candidate checkpoint");
    expect(decoded.native == native,
        "candidate checkpoint round-trips typed native image");
    expect(decoded.battle_audio_selector == image.battle_audio_selector,
        "candidate checkpoint round-trips local battle-audio selector state");
    expect(decoded.move_dispatch == image.move_dispatch,
        "candidate checkpoint round-trips MoveDispatch semantic state");
    expect(decoded.local_images.size() == 2
            && decoded.local_images.front().serializer_id
                == LocalSerializerId::HgCpuDirect
            && decoded.local_images.front().serializer_version
                == hgcpu_direct_serializer_version
            && decoded.local_images.front().context == hgcpu.context
            && decoded.local_images.front().cursor == hgcpu.cursor
            && decoded.local_images.front().checksum == hgcpu.checksum
            && decoded.local_images.front().bytes == hgcpu.bytes
            && decoded.local_images[1].serializer_id
                == LocalSerializerId::MotionBankTriples
            && decoded.local_images[1].bytes == motion_image.bytes,
        "candidate checkpoint round-trips ordered local reconstruction images");
    expect(decoded.ucrt == image.ucrt,
        "candidate checkpoint round-trips value-only UCRT state");
    expect(decoded.wind == image.wind,
        "candidate checkpoint round-trips pointer-free wind state");

    auto changed_movevm_state = image;
    changed_movevm_state.native.movevm_state_shorts.fighters[0][25] ^= 1;
    Snapshot changed_movevm_state_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, changed_movevm_state,
            changed_movevm_state_snapshot).ok()
            && changed_movevm_state_snapshot.canonical_hash
                != snapshot.canonical_hash
            && changed_movevm_state_snapshot.canonical_native[30]
                != snapshot.canonical_native[30],
        "per-fighter MoveVM state shorts, including Tira behavior slot 25, are canonical truth");

    auto presentation_audio = image;
    presentation_audio.battle_audio_selector.alternations[0] = 0;
    Snapshot presentation_audio_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, presentation_audio,
            presentation_audio_snapshot).ok()
            && presentation_audio_snapshot.canonical_hash
                == snapshot.canonical_hash
            && presentation_audio_snapshot.bytes != snapshot.bytes,
        "battle-audio selector remains local-restorable but excluded from canonical peer truth");

    auto presentation_wind = image;
    presentation_wind.wind.nodes.front().derived_state.front() ^= std::byte{1};
    presentation_wind.wind.output_force.front() ^= std::byte{1};
    Snapshot presentation_snapshot{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, presentation_wind, presentation_snapshot).ok()
            && presentation_snapshot.canonical_hash == snapshot.canonical_hash
            && presentation_snapshot.bytes != snapshot.bytes,
        "node and root wind sampled/output force remain local-restorable but are excluded "
        "from canonical peer truth");

    auto duplicate_local = image;
    duplicate_local.local_images[1] = hgcpu;
    Snapshot rejected_duplicate{};
    expect(CandidateCheckpointCodec::Encode(
            {7, 30}, 0x9191, duplicate_local, rejected_duplicate).code
            == FailureCode::IdentityMismatch,
        "checkpoint rejects an unsupported duplicate local serializer");

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
    const std::array local_marker{
        std::byte{0x80}, std::byte{0x81}, std::byte{0x82}, std::byte{0x83},
        std::byte{0x84}, std::byte{0x85}, std::byte{0x86}, std::byte{0x87}};
    const auto local_byte = std::search(corrupted.bytes.begin(),
        corrupted.bytes.end(), local_marker.begin(), local_marker.end());
    expect(local_byte != corrupted.bytes.end(),
        "checkpoint contains opaque local reconstruction payload");
    if (local_byte != corrupted.bytes.end()) *local_byte ^= std::byte{1};
    expect(CandidateCheckpointCodec::Decode(corrupted, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects corrupted local reconstruction bytes");

    Snapshot wrong_generation = snapshot;
    ++wrong_generation.coordinate.generation;
    expect(CandidateCheckpointCodec::Decode(wrong_generation, decoded).code
            == FailureCode::RestoreVerificationFailed,
        "checkpoint rejects native generation drift");
}

void test_motion_bank_snapshot_is_bounded_and_transactional()
{
    Fixture fixture;
    MotionBankSnapshot motion{fixture.memory};
    expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
        "bind all-three-slot motion-bank topology");
    LocalReconstructionImage baseline{};
    expect(motion.Capture(baseline).ok()
            && baseline.bytes.size() == motion_bank_image_bytes
            && MotionBankSnapshot::ValidateLocalImage(baseline),
        "capture bounded pointer-free motion-bank image");
    expect(!contains_qword(baseline.bytes, Fixture::memory_base + 0xC0000)
            && std::all_of(baseline.bytes.begin(), baseline.bytes.begin() + 8,
                [](std::byte value) {
                    return std::to_integer<std::uint8_t>(value) < 3;
                }),
        "motion image stores slot identities rather than live pointers");

    const auto first_primary_slot = Fixture::memory_base + 0xC0000;
    fixture.memory.Fill(first_primary_slot, motion_bank_primary_bytes,
        std::byte{0xE1});
    fixture.memory.Fill(fixture.addresses.fighter_roots[0]
            + motion_tail_fighter_offset,
        motion_tail_bytes, std::byte{0xE2});
    const auto first_primary_bank = fixture.addresses.fighter_roots[0] + 0x35A0;
    fixture.memory.Set(first_primary_bank + 0x20, std::uint32_t{2});
    fixture.memory.Set(first_primary_bank + 0x28,
        first_primary_slot + 2 * motion_bank_primary_bytes);
    fixture.memory.Set(first_primary_bank + 0x30, first_primary_slot);
    expect(motion.RestoreTransactional(baseline).ok(),
        "restore all motion slots and slot controller atomically");
    LocalReconstructionImage restored{};
    expect(motion.Capture(restored).ok() && restored.bytes == baseline.bytes,
        "motion-bank restore recaptures exact local image");

    fixture.memory.Set(first_primary_bank + 8,
        Fixture::memory_base + 0x150000);
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(motion.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "motion allocation replacement invalidates without mutation");
    fixture.memory.Set(first_primary_bank + 8, first_primary_slot);

    auto corrupt = baseline;
    ++corrupt.checksum;
    expect(motion.RestoreTransactional(corrupt).code
            == FailureCode::RestorePreflightFailed,
        "motion checksum mismatch fails preflight");

    fixture.memory.Fill(first_primary_slot, motion_bank_primary_bytes,
        std::byte{0xA7});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(motion.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial motion write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial motion write restores the complete undo image exactly");
}

void test_secondary_event_state_is_pointer_free_and_transactional()
{
    Fixture fixture;
    SecondaryEventState secondary{fixture.memory};
    expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind secondary-event pointer topology");
    SecondaryEventStateImage baseline{};
    expect(secondary.Capture(baseline).ok(),
        "capture pointer-free secondary-event state");
    const auto canonical = SecondaryEventState::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, fixture.addresses.fighter_roots[0])
            && !contains_qword(canonical, fixture.addresses.fighter_roots[1]),
        "secondary-event canonical state excludes fighter back-pointers");

    const auto stack = fixture.addresses.fighter_roots[0]
        + secondary_event_stack_fighter_offset;
    const auto preserved_back_pointer = fixture.addresses.fighter_roots[0]
        + 0x111;
    fixture.memory.Fill(stack, 8, std::byte{0xE1});
    fixture.memory.Set(stack + 8, preserved_back_pointer);
    fixture.memory.Fill(stack + 0x10, 8, std::byte{0xE2});
    fixture.memory.Fill(stack + 0x258, 8, std::byte{0xE3});
    fixture.memory.Set(Fixture::memory_base + 0x41000 + 2,
        std::uint16_t{0x7777});
    expect(secondary.RestoreTransactional(baseline).ok(),
        "restore secondary-event semantic fields and cursors");
    SecondaryEventStateImage restored{};
    expect(secondary.Capture(restored).ok() && restored == baseline,
        "secondary-event restore recaptures exact typed image");
    std::uintptr_t observed_back_pointer{};
    expect(fixture.memory.Read(stack + 8,
            std::as_writable_bytes(std::span{&observed_back_pointer, 1}))
            && observed_back_pointer == preserved_back_pointer,
        "secondary-event restore does not overwrite fighter back-pointers");

    const auto table_pointer_address = stack + 0x240;
    fixture.memory.Set(table_pointer_address, std::uintptr_t{});
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(secondary.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "secondary-event allocation replacement fails without mutation");
    fixture.memory.Set(table_pointer_address, Fixture::memory_base + 0x40000);

    auto wrong_count = baseline;
    ++wrong_count.header_counts[0];
    const auto before_count_rejection = fixture.memory.bytes();
    expect(secondary.RestoreTransactional(wrong_count).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_count_rejection,
        "secondary-event header-count mismatch fails before undo capture");

    fixture.memory.Fill(stack, 8, std::byte{0xA1});
    fixture.memory.Fill(stack + 0x10, 8, std::byte{0xA2});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(secondary.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial secondary-event write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial secondary-event write restores the complete undo image exactly");
}

void test_chara_animation_state_normalizes_sections_and_undoes_exactly()
{
    Fixture fixture;
    CharaAnimationState animation{fixture.memory};
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "bind character-animation scheduler topology");
    CharaAnimationStateImage baseline{};
    expect(animation.Capture(baseline).ok()
            && baseline.players[0].clip_section.present
            && baseline.players[0].clip_section.index == 0
            && baseline.players[0].trigger_count == 2,
        "capture normalized clip section and bounded trigger state");
    const auto canonical = CharaAnimationState::CanonicalBytes(baseline);
    const auto fighter = fixture.addresses.fighter_roots[0];
    const auto scheduler = Fixture::memory_base + 0x80000;
    expect(!contains_qword(canonical, fighter)
            && !contains_qword(canonical, scheduler)
            && !contains_qword(canonical, scheduler + 0x100)
            && !contains_qword(canonical, scheduler + 0x400),
        "character-animation canonical state excludes owner, list, and payload pointers");

    const auto packed = Fixture::memory_base + 0x90000;
    const auto section_table = packed + 0x100;
    const auto clip = fighter + chara_anim_clip_player_offset;
    const auto runtime = fighter + chara_anim_runtime_offset;
    fixture.memory.Set(scheduler + 8, fixture.addresses.fighter_roots[1]);
    CharaAnimationStateImage cross_fighter_scheduler{};
    expect(animation.Capture(cross_fighter_scheduler).code
            == FailureCode::IdentityMismatch,
        "scheduler character rebinding invalidates the checkpoint generation");
    expect(animation.Bind(fixture.addresses.fighter_roots, 8).ok()
            && animation.Capture(cross_fighter_scheduler).ok()
            && cross_fighter_scheduler.players[0].scheduler_chara_bound,
        "scheduler rebind starts a fresh pointer-free checkpoint generation");
    fixture.memory.Set(scheduler + 8, fighter);
    expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
        "restore original animation topology after generation test");

    fixture.memory.Set(clip + 0x28, std::uint32_t{});
    fixture.memory.Set(runtime, Fixture::memory_base + 0xA8000);
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xD7});
    CharaAnimationStateImage dormant{};
    expect(animation.Capture(dormant).ok()
            && !dormant.players[0].runtime_section.present
            && std::all_of(dormant.players[0].runtime_scalars.begin(),
                dormant.players[0].runtime_scalars.end(),
                [](std::byte value) { return value == std::byte{}; }),
        "inactive clip canonicalizes lagging presentation cleanup as absent");
    expect(animation.RestoreTransactional(baseline).ok(),
        "active clip restore reconstructs runtime after inactive cleanup lag");

    fixture.memory.Set(clip + 0x28, std::uint32_t{1});
    fixture.memory.Set(clip + 0x2C, std::uint32_t{});
    fixture.memory.Set(runtime, std::uintptr_t{});
    fixture.memory.Fill(runtime + 8, 8, std::byte{});
    CharaAnimationStateImage pending_bootstrap{};
    expect(animation.Capture(pending_bootstrap).ok()
            && !pending_bootstrap.players[0].runtime_section.present,
        "active pre-bootstrap clip preserves the native null runtime boundary");
    fixture.memory.Set(clip + 0x2C, std::uint32_t{1});
    fixture.memory.Set(runtime, section_table + 0x40);
    expect(animation.RestoreTransactional(pending_bootstrap).ok(),
        "active pre-bootstrap restore reproduces the native null runtime boundary");

    fixture.memory.Set(clip + 8, section_table + 0x60);
    fixture.memory.Set(clip + 0x2C, std::uint32_t{1});
    fixture.memory.Set(runtime, section_table + 0x40);
    CharaAnimationStateImage rebound_clip{};
    expect(animation.Capture(rebound_clip).ok()
            && rebound_clip.players[0].clip_section.present
            && rebound_clip.players[0].runtime_section.present
            && rebound_clip.players[0].clip_section
                != rebound_clip.players[0].runtime_section,
        "active clip rebinding preserves the independently consumed runtime section");
    fixture.memory.Set(runtime, section_table + 0x60);
    expect(animation.RestoreTransactional(rebound_clip).ok(),
        "animation restore reconstructs distinct clip and runtime section identities");
    expect(animation.RestoreTransactional(baseline).ok(),
        "bootstrapped clip restore reconstructs runtime after pre-bootstrap state");

    fixture.memory.Set(clip + 8, section_table + 0x60);
    fixture.memory.Fill(clip + 0x10, 0x20, std::byte{0xE1});
    fixture.memory.Set(runtime, section_table + 0x60);
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xE2});
    fixture.memory.Fill(scheduler + 0x10, 0x5C, std::byte{0xE3});
    fixture.memory.Fill(scheduler + 0x408, 0x18, std::byte{0xE4});
    fixture.memory.Set(scheduler + 0x6C, std::uint32_t{0x12345678});
    expect(animation.RestoreTransactional(baseline).ok(),
        "restore animation scalars and reconstruct authored section pointers");
    CharaAnimationStateImage restored{};
    expect(animation.Capture(restored).ok() && restored == baseline,
        "character-animation restore recaptures exact pointer-free image");
    std::uintptr_t restored_clip_pointer{}, restored_runtime_pointer{};
    expect(fixture.memory.Read(clip + 8,
                std::as_writable_bytes(std::span{&restored_clip_pointer, 1}))
            && fixture.memory.Read(runtime,
                std::as_writable_bytes(std::span{&restored_runtime_pointer, 1}))
            && restored_clip_pointer == section_table + 0x40
            && restored_runtime_pointer == section_table + 0x40,
        "animation restore derives pointers from live packed-data topology");
    std::uint32_t allocator_residue{};
    expect(fixture.memory.Read(scheduler + 0x6C,
                std::as_writable_bytes(std::span{&allocator_residue, 1}))
            && allocator_residue == 0x12345678,
        "animation restore preserves noncanonical scheduler allocator residue");

    fixture.memory.Set(scheduler + 0x200, scheduler + 0x100);
    const auto before_topology_rejection = fixture.memory.bytes();
    expect(animation.RestoreTransactional(baseline).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_topology_rejection,
        "animation list replacement fails before mutation");
    fixture.memory.Set(scheduler + 0x200, scheduler + 0x220);

    auto wrong_count = baseline;
    ++wrong_count.players[0].trigger_count;
    const auto before_count_rejection = fixture.memory.bytes();
    expect(animation.RestoreTransactional(wrong_count).code
            == FailureCode::RestorePreflightFailed
            && fixture.memory.bytes() == before_count_rejection,
        "animation trigger-count drift fails before undo capture");

    fixture.memory.Fill(clip + 0x10, 0x20, std::byte{0xA1});
    fixture.memory.Fill(runtime + 8, 8, std::byte{0xA2});
    const auto before_partial_failure = fixture.memory.bytes();
    fixture.memory.FailWrite(4);
    expect(animation.RestoreTransactional(baseline).code
            == FailureCode::RestoreWriteFailed,
        "partial character-animation write reports transactional failure");
    fixture.memory.AllowWrites();
    expect(fixture.memory.bytes() == before_partial_failure,
        "partial character-animation write restores the complete undo image exactly");
}

class EmptyStageWindAllocator final : public IStageWindAllocator
{
public:
    std::uintptr_t Allocate(std::size_t) noexcept override { return 0; }
    void Free(std::uintptr_t) noexcept override {}
};

void test_battle_audio_selector_is_generation_bound_and_transactional()
{
    Fixture fixture;
    constexpr auto handler = Fixture::memory_base + 0x155000;
    std::array<std::uintptr_t, maximum_battle_audio_handlers>
        observed_handlers{handler};
    fixture.memory.Set(handler,
        Fixture::image_base + std::uintptr_t{0x326A6C8});
    fixture.memory.Set(handler + 0x3E0, std::int32_t{});
    BattleAudioSelectorState selector{fixture.memory};
    const BattleAudioSelectorBinding binding{
        Fixture::image_base, 0x5000000, hgcpu_context(),
        &resolve_test_battle_audio_handler,
        &test_battle_audio_handler_overflow, &observed_handlers};
    expect(selector.Bind(binding).ok(),
        "bind generation-scoped battle-audio selector state");
    observed_handlers[0] = 0;
    BattleAudioSelectorImage undiscovered{};
    expect(selector.Capture(undiscovered).ok()
            && undiscovered.observed_count == 0
            && undiscovered.alternations[0] == 0,
        "capture the deterministic initial selector before handler discovery");
    observed_handlers[0] = handler;
    observed_handlers[1] = handler + 0x800;
    fixture.memory.Set(observed_handlers[1],
        Fixture::image_base + std::uintptr_t{0x326A6C8});
    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    fixture.memory.Set(observed_handlers[1] + 0x3E0, std::int32_t{1});
    expect(selector.RestoreTransactional(undiscovered).ok(),
        "restore a pre-discovery checkpoint after multiple handlers become known");
    std::int32_t second_restored{};
    fixture.memory.Read(observed_handlers[1] + 0x3E0,
        std::as_writable_bytes(std::span{&second_restored, 1}));
    expect(second_restored == 0,
        "pre-discovery restore initializes every later handler slot to zero");
    BattleAudioSelectorImage baseline{};
    expect(selector.Capture(baseline).ok() && baseline.observed_count == 2
            && baseline.alternations[0] == 0
            && baseline.alternations[1] == 0,
        "capture the ordered set of battle-audio selectors");

    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    expect(selector.RestoreTransactional(baseline).ok(),
        "restore battle-audio selector before semantic replay");
    std::int32_t restored{};
    fixture.memory.Read(handler + 0x3E0,
        std::as_writable_bytes(std::span{&restored, 1}));
    expect(restored == 0, "battle-audio selector restore writes exact value");

    auto wrong_generation = baseline;
    ++wrong_generation.round_generation;
    expect(selector.PreflightRestore(wrong_generation).code
            == FailureCode::GenerationMismatch,
        "battle-audio selector rejects generation drift before mutation");

    fixture.memory.Set(handler + 0x3E0, std::int32_t{1});
    fixture.memory.CorruptAfterWrite(
        1, handler + 0x3E0, std::byte{0x02});
    expect(selector.RestoreTransactional(baseline).code
            == FailureCode::RestoreVerificationFailed,
        "battle-audio selector reports post-write verification failure");
    fixture.memory.AllowWrites();
    fixture.memory.Read(handler + 0x3E0,
        std::as_writable_bytes(std::span{&restored, 1}));
    expect(restored == 1,
        "battle-audio selector verification failure restores exact undo value");

    observed_handlers[0] = handler + 0x800;
    expect(selector.PreflightRestore(baseline).code
            == FailureCode::IdentityMismatch,
        "battle-audio selector rejects handler identity drift");
    observed_handlers[0] = handler;
    fixture.memory.Set(handler, Fixture::image_base + std::uintptr_t{0x1234});
    selector.Reset();
    expect(selector.Bind(binding).ok()
            && selector.Capture(baseline).code
                == FailureCode::CapturePreflightFailed,
        "battle-audio selector rejects an invalid handler vtable");
    selector.Reset();
    expect(selector.Capture(baseline).code == FailureCode::AdapterUnqualified,
        "battle-audio selector lifecycle reset clears the binding");
}

struct CandidateWindFixture
{
    explicit CandidateWindFixture(Fixture& fixture)
        : probe(fixture.memory), transaction(fixture.memory, allocator),
          audio_selector(fixture.memory),
          motion(fixture.memory), secondary(fixture.memory),
          animation(fixture.memory), move_dispatch(fixture.memory)
    {
        addresses = {Fixture::image_base, 0x4300000,
            Fixture::memory_base + 0x1F000, 7};
        root = Fixture::memory_base + 0x1E000;
        fixture.memory.Set(addresses.root_pointer, root);
        fixture.memory.Set(root, std::uintptr_t{});
        fixture.memory.Set(root + 0x98, std::uint32_t{});
        fixture.memory.Set(root + 0x9C, std::int32_t{});
        expect(probe.Bind(addresses).ok(), "bind empty candidate wind fixture");
        expect(motion.Bind(fixture.addresses.fighter_roots, hgcpu_context()).ok(),
            "bind candidate matrix-bank fixture");
        expect(secondary.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "bind candidate secondary-event fixture");
        expect(animation.Bind(fixture.addresses.fighter_roots, 7).ok(),
            "bind candidate character-animation fixture");
        expect(move_dispatch.Bind(fixture.addresses.move_dispatch, 7).ok(),
            "bind candidate MoveDispatch fixture");
        handlers[0] = Fixture::memory_base + 0x155000;
        fixture.memory.Set(handlers[0],
            Fixture::image_base + std::uintptr_t{0x326A6C8});
        fixture.memory.Set(handlers[0] + 0x3E0, std::int32_t{});
        const BattleAudioSelectorBinding selector_binding{
            Fixture::image_base, 0x5000000, hgcpu_context(),
            &resolve_test_battle_audio_handler,
            &test_battle_audio_handler_overflow, &handlers};
        expect(audio_selector.Bind(selector_binding).ok(),
            "bind candidate battle-audio selector fixture");
    }

    EmptyStageWindAllocator allocator;
    StageWindTopologyProbe probe;
    StageWindGraphTransaction transaction;
    BattleAudioSelectorState audio_selector;
    MotionBankSnapshot motion;
    SecondaryEventState secondary;
    CharaAnimationState animation;
    MoveDispatchState move_dispatch;
    StageWindTopologyAddresses addresses{};
    std::uintptr_t root{};
    std::array<std::uintptr_t, maximum_battle_audio_handlers> handlers{};
};

CandidateAdapterBinding candidate_binding(
    HgCpuExecFn reader, CandidateWindFixture& wind)
{
    prepare_candidate_ucrt_broker();
    CandidateAdapterBinding binding{};
    binding.context = NativeContext{7, 11, {101, 102}, 201};
    binding.battle_audio_selector = &wind.audio_selector;
    binding.hgcpu_context = hgcpu_context();
    binding.hgcpu_writer = &fake_hgcpu_writer;
    binding.hgcpu_reader = reader;
    binding.motion_banks = &wind.motion;
    binding.move_dispatch = &wind.move_dispatch;
    binding.secondary_events = &wind.secondary;
    binding.chara_animation = &wind.animation;
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
    const auto baseline_snapshot = restored_snapshots.Load({7, 0});
    expect(baseline_snapshot.has_value(),
        "retain candidate baseline for canonical recapture verification");
    hgcpu_payload.fill(std::byte{0x7E});
    expect(restored_adapter.VerifyRestoredState(*baseline_snapshot).ok(),
        "opaque native recapture bytes are not canonical verification fields");
    hgcpu_payload = initial_hgcpu;
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
    fixture.memory.Fill(fixture.addresses.vm_freeze_record, 0x40,
        std::byte{0xE7});
    fixture.memory.Fill(Fixture::memory_base + 0x10700,
        native_stage_wind_emitter_state_size, std::byte{0xE6});
    fixture.memory.Fill(Fixture::memory_base + 0x10800,
        native_stage_wind_emitter_state_size, std::byte{0xE5});
    fixture.memory.Fill(Fixture::memory_base + 0x10700
            + native_stage_wind_emitter_state_size,
        8, std::byte{0xA9});
    fixture.memory.Set(fixture.addresses.pending_hit_record, std::uint32_t{0xABCD});
    fixture.memory.Set(fixture.addresses.pending_hit_record + 4, -0.125f);
    fixture.memory.Set(fixture.addresses.pending_hit_record + 8,
        fixture.addresses.fighter_roots[0]);
    fixture.memory.Set(fixture.addresses.pending_hit_record + 0x10,
        std::uint32_t{0x200000});
    fixture.memory.Set(fixture.addresses.pending_launcher_sync, std::uint8_t{0});
    const auto player_watch = fixture.addresses.camera_action_backing + 3 * 0x3E0;
    fixture.memory.Fill(player_watch + 0x25C, 16 * sizeof(float), std::byte{0xCC});
    fixture.memory.Set(player_watch + 0x29C, std::int32_t{19});
    fixture.memory.Set(player_watch + 0x2A0, std::uint32_t{12});
    fixture.memory.Set(fixture.addresses.frame_counter, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.input_log + 0x3A0, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.input_log + 0x3A4, std::int32_t{99});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1488, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x148C, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1490, std::uint32_t{44});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x14F0, std::int32_t{7});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1462, std::uint8_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1463, std::uint8_t{3});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1478, std::int32_t{1});
    fixture.memory.Set(fixture.addresses.battle_manager + 0x1480, std::uint8_t{9});
    fixture.memory.Set(Fixture::memory_base + 0x1C300,
        std::array<std::uint8_t, 3>{9, 9, 9});
    fixture.memory.Fill(Fixture::memory_base + 0x1C000, 8, std::byte{0xEC});
    fixture.memory.Fill(Fixture::memory_base + 0x1C100, 16, std::byte{0xED});
    fixture.memory.Fill(Fixture::memory_base + 0x1C200, 16, std::byte{0xEE});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C0, std::int32_t{8});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C4, std::uint32_t{99});
    fixture.memory.Set(fixture.addresses.input_log + 0x3C8, std::uint32_t{0xFE});
    fixture.memory.Set(fixture.addresses.input_log + 0x3CC, std::uint8_t{0});

    fixture.memory.Fill(fixture.addresses.pump_state + 0x3C, 1, std::byte{0xA1});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x0C, 1, std::byte{0xA6});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x18, 1, std::byte{0xA7});
    fixture.memory.Fill(fixture.addresses.scheduler_base + 0x5C, 1, std::byte{0xA8});
    fixture.memory.Fill(Fixture::memory_base + 0x500C, 1, std::byte{0xA2});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x2A28, 1, std::byte{0xA3});
    fixture.memory.Fill(fixture.addresses.move_command_base + 0x3034, 1, std::byte{0xA4});
    fixture.memory.Fill(fixture.addresses.slot_param_base + 0x28, 1, std::byte{0xA5});
    fixture.memory.Fill(fixture.addresses.input_log + 0x3CD, 3, std::byte{0xAF});

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
    expect(fixture.memory.Get(fixture.addresses.input_log + 0x3CD) == std::byte{0xAF},
        "preserve initialized cache-row reserved bytes");
    expect(restored.rng == baseline.rng,
        "restore all four explicit Lux RNG streams exactly");
    expect(restored.vm_freeze_record == baseline.vm_freeze_record,
        "restore the complete simulation freeze-output record exactly");
    expect(restored.stage_wind_emitters == baseline.stage_wind_emitters
            && restored.stage_wind_emitters.states.size() == 2,
        "restore bounded stage-wind emitter timers and admission state exactly");
    expect(fixture.memory.Get(Fixture::memory_base + 0x10700
            + native_stage_wind_emitter_state_size) == std::byte{0xA9},
        "preserve the unverified stage-wind emitter tail");
    expect(restored.frame == baseline.frame,
        "restore the coordinate clocks and input-pair boundary exactly");
    expect(restored.round_sequence == baseline.round_sequence,
        "restore the bounded round-state sequence values and current state");
    expect(restored.pending_hit == baseline.pending_hit
            && restored.pending_hit.attacker_slot == 2,
        "restore the pending-hit record through its fighter-relative slot");
    expect(restored.camera_distance_history == baseline.camera_distance_history
            && restored.camera_distance_history[3].present == 1
            && restored.camera_distance_history[3].sample_count == 11
            && restored.camera_distance_history[3].cursor == 7,
        "restore exact PlayerWatch distance-history ring and cursors");

    const auto canonical = NativeCandidateRegions::CanonicalBytes(baseline);
    expect(!contains_qword(canonical, fixture.event_masks), "canonical bytes exclude event owner pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x5000), "canonical bytes exclude SubVM pointer");
    expect(!contains_qword(canonical, fixture.addresses.input_log),
        "canonical bytes exclude InputLog owner pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x1C100),
        "canonical bytes exclude input-pair backing pointer");
    expect(!contains_qword(canonical, Fixture::memory_base + 0x1C300),
        "canonical bytes exclude round-sequence backing pointer");
    expect(!contains_qword(canonical, fixture.addresses.fighter_roots[0])
            && !contains_qword(canonical, fixture.addresses.fighter_roots[1]),
        "canonical bytes exclude pending-hit fighter pointers");
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

    Fixture camera_class;
    expect(camera_class.regions.Bind(camera_class.addresses).ok(),
        "bind camera-class drift fixture");
    expect(camera_class.regions.Capture(baseline).ok(),
        "capture camera-class drift baseline");
    camera_class.memory.Set(camera_class.addresses.camera_action_backing + 3 * 0x3E0,
        Fixture::image_base + std::uintptr_t{0x3E88018});
    const auto camera_before = camera_class.memory.bytes();
    expect(camera_class.regions.RestoreTransactional(baseline).code
            == FailureCode::IdentityMismatch,
        "camera action class drift rejects restore");
    expect(camera_class.memory.bytes() == camera_before,
        "camera class rejection performs zero mutation");

    Fixture camera_cursor;
    camera_cursor.memory.Set(
        camera_cursor.addresses.camera_action_backing + 3 * 0x3E0 + 0x2A0,
        std::uint32_t{16});
    expect(camera_cursor.regions.Bind(camera_cursor.addresses).code
            == FailureCode::CapturePreflightFailed,
        "PlayerWatch distance-history cursor outside the 16-slot ring fails closed");

    Fixture no_camera;
    no_camera.addresses.camera_action_backing = 0;
    NativeCandidateImage no_camera_image{};
    expect(no_camera.regions.Bind(no_camera.addresses).ok()
            && no_camera.regions.Capture(no_camera_image).ok()
            && std::all_of(no_camera_image.camera_distance_history.begin(),
                no_camera_image.camera_distance_history.end(),
                [](const NativeCameraDistanceHistoryImage& history) {
                    return history.present == 0;
                }),
        "an absent optional camera produces an empty bounded history image");

    Fixture prior_header;
    prior_header.memory.Set(
        prior_header.addresses.battle_manager + 0x14C4, std::int32_t{1});
    expect(prior_header.regions.Bind(prior_header.addresses).code
            == FailureCode::AdapterUnqualified,
        "prior-input TArray capacity below its two-player count is unqualified");
    const auto diagnostic = prior_header.regions.validation_diagnostic();
    expect(diagnostic.issue == NativeCandidateValidationIssue::IdentityRead
            && diagnostic.index == 22 && diagnostic.observed_a == 2
            && diagnostic.observed_b == 1,
        "prior-input TArray rejection identifies the exact invalid header");

    Fixture pending_owner;
    pending_owner.memory.Set(pending_owner.addresses.pending_hit_record + 8,
        Fixture::memory_base + 0x21000);
    expect(pending_owner.regions.Bind(pending_owner.addresses).code
            == FailureCode::CapturePreflightFailed,
        "pending-hit owner outside the bound fighter pair fails closed");
    const auto pending_diagnostic = pending_owner.regions.validation_diagnostic();
    expect(pending_diagnostic.issue
                == NativeCandidateValidationIssue::CandidateRegionRead
            && pending_diagnostic.index == 14,
        "pending-hit owner rejection identifies the fighter-slot mapping");

    Fixture oversized_sequence;
    oversized_sequence.memory.Set(
        oversized_sequence.addresses.battle_manager + 0x1478,
        std::int32_t{static_cast<std::int32_t>(native_round_sequence_max_states + 1)});
    expect(oversized_sequence.regions.Bind(oversized_sequence.addresses).code
            == FailureCode::AdapterUnqualified,
        "oversized round-state sequence fails closed before capture");
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

void test_stage_break_presentation_identity_is_generation_scoped()
{
    constexpr std::uintptr_t wall = 0x10001000;
    constexpr std::uintptr_t barrier = 0x10002000;
    constexpr std::uintptr_t wall_asset = 0x20001000;
    constexpr std::uintptr_t hit_asset = 0x20002000;
    constexpr std::uintptr_t break_asset = 0x20003000;
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    StageBreakListenerTopology topology{};
    topology.signature = 0x12345678;
    topology.actors = {
        {StageBreakActorKind::Wall, 7, 0, no_repeated_actor_reference},
        {StageBreakActorKind::Barrier, 9, 1, no_repeated_actor_reference},
        {StageBreakActorKind::Barrier, 9, 2, 1},
    };
    const std::array assets{
        StageBreakParticleAssetRef{wall, ParticleRoute::WallBreak, 0, wall_asset},
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierHit, 0, hit_asset},
        // A repeated template slot is one logical native asset identity.
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierHit, 1, hit_asset},
        StageBreakParticleAssetRef{barrier, ParticleRoute::BarrierBreak, 0, break_asset},
    };

    StageBreakPresentationIdentityMap identities{};
    expect(identities.Bind(11, actors, topology, assets).ok()
            && identities.bound() && identities.generation() == 11
            && identities.topology_signature() == topology.signature,
        "seal bounded stage-break presentation identity topology");
    StageBreakPresentationIdentity wall_identity{};
    StageBreakPresentationIdentity hit_identity{};
    StageBreakPresentationIdentity break_identity{};
    expect(identities.Resolve(11, wall, ParticleRoute::WallBreak,
                wall_asset, wall_identity).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierHit,
                hit_asset, hit_identity).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierBreak,
                break_asset, break_identity).ok()
            && wall_identity.owner_logical_id != wall
            && hit_identity.owner_logical_id != barrier
            && hit_identity.asset_logical_id != hit_asset
            && hit_identity.owner_logical_id == break_identity.owner_logical_id
            && hit_identity.asset_logical_id != break_identity.asset_logical_id,
        "resolve route-qualified pointer-free owner and asset identities");
    std::uint64_t resolved_owner{};
    std::uintptr_t resolved_actor{};
    std::uintptr_t resolved_asset{};
    expect(identities.ResolveActor(11, barrier, resolved_owner).ok()
            && resolved_owner == hit_identity.owner_logical_id
            && identities.ResolveActorAddress(11, resolved_owner,
                StageBreakActorKind::Barrier, resolved_actor).ok()
            && resolved_actor == barrier
            && identities.ResolveAssetAddress(11, resolved_owner,
                hit_identity.asset_logical_id, ParticleRoute::BarrierHit,
                resolved_actor, resolved_asset).ok()
            && resolved_actor == barrier && resolved_asset == hit_asset,
        "reverse logical stage identities only within their native generation");
    StageBreakPresentationIdentity rejected{};
    expect(identities.Resolve(12, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::GenerationMismatch
            && rejected.owner_logical_id == 0
            && identities.Resolve(11, barrier, ParticleRoute::WallBreak,
                hit_asset, rejected).code == FailureCode::UnsupportedContent,
        "generation drift and cross-route aliases fail closed");
    expect(identities.ResolveActorAddress(12, resolved_owner,
                StageBreakActorKind::Barrier, resolved_actor).code
                == FailureCode::GenerationMismatch
            && resolved_actor == 0
            && identities.ResolveAssetAddress(11, resolved_owner,
                hit_identity.asset_logical_id, ParticleRoute::WallBreak,
                resolved_actor, resolved_asset).code
                == FailureCode::UnsupportedContent
            && resolved_actor == 0 && resolved_asset == 0,
        "reverse logical identity rejects generation and route drift");

    auto replacement_actors = actors;
    replacement_actors[1].address = 0x10004000;
    replacement_actors[2].address = 0x10004000;
    expect(identities.Bind(12, replacement_actors, topology, {}).ok()
            && identities.Resolve(11, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::GenerationMismatch
            && identities.Resolve(12, barrier, ParticleRoute::BarrierHit,
                hit_asset, rejected).code == FailureCode::UnsupportedContent,
        "allocation replacement atomically revokes prior native bindings");

    auto invalid_topology = topology;
    invalid_topology.actors[2].repeated_reference_of = 0;
    expect(identities.Bind(13, actors, invalid_topology, assets).code
            == FailureCode::IdentityMismatch
            && !identities.bound(),
        "invalid repeated-reference topology leaves the identity map unbound");
}

void test_stage_break_particle_asset_capture_is_bounded_and_atomic()
{
    constexpr std::uintptr_t base = 0x30000000;
    constexpr std::uintptr_t wall = base + 0x1000;
    constexpr std::uintptr_t barrier = base + 0x2000;
    constexpr std::uintptr_t hit_array = base + 0x5000;
    constexpr std::uintptr_t wall_asset = base + 0x7000;
    constexpr std::uintptr_t hit_asset0 = base + 0x7100;
    constexpr std::uintptr_t hit_asset1 = base + 0x7200;
    constexpr std::uintptr_t break_asset = base + 0x7300;
    struct NativeArray
    {
        std::uintptr_t data;
        std::int32_t count;
        std::int32_t capacity;
    };

    FakeNativeMemory memory{base, 0x10000};
    memory.Set(wall + 0x458, wall_asset);
    memory.Set(barrier + 0x450, NativeArray{hit_array, 3, 3});
    memory.Set(hit_array, hit_asset0);
    memory.Set(hit_array + 8, std::uintptr_t{});
    memory.Set(hit_array + 16, hit_asset1);
    memory.Set(barrier + 0x460, break_asset);
    const std::array actors{
        StageBreakActorRef{StageBreakActorKind::Wall, wall},
        StageBreakActorRef{StageBreakActorKind::Barrier, barrier},
    };
    std::array<StageBreakParticleAssetRef,
        StageBreakPresentationIdentityMap::maximum_assets> assets{};
    std::size_t asset_count{};
    expect(CaptureStageBreakParticleAssets(
               memory, actors, assets, asset_count).ok()
            && asset_count == 4
            && assets[0].actor_address == wall
            && assets[0].route == ParticleRoute::WallBreak
            && assets[0].asset_ordinal == 0
            && assets[0].asset_address == wall_asset
            && assets[1].actor_address == barrier
            && assets[1].route == ParticleRoute::BarrierHit
            && assets[1].asset_ordinal == 0
            && assets[1].asset_address == hit_asset0
            && assets[2].route == ParticleRoute::BarrierHit
            && assets[2].asset_ordinal == 2
            && assets[2].asset_address == hit_asset1
            && assets[3].route == ParticleRoute::BarrierBreak
            && assets[3].asset_ordinal == 0
            && assets[3].asset_address == break_asset,
        "capture native stage-break assets with stable route ordinals");

    const auto prior_assets = assets;
    const auto prior_count = asset_count;
    memory.Set(barrier + 0x450, NativeArray{hit_array, 4, 3});
    const auto assets_unchanged = [&]() noexcept {
        for (std::size_t index = 0; index < assets.size(); ++index)
        {
            if (assets[index].actor_address
                    != prior_assets[index].actor_address
                || assets[index].route != prior_assets[index].route
                || assets[index].asset_ordinal
                    != prior_assets[index].asset_ordinal
                || assets[index].asset_address
                    != prior_assets[index].asset_address)
                return false;
        }
        return true;
    };
    expect(CaptureStageBreakParticleAssets(
               memory, actors, assets, asset_count).code
                == FailureCode::ContextUnavailable
            && asset_count == prior_count && assets_unchanged(),
        "malformed native asset arrays leave the prior binding unchanged");
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
    const auto* node_storage = image.nodes.data();
    const auto* first_semantic_storage = image.nodes[0].semantic_state.data();
    const auto* first_derived_storage = image.nodes[0].derived_state.data();
    const auto node_capacity = image.nodes.capacity();
    const auto semantic_capacity = image.nodes[0].semantic_state.capacity();
    const auto derived_capacity = image.nodes[0].derived_state.capacity();
    expect(probe.Capture(image).ok()
            && image.nodes.data() == node_storage
            && image.nodes.capacity() == node_capacity
            && image.nodes[0].semantic_state.data() == first_semantic_storage
            && image.nodes[0].semantic_state.capacity() == semantic_capacity
            && image.nodes[0].derived_state.data() == first_derived_storage
            && image.nodes[0].derived_state.capacity() == derived_capacity,
        "repeated wind capture reuses every bounded topology buffer");
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

class RetryingPresentationSink final : public IPresentationSink
{
public:
    Status Publish(const PresentationEvent& event) noexcept override
    {
        ++attempts;
        if (fail_attempt != 0 && attempts == fail_attempt)
            return Status::failure(FailureCode::PresentationFailed);
        identities.push_back(event.identity);
        return Status::success();
    }

    std::size_t attempts{};
    std::size_t fail_attempt{};
    std::vector<std::uint64_t> identities;
};

PresentationEvent presentation_event(
    std::uint64_t generation, std::uint64_t frame,
    std::uint64_t identity, std::uint16_t payload_size = 8,
    std::uint32_t source_ordinal = 0)
{
    PresentationEvent event{};
    event.coordinate = {generation, frame};
    event.source_ordinal = source_ordinal == 0
        ? static_cast<std::uint32_t>(identity) : source_ordinal;
    event.kind = 1;
    event.identity = identity;
    event.payload_size = payload_size;
    event.payload[0] = std::byte(identity & 0xff);
    return event;
}

void test_presentation_journal_is_bounded_and_retry_safe()
{
    PresentationJournal journal{3, 24};
    expect(journal.capacity() == 3 && journal.pending_count() == 0
            && journal.payload_bytes() == 0,
        "presentation journal allocates a fixed slot budget up front");
    expect(journal.Record(presentation_event(7, 11, 3, 8, 1)).ok()
            && journal.Record(presentation_event(7, 10, 10, 8, 2)).ok()
            && journal.Record(presentation_event(7, 10, 90, 8, 1)).ok(),
        "presentation journal fills its fixed event and payload capacity");
    expect(journal.Record(presentation_event(7, 12, 4)).code
            == FailureCode::CapacityExceeded,
        "presentation journal fails closed at fixed capacity");
    expect(journal.Record(presentation_event(7, 10, 90, 8, 1)).ok()
            && journal.pending_count() == 3,
        "presentation journal suppresses a pending duplicate without growth");

    RetryingPresentationSink sink{};
    sink.fail_attempt = 2;
    expect(journal.CommitThrough({7, 11}, sink).code
            == FailureCode::PresentationFailed
            && sink.identities == std::vector<std::uint64_t>{90}
            && journal.pending_count() == 2,
        "partial presentation failure commits only the successful prefix");
    sink.fail_attempt = 0;
    expect(journal.CommitThrough({7, 11}, sink).ok()
            && sink.identities == std::vector<std::uint64_t>({90, 10, 3})
            && journal.pending_count() == 0
            && journal.payload_bytes() == 0,
        "presentation retry preserves authored order and resumes without replaying prefix");
    expect(journal.Record(presentation_event(7, 10, 9, 8, 1)).ok()
            && journal.pending_count() == 0,
        "committed frame watermark suppresses late speculative duplicates");

    expect(journal.Record(presentation_event(8, 20, 5)).ok()
            && journal.Record(presentation_event(8, 21, 6)).ok(),
        "presentation journal accepts a new generation within fixed storage");
    const std::array oversized_replacement{
        presentation_event(8, 21, 7, 16),
        presentation_event(8, 22, 8, 16),
        presentation_event(8, 23, 9, 16)};
    expect(journal.ReplaceFrom({8, 21}, oversized_replacement).code
            == FailureCode::CapacityExceeded
            && journal.pending_count() == 2 && journal.payload_bytes() == 16,
        "capacity failure leaves the original presentation suffix intact");
    const std::array corrected_replacement{
        presentation_event(8, 21, 60, 8),
        presentation_event(8, 22, 70, 8)};
    expect(journal.ReplaceFrom({8, 21}, corrected_replacement).ok()
            && journal.pending_count() == 3 && journal.payload_bytes() == 24,
        "correction atomically replaces a preflighted presentation suffix");
    journal.DiscardFrom({8, 21});
    expect(journal.pending_count() == 1 && journal.payload_bytes() == 8,
        "presentation correction discards only the invalid suffix");
    journal.InvalidateGeneration(8);
    const auto stats = journal.statistics();
    expect(journal.pending_count() == 0 && journal.payload_bytes() == 0
            && stats.attempted == 10 && stats.recorded == 7
            && stats.duplicates == 2 && stats.capacity_failures == 2
            && stats.committed == 3 && stats.discarded == 4
            && stats.publish_failures == 1
            && stats.first_publish_failure
                == FailureCode::PresentationFailed
            && stats.first_failed_event.identity == 10
            && stats.last_publish_failure
                == FailureCode::PresentationFailed
            && stats.last_failed_event.identity == 10,
        "presentation journal exposes bounded lifecycle and retry counters");

    PresentationJournal invalid{2,
        2 * Schema::maximum_presentation_payload + 1};
    expect(invalid.capacity() == 0
            && invalid.Record(presentation_event(9, 1, 1)).code
                == FailureCode::CapacityExceeded,
        "invalid journal byte budgets cannot allocate or accept events");
}
}

int main()
{
    test_hgcpu_stream_contract();
    test_candidate_checkpoint_codec();
    test_motion_bank_snapshot_is_bounded_and_transactional();
    test_secondary_event_state_is_pointer_free_and_transactional();
    test_chara_animation_state_normalizes_sections_and_undoes_exactly();
    test_battle_audio_selector_is_generation_bound_and_transactional();
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
    test_stage_break_presentation_identity_is_generation_scoped();
    test_stage_break_particle_asset_capture_is_bounded_and_atomic();
    test_callback_topology_is_generation_bound_and_pointer_free();
    test_stage_wind_topology_is_bounded_and_pointer_free();
    test_stage_wind_graph_restore_is_transactional();
    test_presentation_journal_is_bounded_and_retry_safe();
    if (failures == 0)
        std::cout << "NativeCandidateRegionsSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
