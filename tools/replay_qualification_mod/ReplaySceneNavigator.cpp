#include "ReplaySceneNavigator.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <array>
#include <cstddef>
#include <cstring>

namespace Horse::Qualification
{
namespace
{
using InitPathFn = void* (__fastcall*)(void*);
using DestroyPathFn = void (__fastcall*)(void*);

struct DataTablePath
{
    void* vtable{};
    void* node_ref{};
    void* reference_count{};
};
static_assert(sizeof(DataTablePath) == 0x18);

struct NullScriptDelegate
{
    std::int32_t object_index{-1};
    std::int32_t object_serial{};
    std::int32_t function_name_index{};
    std::int32_t function_name_number{};
};
static_assert(sizeof(NullScriptDelegate) == 0x10);

struct ChangeSceneParams
{
    RC::Unreal::FString transition_tag;
    DataTablePath inherited_data{};
    NullScriptDelegate change_scene_param{};

    explicit ChangeSceneParams(const wchar_t* tag) : transition_tag(tag) {}
};
static_assert(sizeof(ChangeSceneParams) == 0x38);

InitPathFn g_initialize_path{};
DestroyPathFn g_destroy_path{};

bool SignatureMatches(std::uintptr_t address,
                      const std::array<std::byte, 8>& expected) noexcept
{
    __try { return std::memcmp(reinterpret_cast<void*>(address),
                               expected.data(), expected.size()) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

RC::Unreal::UObject* FindManager() noexcept
{
    RC::Unreal::UObject* manager =
        RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxUIGameFlowManager");
    return manager != nullptr && RC::Unreal::UObject::IsReal(manager)
        ? manager : nullptr;
}

RC::Unreal::UObject* CurrentScene(RC::Unreal::UObject* manager) noexcept
{
    if (manager == nullptr) return nullptr;
    auto** value = manager->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        L"CurrentScene");
    return value != nullptr && *value != nullptr
        && RC::Unreal::UObject::IsReal(*value) ? *value : nullptr;
}

bool ChangeScene(RC::Unreal::UObject* manager, const wchar_t* tag)
{
    auto* function = manager->GetFunctionByNameInChain(L"ChangeScene");
    if (function == nullptr || g_initialize_path == nullptr
        || g_destroy_path == nullptr)
    {
        return false;
    }
    ChangeSceneParams params(tag);
    g_initialize_path(&params.inherited_data);
    manager->ProcessEvent(function, &params);
    g_destroy_path(&params.inherited_data);
    return true;
}
}

bool ReplaySceneNavigator::Bind(std::uintptr_t image_base) noexcept
{
    constexpr std::array<std::byte, 8> kInit{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74}};
    constexpr std::array<std::byte, 8> kDestroy{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}};
    g_initialize_path = {};
    g_destroy_path = {};
    if (image_base == 0
        || !SignatureMatches(image_base + 0x2ed1370, kInit)
        || !SignatureMatches(image_base + 0x2ed6a80, kDestroy))
    {
        return false;
    }
    g_initialize_path = reinterpret_cast<InitPathFn>(image_base + 0x2ed1370);
    g_destroy_path = reinterpret_cast<DestroyPathFn>(image_base + 0x2ed6a80);
    return true;
}

NavigationState ReplaySceneNavigator::Tick(
    bool playback_context_staged, std::string& detail)
{
    RC::Unreal::UObject* manager = FindManager();
    RC::Unreal::UObject* scene = CurrentScene(manager);
    if (manager == nullptr || scene == nullptr)
    {
        detail = "game_flow_context_unavailable";
        return NavigationState::Waiting;
    }
    const std::string scene_name = RC::to_string(scene->GetClassPrivate()->GetName());
    if (scene_name.find("ReplayBattleScene") != std::string::npos)
    {
        detail = scene_name;
        return NavigationState::Ready;
    }
    if (scene_name.find("InitScene") != std::string::npos
        || scene_name.find("AdvertiseScene") != std::string::npos)
    {
        retry_frames_ = 0;
        detail = scene_name;
        return NavigationState::Waiting;
    }
    if (scene_name != last_scene_)
    {
        last_scene_ = scene_name;
        retry_frames_ = 0;
    }
    if (++retry_frames_ < 15)
    {
        detail = scene_name;
        return NavigationState::Waiting;
    }
    retry_frames_ = 0;
    if (scene_name.find("ReplaySetupScene") != std::string::npos)
    {
        detail = scene_name;
        return NavigationState::Waiting;
    }
    if (scene_name.find("TitleScene") != std::string::npos)
    {
        // Drive the same game-flow boundary used after the main menu. Both
        // EmulateTitleDecide and CeBankManager::TitleToMainMenu depend on the
        // active title-state/modal owner; after an unclean prior exit they can
        // acknowledge a quit path or block inside that state machine. The
        // replay-list transition is owned by the game-flow manager and carries
        // the normal inherited-data/delegate values assembled by ChangeScene.
        if (!ChangeScene(manager, L"replay_list"))
        {
            detail = "title_replay_list_failed";
            return NavigationState::Failed;
        }
        detail = "title_replay_list_requested";
        return NavigationState::Waiting;
    }
    if (scene_name.find("ReplayListScene") != std::string::npos
        && !playback_context_staged)
    {
        detail = scene_name;
        return NavigationState::ReplayListReady;
    }
    const wchar_t* tag = scene_name.find("ReplayListScene") != std::string::npos
        ? L"battlesetup" : L"replay_list";
    if (!ChangeScene(manager, tag))
    {
        detail = "change_scene_failed:" + scene_name;
        return NavigationState::Failed;
    }
    detail = "change_scene_requested:" + scene_name;
    return NavigationState::Waiting;
}
}
