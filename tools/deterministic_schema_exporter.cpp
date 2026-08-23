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
