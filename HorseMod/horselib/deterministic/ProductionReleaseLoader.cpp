#include "ProductionReleaseLoader.hpp"

#include "ProductionReleaseIdentities.generated.hpp"
#include "Schema.hpp"

#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <charconv>
#include <fstream>
#include <map>

namespace Horse::Deterministic
{
Status HashFileIdentity(
    const std::filesystem::path& path, CanonicalHash& output) noexcept;

namespace
{
constexpr std::size_t maximum_certificate_bytes = 64 * 1024;

bool DecodeHash(std::string_view text, CanonicalHash& output) noexcept
{
    output = {};
    if (text.size() != output.size() * 2) return false;
    for (std::size_t index = 0; index < output.size(); ++index)
    {
        unsigned value{};
        const auto parsed = std::from_chars(text.data() + index * 2,
            text.data() + index * 2 + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + index * 2 + 2)
            return false;
        output[index] = static_cast<std::byte>(value);
    }
    return true;
}

bool HashMatches(const std::filesystem::path& path,
    std::string_view encoded, CanonicalHash& output) noexcept
{
    CanonicalHash expected{};
    return DecodeHash(encoded, expected)
        && HashFileIdentity(path, output).ok() && output == expected;
}

bool HashBytes(std::string_view value, CanonicalHash& output) noexcept
{
    output = {};
    return value.size() <= ULONG_MAX
        && BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
            static_cast<ULONG>(value.size()),
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size())));
}

// The exact bytes read from this one bounded handle are both hashed and
// interpreted. Evidence identity can therefore never describe another open.
bool ReadAndHashCertificate(const std::filesystem::path& path,
    std::string_view encoded, std::string& bytes, CanonicalHash& output)
{
    CanonicalHash expected{};
    if (!DecodeHash(encoded, expected)) return false;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    const auto length = stream.tellg();
    if (length <= 0
        || static_cast<std::uintmax_t>(length) > maximum_certificate_bytes)
        return false;
    bytes.resize(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size()))
        return false;
    return HashBytes(bytes, output) && output == expected;
}

std::string Hex(const CanonicalHash& value)
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

void AppendQuoted(std::string& output, std::string_view value)
{
    output.push_back('"');
    for (const char item : value)
    {
        switch (item)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(item); break;
        }
    }
    output.push_back('"');
}

void AppendStringField(std::string& output, std::string_view key,
    std::string_view value)
{
    if (output.back() != '{') output.push_back(',');
    AppendQuoted(output, key);
    output.push_back(':');
    AppendQuoted(output, value);
}

std::string ExpectedCertificate(bool paired,
    const ProductionContentEntry& candidate,
    std::string_view executable, std::string_view dll,
    std::string_view source, std::string_view schema,
    std::string_view candidate_manifest, std::string_view region_manifest,
    std::string_view loaded_map)
{
    std::string output{"{\"report_schema\":2"};
    AppendStringField(output, "kind", paired
        ? "paired_online_release_case" : "offline_release_case");
    output += ",\"certifying\":true,\"result\":\"pass\"";
    output += ",\"protocol_version\":" + std::to_string(Schema::protocol_version);
    output += ",\"snapshot_schema_version\":"
        + std::to_string(Schema::snapshot_schema_version);
    AppendStringField(output, "case_id", candidate.case_id);
    output += ",\"fighter_order\":[";
    AppendQuoted(output, candidate.fighter0);
    output.push_back(',');
    AppendQuoted(output, candidate.fighter1);
    output.push_back(']');
    AppendStringField(output, "stage_selection_code", candidate.stage_selection_code);
    AppendStringField(output, "authored_stage_code", candidate.authored_stage_code);
    AppendStringField(output, "stage_package_root", candidate.stage_package_root);
    AppendStringField(output, "map_path", candidate.map_path);
    AppendStringField(output, "native_display_name", candidate.stage_display_name);
    AppendStringField(output, "rng_policy",
        "authored_stage_only_random_selection_forbidden");
    AppendStringField(output, "renderer", "normal");
    AppendStringField(output, "game_executable_sha256", executable);
    AppendStringField(output, "horsemod_dll_sha256", dll);
    AppendStringField(output, "source_identity_sha256", source);
    AppendStringField(output, "schema_sha256", schema);
    AppendStringField(output, "candidate_manifest_sha256", candidate_manifest);
    AppendStringField(output, "region_manifest_sha256", region_manifest);
    AppendStringField(output, "loaded_map_sha256", loaded_map);
    output += ",\"canonical_divergences\":0";
    output += ",\"ordered_audio_payload_ids\":true";
    AppendStringField(output, "presentation_reconciliation", "exact");
    if (paired)
    {
        output += ",\"authenticated_steam_p2p\":true";
        output += ",\"multi_round_real_corrections\":true";
        output += ",\"impairments_and_failures_complete\":true";
    }
    else
    {
        output += ",\"normal_render_matrix_rows\":17";
        output += ",\"strict_replay_gates_complete\":true";
    }
    output += ",\"qualification_complete\":true}";
    return output;
}
}

