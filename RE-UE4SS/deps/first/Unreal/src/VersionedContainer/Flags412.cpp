#include <Unreal/VersionedContainer/Flags412.hpp>
#include <cstdint>

namespace RC::Unreal
{
    auto Flags412::to_impl_flags(const EObjectFlags flags) -> EObjectFlags_Impl
    {
        // Preserve this version's mapping, including ignored unsupported bits.
        // TagGarbageTemp and Dynamic move one bit left; the other supported
        // bits retain their positions. NonPIEDuplicateTransient aliases the
        // public StrongRefOnFrame bit and keeps its existing conversion.
        constexpr auto unchanged = RF_Public | RF_Standalone | RF_MarkAsNative
            | RF_Transactional | RF_ClassDefaultObject | RF_ArchetypeObject
            | RF_Transient | RF_MarkAsRootSet | RF_NeedLoad | RF_NeedPostLoad
            | RF_NeedPostLoadSubobjects | RF_BeginDestroyed | RF_FinishDestroyed
            | RF_BeingRegenerated | RF_DefaultSubObject | RF_WasLoaded
            | RF_TextExportTransient | RF_LoadCompleted
            | RF_InheritableComponentTemplate | RF_StrongRefOnFrame;
        const auto bits = static_cast<std::uint32_t>(flags);
        return static_cast<EObjectFlags_Impl>((bits & static_cast<std::uint32_t>(unchanged))
            | ((bits & static_cast<std::uint32_t>(RF_TagGarbageTemp | RF_Dynamic)) << 1));
    }
}
