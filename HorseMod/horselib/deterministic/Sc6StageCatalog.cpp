#include "Sc6StageCatalog.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
const Sc6QualifiedStage* FindQualifiedStage(
    std::string_view selection_code) noexcept
{
    const auto found = std::find_if(qualified_stage_catalog.begin(),
        qualified_stage_catalog.end(), [selection_code](const auto& stage) {
            return stage.selection_code == selection_code;
        });
    return found == qualified_stage_catalog.end() ? nullptr : &*found;
}
}
