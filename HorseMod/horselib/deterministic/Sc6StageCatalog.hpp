#pragma once

#include <array>
#include <string_view>

namespace Horse::Deterministic
{
struct Sc6QualifiedStage
{
    std::string_view selection_code;
    std::string_view authored_stage_code;
    std::string_view package_root;
    std::string_view map_path;
    std::string_view localization_key;
    std::string_view display_name;
};

// The selection code is the native DB_BattleStageSetup row serialized by
// LuxOnlineBattleSync. It is not interchangeable with the authored stage code.
inline constexpr std::array<Sc6QualifiedStage, 3> qualified_stage_catalog{{
    {"273", "111", "/Game/DLC/07/Stage/STG011_R",
        "/Game/DLC/07/Stage/STG011_R/Maps/STG011_R",
        "ID_DLC7_CMN_Stag_D_011_R", "Silver Wolves' Haven"},
    {"009", "009", "/Game/Stage/STG009",
        "/Game/Stage/STG009/Maps/STG009",
        "ID_CMN_Stag_D_009", "Snow-Capped Showdown"},
    {"023", "017", "/Game/DLC/11/Stage/STG017",
        "/Game/DLC/11/Stage/STG017/Maps/STG017",
        "ID_DLC11_CMN_Stag_D_017", "Murakumo Shrine Grounds"},
}};

[[nodiscard]] const Sc6QualifiedStage* FindQualifiedStage(
    std::string_view selection_code) noexcept;
}
