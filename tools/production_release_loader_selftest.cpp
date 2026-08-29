#include "deterministic/ProductionReleaseLoader.hpp"
#include "ProductionReleaseIdentities.generated.hpp"
#include "deterministic/Schema.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace Horse::Deterministic;

namespace Horse::Deterministic
{
Status HashFileIdentity(
    const std::filesystem::path& path, CanonicalHash& output) noexcept;
}

namespace
{
int failures{};

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string hex(const CanonicalHash& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2, '0');
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const auto byte = std::to_integer<unsigned>(value[index]);
        output[index * 2] = digits[byte >> 4];
        output[index * 2 + 1] = digits[byte & 15];
    }
    return output;
}

void write_bytes(const std::filesystem::path& path, std::string_view value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

CanonicalHash file_hash(const std::filesystem::path& path)
{
    CanonicalHash value{};
    expect(HashFileIdentity(path, value).ok(), "test artifact hashes");
    return value;
}
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: ProductionReleaseLoaderSelfTest SCHEMA CANDIDATES REGIONS\n";
        return 2;
    }
    const std::filesystem::path schema = argv[1];
    const std::filesystem::path candidates = argv[2];
    const std::filesystem::path regions = argv[3];
    expect(file_hash(schema) == production_compiled_schema_sha256,
        "test schema is the compile-anchored schema");
    expect(file_hash(candidates) == production_candidate_manifest_sha256,
        "test candidate manifest is compile-anchored");
    expect(file_hash(regions) == Schema::production_region_manifest_sha256,
        "test region manifest is compile-anchored");

    const auto directory = std::filesystem::temp_directory_path()
        / ("horsemod-release-loader-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    CanonicalHash executable{};
    CanonicalHash dll{};
    executable.fill(std::byte{0x11});
    dll.fill(std::byte{0x22});
    const auto executable_hex = hex(executable);
    const auto dll_hex = hex(dll);
    const auto source_hex = hex(production_compiled_source_sha256);
    const auto schema_hex = hex(production_compiled_schema_sha256);
    const auto candidates_hex = hex(production_candidate_manifest_sha256);
    const auto regions_hex = hex(Schema::production_region_manifest_sha256);

    std::array<std::filesystem::path, production_content_candidates.size()> offline{};
    std::array<std::filesystem::path, production_content_candidates.size()> paired{};
    std::array<std::string, production_content_candidates.size()> loaded{};
    for (std::size_t index = 0; index < production_content_candidates.size(); ++index)
    {
        CanonicalHash package{};
        package.fill(static_cast<std::byte>(0x30 + index));
        loaded[index] = hex(package);
        offline[index] = directory / ("offline-" + std::to_string(index) + ".json");
        paired[index] = directory / ("paired-" + std::to_string(index) + ".json");
        write_bytes(offline[index], FormatProductionReleaseCertificate(false,
            production_content_candidates[index], executable_hex, dll_hex,
            source_hex, schema_hex, candidates_hex, regions_hex, loaded[index]));
        write_bytes(paired[index], FormatProductionReleaseCertificate(true,
            production_content_candidates[index], executable_hex, dll_hex,
            source_hex, schema_hex, candidates_hex, regions_hex, loaded[index]));
    }

    const auto allowlist = directory / "production-allowlist.ini";
    auto write_allowlist = [&] {
        std::ofstream output(allowlist, std::ios::trunc);
        output << "version=1\nsource_commit=test-source\n"
               << "game_executable_sha256=" << executable_hex << '\n'
               << "horsemod_dll_sha256=" << dll_hex << '\n'
               << "source_identity_sha256=" << source_hex << '\n'
               << "schema_path=" << schema.string() << '\n'
               << "schema_sha256=" << schema_hex << '\n'
               << "candidate_manifest_path=" << candidates.string() << '\n'
               << "candidate_manifest_sha256=" << candidates_hex << '\n'
               << "region_manifest_path=" << regions.string() << '\n'
               << "region_manifest_sha256=" << regions_hex << '\n';
        for (std::size_t index = 0; index < production_content_candidates.size(); ++index)
        {
            output << "case" << index << "_offline_report_path="
                   << offline[index].string() << '\n'
                   << "case" << index << "_offline_report_sha256="
                   << hex(file_hash(offline[index])) << '\n'
                   << "case" << index << "_paired_report_path="
                   << paired[index].string() << '\n'
                   << "case" << index << "_paired_report_sha256="
                   << hex(file_hash(paired[index])) << '\n'
                   << "case" << index << "_loaded_map_sha256="
                   << loaded[index] << '\n';
        }
    };
    write_allowlist();
    ProductionEvidenceBinding binding{};
    expect(LoadProductionReleaseBinding(allowlist, executable, dll,
        "test-source", binding).ok(),
        "strict complete release binding is admitted");
    expect(binding.loaded_map_ids[0][0] == std::byte{0x30},
        "loaded package-set identity is report-bound");

    write_bytes(paired[0], "{\"report_schema\":2,\"certifying\":true,\"result\":\"pass\"}");
    write_allowlist();
    expect(!LoadProductionReleaseBinding(allowlist, executable, dll,
        "test-source", binding).ok(),
        "superficial certifying JSON cannot authorize production");

    std::filesystem::remove_all(directory);
    if (failures == 0) std::cout << "ProductionReleaseLoaderSelfTest passed\n";
    return failures == 0 ? 0 : 1;
}
