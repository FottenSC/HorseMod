#include "deterministic/Config.hpp"
#include "deterministic/ReplayCoordinator.hpp"
#include "deterministic/Schema.hpp"

#include <fstream>
#include <iostream>
#include <ostream>

using namespace Horse::Deterministic;

namespace
{
void write_schema(std::ostream& output)
{
    output << "{\n"
           << "  \"config_version\": " << Config::current_version << ",\n"
           << "  \"protocol_version\": " << Schema::protocol_version << ",\n"
           << "  \"snapshot_schema_version\": " << Schema::snapshot_schema_version << ",\n"
           << "  \"maximum_transport_payload\": " << Schema::maximum_transport_payload << ",\n"
           << "  \"checkpoint_interval\": " << ReplayCoordinator::checkpoint_interval << ",\n"
           << "  \"replay_timeline_memory_limit\": "
           << Schema::replay_timeline_memory_limit << ",\n"
           << "  \"replay_input_memory_budget\": "
           << Schema::replay_input_memory_budget << ",\n"
           << "  \"replay_checkpoint_memory_budget\": "
           << Schema::replay_checkpoint_memory_budget << ",\n"
           << "  \"native_hooks\": {\n"
           << "    \"frame_fencepost_rva\": "
           << Schema::Sc6FrameLayout::landing_fencepost_rva << ",\n"
           << "    \"frame_counter_rva\": "
           << Schema::Sc6FrameLayout::frame_counter_rva << ",\n"
           << "    \"manager_input_log_offset\": "
           << Schema::Sc6FrameLayout::manager_input_log << ",\n"
           << "    \"manager_input_pair_array_offset\": "
           << Schema::Sc6FrameLayout::manager_input_pair_array << ",\n"
           << "    \"manager_active_player_count_offset\": "
           << Schema::Sc6FrameLayout::manager_active_player_count << ",\n"
           << "    \"input_log_game_round_offset\": "
           << Schema::Sc6FrameLayout::input_log_game_round << ",\n"
           << "    \"input_log_game_time_offset\": "
           << Schema::Sc6FrameLayout::input_log_game_time << ",\n"
           << "    \"replay_post_tick_rva\": "
           << Schema::Sc6ReplayLayout::post_tick_rva << ",\n"
           << "    \"replay_exit_guard_offset\": "
           << Schema::Sc6ReplayLayout::exit_guard << "\n"
           << "  },\n"
           << "  \"player_input\": {\n"
           << "    \"size\": " << sizeof(PlayerInput) << ",\n"
           << "    \"held_offset\": " << offsetof(PlayerInput, held) << ",\n"
           << "    \"rising_offset\": " << offsetof(PlayerInput, rising) << "\n"
           << "  },\n"
           << "  \"production_regions\": [";
    for (std::size_t index = 0; index < Schema::production_regions.size(); ++index)
    {
        const auto& region = Schema::production_regions[index];
        output << (index == 0 ? "\n" : ",\n")
               << "    {\"name\": \"" << region.name
               << "\", \"address\": " << region.address
               << ", \"size\": " << region.size
               << ", \"class\": \"" << Schema::region_class_name(region.classification)
               << "\"}";
    }
    if (!Schema::production_regions.empty()) output << '\n';
    output << "  ]\n}\n";
}
}

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        write_schema(std::cout);
        return 0;
    }
    if (argc != 2)
    {
        std::cerr << "usage: DeterministicSchemaExporter [output.json]\n";
        return 2;
    }
    std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
    if (!output)
    {
        std::cerr << "unable to open schema output\n";
        return 1;
    }
    write_schema(output);
    return output ? 0 : 1;
}