std::string FormatProductionReleaseCertificate(bool paired,
    const ProductionContentEntry& candidate,
    std::string_view executable, std::string_view dll,
    std::string_view source, std::string_view schema,
    std::string_view candidate_manifest, std::string_view region_manifest,
    std::string_view loaded_map)
{
    return ExpectedCertificate(paired, candidate, executable, dll, source,
        schema, candidate_manifest, region_manifest, loaded_map);
}

Status LoadProductionReleaseBinding(
    const std::filesystem::path& allowlist_path,
    const CanonicalHash& executable_id,
    const CanonicalHash& build_id,
    std::string_view source_commit,
    ProductionEvidenceBinding& output) noexcept
{
    output = {};
    try
    {
        std::ifstream stream(allowlist_path);
        if (!stream) return Status::failure(FailureCode::ContextUnavailable);
        std::map<std::string, std::string> fields;
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty() || line.front() == '#' || line.front() == ';')
                continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos || separator == 0
                || separator + 1 == line.size()
                || !fields.emplace(line.substr(0, separator),
                    line.substr(separator + 1)).second)
                return Status::failure(FailureCode::InvalidConfiguration);
        }
        const auto get = [&](const std::string& key) -> const std::string* {
            const auto found = fields.find(key);
            return found == fields.end() ? nullptr : &found->second;
        };
        const auto version = get("version");
        const auto source = get("source_commit");
        const auto executable = get("game_executable_sha256");
        const auto dll = get("horsemod_dll_sha256");
        if (version == nullptr || *version != "1" || source == nullptr
            || *source != source_commit || executable == nullptr || dll == nullptr
            || !DecodeHash(*executable, output.executable_id)
            || !DecodeHash(*dll, output.build_id)
            || output.executable_id != executable_id || output.build_id != build_id)
            return Status::failure(FailureCode::IdentityMismatch);

        const auto verify_contract = [&](const char* prefix,
            CanonicalHash& identity, const CanonicalHash& compiled) {
            const auto* path = get(std::string(prefix) + "_path");
            const auto* hash = get(std::string(prefix) + "_sha256");
            return path != nullptr && hash != nullptr
                && HashMatches(std::filesystem::path(*path), *hash, identity)
                && identity == compiled;
        };
        if (!verify_contract("schema", output.schema_id,
                production_compiled_schema_sha256)
            || !verify_contract("candidate_manifest",
                output.candidate_manifest_id, production_candidate_manifest_sha256)
            || !verify_contract("region_manifest",
                output.region_manifest_id, Schema::production_region_manifest_sha256))
            return Status::failure(FailureCode::IdentityMismatch);

        const auto* source_hash = get("source_identity_sha256");
        if (source_hash == nullptr
            || !DecodeHash(*source_hash, output.source_id)
            || output.source_id != production_compiled_source_sha256)
            return Status::failure(FailureCode::IdentityMismatch);

        const std::string executable_hex = Hex(executable_id);
        const std::string dll_hex = Hex(build_id);
        const std::string source_hex = Hex(output.source_id);
        const std::string schema_hex = Hex(output.schema_id);
        const std::string candidate_hex = Hex(output.candidate_manifest_id);
        const std::string regions_hex = Hex(output.region_manifest_id);
        for (std::size_t index = 0; index < production_content_candidates.size();
             ++index)
        {
            const auto prefix = "case" + std::to_string(index) + "_";
            const auto* offline_path = get(prefix + "offline_report_path");
            const auto* offline_hash = get(prefix + "offline_report_sha256");
            const auto* paired_path = get(prefix + "paired_report_path");
            const auto* paired_hash = get(prefix + "paired_report_sha256");
            const auto* loaded_map = get(prefix + "loaded_map_sha256");
            std::string offline_text;
            std::string paired_text;
            if (offline_path == nullptr || offline_hash == nullptr
                || paired_path == nullptr || paired_hash == nullptr
                || loaded_map == nullptr
                || !DecodeHash(*loaded_map, output.loaded_map_ids[index])
                || !ReadAndHashCertificate(*offline_path, *offline_hash,
                    offline_text, output.offline_report_ids[index])
                || !ReadAndHashCertificate(*paired_path, *paired_hash,
                    paired_text, output.paired_report_ids[index]))
                return Status::failure(FailureCode::IdentityMismatch);
            const auto& candidate = production_content_candidates[index];
            if (offline_text != FormatProductionReleaseCertificate(false, candidate,
                    executable_hex, dll_hex, source_hex, schema_hex,
                    candidate_hex, regions_hex, *loaded_map)
                || paired_text != FormatProductionReleaseCertificate(true, candidate,
                    executable_hex, dll_hex, source_hex, schema_hex,
                    candidate_hex, regions_hex, *loaded_map))
                return Status::failure(FailureCode::IdentityMismatch);
        }
        if (fields.size() != 26)
            return Status::failure(FailureCode::InvalidConfiguration);
        return Status::success();
    }
    catch (...)
    {
        output = {};
        return Status::failure(FailureCode::InvalidConfiguration);
    }
}
}
