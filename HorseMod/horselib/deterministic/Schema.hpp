#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Horse::Deterministic::Schema
{
inline constexpr std::uint32_t protocol_version = 1;
inline constexpr std::uint32_t snapshot_schema_version = 39;
inline constexpr std::size_t maximum_transport_payload = 1200;
inline constexpr std::size_t maximum_presentation_payload = 256;
inline constexpr std::uint64_t checkpoint_interval = 30;
inline constexpr std::uint32_t maximum_supported_native_batch_width = 12;
inline constexpr std::uint16_t particle_presentation_schema_version = 1;
inline constexpr std::uint32_t particle_presentation_event_kind = 1;
inline constexpr std::size_t particle_presentation_payload_size = 68;
inline constexpr std::size_t replay_timeline_memory_limit =
    512ull * 1024ull * 1024ull;
inline constexpr std::size_t replay_input_memory_budget =
    96ull * 1024ull * 1024ull;
inline constexpr std::size_t replay_native_batch_memory_budget =
    32ull * 1024ull * 1024ull;
inline constexpr std::size_t replay_canonical_hash_memory_budget =
    16ull * 1024ull * 1024ull;
inline constexpr std::size_t replay_checkpoint_memory_budget =
    replay_timeline_memory_limit - replay_input_memory_budget
    - replay_native_batch_memory_budget - replay_canonical_hash_memory_budget;
inline constexpr std::size_t replay_input_entry_budget = 128;
inline constexpr std::size_t replay_native_batch_entry_budget = 1536;
inline constexpr std::size_t replay_native_batch_coordinate_budget = 32;
inline constexpr std::size_t replay_round_image_size = 0xc0;
inline constexpr std::uint32_t maximum_replay_round_images = 64;

namespace Sc6ReplayLayout
{
inline constexpr std::uintptr_t post_tick_rva = 0x3829f0;
inline constexpr std::ptrdiff_t exit_guard = 0x18;
inline constexpr std::array<std::byte, 16> post_tick_signature{
    std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x28},
    std::byte{0x83}, std::byte{0x79}, std::byte{0x18}, std::byte{0x00},
    std::byte{0x0f}, std::byte{0x85}, std::byte{0xa3}, std::byte{0x02},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x48}, std::byte{0x8b}};
inline constexpr std::uintptr_t replay_enabled = 0x398;
inline constexpr std::uintptr_t round_images = 0x3a8;
inline constexpr std::uintptr_t round_count = 0x3b0;
inline constexpr std::uintptr_t round_capacity = 0x3b4;
inline constexpr std::uintptr_t manager_round_image = 0x1360;
inline constexpr std::uintptr_t manager_move_state = 0x1463;
inline constexpr std::uintptr_t manager_pending_dispatch = 0x1464;
inline constexpr std::uintptr_t manager_round_image_applied = 0x1465;
inline constexpr std::uintptr_t manager_status = 0x1480;
inline constexpr std::uintptr_t set_move_state_rva = 0x3f8370;
inline constexpr std::array<std::byte, 7> set_move_state_signature{
    std::byte{0x88}, std::byte{0x91}, std::byte{0x63}, std::byte{0x14},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc3}};
}

