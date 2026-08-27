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
using EmulateTitleDecideFn = void (__fastcall*)();

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

struct NativeFKey
{
    std::uint64_t name{};
    void* details{};
    void* details_control{};
};
static_assert(sizeof(NativeFKey) == 0x18);

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
EmulateTitleDecideFn g_emulate_title_decide{};
const NativeFKey* g_title_decide_key{};

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

RC::Unreal::UObject* ObjectProperty(RC::Unreal::UObject* owner,
                                    const wchar_t* name) noexcept
{
    if (owner == nullptr) return nullptr;
    auto** value = owner->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        name);
    return value != nullptr && *value != nullptr
        && RC::Unreal::UObject::IsReal(*value) ? *value : nullptr;
}

bool CallStringParam(RC::Unreal::UObject* owner, const wchar_t* name,
                     const wchar_t* value)
{
    if (owner == nullptr) return false;
    auto* function = owner->GetFunctionByNameInChain(name);
    if (function == nullptr) return false;
    struct Params { RC::Unreal::FString value; };
    Params params{RC::Unreal::FString(value)};
    owner->ProcessEvent(function, &params);
    return true;
}

bool EmulateTitleDecide()
{
    if (g_emulate_title_decide == nullptr) return false;
    __try
    {
        g_emulate_title_decide();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsTitleDecideKeyReady() noexcept
{
    if (g_title_decide_key == nullptr) return false;
    __try
    {
        return g_title_decide_key->name != 0
            && g_title_decide_key->details != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::string StringProperty(RC::Unreal::UObject* owner,
                           const wchar_t* name) noexcept
{
    if (owner == nullptr) return {};
    auto* value = owner->GetValuePtrByPropertyNameInChain<RC::Unreal::FString>(
        name);
    return value != nullptr ? RC::to_string(**value) : std::string{};
}

bool ChangeScene(RC::Unreal::UObject* owner, const wchar_t* tag)
{
    auto* function = owner->GetFunctionByNameInChain(L"ChangeScene");
    if (function == nullptr || g_initialize_path == nullptr
        || g_destroy_path == nullptr)
    {
        return false;
    }
    ChangeSceneParams params(tag);
    g_initialize_path(&params.inherited_data);
    owner->ProcessEvent(function, &params);
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
    constexpr std::array<std::byte, 8> kEmulateTitleDecide{
        std::byte{0x48}, std::byte{0x83}, std::byte{0xec}, std::byte{0x48},
        std::byte{0x48}, std::byte{0x8b}, std::byte{0x05}, std::byte{0x45}};
    g_initialize_path = {};
    g_destroy_path = {};
    g_emulate_title_decide = {};
    g_title_decide_key = {};
    if (image_base == 0
        || !SignatureMatches(image_base + 0x2ed1370, kInit)
        || !SignatureMatches(image_base + 0x2ed6a80, kDestroy)
        || !SignatureMatches(image_base + 0x4b9e10, kEmulateTitleDecide))
    {
        return false;
    }
    g_initialize_path = reinterpret_cast<InitPathFn>(image_base + 0x2ed1370);
    g_destroy_path = reinterpret_cast<DestroyPathFn>(image_base + 0x2ed6a80);
    g_emulate_title_decide = reinterpret_cast<EmulateTitleDecideFn>(
        image_base + 0x4b9e10);
    g_title_decide_key = reinterpret_cast<const NativeFKey*>(
        image_base + 0x42a2a60);
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
        title_top_requested_ = false;
        title_decide_requested_ = false;
    }
    if (scene_name.find("TitleScene") != std::string::npos)
    {
        // The cooked startup graph creates RefTitleMenu and MainBehavior before
        // ReadyToStart. Transitioning earlier tears down partially initialized
        // title state. Once both owners exist, take the exact Movie-to-Top state
        // edge used by TitleMovieState's SkipMovie path. Top then owns sign-in
        // and startup progression through the game's normal callbacks.
        RC::Unreal::UObject* behavior = ObjectProperty(scene, L"MainBehavior");
        if (ObjectProperty(scene, L"RefTitleMenu") == nullptr
            || behavior == nullptr)
        {
            detail = "title_startup_pending";
            return NavigationState::Waiting;
        }
        if (!title_top_requested_)
        {
            if (!CallStringParam(behavior, L"ChangeState", L"Top"))
            {
                detail = "title_top_failed";
                return NavigationState::Failed;
            }
            title_top_requested_ = true;
        }
        RC::Unreal::UObject* machine = ObjectProperty(behavior, L"Machine");
        const std::string state = StringProperty(machine, L"CurrentStateCode");
        if (state == "Top" && !title_decide_requested_ && retry_frames_++ > 0)
        {
            if (!IsTitleDecideKeyReady())
            {
                detail = "title_state:Top:key_pending";
                return NavigationState::Waiting;
            }
            if (!EmulateTitleDecide())
            {
                detail = "title_top_decide_failed";
                return NavigationState::Failed;
            }
            title_decide_requested_ = true;
            detail = "title_state:Top:decide_requested";
            return NavigationState::Waiting;
        }
        detail = state.empty() ? "title_top_requested" : "title_state:" + state;
        return NavigationState::Waiting;
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
