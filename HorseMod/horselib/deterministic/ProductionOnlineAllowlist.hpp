#pragma once

#include "OnlineCoordinator.hpp"
#include "Sc6StageCatalog.hpp"
#include "ProductionCandidateManifest.generated.hpp"

#include <array>
#include <atomic>
#include <filesystem>
#include <string_view>

namespace Horse::Deterministic
{
Status HashOnlineSelectionIdentity(
    const OnlineContentContract& content, CanonicalHash& output) noexcept;

struct ProductionEvidenceBinding
{
    CanonicalHash executable_id{};
    CanonicalHash build_id{};
    CanonicalHash source_id{};
    CanonicalHash schema_id{};
    CanonicalHash region_manifest_id{};
    CanonicalHash candidate_manifest_id{};
    std::array<CanonicalHash, production_content_candidates.size()>
        offline_report_ids{};
    std::array<CanonicalHash, production_content_candidates.size()>
        paired_report_ids{};
    std::array<CanonicalHash, production_content_candidates.size()>
        loaded_map_ids{};
};

// One-way publication surface. An instance starts empty and can be populated
// exactly once after the external release gate has verified all evidence
// against the still-loaded DLL. There is no wildcard or per-case arming API.
class ProductionOnlineAllowlist final : public IOnlineContentAllowlist
{
public:
    [[nodiscard]] Status LoadAndPublish(
        const std::filesystem::path& allowlist_path,
        const CanonicalHash& executable_id,
        const CanonicalHash& build_id,
        std::string_view source_commit) noexcept;
    [[nodiscard]] bool IsPublished() const noexcept;
    [[nodiscard]] bool IsQualified(
        const OnlineContentContract& content) const noexcept override;
    [[nodiscard]] const CanonicalHash* ExpectedLoadedMapIdentity(
        const OnlineContentContract& content) const noexcept;
    [[nodiscard]] const ProductionEvidenceBinding& binding() const noexcept
    {
        return binding_;
    }

private:
    [[nodiscard]] bool publish_verified(
        const ProductionEvidenceBinding& binding) noexcept;
    ProductionEvidenceBinding binding_{};
    std::atomic<bool> published_{};
};
}