namespace Sc6FrameLayout
{
inline constexpr std::uintptr_t stage_break_wall_handler_rva = 0x53d4b0;
inline constexpr std::uintptr_t stage_break_barrier_handler_rva = 0x549f40;
inline constexpr std::uintptr_t stage_break_dispatch_rva = 0x53d130;
inline constexpr std::uintptr_t battle_audio_dispatch_rva = 0x519480;
inline constexpr std::uintptr_t battle_audio_remap_rva = 0x3ba080;
inline constexpr std::uintptr_t battle_audio_contact_handler_rva = 0x3c63c0;
inline constexpr std::uintptr_t battle_audio_phase_changed_rva = 0x3c41c0;
inline constexpr std::uintptr_t battle_audio_tracking_remove_rva = 0xe0d1f0;
inline constexpr std::uintptr_t battle_audio_tracking_insert_rva = 0x156bff0;
inline constexpr std::uintptr_t battle_audio_tracking_rehash_rva = 0x3c9d00;
inline constexpr std::uintptr_t battle_audio_blueprint_publish_rva = 0x979e00;
inline constexpr std::uintptr_t battle_audio_register_voice_rva = 0x54f6d0;
inline constexpr std::uintptr_t battle_audio_append_command_rva = 0x5656d0;
inline constexpr std::uintptr_t battle_audio_stop_all_rva = 0x560940;
inline constexpr std::uintptr_t battle_audio_append_parameter_rva = 0x55b4b0;
inline constexpr std::uintptr_t particle_spawn_rva = 0x8a3920;
inline constexpr std::uintptr_t particle_finished_bind_rva = 0x533f40;
inline constexpr std::uintptr_t particle_wall_return_rva = 0x53d69f;
inline constexpr std::uintptr_t particle_barrier_hit_return_rva = 0x54a1ef;
inline constexpr std::uintptr_t particle_barrier_break_return_rva = 0x54a364;
inline constexpr std::uintptr_t particle_blueprint_return_rva = 0xcf44f3;
inline constexpr std::uint16_t required_observation_read_mask = 0x3fff;
inline constexpr std::uint16_t required_outer_tick_read_mask = 0x00ff;
inline constexpr std::uintptr_t frame_counter_rva = 0x470d0c4;
inline constexpr std::uintptr_t landing_fencepost_rva = 0x3fca60;
inline constexpr std::uintptr_t outer_tick_rva = 0x3fbf30;
inline constexpr std::uintptr_t callback_executor_rva = 0x1d38300;
inline constexpr std::ptrdiff_t manager_input_filter_callbacks = 0x1210;
inline constexpr std::ptrdiff_t manager_input_log = 0x478;
inline constexpr std::ptrdiff_t manager_input_pair_array = 0x14a8;
inline constexpr std::ptrdiff_t manager_active_player_count = 0x14b0;
inline constexpr std::ptrdiff_t manager_repeat_pending = 0x1462;
inline constexpr std::ptrdiff_t manager_main_state = 0x1461;
inline constexpr std::ptrdiff_t manager_pending_move_state = 0x1463;
inline constexpr std::ptrdiff_t manager_game_round_cursor = 0x1488;
inline constexpr std::ptrdiff_t manager_game_time_cursor = 0x148c;
inline constexpr std::ptrdiff_t manager_round_state_frame = 0x1490;
inline constexpr std::ptrdiff_t manager_unpause_countdown = 0x14f0;
inline constexpr std::ptrdiff_t input_log_game_round = 0x3a0;
inline constexpr std::ptrdiff_t input_log_game_time = 0x3a4;
inline constexpr std::ptrdiff_t input_log_update_time = 0x3ac;
inline constexpr std::ptrdiff_t input_log_input_delay = 0x390;
inline constexpr std::ptrdiff_t input_log_cache = 0x3c0;
inline constexpr std::size_t input_log_cache_rows_per_player = 512;
inline constexpr std::size_t input_log_cache_row_stride = 0x10;
inline constexpr std::array<std::byte, 16> landing_fencepost_signature{
    std::byte{0x41}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x50}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xb9}, std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x4c}, std::byte{0x8b}};
inline constexpr std::array<std::byte, 16> outer_tick_signature{
    std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x30}, std::byte{0x48}, std::byte{0x8b},
    std::byte{0xd9}, std::byte{0x0f}, std::byte{0x29}, std::byte{0x74},
    std::byte{0x24}, std::byte{0x20}, std::byte{0x0f}, std::byte{0xb6}};
inline constexpr std::array<std::byte, 16> callback_executor_signature{
    std::byte{0x40}, std::byte{0x56}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x20}, std::byte{0xff}, std::byte{0x41},
    std::byte{0x64}, std::byte{0x48}, std::byte{0x8b}, std::byte{0xf1},
    std::byte{0x8b}, std::byte{0x41}, std::byte{0x50}, std::byte{0x48}};
