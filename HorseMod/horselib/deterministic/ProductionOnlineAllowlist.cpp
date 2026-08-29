#include "ProductionOnlineAllowlist.hpp"
#include "ProductionReleaseLoader.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
namespace
{
bool HasIdentity(const CanonicalHash& value) noexcept
{
    return std::any_of(value.begin(), value.end(),
        [](std::byte item) { return item != std::byte{}; });
}

bool BindingComplete(const ProductionEvidenceBinding& value) noexcept
{
    return HasIdentity(value.executable_id) && HasIdentity(value.build_id)
        && HasIdentity(value.source_id) && HasIdentity(value.schema_id)
        && HasIdentity(value.region_manifest_id)
        && HasIdentity(value.candidate_manifest_id)
        && std::all_of(value.offline_report_ids.begin(),
            value.offline_report_ids.end(), HasIdentity)
        && std::all_of(value.paired_report_ids.begin(),
            value.paired_report_ids.end(), HasIdentity)
        && std::all_of(value.loaded_map_ids.begin(),
            value.loaded_map_ids.end(), HasIdentity);
}
}

bool ProductionOnlineAllowlist::publish_verified(
    const ProductionEvidenceBinding& binding) noexcept
{
    if (published_.load(std::memory_order_acquire)
        || !BindingComplete(binding))
        return false;
    binding_ = binding;
    bool expected = false;
    return published_.compare_exchange_strong(expected, true,
        std::memory_order_release, std::memory_order_relaxed);
}

Status ProductionOnlineAllowlist::LoadAndPublish(
    const std::filesystem::path& allowlist_path,
    const CanonicalHash& executable_id,
    const CanonicalHash& build_id,
    std::string_view source_commit) noexcept
{
    ProductionEvidenceBinding binding{};
    const auto loaded = LoadProductionReleaseBinding(allowlist_path,
        executable_id, build_id, source_commit, binding);
    if (!loaded.ok()) return loaded;
    return publish_verified(binding) ? Status::success()
        : Status::failure(FailureCode::IllegalTransition);
}

bool ProductionOnlineAllowlist::IsPublished() const noexcept
{
    return published_.load(std::memory_order_acquire);
}

bool ProductionOnlineAllowlist::IsQualified(
    const OnlineContentContract& content) const noexcept
{
    if (!IsPublished()) return false;
    const std::string_view fighter0{content.fighter_codes[0].data()};
    const std::string_view fighter1{content.fighter_codes[1].data()};
    const std::string_view stage_code{content.stage_code.data()};
    const std::string_view package{content.map_name.data()};
    const auto found = std::find_if(production_content_candidates.begin(),
        production_content_candidates.end(), [&](const auto& candidate) {
            return fighter0 == candidate.fighter0
                && fighter1 == candidate.fighter1
                && stage_code == candidate.stage_selection_code
                && package == candidate.stage_package_root
                && (!candidate.require_authored_stage
                    || !content.stage_was_random);
        });
    if (found == production_content_candidates.end()) return false;
    const auto* stage = FindQualifiedStage(stage_code);
    if (stage == nullptr || stage->package_root != package
        || stage->display_name != found->stage_display_name)
        return false;
    CanonicalHash expected{};
    return HashOnlineSelectionIdentity(content, expected).ok()
        && expected == content.map_identity;
}

const CanonicalHash* ProductionOnlineAllowlist::ExpectedLoadedMapIdentity(
    const OnlineContentContract& content) const noexcept
{
    if (!IsQualified(content)) return nullptr;
    const std::string_view fighter0{content.fighter_codes[0].data()};
    const std::string_view fighter1{content.fighter_codes[1].data()};
    const std::string_view stage_code{content.stage_code.data()};
    for (std::size_t index = 0; index < production_content_candidates.size();
         ++index)
    {
        const auto& candidate = production_content_candidates[index];
        if (fighter0 == candidate.fighter0 && fighter1 == candidate.fighter1
            && stage_code == candidate.stage_selection_code)
            return &binding_.loaded_map_ids[index];
    }
    return nullptr;
}
}
