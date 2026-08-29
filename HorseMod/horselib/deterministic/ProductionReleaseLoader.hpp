#pragma once

#include "ProductionOnlineAllowlist.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Horse::Deterministic
{
std::string FormatProductionReleaseCertificate(
    bool paired,
    const ProductionContentEntry& candidate,
    std::string_view executable,
    std::string_view dll,
    std::string_view source,
    std::string_view schema,
    std::string_view candidate_manifest,
    std::string_view region_manifest,
    std::string_view loaded_map);

Status LoadProductionReleaseBinding(
    const std::filesystem::path& allowlist_path,
    const CanonicalHash& executable_id,
    const CanonicalHash& build_id,
    std::string_view source_commit,
    ProductionEvidenceBinding& output) noexcept;
}