inline constexpr std::array<std::byte, 18> stage_break_wall_handler_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x18}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74},
    std::byte{0x24}, std::byte{0x20}, std::byte{0x55}, std::byte{0x48},
    std::byte{0x8b}, std::byte{0xec}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x70},
};
inline constexpr std::array<std::byte, 23> stage_break_barrier_handler_signature{
    std::byte{0x4c}, std::byte{0x8b}, std::byte{0xdc}, std::byte{0x49},
    std::byte{0x89}, std::byte{0x5b}, std::byte{0x18}, std::byte{0x49},
    std::byte{0x89}, std::byte{0x73}, std::byte{0x20}, std::byte{0x55},
    std::byte{0x49}, std::byte{0x8d}, std::byte{0x6b}, std::byte{0xa1},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xec}, std::byte{0xc0},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};
inline constexpr std::array<std::byte, 16> stage_break_dispatch_signature{
    std::byte{0x40}, std::byte{0x56}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x30}, std::byte{0xff}, std::byte{0x41},
    std::byte{0x64}, std::byte{0x48}, std::byte{0x8b}, std::byte{0xf1},
    std::byte{0x8b}, std::byte{0x41}, std::byte{0x50}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 24> battle_audio_dispatch_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x20}, std::byte{0x55}, std::byte{0x56}, std::byte{0x41},
    std::byte{0x57}, std::byte{0x48}, std::byte{0x83}, std::byte{0xec},
    std::byte{0x60}, std::byte{0x41}, std::byte{0x0f}, std::byte{0xb6},
    std::byte{0xe8}, std::byte{0x4c}, std::byte{0x8b}, std::byte{0xfa},
    std::byte{0x48}, std::byte{0x8b}, std::byte{0xd9}, std::byte{0x45},
};
inline constexpr std::array<std::byte, 32> battle_audio_remap_signature{
    std::byte{0x8d}, std::byte{0x42}, std::byte{0xfa}, std::byte{0x44},
    std::byte{0x8b}, std::byte{0xca}, std::byte{0x83}, std::byte{0xf8},
    std::byte{0x0e}, std::byte{0x77}, std::byte{0x65}, std::byte{0x48},
    std::byte{0x8d}, std::byte{0x15}, std::byte{0x6e}, std::byte{0x5f},
    std::byte{0xc4}, std::byte{0xff}, std::byte{0x44}, std::byte{0x8b},
    std::byte{0x84}, std::byte{0x82}, std::byte{0xf4}, std::byte{0xa0},
    std::byte{0x3b}, std::byte{0x00}, std::byte{0x4c}, std::byte{0x03},
    std::byte{0xc2}, std::byte{0x41}, std::byte{0xff}, std::byte{0xe0},
};
inline constexpr std::array<std::byte, 16> battle_audio_contact_handler_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x54}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x55}, std::byte{0x53}, std::byte{0x57},
    std::byte{0x41}, std::byte{0x55}, std::byte{0x48}, std::byte{0x8d},
    std::byte{0x6c}, std::byte{0x24}, std::byte{0xd8}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 16> battle_audio_phase_changed_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x55}, std::byte{0x56}, std::byte{0x57},
    std::byte{0x48}, std::byte{0x8b}, std::byte{0xec}, std::byte{0x48},
    std::byte{0x83}, std::byte{0xec}, std::byte{0x60}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 16> battle_audio_tracking_remove_signature{
    std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x20}, std::byte{0x8b}, std::byte{0x41},
    std::byte{0x08}, std::byte{0x4c}, std::byte{0x8b}, std::byte{0xd9},
    std::byte{0x48}, std::byte{0x63}, std::byte{0xda}, std::byte{0x3b},
};
inline constexpr std::array<std::byte, 16> battle_audio_tracking_insert_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x08}, std::byte{0x48}, std::byte{0x89}, std::byte{0x6c},
    std::byte{0x24}, std::byte{0x10}, std::byte{0x48}, std::byte{0x89},
    std::byte{0x74}, std::byte{0x24}, std::byte{0x18}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 16> battle_audio_tracking_rehash_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x08}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74},
    std::byte{0x24}, std::byte{0x10}, std::byte{0x57}, std::byte{0x48},
    std::byte{0x83}, std::byte{0xec}, std::byte{0x50}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 16> battle_audio_blueprint_publish_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x54}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x4c},
    std::byte{0x24}, std::byte{0x08}, std::byte{0x56}, std::byte{0x57},
    std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x48},
};
inline constexpr std::array<std::byte, 16> battle_audio_register_voice_signature{
    std::byte{0x48}, std::byte{0x8b}, std::byte{0x09}, std::byte{0x48},
    std::byte{0x85}, std::byte{0xc9}, std::byte{0x0f}, std::byte{0x85},
    std::byte{0xd4}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x33}, std::byte{0xc0}, std::byte{0xc3}, std::byte{0xcc},
};
inline constexpr std::array<std::byte, 16> battle_audio_append_command_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x08}, std::byte{0x48}, std::byte{0x89}, std::byte{0x6c},
    std::byte{0x24}, std::byte{0x10}, std::byte{0x48}, std::byte{0x89},
    std::byte{0x74}, std::byte{0x24}, std::byte{0x18}, std::byte{0x57},
};
inline constexpr std::array<std::byte, 16> battle_audio_stop_all_signature{
    std::byte{0x40}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x40}, std::byte{0x48}, std::byte{0x83},
    std::byte{0x39}, std::byte{0x00}, std::byte{0x48}, std::byte{0x8b},
    std::byte{0xf9}, std::byte{0x0f}, std::byte{0x84}, std::byte{0x9a},
};
inline constexpr std::array<std::byte, 16> battle_audio_append_parameter_signature{
    std::byte{0x48}, std::byte{0x8b}, std::byte{0x09}, std::byte{0x48},
    std::byte{0x85}, std::byte{0xc9}, std::byte{0x0f}, std::byte{0x85},
    std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0xc3}, std::byte{0xcc}, std::byte{0xcc}, std::byte{0xcc},
};
inline constexpr std::array<std::byte, 16> particle_spawn_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x6c},
    std::byte{0x24}, std::byte{0x18}, std::byte{0x56}, std::byte{0x48},
    std::byte{0x83}, std::byte{0xec}, std::byte{0x50}, std::byte{0x49},
};
inline constexpr std::array<std::byte, 16> particle_finished_bind_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x4c}, std::byte{0x89}, std::byte{0x4c},
    std::byte{0x24}, std::byte{0x20}, std::byte{0x55}, std::byte{0x56},
    std::byte{0x57}, std::byte{0x48}, std::byte{0x83}, std::byte{0xec},
};
}

namespace Sc6UcrtLayout
{
inline constexpr std::uint32_t algorithm_version = 1;
inline constexpr std::uint32_t allowlist_version = 1;
inline constexpr std::uintptr_t rand_iat_rva = 0x322d800;
inline constexpr std::uintptr_t srand_iat_rva = 0x322d818;
inline constexpr std::uintptr_t rng_init_srand_return_rva = 0x34f634;
inline constexpr std::uintptr_t rng_init_rand_return_rva = 0x34f658;
inline constexpr std::uintptr_t movevm_rand_return_rva = 0x366ff4;
}

enum class RegionClass : std::uint8_t
{
    CanonicalGameplay,
    Derived,
    ClientLocalDiagnostic,
    PersistentPresentation,
    EphemeralPresentation,
};

struct NativeRegionDescriptor
{
    std::string_view name;
    std::uintptr_t address;
    std::size_t size;
    RegionClass classification;
};

// Admission is deliberately fail-closed. Regions are added only after their complete
// native read/write and lifetime surface is recorded in the Ghidra-backed contract.
inline constexpr std::array<NativeRegionDescriptor, 0> production_regions{};

constexpr std::string_view region_class_name(RegionClass value) noexcept
{
    switch (value)
    {
    case RegionClass::CanonicalGameplay: return "canonical_gameplay";
    case RegionClass::Derived: return "derived";
    case RegionClass::ClientLocalDiagnostic: return "client_local_diagnostic";
    case RegionClass::PersistentPresentation: return "persistent_presentation";
    case RegionClass::EphemeralPresentation: return "ephemeral_presentation";
    }
    return "unknown";
}
}
